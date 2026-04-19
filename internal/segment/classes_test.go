package segment

import "testing"

func TestClassReduceSamples(t *testing.T) {
	cases := map[string]string{
		"t":   "t",
		"tʰ":  "t",
		"kʷ":  "k",
		"aː":  "a",
		"ã":   "a",
		"ⁿg":  "g",
		"ɡ":   "ɡ",
		"tɕʰ": "tɕ",
		"ʔ":   "ʔ",
		"ai":  "ai",
		"⁴²":  "⁴²",
		"²¹":  "²¹",
		"p":   "p",
	}
	for in, want := range cases {
		got := ClassReduce(in)
		if got != want {
			t.Errorf("ClassReduce(%q) = %q, want %q", in, got, want)
		}
	}
}
