// Package pipeline orchestrates the end-to-end cognate detection
// run: per-family, per-ordered-lect-pair EM training of
// correspondence tables, per-concept graph construction with a
// data-driven Otsu threshold, and Infomap clustering. Outputs a
// slice of SidecarRows ready for the wordlist writer.
package pipeline

import (
	"fmt"
	"math/rand/v2"
	"runtime"
	"sort"
	"sync"

	"github.com/tresoldi/cognator/internal/align"
	"github.com/tresoldi/cognator/internal/cluster"
	"github.com/tresoldi/cognator/internal/evidence"
	"github.com/tresoldi/cognator/internal/merkmal"
	"github.com/tresoldi/cognator/internal/score"
	"github.com/tresoldi/cognator/internal/segment"
	"github.com/tresoldi/cognator/internal/wordlist"
)

// Spec is the user-visible configuration for a cognator run.
// Zero values fall back to documented defaults.
type Spec struct {
	Iterations                int     // EM passes per lect-pair (default 3)
	Lambda                    float64 // learned/prior blend (default 0.5)
	Threshold                 float64 // manual override; 0 → Otsu auto-threshold
	ThresholdMode             string  // "family" (default) | "concept-shrink"
	ThresholdMin              float64 // Otsu clamp floor (default 0.2)
	ThresholdMax              float64 // Otsu clamp ceiling (default 0.8)
	ThresholdShrink           float64 // shrinkage toward family prior in "concept-shrink" mode (default 0.5)
	GraphMode                 string  // "threshold" (default) | "support"
	GraphExpansionMode        string  // "either" (default) | "mutual" | "adaptive" for top-k expansion
	GraphTopK                 int     // top-k nearest-neighbor candidate expansion in support mode (default 2)
	GraphAdaptiveMargin       float64 // top1-vs-top2 support gap needed to sponsor unilateral expansion in adaptive mode
	GraphAdaptiveEitherMax    float64 // ambiguity-share ceiling below which adaptive mode reuses either expansion
	GraphAdaptiveAmbiguity    float64 // ambiguous-node share above which adaptive mode falls back to mutual expansion
	GraphMutualBoost          float64 // weight boost for mutual top-k edges (default 0.2)
	GraphTriangleBoost        float64 // weight boost for triangle-supported edges (default 0.3)
	GraphContrastivePenalty   float64 // optional penalty for edges dominated by stronger local competitors
	GraphContrastiveFloor     float64 // ignore small rival gaps before contrastive penalty activates
	GraphRiskRankWeight       float64 // weight of endpoint-rank risk in calibrated edge-risk score
	GraphRiskGapWeight        float64 // weight of best-rival gap risk in calibrated edge-risk score
	GraphUnilateralPenalty    float64 // penalty on non-mutual expanded edges when endpoints have stronger rivals
	GraphRivalFloor           float64 // rival-support floor at which unilateral penalty activates
	RefineMode                string  // "none" (default) | "bridge-cut"
	RefineConfidenceThreshold float64 // bridge-cut activates for nodes below this confidence
	RefineBridgeRatio         float64 // weak-edge cutoff relative to node mean for bridge cutting
	Seed                      uint64  // Infomap RNG seed
	Jobs                      int     // concurrent workers; 0 → GOMAXPROCS
	Bootstrap                 int     // confidence bootstrap runs (default 0 = off)
	BootstrapSigma            float64 // stddev of edge-distance perturbation (default 0.05)
	ClusterScope              string  // "concept" (default) | "global"
	MinPairs                  int     // skip training a scorer if the pair has <N form-pairs; use prior (default 5)
	AlignmentMode             string  // "global" | "combined" (default; global + local + overlap)
	ClassReduce               bool    // strip IPA modifiers/diacritics upstream (NFD-strip heuristic)
	NullMode                  string  // "marginal" (default, cheap) | "shuffle" (permutation-based, more accurate on sparse pairs)
	NullMaxRuns               int     // cap on shuffle runs per lect-pair (default 200; adaptive stopping usually stops earlier)
	ScorerMode                string  // "learned" (default; legacy CorrespondenceTable + lambda blend) | "posterior" (Dirichlet-prior PosteriorTable)
	DirichletConcentration    float64 // α₀ for the Dirichlet-prior scorer; 0 falls back to DefaultDirichletConcentration
	DirichletSharpness        float64 // k for the softmax over merkmal distances in the Dirichlet base measure; 0 falls back to DefaultDirichletSharpness
}

