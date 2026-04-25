package pipeline

import (
	"fmt"
	"os"
	"sort"
	"testing"

	"github.com/tresoldi/cognator/internal/eval"
	"github.com/tresoldi/cognator/internal/phon"
	"github.com/tresoldi/cognator/internal/wordlist"
)

var bundle *phon.Phon

func init() {
	b, err := phon.LoadDir("../../tests/fixtures/merkmal-models", "descriptive")
	if err != nil {
		panic(err)
	}
	bundle = b
}

// synthetic corpus: three lects with systematic reflexes, 10 cognate
// sets per concept so each cognate set has known truth.
func syntheticCorpus() []wordlist.Record {
	// cognate #k: stem_k is distinct across concepts (good ground truth),
	// but each lect applies a consistent sound shift.
	type reflex struct {
		lect string
		// fn maps the canonical stem to the lect's realization.
		mapFunc func(stem string) string
	}
	lects := []reflex{
		{"lectA", func(s string) string { return s }}, // canonical
		{"lectB", func(s string) string { return rewrite(s, map[string]string{"k": "g"}) }},
		{"lectC", func(s string) string { return rewrite(s, map[string]string{"p": "b"}) }},
	}
	// Ten distinct stems, each a concept with one form per lect.
	stems := []string{
		"p a k a", "t u k i", "p i n u", "k a t a", "m a l o",
		"n u p a", "l i k a", "t a p u", "p u n i", "k u l i",
	}
	var out []wordlist.Record
	for ci, stem := range stems {
		concept := fmt.Sprintf("C%02d", ci)
		for _, l := range lects {
			segs := l.mapFunc(stem)
			out = append(out, wordlist.Record{
				ID:         fmt.Sprintf("%s_%s", l.lect, concept),
				LanguageID: l.lect,
				Concept:    concept,
				Segments:   segs,
				Family:     "syn",
				Cognacy:    fmt.Sprintf("syn_%s", concept),
			})
		}
	}
	return out
}

func rewrite(segs string, sub map[string]string) string {
	parts := splitFields(segs)
	for i, p := range parts {
		if n, ok := sub[p]; ok {
			parts[i] = n
		}
	}
	return join(parts, " ")
}

func splitFields(s string) []string {
	var out []string
	cur := ""
	for _, r := range s {
		if r == ' ' {
			if cur != "" {
				out = append(out, cur)
				cur = ""
			}
			continue
		}
		cur += string(r)
	}
	if cur != "" {
		out = append(out, cur)
	}
	return out
}

func join(parts []string, sep string) string {
	s := ""
	for i, p := range parts {
		if i > 0 {
			s += sep
		}
		s += p
	}
	return s
}

func TestDetectSyntheticRecoversEachConceptOneCluster(t *testing.T) {
	recs := syntheticCorpus()
	spec := Spec{Iterations: 3, Lambda: 0.5, Seed: 1}
	res := Detect(recs, bundle, spec)
	if len(res.Rows) != len(recs) {
		t.Fatalf("got %d rows, want %d", len(res.Rows), len(recs))
	}
	t.Logf("Otsu threshold used: %.3f", res.Threshold)
	// Each of 10 concepts should resolve to one cognator cluster per
	// concept (since all 3 lects' forms derive from the same stem).
	byConcept := map[string]map[string]int{}
	for _, r := range res.Rows {
		// cluster IDs are "<family>:<concept>:<n>"; key by the first
		// two components to count clusters per concept.
		parts := splitAtColon(r.CognatorID)
		if len(parts) < 3 {
			t.Fatalf("malformed CognatorID %q", r.CognatorID)
		}
		key := parts[0] + ":" + parts[1]
		if _, ok := byConcept[key]; !ok {
			byConcept[key] = map[string]int{}
		}
		byConcept[key][r.CognatorID]++
	}
	multiCluster := 0
	for _, clusters := range byConcept {
		if len(clusters) > 1 {
			multiCluster++
		}
	}
	if multiCluster > 1 {
		t.Errorf("expected ≥9/10 concepts to resolve as single cluster; %d concepts split", multiCluster)
	}
}

func splitAtColon(s string) []string {
	var out []string
	cur := ""
	for _, r := range s {
		if r == ':' {
			out = append(out, cur)
			cur = ""
			continue
		}
		cur += string(r)
	}
	out = append(out, cur)
	return out
}

// Linguistic narrative: Detect on walworth Polynesian full 7-lect
// subset. Report B-cubed F against expert Cognacy so we have a
// living baseline as the pipeline evolves.
func TestDetectWalworthPolynesianB3(t *testing.T) {
	path := "/tmp/arcaverborum_corecog/arcaverborum-A-corecog-20251008/forms.csv"
	if _, err := os.Stat(path); os.IsNotExist(err) {
		t.Skipf("arcaverborum not present at %s", path)
	}
	all, err := wordlist.ReadTable(path)
	if err != nil {
		t.Fatal(err)
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
	var recs []wordlist.Record
	for _, r := range all {
		if r.Family == "walworthpolynesian" && want[r.LanguageID] && r.Cognacy != "" {
			recs = append(recs, r)
		}
	}
	if len(recs) < 100 {
		t.Skipf("too few walworth records: %d", len(recs))
	}

	spec := Spec{Iterations: 3, Lambda: 0.5, Seed: 1}
	res := Detect(recs, bundle, spec)
	pred := map[string]string{}
	for _, r := range res.Rows {
		pred[r.ID] = r.CognatorID
	}
	truth := map[string]string{}
	for _, r := range recs {
		truth[r.ID] = r.Concept + ":" + r.Cognacy
	}
	p, r, f := eval.BCubed(pred, truth)
	t.Logf("walworth Polynesian (7 lects, %d forms): B-cubed P=%.3f R=%.3f F=%.3f  threshold=%.3f  clusters=%d",
		len(recs), p, r, f, res.Threshold, uniqueStr(pred))
	if f < 0.40 {
		t.Errorf("B-cubed F=%.3f below baseline floor of 0.40 — regression?", f)
	}
}

func uniqueStr(m map[string]string) int {
	set := map[string]struct{}{}
	for _, v := range m {
		set[v] = struct{}{}
	}
	return len(set)
}

// used by future debug dumps
var _ = sort.Strings
