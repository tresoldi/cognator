// Package score learns per-lect-pair correspondence tables from
// form-pairs sharing a concept. Counts are tallied from
// Needleman-Wunsch alignments under a starting scorer; smoothed
// log-odds are then computed against a marginal-product null with
// a phon-weighted pseudocount. A LearnedScorer blends these
// learned log-odds with the phon prior and can be fed back into
// align.NeedlemanWunsch for a second alignment pass (EM-style) or
// directly into distance computation for cluster input.
package score

import (
	"math"
	"runtime"
	"sync"

	"github.com/tresoldi/cognator/internal/align"
	"github.com/tresoldi/cognator/internal/phon"
	"github.com/tresoldi/cognator/internal/segment"
	"github.com/tresoldi/cognator/internal/wordlist"
)

// Gap is the sentinel grapheme used when counting gap-bearing
// alignment columns.
const Gap = "-"

// priorSharpness controls how quickly the phon-distance prior
// decays. exp(-priorSharpness·d): d=0 → 1.0, d=0.5 → 0.135, d=1 →
// 0.018. Chosen to give identical graphemes dominant prior mass
// while still distinguishing nearby-similar from distant pairs.
const priorSharpness = 4.0

// tanhScale squashes log-odds (log₂ space) into tanh's effective
// range. log₂(10) ≈ 3.3, so lo/3 puts a "10× overrepresented" cell
// near tanh's saturating region. Tested against raw-clamp
// alternatives (±2, ±5): clamping gains on low-recall datasets but
// crashes precision on high-precision ones.
const tanhScale = 3.0

// FormPair is one comparandum: two grapheme sequences sharing a
// concept across the source and target lects. SourceID/TargetID
// are the originating wordlist.Record IDs, used by downstream
// clustering to map back to per-concept node indices.
type FormPair struct {
	Source, Target     []string
	Concept            string
	SourceID, TargetID string
}

// PairKey identifies an ordered lect pair.
type PairKey struct {
	Source, Target string
}

// CorrespondenceTable holds the attested alignment-column counts and
// derived marginals for one ordered lect pair. Optionally carries a
// shuffle-based null (shuffleExpected) that replaces the marginal-
// product expectation in LogOdds when present.
type CorrespondenceTable struct {
	SourceLect, TargetLect string
	attested               map[string]map[string]int
	sourceCount            map[string]int
	targetCount            map[string]int
	totalColumns           int
	totalPairs             int
	// Shuffle null (nil if not trained with ComputeShuffleNull):
	// per-grapheme-pair expected count averaged over shuffle runs,
	// plus the per-run total column count for normalization.
	shuffleExpected  map[string]map[string]float64
	totalShuffleCols int
}

// NullDistribution is a precomputed shuffle-based null: the expected
// correspondence counts under random re-pairing of source with
// target forms. Attach to a CorrespondenceTable via Train's `null`
// argument. Total is the per-run mean column count.
type NullDistribution struct {
	Expected         map[string]map[string]float64
	TotalColumnsMean int
}

// Train aligns every form-pair under prior and tallies grapheme-pair
// frequencies. Returns a new CorrespondenceTable; never mutates
// inputs.
//
// The table's LogOdds uses marginal-product expectation. For a
// shuffle-based null, call TrainWithNull instead.
func Train(srcLect, tgtLect string, pairs []FormPair, prior align.Scorer) *CorrespondenceTable {
	t := &CorrespondenceTable{
		SourceLect:  srcLect,
		TargetLect:  tgtLect,
		attested:    make(map[string]map[string]int),
		sourceCount: make(map[string]int),
		targetCount: make(map[string]int),
	}
	for _, p := range pairs {
		if len(p.Source) == 0 || len(p.Target) == 0 {
			continue
		}
		al := align.NeedlemanWunsch(p.Source, p.Target, prior)
		for i := range al.Source {
			s := al.Source[i]
			u := al.Target[i]
			if s == "" {
				s = Gap
			}
			if u == "" {
				u = Gap
			}
			inner, ok := t.attested[s]
			if !ok {
				inner = make(map[string]int)
				t.attested[s] = inner
			}
			inner[u]++
			t.sourceCount[s]++
			t.targetCount[u]++
			t.totalColumns++
		}
		t.totalPairs++
	}
	return t
}