// Result is the end-to-end output.
type Result struct {
	Rows      []wordlist.SidecarRow
	Threshold float64 // actual Otsu-or-override threshold
	OOV       map[string]int
}

// ResolveDefaults applies the documented defaults to any zero-valued
// Spec fields and returns the filled-in Spec. Callers that want to
// record the effective configuration (for benchmark metadata) should
// invoke this before consuming the Spec.
func ResolveDefaults(spec Spec) Spec {
	if spec.Iterations <= 0 {
		spec.Iterations = DefaultIterations
	}
	if spec.Lambda <= 0 {
		spec.Lambda = DefaultLambda
	}
	if spec.ThresholdMin <= 0 {
		spec.ThresholdMin = DefaultThresholdMin
	}
	if spec.ThresholdMax <= 0 {
		spec.ThresholdMax = DefaultThresholdMax
	}
	if spec.ThresholdMode == "" {
		spec.ThresholdMode = DefaultThresholdMode
	}
	if spec.ThresholdShrink <= 0 {
		spec.ThresholdShrink = DefaultThresholdShrink
	}
	if spec.ThresholdShrink > 1 {
		spec.ThresholdShrink = 1
	}
	if spec.GraphMode == "" {
		spec.GraphMode = DefaultGraphMode
	}
	if spec.GraphExpansionMode == "" {
		spec.GraphExpansionMode = DefaultGraphExpansionMode
	}
	if spec.GraphTopK <= 0 {
		spec.GraphTopK = DefaultGraphTopK
	}
	if spec.GraphAdaptiveMargin <= 0 {
		spec.GraphAdaptiveMargin = DefaultGraphAdaptiveMargin
	}
	if spec.GraphAdaptiveEitherMax < 0 {
		spec.GraphAdaptiveEitherMax = DefaultGraphAdaptiveEitherMax
	}
	if spec.GraphAdaptiveAmbiguity <= 0 {
		spec.GraphAdaptiveAmbiguity = DefaultGraphAdaptiveAmbiguity
	}
	if spec.GraphContrastivePenalty < 0 {
		spec.GraphContrastivePenalty = DefaultGraphContrastivePenalty
	}
	if spec.GraphContrastiveFloor < 0 {
		spec.GraphContrastiveFloor = DefaultGraphContrastiveFloor
	}
	if spec.GraphRiskRankWeight < 0 {
		spec.GraphRiskRankWeight = DefaultGraphRiskRankWeight
	}
	if spec.GraphRiskGapWeight < 0 {
		spec.GraphRiskGapWeight = DefaultGraphRiskGapWeight
	}
	if spec.GraphMutualBoost <= 0 {
		spec.GraphMutualBoost = DefaultGraphMutualBoost
	}
	if spec.GraphTriangleBoost <= 0 {
		spec.GraphTriangleBoost = DefaultGraphTriangleBoost
	}
	if spec.GraphUnilateralPenalty < 0 {
		spec.GraphUnilateralPenalty = DefaultGraphUnilateralPenalty
	}
	if spec.GraphRivalFloor <= 0 {
		spec.GraphRivalFloor = DefaultGraphRivalFloor
	}
	if spec.RefineMode == "" {
		spec.RefineMode = DefaultRefineMode
	}
	if spec.RefineConfidenceThreshold <= 0 {
		spec.RefineConfidenceThreshold = DefaultRefineConfidenceThreshold
	}
	if spec.RefineBridgeRatio <= 0 {
		spec.RefineBridgeRatio = DefaultRefineBridgeRatio
	}
	if spec.BootstrapSigma <= 0 {
		spec.BootstrapSigma = DefaultBootstrapSigma
	}
	if spec.ClusterScope == "" {
		spec.ClusterScope = DefaultClusterScope
	}
	if spec.MinPairs <= 0 {
		spec.MinPairs = DefaultMinPairs
	}
	if spec.AlignmentMode == "" {
		spec.AlignmentMode = DefaultAlignmentMode
	}
	if spec.NullMode == "" {
		spec.NullMode = DefaultNullMode
	}
	if spec.NullMaxRuns <= 0 {
		spec.NullMaxRuns = DefaultNullMaxRuns
	}
	if spec.ScorerMode == "" {
		spec.ScorerMode = DefaultScorerMode
	}
	if spec.DirichletConcentration <= 0 {
		spec.DirichletConcentration = score.DefaultDirichletConcentration
	}
	if spec.DirichletSharpness <= 0 {
		spec.DirichletSharpness = score.DefaultDirichletSharpness
	}
	return spec
}

