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
	"github.com/tresoldi/cognator/internal/merkmal"
	"github.com/tresoldi/cognator/internal/score"
	"github.com/tresoldi/cognator/internal/segment"
	"github.com/tresoldi/cognator/internal/wordlist"
)

// Spec is the user-visible configuration for a cognator run.
// Zero values fall back to documented defaults.
type Spec struct {
	Iterations     int     // EM passes per lect-pair (default 3)
	Lambda         float64 // learned/prior blend (default 0.5)
	Alpha          float64 // Dirichlet pseudocount (default 1.0)
	Threshold      float64 // manual override; 0 → Otsu auto-threshold
	ThresholdMin   float64 // Otsu clamp floor (default 0.2)
	ThresholdMax   float64 // Otsu clamp ceiling (default 0.8)
	Seed           uint64  // Infomap RNG seed
	Jobs           int     // concurrent workers; 0 → GOMAXPROCS
	Bootstrap      int     // confidence bootstrap runs (default 0 = off)
	BootstrapSigma float64 // stddev of edge-distance perturbation (default 0.05)
	ClusterScope   string  // "concept" (default) | "global"
	MinPairs       int     // skip training a scorer if the pair has <N form-pairs; use prior (default 5)
	AlignmentMode  string  // "global" | "combined" (default; global + local + overlap, lingpy-style)
	ClassReduce    bool    // strip IPA modifiers/diacritics upstream so scoring trains on coarse classes (NFD-strip heuristic)
	PartitionLevel string  // use merkmal's partition table at this level ("coarse"/"medium"/"fine") instead of NFD-strip; overrides ClassReduce when set
	NullMode       string  // "marginal" (default, cheap) | "shuffle" (permutation-based, more accurate on sparse pairs)
	NullMinRuns    int     // floor on shuffle runs before adaptive stopping can check (default 20)
	NullMaxRuns    int     // cap on shuffle runs per lect-pair (default 200; adaptive stopping usually stops earlier)
}

// Result is the end-to-end output.
type Result struct {
	Rows      []wordlist.SidecarRow
	Threshold float64 // actual Otsu-or-override threshold
	OOV       map[string]int
}