// TrainMany runs Train concurrently over many lect pairs, bounded by
// GOMAXPROCS. Order is stable by key because the returned map is
// populated under a lock; numerics are deterministic because Train
// itself is deterministic.
func TrainMany(pairs map[PairKey][]FormPair, prior align.Scorer) map[PairKey]*CorrespondenceTable {
	out := make(map[PairKey]*CorrespondenceTable, len(pairs))
	var mu sync.Mutex
	var wg sync.WaitGroup
	sem := make(chan struct{}, runtime.GOMAXPROCS(0))
	for key, fps := range pairs {
		wg.Add(1)
		sem <- struct{}{}
		go func(k PairKey, p []FormPair) {
			defer wg.Done()
			defer func() { <-sem }()
			t := Train(k.Source, k.Target, p, prior)
			mu.Lock()
			out[k] = t
			mu.Unlock()
		}(key, fps)
	}
	wg.Wait()
	return out
}

// LogOdds is the smoothed log-base-2 ratio of observed to expected
// correspondence frequency. Positive means the pair (source, target)
// occurs more often than a null independence model predicts,
// weighted by a phon-derived Dirichlet pseudocount.
//
// If a shuffle null has been attached (via TrainWithNull /
// ComputeShuffleNull) it is used as the expectation. Otherwise the
// fallback is marginal-product independence — faster but looser
// on sparse lect-pairs where the marginal approximation breaks down.
func (t *CorrespondenceTable) LogOdds(b *phon.Phon, source, target string, alpha float64) float64 {
	if t.totalColumns == 0 {
		return 0
	}
	observed := float64(t.attested[source][target])
	total := float64(t.totalColumns)
	prior := math.Exp(-priorSharpness * b.Distance(source, target))
	numerator := (observed + alpha*prior) / (total + alpha)

	var denominator float64
	if t.shuffleExpected != nil && t.totalShuffleCols > 0 {
		var expObs float64
		if inner, ok := t.shuffleExpected[source]; ok {
			expObs = inner[target]
		}
		denominator = (expObs + alpha*prior) / (float64(t.totalShuffleCols) + alpha)
	} else {
		ns := float64(t.sourceCount[source])
		nt := float64(t.targetCount[target])
		denominator = (ns * nt) / (total * total)
	}

	if numerator <= 0 || denominator <= 0 {
		return 0
	}
	return math.Log2(numerator / denominator)
}

// Count returns the attested joint count for debugging.
func (t *CorrespondenceTable) Count(source, target string) int {
	return t.attested[source][target]
}

// TotalPairs returns the number of form pairs used for training.
func (t *CorrespondenceTable) TotalPairs() int { return t.totalPairs }

// PairByConcept groups records by Concept and emits every cross-lect
// form pair between srcLect and tgtLect. Segments are tokenized and
// tone-reattached via the bundle; boundary tokens are stripped.
//
// O(N) in the total corpus size. If you are calling this repeatedly
// for many (src, tgt) pairs (classic in a full-family pipeline),
// prefer PairByConceptIndexed, which amortizes the bucketing to O(1)
// per call.
func PairByConcept(recs []wordlist.Record, srcLect, tgtLect string, b *phon.Phon) []FormPair {
	idx := IndexByLect(recs, b)
	return PairByConceptIndexed(idx, srcLect, tgtLect)
}

// LectIndex is a pre-computed (lect, concept) → graphemes mapping.
// Build once per corpus; pass to PairByConceptIndexed for each
// lect-pair you want to align.
type LectIndex struct {
	// byLect[lect][concept] → list of (record ID, graphemes)
	byLect map[string]map[string][]indexedForm
}

type indexedForm struct {
	ID        string
	Graphemes []string
}

// IndexByLect tokenizes every record once and buckets by (lect, concept).
// Call once per family, not per lect-pair.
//
// Backwards-compatible wrapper: calls IndexByLectOpts with
// ClassReduce=false.
func IndexByLect(recs []wordlist.Record, b *phon.Phon) *LectIndex {
	return IndexByLectOpts(recs, b, false)
}

// IndexByLectOpts is IndexByLect with explicit options. When
// classReduce is true, graphemes are upstream-normalized via
// segment.ClassReduce (stripping modifier letters, diacritics, tone
// markers) so correspondence tables train on ~30 coarse classes
// rather than 700+ IPA graphemes — matching lingpy's SCA strategy.
func IndexByLectOpts(recs []wordlist.Record, b *phon.Phon, classReduce bool) *LectIndex {
	var reducer func(string) string
	if classReduce {
		reducer = segment.ClassReduce
	}
	return IndexByLectWithReducer(recs, b, reducer)
}