// Detect runs the full pipeline. recs must include Family, Concept,
// Language_ID, and Segments; Cognacy is ignored (this is cognate
// DETECTION, not evaluation).
func Detect(recs []wordlist.Record, bundle *merkmal.Bundle, spec Spec) Result {
	spec = ResolveDefaults(spec)

	out := Result{OOV: map[string]int{}}
	families := groupByFamily(recs)
	famNames := sortedKeys(families)

	for _, fam := range famNames {
		famRecs := families[fam]
		fres := detectFamily(fam, famRecs, bundle, spec)
		out.Rows = append(out.Rows, fres.Rows...)
		for g, c := range fres.OOV {
			out.OOV[g] += c
		}
		// Record the first family's threshold — in v1 families are
		// independent, but for reporting purposes keep a representative.
		if out.Threshold == 0 {
			out.Threshold = fres.Threshold
		}
	}
	return out
}

type familyResult struct {
	Rows      []wordlist.SidecarRow
	Threshold float64
	OOV       map[string]int
}

func detectFamily(fam string, recs []wordlist.Record, b *merkmal.Bundle, spec Spec) familyResult {
	res := familyResult{OOV: map[string]int{}}

	// Tokenize all forms once; stash the graphemes per record ID.
	tokens := make(map[string][]string, len(recs))
	for _, r := range recs {
		toks, _ := segment.Parse(r.Segments, b)
		var gs []string
		for _, t := range toks {
			if t.Boundary || t.Grapheme == "" {
				continue
			}
			g := t.Grapheme
			if !b.Known(g) {
				res.OOV[g]++
			}
			if spec.ClassReduce {
				g = segment.ClassReduce(g)
			}
			gs = append(gs, g)
		}
		tokens[r.ID] = gs
	}

	lects := uniqueLects(recs)
	workers := spec.Jobs
	if workers <= 0 {
		workers = runtime.GOMAXPROCS(0)
	}

	// Bucket records by concept; assign a stable index per form so
	// edges can reference (concept, i, j) without carrying whole
	// Records around.
	conceptForms := map[string][]wordlist.Record{}
	for _, r := range recs {
		conceptForms[r.Concept] = append(conceptForms[r.Concept], r)
	}
	conceptNames := sortedKeys(conceptForms)
	conceptIndex := map[string]map[string]int{}
	for c, forms := range conceptForms {
		idx := make(map[string]int, len(forms))
		for i, f := range forms {
			idx[f.ID] = i
		}
		conceptIndex[c] = idx
	}

	// Walk every ordered lect-pair in parallel, training a scorer,
	// using it to compute distances for just the form-pairs this
	// lect-pair produces, and discarding the scorer. This keeps peak
	// memory at O(workers × one scorer) rather than O(L² scorers).
	conceptEdges := make(map[string][]cEdge, len(conceptNames))
	conceptDistances := make(map[string][]float64, len(conceptNames))
	var globalEdges []cEdge
	var globalForms []wordlist.Record
	var allDistances []float64

	if spec.ClusterScope == "global" {
		globalForms = append(globalForms, recs...)
	}

	globalIndex := map[string]int{}
	if spec.ClusterScope == "global" {
		for i, r := range globalForms {
			globalIndex[r.ID] = i
		}
	}

	var reducer func(string) string
	if spec.ClassReduce {
		reducer = segment.ClassReduce
	}
	lectIndex := score.IndexByLectWithReducer(recs, b, reducer)
	lectPairs := pairList(lects)

	type job struct{ a, b string }
	jobs := make(chan job, workers*2)
	var wg sync.WaitGroup
	var mu sync.Mutex
	priorForWorkers := align.ProsodyAwareScorer(b)
	for w := 0; w < workers; w++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for jb := range jobs {
				fps := score.PairByConceptIndexed(lectIndex, jb.a, jb.b)
				if len(fps) == 0 {
					continue
				}
				var scorer align.Scorer
				switch spec.ScorerMode {
				case "posterior":
					prior := score.NewDirichletPrior(b, spec.DirichletConcentration, spec.DirichletSharpness, score.InventoryFromPairs(fps))
					tab := score.TrainPosteriorIterated(jb.a, jb.b, fps, b, spec.Iterations, prior)
					scorer = score.PosteriorScorer(b, tab)
				default:
					if len(fps) < spec.MinPairs {
						scorer = priorForWorkers
					} else {
						var null *score.NullDistribution
						if spec.NullMode == "shuffle" {
							nullSpec := score.DefaultShuffleNullSpec(spec.Seed + uint64(len(fps)))
							nullSpec.MaxRuns = spec.NullMaxRuns
							null = score.ComputeShuffleNull(fps, priorForWorkers, nullSpec)
						}
						tab := score.TrainIteratedWithShuffle(jb.a, jb.b, fps, b, spec.Iterations, spec.Lambda, null)
						scorer = score.LearnedScorerOf(b, tab, spec.Lambda)
					}
				}
				distFn := align.Distance
				if spec.AlignmentMode == "combined" {
					distFn = align.DistanceCombined
				}
				// Buffer this worker's output, then fold under mutex.
				perConcept := map[string][]cEdge{}
				var localGlobal []cEdge
				localDists := make([]float64, 0, len(fps))
				for _, fp := range fps {
					d := distFn(fp.Source, fp.Target, scorer)
					localDists = append(localDists, d)
					idx := conceptIndex[fp.Concept]
					i := idx[fp.SourceID]
					j := idx[fp.TargetID]
					if i > j {
						i, j = j, i
					}
					perConcept[fp.Concept] = append(perConcept[fp.Concept], cEdge{i: i, j: j, d: d})
					if spec.ClusterScope == "global" {
						gi := globalIndex[fp.SourceID]
						gj := globalIndex[fp.TargetID]
						if gi > gj {
							gi, gj = gj, gi
						}
						localGlobal = append(localGlobal, cEdge{i: gi, j: gj, d: d})
					}
				}
				mu.Lock()
				for c, es := range perConcept {
					conceptEdges[c] = append(conceptEdges[c], es...)
				}
				for c, es := range perConcept {
					ds := conceptDistances[c]
					for _, e := range es {
						ds = append(ds, e.d)
					}
					conceptDistances[c] = ds
				}
				if len(localGlobal) > 0 {
					globalEdges = append(globalEdges, localGlobal...)
				}
				allDistances = append(allDistances, localDists...)
				mu.Unlock()
			}
		}()
	}
	for _, p := range lectPairs {
		jobs <- job{a: p.a, b: p.b}
	}
	close(jobs)
	wg.Wait()

	// Global mode requires one more edge source: cross-concept
	// cross-lect pairs. Compute them with prior only (training is
	// concept-scope by design of PairByConcept). For typical corpora
	// these add dominant volume, so we parallelize.
	if spec.ClusterScope == "global" {
		priorOnly := align.ProsodyAwareScorer(b)
		crossEdges, crossDists := computeCrossConceptEdges(globalForms, tokens, priorOnly, workers)
		globalEdges = append(globalEdges, crossEdges...)
		allDistances = append(allDistances, crossDists...)
	}

	familyThreshold := resolveFamilyThreshold(allDistances, spec)
	res.Threshold = familyThreshold

	if spec.ClusterScope == "global" {
		res.Rows = clusterGlobal(fam, globalForms, globalEdges, familyThreshold, spec.Seed, spec.Bootstrap, spec.BootstrapSigma, spec)
		return res
	}

	// Per-concept clustering — embarrassingly parallel.
	type conceptResult struct {
		concept string
		rows    []wordlist.SidecarRow
	}
	resCh := make(chan conceptResult, len(conceptNames))
	type cjob struct{ concept string }
	cjobs := make(chan cjob, workers*2)
	var wg2 sync.WaitGroup
	for w := 0; w < workers; w++ {
		wg2.Add(1)
		go func() {
			defer wg2.Done()
			for cj := range cjobs {
				forms := conceptForms[cj.concept]
				edges := conceptEdges[cj.concept]
				threshold := resolveConceptThreshold(conceptDistances[cj.concept], familyThreshold, spec)
				rows := clusterConcept(fam, cj.concept, forms, edges, threshold, spec.Seed, spec.Bootstrap, spec.BootstrapSigma, spec)
				resCh <- conceptResult{concept: cj.concept, rows: rows}
			}
		}()
	}
	for _, c := range conceptNames {
		cjobs <- cjob{concept: c}
	}
	close(cjobs)
	wg2.Wait()
	close(resCh)
	for cr := range resCh {
		res.Rows = append(res.Rows, cr.rows...)
	}
	sort.Slice(res.Rows, func(i, j int) bool { return res.Rows[i].ID < res.Rows[j].ID })
	return res
}

