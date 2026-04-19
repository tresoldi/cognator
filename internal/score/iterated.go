package score

import (
	"github.com/tresoldi/cognator/internal/align"
	"github.com/tresoldi/cognator/internal/merkmal"
)

// TrainIterated runs EM-style refinement: align with the current
// scorer, tally correspondences, wrap them into a new LearnedScorer,
// and repeat. Returns the final correspondence table.
//
// With iterations=1 this is equivalent to Train(..., prior). With
// iterations>1 each round uses the previous round's learned scorer
// for alignment, which lets systematic correspondences surface that
// the prior alignment would have split across gaps.
//
// lambda controls how strongly the learned signal blends with the
// prior during each alignment pass (see LearnedScorer).
//
// The expectation model is marginal-product. For shuffle-null see
// TrainIteratedWithShuffle.
func TrainIterated(srcLect, tgtLect string, pairs []FormPair, bundle *merkmal.Bundle, iterations int, lambda float64) *CorrespondenceTable {
	return TrainIteratedWithShuffle(srcLect, tgtLect, pairs, bundle, iterations, lambda, nil)
}

// TrainIteratedWithShuffle is TrainIterated but attaches a
// precomputed shuffle null to every iteration's table. The null is
// computed once under the prior and reused across iterations: the
// EM loop only re-estimates attested counts, not the null.
//
// Pass nil for null to fall back to marginal-product expectation.
func TrainIteratedWithShuffle(srcLect, tgtLect string, pairs []FormPair, bundle *merkmal.Bundle, iterations int, lambda float64, null *NullDistribution) *CorrespondenceTable {
	if iterations < 1 {
		iterations = 1
	}
	var scorer align.Scorer = align.ProsodyAwareScorer(bundle)
	var table *CorrespondenceTable
	for i := 0; i < iterations; i++ {
		if null != nil {
			table = TrainWithNull(srcLect, tgtLect, pairs, scorer, null)
		} else {
			table = Train(srcLect, tgtLect, pairs, scorer)
		}
		scorer = LearnedScorer(bundle, table, lambda)
	}
	return table
}

// LearnedScorerOf returns the LearnedScorer wrapping table at the
// supplied lambda. Sugar for pipeline consumers who have a table but
// no explicit scorer handle.
func LearnedScorerOf(bundle *merkmal.Bundle, table *CorrespondenceTable, lambda float64) align.Scorer {
	return LearnedScorer(bundle, table, lambda)
}
