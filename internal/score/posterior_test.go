package score

import (
	"math"
	"testing"

	"github.com/tresoldi/cognator/internal/merkmal"
)

func loadTestBundle(t *testing.T) *merkmal.Bundle {
	t.Helper()
	b, err := merkmal.LoadBundle("../../tests/fixtures/merkmal/descriptive")
	if err != nil {
		t.Fatalf("load bundle: %v", err)
	}
	return b
}

func approxEqual(a, b, tol float64) bool {
	diff := a - b
	if diff < 0 {
		diff = -diff
	}
	return diff <= tol
}

func TestDirichletPriorAlphaSumsToConcentration(t *testing.T) {
	b := loadTestBundle(t)
	inv := []string{"a", "e", "i", "o", "u", "k", "p", "t", Gap}
	prior := NewDirichletPrior(b, 5.0, 4.0, inv)

	for _, src := range inv {
		var sum float64
		for _, tgt := range inv {
			sum += prior.Alpha(src, tgt)
		}
		if !approxEqual(sum, 5.0, 1e-9) {
			t.Errorf("source %q: Σ_τ Alpha = %v, want 5.0", src, sum)
		}
	}
}

func TestDirichletPriorAlphaZeroOutsideInventory(t *testing.T) {
	b := loadTestBundle(t)
	prior := NewDirichletPrior(b, 5.0, 4.0, []string{"a", "e", Gap})
	if got := prior.Alpha("a", "z"); got != 0 {
		t.Fatalf("Alpha for out-of-inventory target = %v, want 0", got)
	}
}

func TestDirichletPriorFavorsShorterDistance(t *testing.T) {
	b := loadTestBundle(t)
	// p→b should have a much smaller merkmal distance than p→a, so
	// under the softmax prior Alpha(p, b) > Alpha(p, a).
	if b.Distance("p", "b") >= b.Distance("p", "a") {
		t.Skip("bundle does not rank p/b closer than p/a; test assumption false")
	}
	inv := []string{"p", "b", "a", Gap}
	prior := NewDirichletPrior(b, 5.0, 4.0, inv)
	ab := prior.Alpha("p", "b")
	aa := prior.Alpha("p", "a")
	if ab <= aa {
		t.Fatalf("Alpha(p→b)=%v should exceed Alpha(p→a)=%v under the softmax prior", ab, aa)
	}
}

func TestPosteriorMeanAtZeroCountsEqualsPriorMean(t *testing.T) {
	b := loadTestBundle(t)
	inv := []string{"a", "e", "i", "o", "u", Gap}
	prior := NewDirichletPrior(b, 5.0, 4.0, inv)
	tab := NewPosteriorTable("S", "T", prior)

	for _, src := range inv {
		for _, tgt := range inv {
			got := tab.PosteriorMean(src, tgt)
			want := prior.Alpha(src, tgt) / prior.Concentration
			if !approxEqual(got, want, 1e-9) {
				t.Errorf("PosteriorMean(%q,%q) at zero counts = %v, want prior mean %v", src, tgt, got, want)
			}
		}
	}
}

func TestPosteriorMeanApproachesMLEWithManyObservations(t *testing.T) {
	b := loadTestBundle(t)
	inv := []string{"a", "e", "i", "o", "u", Gap}
	prior := NewDirichletPrior(b, 5.0, 4.0, inv)
	tab := NewPosteriorTable("S", "T", prior)
	// Inject 1000 observations of (p → b). p/b are *not* in the
	// inventory but Add still accepts them: counts dominate the
	// posterior even without a non-zero prior.
	for i := 0; i < 1000; i++ {
		tab.Add("p", "b")
	}
	// MLE: P(b|p) = 1.0
	got := tab.PosteriorMean("p", "b")
	if got < 0.99 {
		t.Fatalf("PosteriorMean(p,b) with 1000 observations = %v, want ≈1.0 (MLE regime)", got)
	}
}

func TestPosteriorIsAsymmetric(t *testing.T) {
	b := loadTestBundle(t)
	inv := []string{"p", "b", Gap}
	prior := NewDirichletPrior(b, 5.0, 4.0, inv)
	fwd := NewPosteriorTable("A", "B", prior)
	rev := NewPosteriorTable("B", "A", prior)
	// Imagine A→B strongly maps p→b, while B→A rarely does.
	for i := 0; i < 20; i++ {
		fwd.Add("p", "b")
	}
	rev.Add("p", "b") // only once in the reverse direction
	// Forward posterior should be dominated by observations (~0.95+);
	// reverse posterior should stay near the prior.
	pf := fwd.PosteriorMean("p", "b")
	pr := rev.PosteriorMean("p", "b")
	if pf <= pr {
		t.Fatalf("asymmetry lost: forward=%v reverse=%v (forward should be much larger)", pf, pr)
	}
}

func TestLogOddsSignsWithRespectToNull(t *testing.T) {
	b := loadTestBundle(t)
	inv := []string{"a", "e", "i", "o", "u", Gap}
	prior := NewDirichletPrior(b, 5.0, 4.0, inv)
	tab := NewPosteriorTable("S", "T", prior)
	// Heavy evidence for (a→e); observe other combinations sparsely.
	for i := 0; i < 50; i++ {
		tab.Add("a", "e")
	}
	for _, tgt := range []string{"a", "i", "o", "u"} {
		tab.Add("a", tgt)
		tab.Add("e", tgt)
		tab.Add("i", tgt)
	}
	// (a→e) should score strictly positive (favored over target marginal).
	lo := tab.LogOdds("a", "e")
	if lo <= 0 {
		t.Fatalf("LogOdds(a,e) = %v, want >0 after dominant observations", lo)
	}
	// A target that never co-occurs with source a should have
	// posterior mean near prior but null mean near the observed
	// marginal — sign depends on details, but the score for the
	// heavily-observed cell must exceed any lightly-observed cell.
	loOther := tab.LogOdds("a", "i")
	if lo <= loOther {
		t.Fatalf("LogOdds(a,e)=%v should dominate LogOdds(a,i)=%v", lo, loOther)
	}
}