func resolveFamilyThreshold(distances []float64, spec Spec) float64 {
	if spec.Threshold > 0 {
		return spec.Threshold
	}
	threshold := cluster.OtsuThreshold(distances, 0, 1, 64)
	return cluster.Clamp(threshold, spec.ThresholdMin, spec.ThresholdMax)
}

func resolveConceptThreshold(distances []float64, familyThreshold float64, spec Spec) float64 {
	if spec.Threshold > 0 {
		return spec.Threshold
	}
	if spec.ThresholdMode != "concept-shrink" {
		return familyThreshold
	}
	local := cluster.OtsuThreshold(distances, 0, 1, 64)
	local = cluster.Clamp(local, spec.ThresholdMin, spec.ThresholdMax)
	shrink := spec.ThresholdShrink
	if shrink < 0 {
		shrink = 0
	} else if shrink > 1 {
		shrink = 1
	}
	threshold := shrink*familyThreshold + (1.0-shrink)*local
	return cluster.Clamp(threshold, spec.ThresholdMin, spec.ThresholdMax)
}

type cEdge struct {
	i, j int
	d    float64
}

// computeCrossConceptEdges produces distance edges for every pair of
// cross-lect cross-concept forms, using the prior scorer only
// (learned scorers are concept-scope by construction). Runs in
// parallel chunks; O(n²) work is the bottleneck so we tile the row
// index across workers.
func computeCrossConceptEdges(forms []wordlist.Record, tokens map[string][]string, prior align.Scorer, workers int) ([]cEdge, []float64) {
	n := len(forms)
	if n < 2 {
		return nil, nil
	}
	chunks := workers
	if chunks < 1 {
		chunks = 1
	}
	type batch struct {
		edges []cEdge
		dists []float64
	}
	results := make([]batch, chunks)
	var wg sync.WaitGroup
	for c := 0; c < chunks; c++ {
		wg.Add(1)
		go func(c int) {
			defer wg.Done()
			local := batch{}
			for i := c; i < n; i += chunks {
				fi := forms[i]
				ti := tokens[fi.ID]
				if len(ti) == 0 {
					continue
				}
				for j := i + 1; j < n; j++ {
					fj := forms[j]
					if fi.LanguageID == fj.LanguageID || fi.Concept == fj.Concept {
						continue
					}
					tj := tokens[fj.ID]
					if len(tj) == 0 {
						continue
					}
					src, tgt := ti, tj
					if fi.LanguageID > fj.LanguageID {
						src, tgt = tj, ti
					}
					d := align.Distance(src, tgt, prior)
					local.edges = append(local.edges, cEdge{i: i, j: j, d: d})
					local.dists = append(local.dists, d)
				}
			}
			results[c] = local
		}(c)
	}
	wg.Wait()
	var e []cEdge
	var d []float64
	for _, r := range results {
		e = append(e, r.edges...)
		d = append(d, r.dists...)
	}
	return e, d
}

