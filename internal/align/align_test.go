package align

import (
	"math"
	"strings"
	"testing"

	"github.com/tresoldi/cognator/internal/merkmal"
)

var descriptiveBundle *merkmal.Bundle

func init() {
	b, err := merkmal.LoadBundle("../../tests/fixtures/merkmal/descriptive")
	if err != nil {
		panic(err)
	}
	descriptiveBundle = b
}

func alignedPairs(a Alignment) string {
	var sb strings.Builder
	for i := range a.Source {
		if i > 0 {
			sb.WriteByte('|')
		}
		s, t := a.Source[i], a.Target[i]
		if s == "" {
			s = "-"
		}
		if t == "" {
			t = "-"
		}
		sb.WriteString(s)
		sb.WriteByte('~')
		sb.WriteString(t)
	}
	return sb.String()
}

func TestIdentityAlignmentZeroDistance(t *testing.T) {
	s := PriorScorer(descriptiveBundle)
	a := split("p a k e")
	al := NeedlemanWunsch(a, a, s)
	if len(al.Source) != len(a) {
		t.Fatalf("alignment length %d, want %d", len(al.Source), len(a))
	}
	d := Distance(a, a, s)
	if d != 0.0 {
		t.Errorf("Distance(a,a) = %f, want 0.0", d)
	}
}

func TestSimpleInsertionAlignsGap(t *testing.T) {
	s := PriorScorer(descriptiveBundle)
	a := split("p a")
	b := split("p a t")
	al := NeedlemanWunsch(a, b, s)
	// Expected shape: p~p | a~a | -~t
	cols := 0
	gaps := 0
	for i := range al.Source {
		cols++
		if al.Source[i] == "" || al.Target[i] == "" {
			gaps++
		}
	}
	if cols != 3 {
		t.Errorf("alignment columns = %d, want 3 (got %s)", cols, alignedPairs(al))
	}
	if gaps != 1 {
		t.Errorf("gaps = %d, want 1 (got %s)", gaps, alignedPairs(al))
	}
}

func TestDistanceBounded(t *testing.T) {
	s := PriorScorer(descriptiveBundle)
	a := split("p a k e")
	b := split("m u r o")
	d := Distance(a, b, s)
	if d < 0.0 || d > 1.0 {
		t.Errorf("Distance out of bounds: %f", d)
	}
}

// Linguistic narrative: Latin–Italian nokte~notte.
// The kt→tt correspondence is the defining Italian reflex of the
// Latin /kt/ cluster (nocte > notte, octo > otto, facta > fatta).
// Expectation: the aligner pairs k~t and t~t (producing a /k/ that
// corresponds to /t/ and a /t/ that corresponds to /t/).
func TestLatinItalianNokteNotte(t *testing.T) {
	s := PriorScorer(descriptiveBundle)
	a := split("n o k t e")
	b := split("n o t t e")
	al := NeedlemanWunsch(a, b, s)
	pairs := alignedPairs(al)
	t.Logf("alignment: %s", pairs)
	// Require: first four identity-ish columns then final e~e; both k
	// and t from Latin paired with a t in Italian.
	if !strings.Contains(pairs, "k~t") {
		t.Errorf("expected k~t in alignment, got %s", pairs)
	}
	if !strings.Contains(pairs, "t~t") {
		t.Errorf("expected t~t in alignment, got %s", pairs)
	}
	d := Distance(a, b, s)
	if d <= 0 || d >= 0.3 {
		t.Errorf("Latin-Italian nokte~notte distance = %f; expected small-but-positive (<0.3)", d)
	}
}

