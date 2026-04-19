package score

import (
	"math"
	"os"
	"strings"
	"testing"

	"github.com/tresoldi/cognator/internal/align"
	"github.com/tresoldi/cognator/internal/merkmal"
	"github.com/tresoldi/cognator/internal/wordlist"
)

var descBundle *merkmal.Bundle

func init() {
	b, err := merkmal.LoadBundle("../../tests/fixtures/merkmal/descriptive")
	if err != nil {
		panic(err)
	}
	descBundle = b
}

func split(s string) []string { return strings.Fields(s) }

func pairs(ss ...string) []FormPair {
	out := make([]FormPair, 0, len(ss)/2)
	for i := 0; i+1 < len(ss); i += 2 {
		out = append(out, FormPair{Source: split(ss[i]), Target: split(ss[i+1])})
	}
	return out
}

func TestTrainYieldsIdentityPositiveLogOdds(t *testing.T) {
	// A toy corpus where every form pair is identical: identity must
	// score strongly positive under the learned log-odds.
	fps := pairs(
		"p a k e", "p a k e",
		"m u r o", "m u r o",
		"t i l a", "t i l a",
		"n a k i", "n a k i",
	)
	tab := Train("src", "tgt", fps, align.PriorScorer(descBundle))
	for _, g := range []string{"p", "a", "k", "e", "m", "u"} {
		if lo := tab.LogOdds(descBundle, g, g, 1.0); lo <= 0 {
			t.Errorf("LogOdds(%s,%s) = %f; expected positive identity correspondence", g, g, lo)
		}
	}
}

func TestTrainShiftedCorrespondenceOvertakesIdentity(t *testing.T) {
	// Every source "a" in target becomes "u" — a systematic
	// correspondence. After training the learned logodds for a~u
	// should be higher than a~a even though merkmal's prior prefers
	// identity.
	fps := pairs(
		"p a", "p u",
		"t a", "t u",
		"k a", "k u",
		"m a", "m u",
		"n a", "n u",
	)
	tab := Train("src", "tgt", fps, align.PriorScorer(descBundle))
	loShift := tab.LogOdds(descBundle, "a", "u", 1.0)
	loIdent := tab.LogOdds(descBundle, "a", "a", 1.0)
	if loShift <= loIdent {
		t.Errorf("learned shift a→u (%f) should outscore identity a→a (%f)", loShift, loIdent)
	}
}

func TestTrainEmptyPairsSafeDefaults(t *testing.T) {
	tab := Train("src", "tgt", nil, align.PriorScorer(descBundle))
	if lo := tab.LogOdds(descBundle, "a", "a", 1.0); lo != 0 {
		t.Errorf("empty-trained LogOdds = %f, want 0", lo)
	}
}

func TestLearnedScorerImplementsAlignScorer(t *testing.T) {
	fps := pairs("p a", "p a")
	tab := Train("s", "t", fps, align.PriorScorer(descBundle))
	ls := LearnedScorer(descBundle, tab, 0.4)
	var _ align.Scorer = ls // compile-time
	// Determinism: same inputs → same score.
	s1 := ls.Score("a", "a")
	s2 := ls.Score("a", "a")
	if s1 != s2 {
		t.Errorf("non-deterministic Score: %f vs %f", s1, s2)
	}
}

func TestLearnedScorerLambdaBlending(t *testing.T) {
	// When λ=0, learned scorer matches the prior. When λ=1, it's pure
	// learned log-odds (transformed to similarity).
	fps := pairs("p a", "p a", "k o", "k o")
	tab := Train("s", "t", fps, align.PriorScorer(descBundle))
	priorOnly := LearnedScorer(descBundle, tab, 0.0)
	learnedOnly := LearnedScorer(descBundle, tab, 1.0)
	prior := align.PriorScorer(descBundle)
	if math.Abs(priorOnly.Score("a", "a")-prior.Score("a", "a")) > 1e-9 {
		t.Error("λ=0 should reproduce the prior scorer")
	}
	if priorOnly.Score("a", "a") == learnedOnly.Score("a", "a") {
		t.Error("λ=0 and λ=1 should not collide for a trained pair")
	}
}