// clusterGlobal runs Infomap on one family-wide graph of cross-lect
// form pairs. Cluster IDs get a "<family>:global:<n>" namespace.
func clusterGlobal(fam string, forms []wordlist.Record, rawEdges []cEdge, threshold float64, seed uint64, bootstrap int, sigma float64, spec Spec) []wordlist.SidecarRow {
	n := len(forms)
	part, graph := runClustering(n, rawEdges, threshold, seed, spec)
	diagnostics := membershipDiagnostics(graph, part)
	boot := bootstrapSummary{Mean: make([]float64, n), Low: make([]float64, n), High: make([]float64, n)}
	if bootstrap > 0 {
		boot = bootstrapConfidences(n, rawEdges, part, threshold, bootstrap, sigma, seed, spec)
	}
	rows := make([]wordlist.SidecarRow, n)
	for i, f := range forms {
		rows[i] = wordlist.SidecarRow{
			ID:                 f.ID,
			CognatorID:         fmt.Sprintf("%s:global:%d", fam, part[i]),
			Confidence:         diagnostics[i].Confidence,
			AssignedSupport:    diagnostics[i].AssignedSupport,
			AlternativeSupport: diagnostics[i].AlternativeSupport,
			SupportMargin:      diagnostics[i].SupportMargin,
			Bootstrap:          boot.Mean[i],
			BootstrapLow:       boot.Low[i],
			BootstrapHigh:      boot.High[i],
		}
	}
	sort.Slice(rows, func(i, j int) bool { return rows[i].ID < rows[j].ID })
	return rows
}

