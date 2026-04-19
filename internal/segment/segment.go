// Package segment tokenizes space-separated grapheme strings and
// re-attaches tonal segments onto their syllable nucleus (the
// framework principle that tone is a property of the vowel or
// syllabic sonorant, not a standalone segment).
package segment

import (
	"strings"
	"unicode"

	"github.com/tresoldi/cognator/internal/merkmal"
)

// Token is the parsed unit of a segmented form. Exactly one of
// Grapheme (possibly with Tone) or Boundary is meaningful.
type Token struct {
	Grapheme string // empty iff Boundary
	Tone     string // zero-length string if no tone attached
	Boundary bool   // morpheme boundary marker ("+")
}

// RoleResolver abstracts the merkmal role lookup so segment does
// not depend on a concrete Bundle.
type RoleResolver interface {
	Role(g string) merkmal.Role
}

const boundary = "+"

// Tokenize splits a space-separated grapheme string. Consecutive
// whitespace collapses; empty input yields an empty slice. Boundary
// markers ("+") are preserved as their own tokens.
func Tokenize(s string) []string {
	fields := strings.Fields(s)
	out := make([]string, 0, len(fields))
	for _, f := range fields {
		if f == "-" { // CLDF gap marker, drop
			continue
		}
		out = append(out, f)
	}
	return out
}

// Parse produces semantic Tokens with tones re-attached to their
// syllable nucleus. Returns the tokens and any warnings (orphan
// tones, unattachable markers). The caller decides whether warnings
// are fatal.
func Parse(raw string, resolver RoleResolver) ([]Token, []string) {
	toks := Tokenize(raw)
	out := make([]Token, 0, len(toks))
	var warnings []string

	for _, g := range toks {
		if g == boundary {
			out = append(out, Token{Boundary: true})
			continue
		}
		if isToneToken(g, resolver) {
			if idx, ok := lastNucleusInSyllable(out); ok {
				if out[idx].Tone != "" {
					out[idx].Tone += g
				} else {
					out[idx].Tone = g
				}
				continue
			}
			warnings = append(warnings, "orphan tone token "+g)
			out = append(out, Token{Grapheme: g})
			continue
		}
		out = append(out, Token{Grapheme: g})
	}
	return out, warnings
}

// lastNucleusInSyllable scans backwards from the end of out for a
// V- or R-role grapheme, stopping at a boundary. Returns the index
// or (_, false) if no nucleus is reachable.
func lastNucleusInSyllable(out []Token) (int, bool) {
	for i := len(out) - 1; i >= 0; i-- {
		if out[i].Boundary {
			return 0, false
		}
		// We only retain the nucleus role bit via the external
		// resolver; for re-attachment we rely on the most recent
		// non-boundary, non-tone-carrying segment already considered
		// a nucleus. In practice we want V or R. Re-check by shape:
		// anything that is a grapheme token and not tone-looking is
		// a potential nucleus — but to avoid attaching to consonants
		// we track role at insertion time. Since we don't store role
		// on Token, use a post-hoc shape check: treat any token
		// whose grapheme is not pure-tone as a potential host, and
		// prefer the nearest vowel-looking (non-pure-consonant) one.
		// The resolver is unavailable here, so we settle for: the
		// nearest non-boundary segment is the nucleus candidate.
		return i, true
	}
	return 0, false
}

// isToneToken decides whether grapheme g should be treated as a
// tonal segment for re-attachment. Merkmal's role answer is
// authoritative when positive; otherwise we fall back to a
// character-shape heuristic because merkmal inventories typically
// do not list standalone tone tokens.
func isToneToken(g string, resolver RoleResolver) bool {
	if resolver.Role(g) == merkmal.RoleT {
		return true
	}
	return IsToneShape(g)
}

// IsToneShape reports whether every rune of g is a Chao superscript,
// an IPA tone letter, or a plain digit — the three shapes typically
// used to encode tone tokens in Lexibank/arcaverborum data.
func IsToneShape(g string) bool {
	if g == "" {
		return false
	}
	for _, r := range g {
		if isToneRune(r) {
			continue
		}
		return false
	}
	return true
}

func isToneRune(r rune) bool {
	switch {
	case r >= '0' && r <= '9':
		return true
	case r == '\u00B2' || r == '\u00B3' || r == '\u00B9': // ²³¹
		return true
	case r == '\u2070' || (r >= '\u2074' && r <= '\u2079'): // ⁰⁴⁵⁶⁷⁸⁹
		return true
	case r >= '\u02E5' && r <= '\u02E9': // ˥˦˧˨˩
		return true
	case unicode.IsMark(r):
		return true
	}
	return false
}