// IndexByLectWithReducer is IndexByLectOpts with a caller-supplied
// grapheme→class function. Pass nil for no reduction. For phon
// partition-based reduction, pass `func(g string) string {
// if c := b.Partition(level, g); c != "" { return c }; return g }`
// or similar.
func IndexByLectWithReducer(recs []wordlist.Record, b *phon.Phon, reducer func(string) string) *LectIndex {
	idx := &LectIndex{byLect: map[string]map[string][]indexedForm{}}
	for _, r := range recs {
		gs := graphemesOnly(r.Segments, b)
		if reducer != nil {
			gs = applyReducer(gs, reducer)
		}
		if len(gs) == 0 {
			continue
		}
		concepts, ok := idx.byLect[r.LanguageID]
		if !ok {
			concepts = map[string][]indexedForm{}
			idx.byLect[r.LanguageID] = concepts
		}
		concepts[r.Concept] = append(concepts[r.Concept], indexedForm{ID: r.ID, Graphemes: gs})
	}
	return idx
}

func applyReducer(gs []string, reducer func(string) string) []string {
	out := make([]string, len(gs))
	for i, g := range gs {
		out[i] = reducer(g)
	}
	return out
}

// PairByConceptIndexed emits all cross-lect form pairs between
// srcLect and tgtLect from a pre-built LectIndex.
func PairByConceptIndexed(idx *LectIndex, srcLect, tgtLect string) []FormPair {
	srcConcepts, okSrc := idx.byLect[srcLect]
	tgtConcepts, okTgt := idx.byLect[tgtLect]
	if !okSrc || !okTgt {
		return nil
	}
	var out []FormPair
	// Iterate the smaller of the two lects' concept sets.
	// Iterate the smaller concept map for O(min) outer loop.
	small := srcConcepts
	large := tgtConcepts
	swap := false
	if len(tgtConcepts) < len(srcConcepts) {
		small, large = tgtConcepts, srcConcepts
		swap = true
	}
	for concept, primary := range small {
		secondary, ok := large[concept]
		if !ok {
			continue
		}
		srcForms, tgtForms := primary, secondary
		if swap {
			srcForms, tgtForms = secondary, primary
		}
		for _, s := range srcForms {
			for _, t := range tgtForms {
				out = append(out, FormPair{
					Source: s.Graphemes, Target: t.Graphemes,
					Concept:  concept,
					SourceID: s.ID, TargetID: t.ID,
				})
			}
		}
	}
	return out
}

func graphemesOnly(raw string, b *phon.Phon) []string {
	toks, _ := segment.Parse(raw, b)
	out := make([]string, 0, len(toks))
	for _, t := range toks {
		if t.Boundary || t.Grapheme == "" {
			continue
		}
		out = append(out, t.Grapheme)
	}
	return out
}

// learnedScorer blends phon-based prior similarity with learned
// correspondence log-odds. It satisfies align.Scorer.
type learnedScorer struct {
	bundle *phon.Phon
	prior  align.Scorer
	table  *CorrespondenceTable
	lambda float64
	alpha  float64
	gapT   float64
	gapS   float64
}

// LearnedScorer returns an align.Scorer whose Score is a weighted
// blend of the prior's similarity and the learned table's log-odds.
// lambda in [0, 1]: 0 → pure prior, 1 → pure learned.
func LearnedScorer(b *phon.Phon, table *CorrespondenceTable, lambda float64) align.Scorer {
	return &learnedScorer{
		bundle: b,
		prior:  align.ProsodyAwareScorer(b),
		table:  table,
		lambda: lambda,
		alpha:  1.0,
		gapT:   align.DefaultGapCost,
		gapS:   align.DefaultGapCost,
	}
}

func (l *learnedScorer) Score(a, b string) float64 {
	ps := l.prior.Score(a, b)
	if l.lambda == 0 {
		return ps
	}
	lo := l.table.LogOdds(l.bundle, a, b, l.alpha)
	ls := math.Tanh(lo / tanhScale)
	return (1.0-l.lambda)*ps + l.lambda*ls
}

func (l *learnedScorer) GapInTarget() float64 { return l.gapT }
func (l *learnedScorer) GapInSource() float64 { return l.gapS }