func clusterConcept(fam, concept string, forms []wordlist.Record, rawEdges []cEdge, threshold float64, seed uint64, bootstrap int, sigma float64, spec Spec) []wordlist.SidecarRow {
	n := len(forms)
	part, graph := runClustering(n, rawEdges, threshold, seed, spec)
	diagnostics := membershipDiagnostics(graph, part)
	boot := bootstrapSummary{Mean: make([]float64, n), Low: make([]float64, n), High: make([]float64, n)}
	if bootstrap > 0 {
		boot = bootstrapConfidences(n, rawEdges, part, threshold, bootstrap, sigma, seed, spec)
	}
	rows := make([]wordlist.SidecarRow, n)
	for i, f := range forms {
		rows[i] = wordlist.SidecarRow{
			ID:                 f.ID,
			CognatorID:         fmt.Sprintf("%s:%s:%d", fam, concept, part[i]),
			Confidence:         diagnostics[i].Confidence,
			AssignedSupport:    diagnostics[i].AssignedSupport,
			AlternativeSupport: diagnostics[i].AlternativeSupport,
			SupportMargin:      diagnostics[i].SupportMargin,
			Bootstrap:          boot.Mean[i],
			BootstrapLow:       boot.Low[i],
			BootstrapHigh:      boot.High[i],
		}
	}
	return rows
}

func runClustering(n int, rawEdges []cEdge, threshold float64, seed uint64, spec Spec) ([]int, *cluster.Graph) {
	pairs := make([]evidence.Pair, 0, len(rawEdges))
	for _, e := range rawEdges {
		pairs = append(pairs, evidence.Pair{A: e.i, B: e.j, Distance: e.d})
	}
	edges := evidence.BuildGraphEdges(n, pairs, threshold, evidence.Spec{
		Mode:               spec.GraphMode,
		ExpansionMode:      spec.GraphExpansionMode,
		TopK:               spec.GraphTopK,
		AdaptiveMargin:     spec.GraphAdaptiveMargin,
		AdaptiveEitherMax:  spec.GraphAdaptiveEitherMax,
		AdaptiveAmbiguity:  spec.GraphAdaptiveAmbiguity,
		MutualBoost:        spec.GraphMutualBoost,
		TriangleBoost:      spec.GraphTriangleBoost,
		ContrastivePenalty: spec.GraphContrastivePenalty,
		ContrastiveFloor:   spec.GraphContrastiveFloor,
		RiskRankWeight:     spec.GraphRiskRankWeight,
		RiskGapWeight:      spec.GraphRiskGapWeight,
		UnilateralPenalty:  spec.GraphUnilateralPenalty,
		RivalFloor:         spec.GraphRivalFloor,
	})
	g := cluster.NewGraph(n, edges)
	part := cluster.Infomap(g, seed)
	part = refinePartition(g, part, spec)
	return part, g
}

type bootstrapSummary struct {
	Mean []float64
	Low  []float64
	High []float64
}

