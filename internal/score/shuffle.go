package score

import (
	"math"
	"math/rand/v2"

	"github.com/tresoldi/cognator/internal/align"
)

// ShuffleNullSpec configures the shuffle-based null estimator.
//
// The null is: randomly re-pair source and target forms (ignoring
// which concept originally linked them), align the shuffled pairs
// under scorer, and count column-level grapheme correspondences.
// Averaged over runs, this gives the expected correspondence counts
// under "no historical signal" — what we'd see if forms were
// associated at random.
//
// Unlike lingpy's fixed runs=1000, this estimator uses ADAPTIVE
// STOPPING: we track per-cell running means of the top-K most
// attested correspondences, and stop when all top-K running means
// have relative change below Tolerance for PatienceRuns consecutive
// checkpoints (default: stop when stable for 3 consecutive 10-run
// windows). This avoids the magic-number-1000 and converges faster
// on corpora where the signal is strong.
type ShuffleNullSpec struct {
	MinRuns      int     // minimum runs before checking convergence (default 20)
	MaxRuns      int     // absolute cap (default 200)
	Checkpoint   int     // convergence-check interval in runs (default 10)
	TopK         int     // number of top-attested cells to track for stability (default 50)
	Tolerance    float64 // per-cell relative change threshold (default 0.05 = 5%)
	PatienceRuns int     // consecutive stable checkpoints before stopping (default 3)
	Seed         uint64  // PRNG seed
}

// DefaultShuffleNullSpec returns sensible defaults derived from
// observed convergence behavior on CoreCog datasets.
func DefaultShuffleNullSpec(seed uint64) ShuffleNullSpec {
	return ShuffleNullSpec{
		MinRuns:      20,
		MaxRuns:      200,
		Checkpoint:   10,
		TopK:         50,
		Tolerance:    0.05,
		PatienceRuns: 3,
		Seed:         seed,
	}
}

// ComputeShuffleNull builds a shuffle-based null by repeatedly
// re-pairing source forms with randomly-chosen target forms from
// the same pool, aligning under scorer, and accumulating column
// counts. Stops when top-cell running means stabilize.
func ComputeShuffleNull(pairs []FormPair, scorer align.Scorer, spec ShuffleNullSpec) *NullDistribution {
	if spec.MinRuns <= 0 {
		spec.MinRuns = 20
	}
	if spec.MaxRuns < spec.MinRuns {
		spec.MaxRuns = spec.MinRuns
	}
	if spec.Checkpoint <= 0 {
		spec.Checkpoint = 10
	}
	if spec.TopK <= 0 {
		spec.TopK = 50
	}
	if spec.Tolerance <= 0 {
		spec.Tolerance = 0.05
	}
	if spec.PatienceRuns <= 0 {
		spec.PatienceRuns = 3
	}

	n := len(pairs)
	null := &NullDistribution{Expected: map[string]map[string]float64{}}
	if n < 2 {
		return null
	}

	rng := rand.New(rand.NewPCG(spec.Seed, spec.Seed^0x5A5A5A5A5A5A5A5A))
	totalCols := 0
	runsDone := 0
	stableCheckpoints := 0
	var prevRatios map[cellKey]float64

	for runsDone < spec.MaxRuns {
		perm := rng.Perm(n)
		for i := 0; i < n; i++ {
			src := pairs[i].Source
			tgt := pairs[perm[i]].Target
			if len(src) == 0 || len(tgt) == 0 {
				continue
			}
			al := align.NeedlemanWunsch(src, tgt, scorer)
			for c := range al.Source {
				s := al.Source[c]
				u := al.Target[c]
				if s == "" {
					s = Gap
				}
				if u == "" {
					u = Gap
				}
				inner, ok := null.Expected[s]
				if !ok {
					inner = map[string]float64{}
					null.Expected[s] = inner
				}
				inner[u]++
				totalCols++
			}
		}
		runsDone++

		if runsDone >= spec.MinRuns && runsDone%spec.Checkpoint == 0 {
			curRatios := topRatios(null.Expected, spec.TopK)
			if converged(prevRatios, curRatios, spec.Tolerance) {
				stableCheckpoints++
				if stableCheckpoints >= spec.PatienceRuns {
					break
				}
			} else {
				stableCheckpoints = 0
			}
			prevRatios = curRatios
		}
	}

	div := float64(runsDone)
	if div < 1 {
		div = 1
	}
	for _, inner := range null.Expected {
		for k := range inner {
			inner[k] /= div
		}
	}
	null.TotalColumnsMean = totalCols / int(div)
	return null
}

type cellKey struct{ s, t string }

// topRatios returns the running-mean ratio (count/total) of the K
// highest-count cells in the shuffle accumulator. Tracking ratios
// rather than raw counts makes the convergence test invariant to
// total-observation growth.
func topRatios(exp map[string]map[string]float64, k int) map[cellKey]float64 {
	type entry struct {
		k cellKey
		v float64
	}
	var all []entry
	var total float64
	for s, inner := range exp {
		for t, v := range inner {
			all = append(all, entry{cellKey{s, t}, v})
			total += v
		}
	}
	if total == 0 {
		return nil
	}
	// Partial sort: pick top K.
	if k > len(all) {
		k = len(all)
	}
	// Simple selection sort for top-k (k is tiny, ~50).
	for i := 0; i < k; i++ {
		best := i
		for j := i + 1; j < len(all); j++ {
			if all[j].v > all[best].v {
				best = j
			}
		}
		all[i], all[best] = all[best], all[i]
	}
	out := make(map[cellKey]float64, k)
	for i := 0; i < k; i++ {
		out[all[i].k] = all[i].v / total
	}
	return out
}

func converged(prev, cur map[cellKey]float64, tol float64) bool {
	if prev == nil {
		return false
	}
	for k, v := range cur {
		p, ok := prev[k]
		if !ok {
			return false
		}
		denom := p
		if denom < 1e-9 {
			denom = 1e-9
		}
		if math.Abs(v-p)/denom > tol {
			return false
		}
	}
	return true
}

// TrainWithNull runs Train and attaches a precomputed null
// distribution to the resulting table. Subsequent LogOdds calls
// use the shuffle null as the expectation rather than marginal
// product.
func TrainWithNull(srcLect, tgtLect string, pairs []FormPair, prior align.Scorer, null *NullDistribution) *CorrespondenceTable {
	t := Train(srcLect, tgtLect, pairs, prior)
	if null != nil {
		t.shuffleExpected = null.Expected
		t.totalShuffleCols = null.TotalColumnsMean
	}
	return t
}