func TestPosteriorScorerReturnsBoundedSimilarity(t *testing.T) {
	b := loadTestBundle(t)
	inv := []string{"a", "e", "i", "o", "u", Gap}
	prior := NewDirichletPrior(b, 5.0, 4.0, inv)
	tab := NewPosteriorTable("S", "T", prior)
	// Concentrate evidence on (a→e) but keep the null distribution
	// spread across targets so posterior-over-null stays informative.
	for i := 0; i < 500; i++ {
		tab.Add("a", "e")
	}
	for i := 0; i < 100; i++ {
		tab.Add("i", "i")
		tab.Add("o", "o")
		tab.Add("u", "u")
	}
	sc := PosteriorScorer(b, tab)
	s := sc.Score("a", "e")
	if s < -1 || s > 1 {
		t.Fatalf("score %v out of expected [-1, 1] range", s)
	}
	if s <= 0 {
		t.Fatalf("score for heavily-observed (a,e) = %v, want positive", s)
	}
}

func TestTrainPosteriorAccumulatesAlignmentColumns(t *testing.T) {
	b := loadTestBundle(t)
	pairs := []FormPair{
		{Source: []string{"p", "a"}, Target: []string{"b", "a"}, Concept: "C1", SourceID: "s1", TargetID: "t1"},
		{Source: []string{"p", "e"}, Target: []string{"b", "e"}, Concept: "C2", SourceID: "s2", TargetID: "t2"},
	}
	prior := NewDirichletPrior(b, 5.0, 4.0, InventoryFromPairs(pairs))
	scorer := PosteriorScorer(b, NewPosteriorTable("A", "B", prior))
	tab := TrainPosterior("A", "B", pairs, scorer, prior)
	if tab.TotalPairs() != 2 {
		t.Fatalf("TotalPairs = %d, want 2", tab.TotalPairs())
	}
	if tab.Count("p", "b") < 1 {
		t.Fatalf("expected p→b to align at least once, got %d", tab.Count("p", "b"))
	}
	if tab.Count("a", "a") < 1 {
		t.Fatalf("expected a→a to align, got %d", tab.Count("a", "a"))
	}
}

func TestTrainPosteriorIteratedSharpensCorrespondences(t *testing.T) {
	b := loadTestBundle(t)
	// Ten pairs of the form p*k ↔ b*g where * is a vowel, so the
	// corresponding columns unambiguously align p↔b and k↔g.
	vowels := []string{"a", "e", "i", "o", "u"}
	pairs := make([]FormPair, 0, 2*len(vowels))
	for i, v1 := range vowels {
		for _, v2 := range vowels {
			pairs = append(pairs, FormPair{
				Source:   []string{"p", v1, "k"},
				Target:   []string{"b", v2, "g"},
				Concept:  "C",
				SourceID: "s", TargetID: "t",
			})
			_ = i
		}
	}
	prior := NewDirichletPrior(b, 5.0, 4.0, InventoryFromPairs(pairs))
	tab := TrainPosteriorIterated("A", "B", pairs, b, 3, prior)
	// After 3 iterations the posterior should strongly prefer p→b and
	// k→g over any competing target. We check dominance rather than
	// absolute values so the test is robust to α₀ choices.
	for _, tgt := range []string{"g", "k", "a", "e", Gap} {
		if tab.PosteriorMean("p", "b") <= tab.PosteriorMean("p", tgt) {
			t.Errorf("p→b should dominate p→%s, got %v vs %v", tgt, tab.PosteriorMean("p", "b"), tab.PosteriorMean("p", tgt))
		}
	}
	for _, tgt := range []string{"b", "p", "a", "e", Gap} {
		if tab.PosteriorMean("k", "g") <= tab.PosteriorMean("k", tgt) {
			t.Errorf("k→g should dominate k→%s, got %v vs %v", tgt, tab.PosteriorMean("k", "g"), tab.PosteriorMean("k", tgt))
		}
	}
}

func TestInventoryFromPairsIncludesGap(t *testing.T) {
	pairs := []FormPair{
		{Source: []string{"p", "a"}, Target: []string{"b", "a"}},
	}
	got := InventoryFromPairs(pairs)
	found := false
	for _, g := range got {
		if g == Gap {
			found = true
		}
	}
	if !found {
		t.Fatalf("InventoryFromPairs(%v) = %v, missing Gap", pairs, got)
	}
}

func TestDefaultDirichletConstants(t *testing.T) {
	// Guard the documented defaults — changing them should be a
	// deliberate act visible in the diff.
	if !approxEqual(DefaultDirichletConcentration, 5.0, 1e-12) {
		t.Errorf("DefaultDirichletConcentration = %v, want 5.0", DefaultDirichletConcentration)
	}
	if !approxEqual(DefaultDirichletSharpness, 4.0, 1e-12) {
		t.Errorf("DefaultDirichletSharpness = %v, want 4.0", DefaultDirichletSharpness)
	}
	if !approxEqual(posteriorScorerScale, 3.0, 1e-12) {
		t.Errorf("posteriorScorerScale = %v, want 3.0", posteriorScorerScale)
	}
	_ = math.E // keep import useful
}