func bootstrapConfidences(n int, rawEdges []cEdge, canonical []int, threshold float64, K int, sigma float64, baseSeed uint64, spec Spec) bootstrapSummary {
	out := bootstrapSummary{
		Mean: make([]float64, n),
		Low:  make([]float64, n),
		High: make([]float64, n),
	}
	if K <= 0 || n == 0 {
		return out
	}
	canonMembers := membershipSets(canonical)
	rng := rand.New(rand.NewPCG(baseSeed, baseSeed^0xABCDEF0123456789))
	perturbed := make([]cEdge, len(rawEdges))
	samples := make([][]float64, n)
	for i := range samples {
		samples[i] = make([]float64, 0, K)
	}
	for r := 0; r < K; r++ {
		for i, e := range rawEdges {
			d := e.d + rng.NormFloat64()*sigma
			if d < 0 {
				d = 0
			} else if d > 1 {
				d = 1
			}
			perturbed[i] = cEdge{i: e.i, j: e.j, d: d}
		}
		part, _ := runClustering(n, perturbed, threshold, baseSeed+uint64(r)+1, spec)
		runMembers := membershipSets(part)
		for i := 0; i < n; i++ {
			canon := canonMembers[canonical[i]]
			run := runMembers[part[i]]
			score := jaccardExcl(canon, run, i)
			out.Mean[i] += score
			samples[i] = append(samples[i], score)
		}
	}
	for i := 0; i < n; i++ {
		out.Mean[i] /= float64(K)
		sort.Float64s(samples[i])
		out.Low[i] = quantileSorted(samples[i], 0.05)
		out.High[i] = quantileSorted(samples[i], 0.95)
	}
	return out
}

func membershipSets(part []int) map[int]map[int]struct{} {
	out := map[int]map[int]struct{}{}
	for i, m := range part {
		s, ok := out[m]
		if !ok {
			s = map[int]struct{}{}
			out[m] = s
		}
		s[i] = struct{}{}
	}
	return out
}

func jaccardExcl(a, b map[int]struct{}, excl int) float64 {
	// Compute |A∩B| and |A∪B| treating a and b as sets, excluding excl.
	var inter, union int
	seen := map[int]struct{}{}
	for x := range a {
		if x == excl {
			continue
		}
		seen[x] = struct{}{}
		union++
		if _, ok := b[x]; ok {
			inter++
		}
	}
	for x := range b {
		if x == excl {
			continue
		}
		if _, ok := seen[x]; ok {
			continue
		}
		union++
	}
	if union == 0 {
		return 1.0 // stable singleton
	}
	return float64(inter) / float64(union)
}

type membershipDiagnostic struct {
	Confidence         float64
	AssignedSupport    float64
	AlternativeSupport float64
	SupportMargin      float64
}

func membershipDiagnostics(g *cluster.Graph, part []int) []membershipDiagnostic {
	out := make([]membershipDiagnostic, len(part))
	if g == nil || len(part) == 0 {
		return out
	}
	for i := range part {
		profile := nodeSupportProfile(g, part, i)
		out[i] = membershipDiagnostic{
			Confidence:         profile.ConfidenceTotal(),
			AssignedSupport:    profile.InTotal,
			AlternativeSupport: profile.BestAltTotal,
			SupportMargin:      profile.InTotal - profile.BestAltTotal,
		}
	}
	return out
}

func maxMapValue(m map[int]float64) float64 {
	var best float64
	for _, v := range m {
		if v > best {
			best = v
		}
	}
	return best
}

func quantileSorted(xs []float64, q float64) float64 {
	if len(xs) == 0 {
		return 0
	}
	if q <= 0 {
		return xs[0]
	}
	if q >= 1 {
		return xs[len(xs)-1]
	}
	pos := q * float64(len(xs)-1)
	lo := int(pos)
	hi := lo + 1
	if hi >= len(xs) {
		return xs[lo]
	}
	frac := pos - float64(lo)
	return xs[lo]*(1-frac) + xs[hi]*frac
}

func refinePartition(g *cluster.Graph, part []int, spec Spec) []int {
	if g == nil || len(part) == 0 || spec.RefineMode == "none" {
		return part
	}
	if spec.RefineMode == "bridge-cut" {
		return refinePartitionBridgeCut(g, part, spec.RefineConfidenceThreshold, spec.RefineBridgeRatio)
	}
	return part
}

type supportProfile struct {
	InMean       float64
	InTotal      float64
	InCount      int
	BestAltMod   int
	BestAltMean  float64
	BestAltTotal float64
	BestAltCount int
}

