package score

import (
	"os"
	"testing"

	"github.com/tresoldi/cognator/internal/wordlist"
)

func TestTrainIteratedSharpensLearnedCorrespondence(t *testing.T) {
	// Systematic a→u shift. The first pass may not attribute all a~u
	// columns to the correct correspondence (prior prefers identity).
	// More iterations should increase the attested a~u count.
	fps := []FormPair{
		{Source: []string{"p", "a"}, Target: []string{"p", "u"}},
		{Source: []string{"t", "a"}, Target: []string{"t", "u"}},
		{Source: []string{"k", "a"}, Target: []string{"k", "u"}},
		{Source: []string{"m", "a"}, Target: []string{"m", "u"}},
		{Source: []string{"n", "a", "p", "a"}, Target: []string{"n", "u", "p", "u"}},
		{Source: []string{"k", "a", "l", "a"}, Target: []string{"k", "u", "l", "u"}},
	}
	one := TrainIterated("s", "t", fps, descBundle, 1, 0.4)
	three := TrainIterated("s", "t", fps, descBundle, 3, 0.4)
	if three.Count("a", "u") < one.Count("a", "u") {
		t.Errorf("iteration regressed: 1-iter a→u count=%d, 3-iter=%d", one.Count("a", "u"), three.Count("a", "u"))
	}
	// At 3 iters we expect every a to be attributed to a u.
	if three.Count("a", "u") < 7 {
		t.Errorf("3-iter a→u count = %d; expected ≥7 (full attribution)", three.Count("a", "u"))
	}
}

// Linguistic narrative: Tongan (preserves Proto-Polynesian *k) vs
// Hawaiian (merged *k > ʔ). M3 showed that single-pass training
// could not pair k~ʔ across this divide because the merkmal prior
// routes them to gaps. With EM iteration the learned scorer should
// pull k~ʔ into alignment on subsequent passes and the correspondence
// should surface as positive log-odds.
//
// Samoan is NOT the right source lect here: Samoan also lost *k > ʔ,
// so the Samoan-Hawaiian correspondence for this class is ʔ~ʔ (which
// M3 already trains strongly). Use Tongan as the k-preserving anchor.
func TestTrainIteratedSurfacesKGlottalMerger(t *testing.T) {
	path := "/tmp/arcaverborum_corecog/arcaverborum-A-corecog-20251008/forms.csv"
	if _, err := os.Stat(path); os.IsNotExist(err) {
		t.Skipf("arcaverborum not present at %s", path)
	}
	recs, err := wordlist.ReadTable(path)
	if err != nil {
		t.Fatal(err)
	}
	src := "walworthpolynesian_Tongan"
	tgt := "walworthpolynesian_Hawaiian"
	fps := PairByConcept(recs, src, tgt, descBundle)
	if len(fps) < 100 {
		t.Skipf("not enough Tongan-Hawaiian pairs (%d)", len(fps))
	}

	one := TrainIterated(src, tgt, fps, descBundle, 1, 0.5)
	five := TrainIterated(src, tgt, fps, descBundle, 5, 0.9)
	t.Logf("Tongan k → Hawaiian ʔ: 1-iter count=%d, 5-iter count=%d", one.Count("k", "ʔ"), five.Count("k", "ʔ"))
	if five.Count("k", "ʔ") <= one.Count("k", "ʔ") {
		t.Errorf("expected iterated training to surface more k→ʔ columns; 1-iter=%d, 5-iter=%d",
			one.Count("k", "ʔ"), five.Count("k", "ʔ"))
	}
	if lo := five.LogOdds(descBundle, "k", "ʔ", 1.0); lo <= 0 {
		t.Errorf("Tongan k → Hawaiian ʔ log-odds = %f after iteration; expected >0", lo)
	}
}
