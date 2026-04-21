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

func TestAttachShuffleNullShrinksLogOddsWhenNullAnticipatesObservation(t *testing.T) {
	b := loadTestBundle(t)
	inv := []string{"a", "e", "i", "o", "u", Gap}
	prior := NewDirichletPrior(b, 5.0, 4.0, inv)
	tab := NewPosteriorTable("S", "T", prior)
	// Observed: a→e always. Spread out other source rows so that the
	// observed target marginal does not itself collapse to e.
	for i := 0; i < 30; i++ {
		tab.Add("a", "e")
	}
	for i := 0; i < 10; i++ {
		tab.Add("i", "i")
		tab.Add("o", "o")
		tab.Add("u", "u")
	}

	// Null A: shuffle says (a, e) is rare under random pairing (near
	// uniform on targets). Posterior dominates null → large LogOdds.
	lowNull := &NullDistribution{
		Expected: map[string]map[string]float64{
			"a": {"e": 6.0, "a": 6.0, "i": 6.0, "o": 6.0, "u": 6.0},
		},
		TotalColumnsMean: 30,
	}
	tab.AttachShuffleNull(lowNull)
	loLow := tab.LogOdds("a", "e")

	// Null B: shuffle says (a, e) is already *expected* under random
	// pairing. Posterior barely exceeds null → small LogOdds.
	highNull := &NullDistribution{
		Expected: map[string]map[string]float64{
			"a": {"e": 28.0, "a": 0.5, "i": 0.5, "o": 0.5, "u": 0.5},
		},
		TotalColumnsMean: 30,
	}
	tab.AttachShuffleNull(highNull)
	loHigh := tab.LogOdds("a", "e")

	if loHigh >= loLow {
		t.Fatalf("shuffle null that anticipates (a,e) should shrink LogOdds relative to a flat null; highNull=%v lowNull=%v", loHigh, loLow)
	}

	tab.AttachShuffleNull(nil)
	marginalLO := tab.LogOdds("a", "e")
	if marginalLO == loLow || marginalLO == loHigh {
		// Not strictly wrong, but the marginal path should give yet
		// another value here since it uses corpus-wide target totals.
		t.Fatalf("clearing the shuffle null should route through the marginal path (marginal=%v lowNull=%v highNull=%v)", marginalLO, loLow, loHigh)
	}
}

func TestAttachScalingSparseBoostRaisesPriorWeightOnSparseRow(t *testing.T) {
	b := loadTestBundle(t)
	inv := []string{"a", "e", "i", "o", "u", Gap}
	prior := NewDirichletPrior(b, 5.0, 4.0, inv)
	tab := NewPosteriorTable("S", "T", prior)
	// Fat row: 100 observations of (a, e).
	for i := 0; i < 100; i++ {
		tab.Add("a", "e")
	}
	// Sparse row: 1 observation of (i, o).
	tab.Add("i", "o")
	// Other rows to give the median a reasonable value.
	for i := 0; i < 10; i++ {
		tab.Add("e", "i")
	}

	// Without scaling, the sparse row's posterior mean is dominated
	// by that single observation plus a small prior contribution.
	flat := tab.PosteriorMean("i", "o")

	tab.AttachScaling("sparse-boost")
	boosted := tab.PosteriorMean("i", "o")

	// On the sparse "i" row, sparse-boost should amplify the prior's
	// influence, pulling the posterior toward the merkmal prior's
	// base measure. For target "o" (far from source "i" by merkmal
	// distance) the posterior mean should DROP relative to flat.
	if !(boosted < flat) {
		t.Fatalf("sparse-boost should shrink posterior toward prior for sparse rows; flat=%v boosted=%v", flat, boosted)
	}

	// The fat row should be untouched.
	tabFlat := NewPosteriorTable("S", "T", prior)
	for i := 0; i < 100; i++ {
		tabFlat.Add("a", "e")
	}
	tabFlat.Add("i", "o")
	for i := 0; i < 10; i++ {
		tabFlat.Add("e", "i")
	}
	tabBoosted := NewPosteriorTable("S", "T", prior)
	for i := 0; i < 100; i++ {
		tabBoosted.Add("a", "e")
	}
	tabBoosted.Add("i", "o")
	for i := 0; i < 10; i++ {
		tabBoosted.Add("e", "i")
	}
	tabBoosted.AttachScaling("sparse-boost")
	// The fat "a" row is at or above median; sparse-boost leaves it alone.
	if !approxEqual(tabFlat.PosteriorMean("a", "e"), tabBoosted.PosteriorMean("a", "e"), 1e-9) {
		t.Fatalf("sparse-boost should leave rich rows untouched: flat=%v boosted=%v",
			tabFlat.PosteriorMean("a", "e"), tabBoosted.PosteriorMean("a", "e"))
	}
}

func TestAttachScalingRichBoostRegularizesRichRow(t *testing.T) {
	b := loadTestBundle(t)
	inv := []string{"a", "e", "i", "o", "u", Gap}
	prior := NewDirichletPrior(b, 5.0, 4.0, inv)
	tab := NewPosteriorTable("S", "T", prior)
	// Rich row: many observations on (a, e).
	for i := 0; i < 100; i++ {
		tab.Add("a", "e")
	}
	// Smaller rows to establish median.
	for i := 0; i < 5; i++ {
		tab.Add("e", "i")
		tab.Add("i", "u")
	}
	flat := tab.PosteriorMean("a", "e")
	tab.AttachScaling("rich-boost")
	boosted := tab.PosteriorMean("a", "e")
	// Rich-boost should pull the fat row's posterior mean toward the
	// prior (away from MLE=1.0), so boosted < flat.
	if !(boosted < flat) {
		t.Fatalf("rich-boost should shrink rich-row posterior toward prior; flat=%v boosted=%v", flat, boosted)
	}
}

func TestAttachScalingEmptyStringLeavesTableUnchanged(t *testing.T) {
	b := loadTestBundle(t)
	inv := []string{"a", "e", Gap}
	prior := NewDirichletPrior(b, 5.0, 4.0, inv)
	tab := NewPosteriorTable("S", "T", prior)
	for i := 0; i < 10; i++ {
		tab.Add("a", "e")
	}
	before := tab.PosteriorMean("a", "e")
	tab.AttachScaling("")
	if got := tab.PosteriorMean("a", "e"); got != before {
		t.Fatalf("AttachScaling(\"\") should be a no-op: before=%v after=%v", before, got)
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
	tab := TrainPosteriorIterated("A", "B", pairs, b, 3, prior, nil, "")
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
	if !approxEqual(DefaultDirichletConcentration, 50.0, 1e-12) {
		t.Errorf("DefaultDirichletConcentration = %v, want 50.0", DefaultDirichletConcentration)
	}
	if !approxEqual(DefaultDirichletSharpness, 4.0, 1e-12) {
		t.Errorf("DefaultDirichletSharpness = %v, want 4.0", DefaultDirichletSharpness)
	}
	if !approxEqual(posteriorScorerScale, 3.0, 1e-12) {
		t.Errorf("posteriorScorerScale = %v, want 3.0", posteriorScorerScale)
	}
	_ = math.E // keep import useful
}