// Linguistic narrative: Latin–Spanish nokte~noče (the classical
// palatalized reflex ct > čh > č).
// Expectation: Spanish "č" (here spelled as tʃ) aligns somewhere
// against the Latin /kt/ cluster; whether the aligner sees it as
// k~tʃ + t~gap or k~gap + t~tʃ depends on merkmal's distance
// between (k, tʃ) and (t, tʃ). We just assert the alignment is
// produced, tʃ is used, and distance is larger than nokte~notte.
func TestLatinSpanishNokteNoche(t *testing.T) {
	s := PriorScorer(descriptiveBundle)
	a := split("n o k t e")
	b := split("n o tʃ e")
	al := NeedlemanWunsch(a, b, s)
	pairs := alignedPairs(al)
	t.Logf("alignment: %s", pairs)
	if !strings.Contains(pairs, "tʃ") {
		t.Errorf("expected tʃ to appear in alignment: %s", pairs)
	}
	dES := Distance(a, b, s)
	dIT := Distance(a, split("n o t t e"), s)
	if dES <= dIT {
		t.Errorf("expected Spanish distance (%f) > Italian distance (%f) — more sound change", dES, dIT)
	}
}

func TestGapCostPullsAlignmentCloser(t *testing.T) {
	// Two strings where the best alignment should be two identity pairs
	// regardless of gap cost, but a zero gap cost would merge them into
	// one column with double gaps. This guards against accidentally
	// setting a gap cost that makes matches free.
	a := split("a b")
	b := split("a b")
	s := PriorScorer(descriptiveBundle)
	al := NeedlemanWunsch(a, b, s)
	if len(al.Source) != 2 {
		t.Errorf("expected 2 columns, got %d (%s)", len(al.Source), alignedPairs(al))
	}
}

func TestSymmetricDistanceOnSymmetricScorer(t *testing.T) {
	// Merkmal distances are symmetric today, so the prior scorer should
	// produce symmetric pair distances (modulo float noise).
	s := PriorScorer(descriptiveBundle)
	a := split("p a k e")
	b := split("t a t o")
	d1 := Distance(a, b, s)
	d2 := Distance(b, a, s)
	if math.Abs(d1-d2) > 1e-9 {
		t.Errorf("asymmetric distance on symmetric scorer: %f vs %f", d1, d2)
	}
}

func split(s string) []string {
	return strings.Fields(s)
}

func TestProsodyAwarePenalizesCrossRoleScore(t *testing.T) {
	plain := PriorScorer(descriptiveBundle)
	prosody := ProsodyAwareScorer(descriptiveBundle)
	// p is a consonant, a is a vowel — distinct known roles.
	plainCV := plain.Score("p", "a")
	prosodyCV := prosody.Score("p", "a")
	if math.Abs((plainCV-prosodyCV)-RoleMismatchPenalty) > 1e-9 {
		t.Errorf("prosody penalty not applied to C~V: plain=%.4f prosody=%.4f want diff=%.4f",
			plainCV, prosodyCV, RoleMismatchPenalty)
	}
	// Same-role pair: no penalty.
	if plain.Score("p", "t") != prosody.Score("p", "t") {
		t.Errorf("prosody penalty spuriously applied to C~C: plain=%.4f prosody=%.4f",
			plain.Score("p", "t"), prosody.Score("p", "t"))
	}
	// Identity: no penalty.
	if plain.Score("a", "a") != prosody.Score("a", "a") {
		t.Errorf("prosody penalty spuriously applied to identity")
	}
}

func TestProsodyAwareKeepsCompatibleAlignments(t *testing.T) {
	// Latin-Italian nokte~notte alignments should still contain the k~t
	// and t~t columns even with role awareness — both k and t are
	// consonants, and the penalty does not fire inside a C~C pairing.
	s := ProsodyAwareScorer(descriptiveBundle)
	a := split("n o k t e")
	b := split("n o t t e")
	pairs := alignedPairs(NeedlemanWunsch(a, b, s))
	if !strings.Contains(pairs, "k~t") || !strings.Contains(pairs, "t~t") {
		t.Errorf("prosody-aware NW broke CC-path alignment: %s", pairs)
	}
}