// Detect runs the full pipeline. recs must include Family, Concept,
// Language_ID, and Segments; Cognacy is ignored (this is cognate
// DETECTION, not evaluation).
func Detect(recs []wordlist.Record, bundle *merkmal.Bundle, spec Spec) Result {
	if spec.Iterations <= 0 {
		spec.Iterations = 3
	}
	if spec.Lambda <= 0 {
		spec.Lambda = 0.5
	}
	if spec.Alpha <= 0 {
		spec.Alpha = 1.0
	}
	if spec.ThresholdMin <= 0 {
		spec.ThresholdMin = 0.2
	}
	if spec.ThresholdMax <= 0 {
		spec.ThresholdMax = 0.8
	}
	if spec.BootstrapSigma <= 0 {
		spec.BootstrapSigma = 0.05
	}
	if spec.ClusterScope == "" {
		spec.ClusterScope = "concept"
	}
	if spec.MinPairs <= 0 {
		spec.MinPairs = 5
	}
	if spec.AlignmentMode == "" {
		spec.AlignmentMode = "combined"
	}
	if spec.NullMode == "" {
		spec.NullMode = "marginal"
	}
	if spec.NullMaxRuns <= 0 {
		spec.NullMaxRuns = 200
	}

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
			if spec.PartitionLevel != "" {
				if c := b.Partition(spec.PartitionLevel, g); c != "" {
					g = c
				} else if spec.ClassReduce {
					g = segment.ClassReduce(g)
				}
			} else if spec.ClassReduce {
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
	if spec.PartitionLevel != "" {
		lvl := spec.PartitionLevel
		reducer = func(g string) string {
			if c := b.Partition(lvl, g); c != "" {
				return c
			}
			if spec.ClassReduce {
				return segment.ClassReduce(g)
			}
			return g
		}
	} else if spec.ClassReduce {
		reducer = segment.ClassReduce
	}
	lectIndex := score.IndexByLectWithReducer(recs, b, reducer)
	lectPairs := pairList(lects)

	type job struct{ a, b string }
	jobs := make(chan job, workers*2)
	var wg sync.WaitGroup
	var mu sync.Mutex
	priorForWorkers := align.PriorScorer(b)
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
				if len(fps) < spec.MinPairs {
					scorer = priorForWorkers
				} else {
					var null *score.NullDistribution
					if spec.NullMode == "shuffle" {
						nullSpec := score.DefaultShuffleNullSpec(spec.Seed + uint64(len(fps)))
						nullSpec.MaxRuns = spec.NullMaxRuns
						if spec.NullMinRuns > 0 {
							nullSpec.MinRuns = spec.NullMinRuns
							if nullSpec.MinRuns > nullSpec.MaxRuns {
								nullSpec.MaxRuns = nullSpec.MinRuns
							}
						}
						null = score.ComputeShuffleNull(fps, priorForWorkers, nullSpec)
					}
					tab := score.TrainIteratedWithShuffle(jb.a, jb.b, fps, b, spec.Iterations, spec.Lambda, null)
					scorer = score.LearnedScorerOf(b, tab, spec.Lambda)
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
		priorOnly := align.PriorScorer(b)
		crossEdges, crossDists := computeCrossConceptEdges(globalForms, tokens, priorOnly, workers)
		globalEdges = append(globalEdges, crossEdges...)
		allDistances = append(allDistances, crossDists...)
	}

	threshold := spec.Threshold
	if threshold <= 0 {
		threshold = cluster.OtsuThreshold(allDistances, 0, 1, 64)
		threshold = cluster.Clamp(threshold, spec.ThresholdMin, spec.ThresholdMax)
	}
	res.Threshold = threshold

	if spec.ClusterScope == "global" {
		res.Rows = clusterGlobal(fam, globalForms, globalEdges, threshold, spec.Seed, spec.Bootstrap, spec.BootstrapSigma)
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
				rows := clusterConcept(fam, cj.concept, forms, edges, threshold, spec.Seed, spec.Bootstrap, spec.BootstrapSigma)
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

// computeGlobalEdges is retained for compatibility with older
// callers; currently unused internally.
func computeGlobalEdges(forms []wordlist.Record, tokens map[string][]string, pairScorers map[pairKey]align.Scorer, prior align.Scorer, workers int) []cEdge {
	n := len(forms)
	if n < 2 {
		return nil
	}
	chunks := workers * 4
	if chunks < 1 {
		chunks = 1
	}
	type batch struct {
		edges []cEdge
	}
	results := make([]batch, chunks)
	var wg sync.WaitGroup
	for c := 0; c < chunks; c++ {
		wg.Add(1)
		go func(c int) {
			defer wg.Done()
			var local []cEdge
			for i := c; i < n; i += chunks {
				fi := forms[i]
				ti := tokens[fi.ID]
				if len(ti) == 0 {
					continue
				}
				for j := i + 1; j < n; j++ {
					fj := forms[j]
					if fi.LanguageID == fj.LanguageID {
						continue
					}
					tj := tokens[fj.ID]
					if len(tj) == 0 {
						continue
					}
					s := pickScorer(pairScorers, prior, fi.LanguageID, fj.LanguageID)
					src, tgt := canonicalGraphemes(tokens, fi, fj)
					d := align.Distance(src, tgt, s)
					local = append(local, cEdge{i: i, j: j, d: d})
				}
			}
			results[c] = batch{edges: local}
		}(c)
	}
	wg.Wait()
	var out []cEdge
	for _, r := range results {
		out = append(out, r.edges...)
	}
	return out
}

// clusterGlobal runs Infomap on one family-wide graph of cross-lect
// form pairs. Cluster IDs get a "<family>:global:<n>" namespace.
func clusterGlobal(fam string, forms []wordlist.Record, rawEdges []cEdge, threshold float64, seed uint64, bootstrap int, sigma float64) []wordlist.SidecarRow {
	n := len(forms)
	part := runClustering(n, rawEdges, threshold, seed)
	confs := clusterConfidences(part, rawEdges, threshold)
	var boot []float64
	if bootstrap > 0 {
		boot = bootstrapConfidences(n, rawEdges, part, threshold, bootstrap, sigma, seed)
	} else {
		boot = make([]float64, n)
	}
	rows := make([]wordlist.SidecarRow, n)
	for i, f := range forms {
		rows[i] = wordlist.SidecarRow{
			ID:         f.ID,
			CognatorID: fmt.Sprintf("%s:global:%d", fam, part[i]),
			Confidence: confs[i],
			Bootstrap:  boot[i],
		}
	}
	sort.Slice(rows, func(i, j int) bool { return rows[i].ID < rows[j].ID })
	return rows
}

func clusterConcept(fam, concept string, forms []wordlist.Record, rawEdges []cEdge, threshold float64, seed uint64, bootstrap int, sigma float64) []wordlist.SidecarRow {
	n := len(forms)
	part := runClustering(n, rawEdges, threshold, seed)
	confs := clusterConfidences(part, rawEdges, threshold)
	var boot []float64
	if bootstrap > 0 {
		boot = bootstrapConfidences(n, rawEdges, part, threshold, bootstrap, sigma, seed)
	} else {
		boot = make([]float64, n)
	}
	rows := make([]wordlist.SidecarRow, n)
	for i, f := range forms {
		rows[i] = wordlist.SidecarRow{
			ID:         f.ID,
			CognatorID: fmt.Sprintf("%s:%s:%d", fam, concept, part[i]),
			Confidence: confs[i],
			Bootstrap:  boot[i],
		}
	}
	return rows
}

func runClustering(n int, rawEdges []cEdge, threshold float64, seed uint64) []int {
	var edges []cluster.Edge
	for _, e := range rawEdges {
		if e.d >= threshold {
			continue
		}
		w := 1.0 - e.d
		if w <= 0 {
			continue
		}
		edges = append(edges, cluster.Edge{A: e.i, B: e.j, Weight: w})
	}
	g := cluster.NewGraph(n, edges)
	return cluster.Infomap(g, seed)
}

func bootstrapConfidences(n int, rawEdges []cEdge, canonical []int, threshold float64, K int, sigma float64, baseSeed uint64) []float64 {
	out := make([]float64, n)
	if K <= 0 || n == 0 {
		return out
	}
	canonMembers := membershipSets(canonical)
	rng := rand.New(rand.NewPCG(baseSeed, baseSeed^0xABCDEF0123456789))
	perturbed := make([]cEdge, len(rawEdges))
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
		part := runClustering(n, perturbed, threshold, baseSeed+uint64(r)+1)
		runMembers := membershipSets(part)
		for i := 0; i < n; i++ {
			canon := canonMembers[canonical[i]]
			run := runMembers[part[i]]
			out[i] += jaccardExcl(canon, run, i)
		}
	}
	for i := range out {
		out[i] /= float64(K)
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

func clusterConfidences(part []int, rawEdges []cEdge, threshold float64) []float64 {
	n := len(part)
	wSum := make([]float64, n)
	wCnt := make([]int, n)
	for _, e := range rawEdges {
		if part[e.i] != part[e.j] {
			continue
		}
		if e.d >= threshold {
			continue
		}
		sim := 1.0 - e.d
		wSum[e.i] += sim
		wSum[e.j] += sim
		wCnt[e.i]++
		wCnt[e.j]++
	}
	out := make([]float64, n)
	for i := range out {
		if wCnt[i] == 0 {
			out[i] = 0
			continue
		}
		out[i] = wSum[i] / float64(wCnt[i])
	}
	return out
}

type pairKey struct{ src, tgt string }

func pickScorer(scorers map[pairKey]align.Scorer, prior align.Scorer, a, b string) align.Scorer {
	src, tgt := canonicalPair(a, b)
	if s, ok := scorers[pairKey{src, tgt}]; ok {
		return s
	}
	return prior
}

func canonicalPair(a, b string) (string, string) {
	if a < b {
		return a, b
	}
	return b, a
}

func canonicalGraphemes(tokens map[string][]string, fa, fb wordlist.Record) ([]string, []string) {
	src, tgt := canonicalPair(fa.LanguageID, fb.LanguageID)
	if fa.LanguageID == src {
		return tokens[fa.ID], tokens[fb.ID]
	}
	if fb.LanguageID == src {
		return tokens[fb.ID], tokens[fa.ID]
	}
	_ = tgt
	return tokens[fa.ID], tokens[fb.ID]
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
