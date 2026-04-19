package pipeline

import (
	"testing"

	"github.com/tresoldi/cognator/internal/eval"
	"github.com/tresoldi/cognator/internal/wordlist"
)

// shiftedCorpus encodes a semantic shift that can only be recovered
// by cross-concept clustering.
//
// Three lects A, B, C. Two concepts NIGHT and EVENING.
//
// Historical story: Proto-* had one root /paka/ for "night" and
// /tumi/ for "evening". In lect C, semantic shift swapped the
// two: C uses /paka/-descendant for EVENING and /tumi/-descendant
// for NIGHT. So the true cognate groups are {all forms descending
// from /paka/} and {all forms descending from /tumi/}, regardless
// of which concept they appear under.
//
// Per-concept clustering cannot recover this — in concept NIGHT it
// sees {paka_A, paka_B, tumi_C} and correctly merges A with B but
// isolates C, giving 2 clusters where 1 of the 3 should merge with
// C's EVENING form.
//
// Global clustering sees both concepts at once and can merge all
// three /paka/-reflexes and all three /tumi/-reflexes.
func shiftedCorpus() ([]wordlist.Record, map[string]string) {
	recs := []wordlist.Record{
		// NIGHT: A and B retain /paka/; C shifted — uses /tumi/ here.
		{ID: "A_night", LanguageID: "A", Concept: "NIGHT", Segments: "p a k a", Family: "syn"},
		{ID: "B_night", LanguageID: "B", Concept: "NIGHT", Segments: "p a k a", Family: "syn"},
		{ID: "C_night", LanguageID: "C", Concept: "NIGHT", Segments: "t u m i", Family: "syn"},
		// EVENING: A and B retain /tumi/; C shifted — uses /paka/ here.
		{ID: "A_evening", LanguageID: "A", Concept: "EVENING", Segments: "t u m i", Family: "syn"},
		{ID: "B_evening", LanguageID: "B", Concept: "EVENING", Segments: "t u m i", Family: "syn"},
		{ID: "C_evening", LanguageID: "C", Concept: "EVENING", Segments: "p a k a", Family: "syn"},
	}
	truth := map[string]string{
		"A_night":   "paka",
		"B_night":   "paka",
		"C_evening": "paka",
		"A_evening": "tumi",
		"B_evening": "tumi",
		"C_night":   "tumi",
	}
	return recs, truth
}

func TestConceptModeCannotRecoverSemanticShift(t *testing.T) {
	recs, truth := shiftedCorpus()
	spec := Spec{Iterations: 2, Lambda: 0.5, Seed: 1, ClusterScope: "concept"}
	res := Detect(recs, bundle, spec)
	pred := map[string]string{}
	for _, r := range res.Rows {
		pred[r.ID] = r.CognatorID
	}
	_, r, f := eval.BCubed(pred, truth)
	t.Logf("concept mode: R=%.3f F=%.3f  pred=%v", r, f, pred)
	if r >= 0.7 {
		t.Errorf("concept-mode recall should be degraded on semantic shift, got R=%.3f", r)
	}
}

func TestGlobalModeRecoversSemanticShift(t *testing.T) {
	recs, truth := shiftedCorpus()
	spec := Spec{Iterations: 2, Lambda: 0.5, Seed: 1, ClusterScope: "global"}
	res := Detect(recs, bundle, spec)
	pred := map[string]string{}
	for _, r := range res.Rows {
		pred[r.ID] = r.CognatorID
	}
	p, r, f := eval.BCubed(pred, truth)
	t.Logf("global mode: P=%.3f R=%.3f F=%.3f  pred=%v", p, r, f, pred)
	// Expect both paka-descendants and both tumi-descendants to have
	// been merged across concepts. A perfect run recovers F=1.0.
	if f < 0.9 {
		t.Errorf("global-mode F=%.3f; expected ≥0.9 on the clean synthetic", f)
	}
}

// Narrative: on walworth Polynesian, global-mode catastrophically
// loses precision because the expert Cognacy partitions within
// concept — every cross-concept merge is a false positive. Log F
// in both modes so future regressions or improvements are visible.
// Slow (O(V²·M) Infomap at family scale); skipped under -short.
func TestGlobalVsConceptModeWalworthNarrative(t *testing.T) {
	if testing.Short() {
		t.Skip("slow; run without -short to execute global-mode on full walworth")
	}
	if !walworthAvailable(t) {
		t.Skip("arcaverborum not present")
	}
	recs := walworthSubset(t)
	truth := map[string]string{}
	for _, r := range recs {
		if r.Cognacy != "" {
			truth[r.ID] = r.Concept + ":" + r.Cognacy
		}
	}

	runMode := func(scope string) (float64, float64, float64, float64) {
		spec := Spec{Iterations: 3, Lambda: 0.5, Seed: 1, ClusterScope: scope}
		res := Detect(recs, bundle, spec)
		pred := map[string]string{}
		for _, r := range res.Rows {
			pred[r.ID] = r.CognatorID
		}
		p, r, f := eval.BCubed(pred, truth)
		return p, r, f, res.Threshold
	}

	pC, rC, fC, thC := runMode("concept")
	pG, rG, fG, thG := runMode("global")
	t.Logf("concept: P=%.4f R=%.4f F=%.4f  threshold=%.3f", pC, rC, fC, thC)
	t.Logf("global:  P=%.4f R=%.4f F=%.4f  threshold=%.3f", pG, rG, fG, thG)
	// Cross-concept mode is expected to catastrophically lose precision
	// when the truth table partitions within concept (as walworth's
	// Cognacy does). Just log for now; don't hard-assert.
}

func walworthAvailable(t *testing.T) bool {
	t.Helper()
	recs := walworthSubset(t)
	return len(recs) > 100
}

func walworthSubset(t *testing.T) []wordlist.Record {
	t.Helper()
	all, err := wordlist.ReadTable("/tmp/arcaverborum_corecog/arcaverborum-A-corecog-20251008/forms.csv")
	if err != nil {
		return nil
	}
	want := map[string]bool{
		"walworthpolynesian_Hawaiian": true,
		"walworthpolynesian_Samoan":   true,
		"walworthpolynesian_Tongan":   true,
		"walworthpolynesian_Maori":    true,
		"walworthpolynesian_Tahitian": true,
		"walworthpolynesian_Niuean":   true,
		"walworthpolynesian_Tuvalu":   true,
	}
	var out []wordlist.Record
	for _, r := range all {
		if r.Family == "walworthpolynesian" && want[r.LanguageID] && r.Cognacy != "" {
			out = append(out, r)
		}
	}
	return out
}
