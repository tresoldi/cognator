package score

import (
	"math"
	"sort"

	"github.com/tresoldi/cognator/internal/align"
	"github.com/tresoldi/cognator/internal/merkmal"
)

// DefaultDirichletConcentration is α₀, the prior strength of the
// Dirichlet prior over correspondence distributions. Interpretation:
// "the prior is worth α₀ observations per source row." Small-sample
// regime (n(σ,·) ≪ α₀) recovers the merkmal-similarity prior;
// rich-sample regime recovers the maximum-likelihood estimate.
//
// Chosen from a Dev-Core sweep over α₀ ∈ {1, 2, 5, 10, 20, 50, 100,
// 200} with --scorer=posterior --preset=support-adaptive-bridge:
// mean F peaks at α₀=50 (0.8121), above α₀=5 (0.7945) and α₀=200
// (0.8023). Large families like peirosaustroasiatic need a stronger
// prior than the initial α₀=5 pick suggested — many observations
// per row still benefit from regularization against over-merging.
const DefaultDirichletConcentration = 50.0

// DefaultDirichletSharpness controls how strongly merkmal distance
// shapes the base measure π(σ, τ) = softmax_τ(-k · d_merkmal(σ, τ)).
// Matches priorSharpness in the legacy LogOdds pseudocount so the
// Bayesian prior has the same curvature as the legacy smoothing term.
const DefaultDirichletSharpness = 4.0

// posteriorScorerScale bounds log-posterior-odds into align.Scorer's
// effective range via tanh(lo / scale). This is an NW-plumbing
// concession, not a modeling choice: alignment needs a bounded
// similarity. Matches tanhScale in the legacy learned scorer to make
// the two paths comparable.
const posteriorScorerScale = 3.0

// DirichletPrior defines a per-source Dirichlet prior over target
// graphemes. α(σ, τ) = α₀ · softmax_τ(-k · d_merkmal(σ, τ)) where the
// softmax is taken over a fixed inventory of target graphemes.
// Denominators are precomputed per source for read-only concurrent use.
type DirichletPrior struct {
	Bundle        *merkmal.Bundle
	Concentration float64
	Sharpness     float64

	inventory    []string
	inventorySet map[string]struct{}
	denominators map[string]float64
}

// NewDirichletPrior returns a prior whose softmax support is the
// supplied inventory. The inventory should be the set of graphemes
// observed in a training corpus plus the gap sentinel; DirichletPrior
// assigns zero Alpha to graphemes outside this set. Concentration and
// sharpness fall back to defaults if non-positive.
func NewDirichletPrior(bundle *merkmal.Bundle, concentration, sharpness float64, inventory []string) *DirichletPrior {
	if concentration <= 0 {
		concentration = DefaultDirichletConcentration
	}
	if sharpness <= 0 {
		sharpness = DefaultDirichletSharpness
	}
	inv := make([]string, 0, len(inventory))
	seen := make(map[string]struct{}, len(inventory))
	for _, g := range inventory {
		if _, ok := seen[g]; ok {
			continue
		}
		seen[g] = struct{}{}
		inv = append(inv, g)
	}
	sort.Strings(inv)
	p := &DirichletPrior{
		Bundle:        bundle,
		Concentration: concentration,
		Sharpness:     sharpness,
		inventory:     inv,
		inventorySet:  seen,
		denominators:  make(map[string]float64, len(inv)),
	}
	for _, src := range inv {
		var sum float64
		for _, t := range inv {
			sum += math.Exp(-sharpness * priorDistance(bundle, src, t))
		}
		p.denominators[src] = sum
	}
	return p
}

// priorDistance is Bundle.Distance with a gap-aware policy: pairs
// involving the Gap sentinel use the maximum distance, reflecting
// that a phonetic prior cannot predict insertion/deletion events.
func priorDistance(b *merkmal.Bundle, source, target string) float64 {
	if source == Gap || target == Gap {
		if source == target {
			return 0
		}
		return 1
	}
	return b.Distance(source, target)
}

// Alpha returns α(source, target). Returns 0 when target is outside
// the inventory (the prior's support), so unknown targets contribute
// only through observed counts in PosteriorTable.
func (p *DirichletPrior) Alpha(source, target string) float64 {
	if _, ok := p.inventorySet[target]; !ok {
		return 0
	}
	denom := p.denominators[source]
	if denom <= 0 {
		// Source never seen during construction — fall back to a
		// uniform share over the inventory.
		if len(p.inventory) == 0 {
			return 0
		}
		return p.Concentration / float64(len(p.inventory))
	}
	num := math.Exp(-p.Sharpness * priorDistance(p.Bundle, source, target))
	return p.Concentration * num / denom
}

// Inventory returns the target-grapheme support (sorted). The slice
// is safe to read but callers must not mutate it.
func (p *DirichletPrior) Inventory() []string {
	return p.inventory
}

// PosteriorTable holds attested correspondence counts for one ordered
// lect pair, equipped with a Dirichlet prior. Scoring is via the
// posterior mean P(τ | σ) = (α(σ, τ) + n(σ, τ)) / (α₀ + n(σ, ·)).
// The legacy CorrespondenceTable+LearnedScorer flow collapses to this
// under α₀=0 (pure MLE) or n=0 (pure prior); the point of this type
// is to make the blend principled rather than a tuned lambda.
type PosteriorTable struct {
	SourceLect, TargetLect string

	prior       *DirichletPrior
	counts      map[string]map[string]int
	rowTotal    map[string]int
	targetTotal map[string]int
	columns     int
	totalPairs  int

	// shuffleNull, when non-nil, replaces the target-marginal null in
	// LogOdds with a permutation-based conditional P_null(target |
	// source) derived from random re-pairings of the source forms.
	// Attach via AttachShuffleNull; composes with any α₀.
	shuffleNull        *NullDistribution
	shuffleNullRowSums map[string]float64
}

