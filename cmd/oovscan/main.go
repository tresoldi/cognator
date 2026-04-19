// Command oovscan sweeps an arcaverborum-shape CSV and reports, per
// merkmal system, the graphemes that appear in the data but not in
// merkmal's inventory. Output is a markdown table suitable for
// handing to the merkmal agent as a feedback note.
package main

import (
	"flag"
	"fmt"
	"os"
	"sort"

	"github.com/tresoldi/cognator/internal/merkmal"
	"github.com/tresoldi/cognator/internal/segment"
	"github.com/tresoldi/cognator/internal/wordlist"
)

func main() {
	var (
		dataPath   = flag.String("data", "", "arcaverborum-shape TSV/CSV")
		bundlePath = flag.String("bundle", "tests/fixtures/merkmal/descriptive", "merkmal bundle")
		top        = flag.Int("top", 40, "top-N OOV graphemes to print")
	)
	flag.Parse()
	if *dataPath == "" {
		fmt.Fprintln(os.Stderr, "usage: oovscan --data=PATH [--bundle=DIR]")
		os.Exit(2)
	}
	b, err := merkmal.LoadBundle(*bundlePath)
	if err != nil {
		fmt.Fprintln(os.Stderr, "bundle:", err)
		os.Exit(1)
	}
	recs, err := wordlist.ReadTable(*dataPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, "read:", err)
		os.Exit(1)
	}

	oov := map[string]int{}
	datasets := map[string]map[string]struct{}{} // grapheme -> set of datasets
	totalSeg := 0
	for _, r := range recs {
		toks, _ := segment.Parse(r.Segments, b)
		for _, t := range toks {
			if t.Boundary || t.Grapheme == "" {
				continue
			}
			totalSeg++
			if !b.Known(t.Grapheme) {
				oov[t.Grapheme]++
				if _, ok := datasets[t.Grapheme]; !ok {
					datasets[t.Grapheme] = map[string]struct{}{}
				}
				datasets[t.Grapheme][r.Family] = struct{}{}
			}
		}
	}

	type kv struct {
		g, ds string
		n, nd int
	}
	rows := make([]kv, 0, len(oov))
	for g, n := range oov {
		ds := ""
		names := make([]string, 0, len(datasets[g]))
		for d := range datasets[g] {
			names = append(names, d)
		}
		sort.Strings(names)
		if len(names) > 3 {
			ds = fmt.Sprintf("%s, %s, %s +%d", names[0], names[1], names[2], len(names)-3)
		} else {
			for i, n := range names {
				if i > 0 {
					ds += ", "
				}
				ds += n
			}
		}
		rows = append(rows, kv{g: g, n: n, nd: len(names), ds: ds})
	}
	sort.Slice(rows, func(i, j int) bool { return rows[i].n > rows[j].n })

	fmt.Printf("# OOV graphemes in arcaverborum (system: %s)\n\n", b.System)
	fmt.Printf("Segments scanned: %d  Distinct OOV types: %d\n\n", totalSeg, len(rows))
	fmt.Println("| Grapheme | Count | Datasets |")
	fmt.Println("|---|---|---|")
	n := *top
	if n > len(rows) {
		n = len(rows)
	}
	for _, r := range rows[:n] {
		fmt.Printf("| `%s` | %d | %s (×%d) |\n", r.g, r.n, r.ds, r.nd)
	}
}