func TestTrainManyMatchesSequential(t *testing.T) {
	all := map[PairKey][]FormPair{
		{"A", "B"}: pairs("p a", "p a", "k o", "k u"),
		{"B", "A"}: pairs("p a", "p a", "k u", "k o"),
		{"A", "C"}: pairs("t i", "t i"),
	}
	prior := align.PriorScorer(descBundle)
	parTables := TrainMany(all, prior)
	for key, fps := range all {
		seq := Train(key.Source, key.Target, fps, prior)
		par := parTables[key]
		if par == nil {
			t.Fatalf("missing table for %v", key)
		}
		for g := range par.attested {
			for h := range par.attested[g] {
				if par.attested[g][h] != seq.attested[g][h] {
					t.Errorf("mismatch for %v/%s~%s: par=%d seq=%d", key, g, h, par.attested[g][h], seq.attested[g][h])
				}
			}
		}
	}
}

func TestPairByConceptGroupsCorrectly(t *testing.T) {
	recs := []wordlist.Record{
		{ID: "s_night", LanguageID: "samoan", Concept: "NIGHT", Segments: "p o"},
		{ID: "h_night", LanguageID: "hawaiian", Concept: "NIGHT", Segments: "p o"},
		{ID: "s_day", LanguageID: "samoan", Concept: "DAY", Segments: "a o"},
		{ID: "h_day", LanguageID: "hawaiian", Concept: "DAY", Segments: "a o"},
		// Unpaired: no Samoan match.
		{ID: "h_orphan", LanguageID: "hawaiian", Concept: "MOON", Segments: "m a"},
	}
	fps := PairByConcept(recs, "samoan", "hawaiian", descBundle)
	if len(fps) != 2 {
		t.Fatalf("PairByConcept = %d pairs, want 2", len(fps))
	}
}

// Linguistic narrative: on walworth Polynesian Samoan–Hawaiian, the
// correspondence ʔ↔ʔ should train to a strongly positive log-odds
// (glottal stop is stable across this pair). Also expect some k↔ʔ
// pairs (the Hawaiian *k reflex as ʔ) to appear.
func TestSamoanHawaiianGlottalStopTrains(t *testing.T) {
	path := "/tmp/arcaverborum_corecog/arcaverborum-A-corecog-20251008/forms.csv"
	if _, err := os.Stat(path); os.IsNotExist(err) {
		t.Skipf("arcaverborum not present at %s", path)
	}
	recs, err := wordlist.ReadTable(path)
	if err != nil {
		t.Fatal(err)
	}
	src := "walworthpolynesian_Samoan"
	tgt := "walworthpolynesian_Hawaiian"
	fps := PairByConcept(recs, src, tgt, descBundle)
	if len(fps) < 100 {
		t.Skipf("too few Samoan-Hawaiian form-pairs (%d) to train", len(fps))
	}
	tab := Train(src, tgt, fps, align.PriorScorer(descBundle))

	loGlot := tab.LogOdds(descBundle, "ʔ", "ʔ", 1.0)
	if loGlot <= 0 {
		t.Errorf("ʔ↔ʔ LogOdds = %f; expected > 0 on Samoan-Hawaiian", loGlot)
	}
	t.Logf("Samoan-Hawaiian ʔ↔ʔ log-odds = %.3f  (form-pairs=%d)", loGlot, len(fps))
	// Also report k→ʔ — Hawaiian's *k > ʔ merger. This should appear
	// with count > 0 in the attested table if training worked.
	if c := tab.attested["k"]["ʔ"]; c == 0 {
		t.Logf("k→ʔ attested count = 0 (unexpected but not fatal at this scale)")
	} else {
		lo := tab.LogOdds(descBundle, "k", "ʔ", 1.0)
		t.Logf("Samoan k → Hawaiian ʔ count=%d log-odds=%.3f", c, lo)
	}
}