// NewPosteriorTable returns an empty table attached to the given prior.
func NewPosteriorTable(srcLect, tgtLect string, prior *DirichletPrior) *PosteriorTable {
	return &PosteriorTable{
		SourceLect:  srcLect,
		TargetLect:  tgtLect,
		prior:       prior,
		counts:      make(map[string]map[string]int),
		rowTotal:    make(map[string]int),
		targetTotal: make(map[string]int),
	}
}

// Add increments n(source, target) by 1, treating empty strings as
// the Gap sentinel.
func (t *PosteriorTable) Add(source, target string) {
	if source == "" {
		source = Gap
	}
	if target == "" {
		target = Gap
	}
	inner, ok := t.counts[source]
	if !ok {
		inner = make(map[string]int)
		t.counts[source] = inner
	}
	inner[target]++
	t.rowTotal[source]++
	t.targetTotal[target]++
	t.columns++
}

// Prior returns the attached DirichletPrior.
func (t *PosteriorTable) Prior() *DirichletPrior { return t.prior }

// AttachShuffleNull installs a precomputed shuffle-based null to be
// used by LogOdds. Recomputing row sums up front means LogOdds stays
// a cheap map lookup under concurrent access. Pass nil to clear.
func (t *PosteriorTable) AttachShuffleNull(n *NullDistribution) {
	t.shuffleNull = n
	t.shuffleNullRowSums = nil
	if n == nil {
		return
	}
	t.shuffleNullRowSums = make(map[string]float64, len(n.Expected))
	for src, row := range n.Expected {
		var sum float64
		for _, v := range row {
			sum += v
		}
		t.shuffleNullRowSums[src] = sum
	}
}

// TotalPairs returns the number of form-pairs that contributed counts.
func (t *PosteriorTable) TotalPairs() int { return t.totalPairs }

// Columns returns the total number of alignment columns observed.
func (t *PosteriorTable) Columns() int { return t.columns }

// Count returns the attested n(source, target).
func (t *PosteriorTable) Count(source, target string) int {
	if inner, ok := t.counts[source]; ok {
		return inner[target]
	}
	return 0
}

// PosteriorMean returns E[P(target | source)] under Dirichlet(α + n).
// Graphemes outside the prior's inventory inherit only the observed
// counts; Alpha returns 0 for them and rowTotal still reflects their
// conditioning mass.
func (t *PosteriorTable) PosteriorMean(source, target string) float64 {
	alpha := t.prior.Alpha(source, target)
	observed := float64(t.Count(source, target))
	denom := t.prior.Concentration + float64(t.rowTotal[source])
	if denom <= 0 {
		return 0
	}
	return (alpha + observed) / denom
}

// nullConditional returns P_null(target | source) under the attached
// null model. When a shuffle null is attached it uses the per-row
// expected counts (the permutation-based conditional); otherwise it
// falls back to the target marginal observed during training. Empty
// corpora fall back to a uniform distribution over the prior's
// inventory so LogOdds never divides by zero.
func (t *PosteriorTable) nullConditional(source, target string) float64 {
	if t.shuffleNull != nil {
		rowSum := t.shuffleNullRowSums[source]
		if rowSum > 0 {
			if row, ok := t.shuffleNull.Expected[source]; ok {
				if v, ok := row[target]; ok && v > 0 {
					return v / rowSum
				}
			}
		}
	}
	if t.columns > 0 {
		if count, ok := t.targetTotal[target]; ok && count > 0 {
			return float64(count) / float64(t.columns)
		}
	}
	if n := len(t.prior.inventory); n > 0 {
		if _, ok := t.prior.inventorySet[target]; ok {
			return 1.0 / float64(n)
		}
	}
	return 0
}

// LogOdds returns log₂ PosteriorMean(source, target) / P_null(target |
// source). The null is the permutation-based conditional when a
// shuffle null is attached, else the target marginal. Unlike the
// legacy CorrespondenceTable.LogOdds this is a proper
// log-posterior-odds — no extra smoothing parameter.
func (t *PosteriorTable) LogOdds(source, target string) float64 {
	post := t.PosteriorMean(source, target)
	if post <= 0 {
		return 0
	}
	null := t.nullConditional(source, target)
	if null <= 0 {
		return 0
	}
	return math.Log2(post / null)
}

// posteriorScorer is an align.Scorer backed by a PosteriorTable. It
// wraps log-posterior-odds in tanh to bound similarity for the NW
// aligner, mirroring the legacy LearnedScorer's squashing but without
// the lambda blend — the posterior already combines prior and
// evidence.
type posteriorScorer struct {
	bundle *merkmal.Bundle
	table  *PosteriorTable
	gapT   float64
	gapS   float64
}

// PosteriorScorer wraps a PosteriorTable in the align.Scorer interface
// and composes it with the prosody-aware role penalty, matching the
// legacy LearnedScorer's treatment of C/V/R/G role mismatches.
func PosteriorScorer(bundle *merkmal.Bundle, table *PosteriorTable) align.Scorer {
	inner := &posteriorScorer{
		bundle: bundle,
		table:  table,
		gapT:   align.DefaultGapCost,
		gapS:   align.DefaultGapCost,
	}
	return align.WrapWithProsody(inner, bundle)
}

func (s *posteriorScorer) Score(a, b string) float64 {
	lo := s.table.LogOdds(a, b)
	return math.Tanh(lo / posteriorScorerScale)
}

func (s *posteriorScorer) GapInTarget() float64 { return s.gapT }
func (s *posteriorScorer) GapInSource() float64 { return s.gapS }
