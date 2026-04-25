package segment

import (
	"reflect"
	"testing"

	"github.com/tresoldi/cognator/internal/phon"
)

// mockResolver answers role queries from a fixed map; anything else is RoleUnknown.
type mockResolver map[string]phon.Role

func (m mockResolver) Role(g string) phon.Role {
	if r, ok := m[g]; ok {
		return r
	}
	return phon.RoleUnknown
}

var polynesianRoles = mockResolver{
	"a": phon.RoleV, "e": phon.RoleV, "i": phon.RoleV,
	"o": phon.RoleV, "u": phon.RoleV,
	"p": phon.RoleC, "t": phon.RoleC, "k": phon.RoleC,
	"ʔ": phon.RoleC, "l": phon.RoleR, "m": phon.RoleR,
	"n": phon.RoleR,
}

var bdpaRoles = mockResolver{
	"a":  phon.RoleV,
	"p":  phon.RoleC,
	"j":  phon.RoleG,
	"tɕ": phon.RoleC,
	"ɥ":  phon.RoleG,
	"k":  phon.RoleC,
	"t":  phon.RoleC,
}

func TestTokenizePreservesBoundaries(t *testing.T) {
	got := Tokenize("ʔ a p a + ʔ a u")
	want := []string{"ʔ", "a", "p", "a", "+", "ʔ", "a", "u"}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("Tokenize = %v, want %v", got, want)
	}
}

func TestTokenizeTrimsAndDropsEmpty(t *testing.T) {
	got := Tokenize("   p   a   ")
	want := []string{"p", "a"}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("Tokenize = %v, want %v", got, want)
	}
}

func TestTokenizeEmptyInput(t *testing.T) {
	if got := Tokenize(""); len(got) != 0 {
		t.Errorf("empty input should yield empty slice, got %v", got)
	}
}

func TestParseNoTones(t *testing.T) {
	got, warnings := Parse("p a k e", polynesianRoles)
	if len(warnings) != 0 {
		t.Errorf("unexpected warnings: %v", warnings)
	}
	want := []Token{
		{Grapheme: "p"}, {Grapheme: "a"}, {Grapheme: "k"}, {Grapheme: "e"},
	}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("Parse = %+v, want %+v", got, want)
	}
}

func TestParsePreservesBoundaries(t *testing.T) {
	got, _ := Parse("ʔ a p a + ʔ a u", polynesianRoles)
	// Samoan "ʔapaʔau" — wing, walworth #109
	wantBoundaryAt := 4
	if !got[wantBoundaryAt].Boundary {
		t.Errorf("expected boundary token at index %d; got %+v", wantBoundaryAt, got)
	}
	// Boundary tokens carry no grapheme.
	if got[wantBoundaryAt].Grapheme != "" {
		t.Errorf("boundary should have empty grapheme, got %q", got[wantBoundaryAt].Grapheme)
	}
}

func TestParseReattachesToneOntoVowel(t *testing.T) {
	// bdpa "pja⁴²" — classical CLDF-style: tone as segment after nucleus.
	got, warnings := Parse("p j a ⁴²", bdpaRoles)
	if len(warnings) != 0 {
		t.Errorf("unexpected warnings: %v", warnings)
	}
	want := []Token{
		{Grapheme: "p"}, {Grapheme: "j"}, {Grapheme: "a", Tone: "⁴²"},
	}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("Parse = %+v, want %+v", got, want)
	}
}

func TestParseReattachesToneInMultiSyllable(t *testing.T) {
	got, warnings := Parse("p a ⁴² + t a ²¹", bdpaRoles)
	if len(warnings) != 0 {
		t.Errorf("unexpected warnings: %v", warnings)
	}
	want := []Token{
		{Grapheme: "p"}, {Grapheme: "a", Tone: "⁴²"},
		{Boundary: true},
		{Grapheme: "t"}, {Grapheme: "a", Tone: "²¹"},
	}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("Parse = %+v, want %+v", got, want)
	}
}

func TestParseToneDoesNotCrossBoundary(t *testing.T) {
	// Pathological: tone after boundary with no nucleus before the tone in its syllable.
	got, warnings := Parse("p a + ²¹", bdpaRoles)
	if len(warnings) == 0 {
		t.Error("expected a warning for unattached tone")
	}
	// Kept as a standalone token so data isn't silently lost.
	if got[len(got)-1].Grapheme != "²¹" {
		t.Errorf("orphan tone dropped; got %+v", got)
	}
}

func TestParseLeadingToneIsOrphan(t *testing.T) {
	got, warnings := Parse("²¹ a", bdpaRoles)
	if len(warnings) == 0 {
		t.Error("expected a warning for leading tone")
	}
	if got[0].Grapheme != "²¹" {
		t.Errorf("leading tone lost; got %+v", got)
	}
	if got[1].Grapheme != "a" || got[1].Tone != "" {
		t.Errorf("following vowel mangled; got %+v", got[1])
	}
}

func TestParseRecognizesToneWithoutMerkmalHelp(t *testing.T) {
	// Merkmal often lacks standalone tone graphemes. IsTone should catch
	// Chao superscripts by shape, so re-attachment still works.
	got, _ := Parse("p a ⁴²", mockResolver{"p": phon.RoleC, "a": phon.RoleV})
	if len(got) != 2 || got[1].Tone != "⁴²" {
		t.Errorf("shape-based tone detection failed: %+v", got)
	}
}

func TestParseToneOnSonorantNucleus(t *testing.T) {
	// Mandarin "m̩²" style: syllabic nasal as nucleus carrying tone.
	roles := mockResolver{"m": phon.RoleR, "²": phon.RoleT}
	got, _ := Parse("m ²", roles)
	if len(got) != 1 || got[0].Grapheme != "m" || got[0].Tone != "²" {
		t.Errorf("tone on sonorant nucleus not attached: %+v", got)
	}
}

func TestIsToneShape(t *testing.T) {
	positives := []string{"¹", "⁴²", "˥", "˥˧", "²¹", "33", "³³"}
	for _, s := range positives {
		if !IsToneShape(s) {
			t.Errorf("IsToneShape(%q) = false, want true", s)
		}
	}
	negatives := []string{"a", "p", "", "+", "a²", "ʔ"}
	for _, s := range negatives {
		if IsToneShape(s) {
			t.Errorf("IsToneShape(%q) = true, want false", s)
		}
	}
}
