// Command diag is a throwaway linguistic-eyeball utility used at the
// end of each milestone. It loads an arcaverborum table, runs
// segment parsing, and prints sanity stats so the author can confirm
// nothing absurd is happening before moving to the next milestone.
package main

import (
	"flag"
	"fmt"
	"os"
	"sort"

	"github.com/tresoldi/cognator/internal/phon"
	"github.com/tresoldi/cognator/internal/segment"
	"github.com/tresoldi/cognator/internal/wordlist"
)

func main() {
	var (
		dataPath   = flag.String("data", "", "path to arcaverborum-shape TSV/CSV")
		merkmalDir = flag.String("merkmal-dir", "tests/fixtures/merkmal-models", "merkmal data root (contains models/ and geometries/)")
		model      = flag.String("model", "descriptive", "merkmal model name")
		family     = flag.String("family", "", "filter to a single family/dataset")
	)
	flag.Parse()
	if *dataPath == "" {
		fmt.Fprintln(os.Stderr, "usage: diag --data=PATH [--merkmal-dir=DIR] [--model=NAME] [--family=NAME]")
		os.Exit(2)
	}
	b, err := phon.LoadDir(*merkmalDir, *model)
	if err != nil {
		fmt.Fprintln(os.Stderr, "bundle:", err)
		os.Exit(1)
	}
	recs, err := wordlist.ReadTable(*dataPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, "read:", err)
		os.Exit(1)
	}
	if *family != "" {
		filtered := recs[:0]
		for _, r := range recs {
			if r.Family == *family {
				filtered = append(filtered, r)
			}
		}
		recs = filtered
	}
	fmt.Printf("rows: %d  system: %s (%d graphemes)\n", len(recs), b.Name, b.GraphemeCount)

	langs := map[string]int{}
	concepts := map[string]struct{}{}
	cogsets := map[string]struct{}{}
	oov := map[string]int{}
	totalSeg := 0
	warnings := 0
	for _, r := range recs {
		langs[r.LanguageID]++
		concepts[r.Concept] = struct{}{}
		if r.Cognacy != "" {
			cogsets[r.Cognacy] = struct{}{}
		}
		toks, ws := segment.Parse(r.Segments, b)
		warnings += len(ws)
		for _, t := range toks {
			if t.Boundary {
				continue
			}
			totalSeg++
			if !b.Known(t.Grapheme) {
				oov[t.Grapheme]++
			}
		}
	}
	fmt.Printf("lects: %d  concepts: %d  expert-cognate-sets: %d\n", len(langs), len(concepts), len(cogsets))
	fmt.Printf("segments: %d  tone/boundary warnings: %d  oov types: %d\n", totalSeg, warnings, len(oov))

	// Top 10 OOV graphemes by frequency
	type kv struct {
		g string
		n int
	}
	sorted := make([]kv, 0, len(oov))
	for g, n := range oov {
		sorted = append(sorted, kv{g, n})
	}
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].n > sorted[j].n })
	if len(sorted) > 10 {
		sorted = sorted[:10]
	}
	fmt.Println("top OOV graphemes (candidates for merkmal feedback):")
	for _, kv := range sorted {
		fmt.Printf("  %-8s %d\n", kv.g, kv.n)
	}
}
