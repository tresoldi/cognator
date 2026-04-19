package segment

import (
	"strings"
	"unicode"

	"golang.org/x/text/unicode/norm"
)

// ClassReduce maps a grapheme to a coarse SCA-like sound class by
// stripping combining marks, IPA modifier letters, and tone markers.
// Intended as an upstream densification: it turns OOV graphemes like
// `tʰ`, `kʷ`, `aː`, `ⁿg`, `ã` into the in-vocabulary base classes
// `t`, `k`, `a`, `g`, `a`. Correspondence-table learning then works
// on ~30 classes instead of 700+ graphemes, matching lingpy's SCA
// strategy.
//
// Exceptions: if a grapheme is ALL modifier content (e.g. a pure
// tone token `⁴²`), it is returned unchanged so tone correspondences
// remain distinguishable.
func ClassReduce(g string) string {
	if g == "" {
		return g
	}
	d := norm.NFD.String(g)
	var out strings.Builder
	for _, r := range d {
		if unicode.Is(unicode.Mn, r) {
			continue
		}
		if r >= '\u02B0' && r <= '\u02FF' { // modifier letters block
			continue
		}
		if r == '\u00B2' || r == '\u00B3' || r == '\u00B9' {
			continue
		}
		if r >= '\u2070' && r <= '\u209F' { // super/subscripts
			continue
		}
		if r >= '\u02E5' && r <= '\u02E9' { // IPA tone letters
			continue
		}
		out.WriteRune(r)
	}
	if out.Len() == 0 {
		return g
	}
	return out.String()
}
