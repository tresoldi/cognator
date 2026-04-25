package score

import (
	"github.com/tresoldi/cognator/internal/align"
	"github.com/tresoldi/cognator/internal/phon"
)

// InventoryFromPairs collects the sorted unique grapheme inventory
// produced by the supplied form-pairs, plus the Gap sentinel. This is
// the natural support of the Dirichlet prior: the softmax denominator
// sums over every grapheme the training corpus actually uses.
func InventoryFromPairs(pairs []FormPair) []string {
	seen := map[string]struct{}{Gap: {}}
	for _, p := range pairs {
		for _, g := range p.Source {
			if g != "" {
				seen[g] = struct{}{}
			}
		}
		for _, g := range p.Target {
			if g != "" {
				seen[g] = struct{}{}
			}
		}
	}
	out := make([]string, 0, len(seen))
	for g := range seen {
		out = append(out, g)
	}
	return out
}

// TrainPosterior aligns every form-pair under scorer and accumulates
// alignment-column counts into a PosteriorTable. It is the Bayesian
// analog of Train: scoring and smoothing are folded into the prior
// and posterior, so there is no lambda and no MinPairs threshold.
func TrainPosterior(srcLect, tgtLect string, pairs []FormPair, scorer align.Scorer, prior *DirichletPrior) *PosteriorTable {
	t := NewPosteriorTable(srcLect, tgtLect, prior)
	for _, p := range pairs {
		if len(p.Source) == 0 || len(p.Target) == 0 {
			continue
		}
		al := align.NeedlemanWunsch(p.Source, p.Target, scorer)
		for i := range al.Source {
			t.Add(al.Source[i], al.Target[i])
		}
		t.totalPairs++
	}
	return t
}

// TrainPosteriorIterated is the Bayesian analog of TrainIterated.
// Iteration 0 aligns under the phon-similarity prior; subsequent
// iterations align under the posterior scorer derived from the
// previous pass's counts. With iterations=1 this is equivalent to
// TrainPosterior under the prior scorer.
//
// Pass a non-nil null to swap the target-marginal null for the
// permutation-based conditional derived from a shuffle run; the same
// null is attached to every iteration's table so log-odds stay
// comparable across passes.
//
// scaleMode selects the per-row α₀ scaling mode (see
// PosteriorTable.AttachScaling). Empty string keeps the flat α₀.
func TrainPosteriorIterated(srcLect, tgtLect string, pairs []FormPair, bundle *phon.Phon, iterations int, prior *DirichletPrior, null *NullDistribution, scaleMode string) *PosteriorTable {
	if iterations < 1 {
		iterations = 1
	}
	var scorer align.Scorer = align.ProsodyAwareScorer(bundle)
	var table *PosteriorTable
	for i := 0; i < iterations; i++ {
		table = TrainPosterior(srcLect, tgtLect, pairs, scorer, prior)
		if null != nil {
			table.AttachShuffleNull(null)
		}
		if scaleMode != "" {
			table.AttachScaling(scaleMode)
		}
		scorer = PosteriorScorer(bundle, table)
	}
	return table
}