func nodeSupportProfile(g *cluster.Graph, part []int, node int) supportProfile {
	var profile supportProfile
	profile.BestAltMod = -1
	alt := map[int]float64{}
	altCount := map[int]int{}
	for _, adj := range g.Adj[node] {
		if part[adj.Target] == part[node] {
			profile.InTotal += adj.Weight
			profile.InCount++
			continue
		}
		alt[part[adj.Target]] += adj.Weight
		altCount[part[adj.Target]]++
	}
	if profile.InCount > 0 {
		profile.InMean = profile.InTotal / float64(profile.InCount)
	}
	for mod, total := range alt {
		if total > profile.BestAltTotal {
			profile.BestAltTotal = total
			profile.BestAltMod = mod
			profile.BestAltCount = altCount[mod]
		}
	}
	if profile.BestAltCount > 0 {
		profile.BestAltMean = profile.BestAltTotal / float64(profile.BestAltCount)
	}
	return profile
}

func confidenceFromSupport(inMean, altWeight float64) float64 {
	if inMean <= 0 {
		if altWeight <= 0 {
			return 1.0
		}
		return 1.0 - altWeight
	}
	if altWeight <= 0 {
		return 1.0
	}
	return inMean / (inMean + altWeight)
}

func (p supportProfile) ConfidenceMean() float64 {
	return confidenceFromSupport(p.InMean, p.BestAltMean)
}

func (p supportProfile) ConfidenceTotal() float64 {
	if p.InTotal <= 0 {
		if p.BestAltTotal <= 0 {
			return 1.0
		}
		conf := 1.0 - p.BestAltTotal
		if conf < 0 {
			return 0
		}
		return conf
	}
	if p.BestAltTotal <= 0 {
		return 1.0
	}
	return p.InTotal / (p.InTotal + p.BestAltTotal)
}

func refinePartitionBridgeCut(g *cluster.Graph, part []int, confidenceThreshold, bridgeRatio float64) []int {
	if g == nil || len(part) == 0 {
		return part
	}
	members := membershipSets(part)
	out := append([]int(nil), part...)
	next := 0
	for _, nodes := range members {
		blocked := map[int]map[int]bool{}
		for v := range nodes {
			profile := nodeSupportProfile(g, part, v)
			conf := profile.ConfidenceMean()
			// Bridge-cut is allowed either for globally low-confidence nodes
			// or for nodes whose internal cohesion is itself weak.
			if (conf >= confidenceThreshold && profile.InMean >= confidenceThreshold) || profile.InMean <= 0 {
				continue
			}
			for _, adj := range g.Adj[v] {
				if part[adj.Target] != part[v] {
					continue
				}
				if adj.Weight >= profile.InMean*bridgeRatio {
					continue
				}
				if blocked[v] == nil {
					blocked[v] = map[int]bool{}
				}
				if blocked[adj.Target] == nil {
					blocked[adj.Target] = map[int]bool{}
				}
				blocked[v][adj.Target] = true
				blocked[adj.Target][v] = true
			}
		}

		seen := map[int]bool{}
		for start := range nodes {
			if seen[start] {
				continue
			}
			stack := []int{start}
			seen[start] = true
			component := []int{}
			for len(stack) > 0 {
				v := stack[len(stack)-1]
				stack = stack[:len(stack)-1]
				component = append(component, v)
				for _, adj := range g.Adj[v] {
					if part[adj.Target] != part[v] || seen[adj.Target] {
						continue
					}
					if blocked[v] != nil && blocked[v][adj.Target] {
						continue
					}
					seen[adj.Target] = true
					stack = append(stack, adj.Target)
				}
			}
			for _, v := range component {
				out[v] = next
			}
			next++
		}
	}
	return out
}

func groupByFamily(recs []wordlist.Record) map[string][]wordlist.Record {
	out := map[string][]wordlist.Record{}
	for _, r := range recs {
		out[r.Family] = append(out[r.Family], r)
	}
	return out
}

func sortedKeys[T any](m map[string]T) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

func uniqueLects(recs []wordlist.Record) []string {
	seen := map[string]struct{}{}
	for _, r := range recs {
		seen[r.LanguageID] = struct{}{}
	}
	out := make([]string, 0, len(seen))
	for k := range seen {
		out = append(out, k)
	}
	sort.Strings(out)
	return out
}

type orderedPair struct {
	a, b string
}

func pairList(lects []string) []orderedPair {
	var out []orderedPair
	for i := 0; i < len(lects); i++ {
		for j := i + 1; j < len(lects); j++ {
			out = append(out, orderedPair{lects[i], lects[j]})
		}
	}
	return out
}
