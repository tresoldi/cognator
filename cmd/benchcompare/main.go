// Command benchcompare joins cognator's benchmark TSV with lingpy's
// benchmark TSV and emits a side-by-side markdown + TSV.
package main

import (
	"encoding/csv"
	"flag"
	"fmt"
	"log"
	"math"
	"os"
	"sort"
	"strconv"
)

type row struct {
	dataset       string
	lects, forms  int
	precision     float64
	recall        float64
	f1            float64
	elapsedMillis int64
	skipped       string
}

func main() {
	cog := flag.String("cognator", "benchmark/results.tsv", "cognator TSV")
	lin := flag.String("lingpy", "benchmark/lingpy_results.tsv", "lingpy TSV")
	outTSV := flag.String("out-tsv", "benchmark/comparison.tsv", "joined TSV")
	outMD := flag.String("out-md", "benchmark/comparison.md", "joined markdown")
	flag.Parse()
	cRows := load(*cog)
	lRows := load(*lin)

	// Deduplicate to a map keyed by dataset. Cognator TSV has a
	// "skipped" column for each side — use those as gates.
	cMap := map[string]row{}
	lMap := map[string]row{}
	for _, r := range cRows {
		cMap[r.dataset] = r
	}
	for _, r := range lRows {
		lMap[r.dataset] = r
	}

	// Union of datasets, sorted.
	all := map[string]struct{}{}
	for d := range cMap {
		all[d] = struct{}{}
	}
	for d := range lMap {
		all[d] = struct{}{}
	}
	names := make([]string, 0, len(all))
	for d := range all {
		names = append(names, d)
	}
	sort.Strings(names)

	var j []joined
	for _, d := range names {
		c := cMap[d]
		l := lMap[d]
		lects := c.lects
		if lects == 0 {
			lects = l.lects
		}
		forms := c.forms
		if forms == 0 {
			forms = l.forms
		}
		j = append(j, joined{
			dataset: d,
			lects:   lects, forms: forms,
			cF: c.f1, cP: c.precision, cR: c.recall, cSkipped: c.skipped, cElapsed: c.elapsedMillis,
			lF: l.f1, lP: l.precision, lR: l.recall, lSkipped: l.skipped, lElapsed: l.elapsedMillis,
		})
	}

	// Write TSV.
	if err := writeTSV(*outTSV, j); err != nil {
		log.Fatal(err)
	}
	log.Printf("wrote %s", *outTSV)

	// Write markdown.
	if err := writeMD(*outMD, j); err != nil {
		log.Fatal(err)
	}
	log.Printf("wrote %s", *outMD)
}

func load(path string) []row {
	f, err := os.Open(path)
	if err != nil {
		log.Fatalf("read %s: %v", path, err)
	}
	defer f.Close()
	r := csv.NewReader(f)
	r.Comma = '\t'
	r.FieldsPerRecord = -1
	recs, err := r.ReadAll()
	if err != nil {
		log.Fatal(err)
	}
	var rows []row
	for i, rec := range recs {
		if i == 0 {
			continue
		}
		if len(rec) < 13 {
			continue
		}
		rows = append(rows, row{
			dataset: rec[0],
			lects:   atoi(rec[1]),
			forms:   atoi(rec[3]),
			precision: atof(rec[8]),
			recall:    atof(rec[9]),
			f1:        atof(rec[10]),
			elapsedMillis: int64(atoi(rec[11])),
			skipped:       rec[12],
		})
	}
	return rows
}

func atoi(s string) int { n, _ := strconv.Atoi(s); return n }
func atof(s string) float64 { f, _ := strconv.ParseFloat(s, 64); return f }

type joined = struct {
	dataset                string
	lects, forms           int
	cF, cP, cR, lF, lP, lR float64
	cSkipped, lSkipped     string
	cElapsed, lElapsed     int64
}

func writeTSV(path string, j []joined) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	fmt.Fprintln(f, "dataset\tlects\tforms\tcognator_P\tcognator_R\tcognator_F\tcognator_ms\tlingpy_P\tlingpy_R\tlingpy_F\tlingpy_ms\tdelta_F\tcognator_skipped\tlingpy_skipped")
	for _, r := range j {
		delta := r.cF - r.lF
		fmt.Fprintf(f, "%s\t%d\t%d\t%.4f\t%.4f\t%.4f\t%d\t%.4f\t%.4f\t%.4f\t%d\t%+.4f\t%s\t%s\n",
			r.dataset, r.lects, r.forms,
			r.cP, r.cR, r.cF, r.cElapsed,
			r.lP, r.lR, r.lF, r.lElapsed,
			delta,
			r.cSkipped, r.lSkipped)
	}
	return nil
}

func writeMD(path string, j []joined) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()

	// Aggregate stats over datasets scored by BOTH (both have a
	// non-zero F1; skipped-entries and absent-from-one-side are
	// excluded from the aggregate).
	var bothN int
	var sumCF, sumLF, sumCP, sumCR, sumLP, sumLR float64
	var sumCms, sumLms int64
	var cWins, lWins, ties int
	for _, r := range j {
		if r.cSkipped != "" || r.lSkipped != "" {
			continue
		}
		if r.cF == 0 || r.lF == 0 {
			continue
		}
		bothN++
		sumCF += r.cF
		sumLF += r.lF
		sumCP += r.cP
		sumCR += r.cR
		sumLP += r.lP
		sumLR += r.lR
		sumCms += r.cElapsed
		sumLms += r.lElapsed
		switch {
		case r.cF > r.lF+1e-6:
			cWins++
		case r.lF > r.cF+1e-6:
			lWins++
		default:
			ties++
		}
	}

	fmt.Fprintln(f, "# Cognator vs. lingpy: head-to-head on arcaverborum CoreCog")
	fmt.Fprintln(f)
	fmt.Fprintln(f, "Both systems scored under their natural defaults (cognator: framework-native prior+learned pipeline, Otsu-selected per-family threshold, 3 EM iterations; lingpy: LexStat scorer with runs=1000 permutation, SCA-classes, fixed threshold=0.55, Infomap).")
	fmt.Fprintln(f)
	fmt.Fprintf(f, "Datasets where both succeeded: **%d**.\n\n", bothN)
	if bothN > 0 {
		fmt.Fprintln(f, "| Metric | Cognator | Lingpy | Δ (cognator − lingpy) |")
		fmt.Fprintln(f, "|---|---|---|---|")
		fmt.Fprintf(f, "| Mean F | **%.4f** | %.4f | %+.4f |\n", sumCF/float64(bothN), sumLF/float64(bothN), (sumCF-sumLF)/float64(bothN))
		fmt.Fprintf(f, "| Mean Precision | %.4f | %.4f | %+.4f |\n", sumCP/float64(bothN), sumLP/float64(bothN), (sumCP-sumLP)/float64(bothN))
		fmt.Fprintf(f, "| Mean Recall | %.4f | %.4f | %+.4f |\n", sumCR/float64(bothN), sumLR/float64(bothN), (sumCR-sumLR)/float64(bothN))
		fmt.Fprintf(f, "| Mean time | %s | %s | %.1fx %s |\n", formatMs(sumCms/int64(bothN)), formatMs(sumLms/int64(bothN)), float64(sumLms)/math.Max(1, float64(sumCms)), "faster" )
		fmt.Fprintln(f)
		fmt.Fprintf(f, "Cognator wins on **%d**, lingpy wins on **%d**, ties on **%d**.\n\n", cWins, lWins, ties)
	}

	// Table sorted by delta descending (cognator best → worst).
	sort.Slice(j, func(i, k int) bool { return (j[i].cF - j[i].lF) > (j[k].cF - j[k].lF) })

	fmt.Fprintln(f, "## Per-dataset breakdown (sorted by Δ; positive = cognator better)")
	fmt.Fprintln(f)
	fmt.Fprintln(f, "| Dataset | Lects | Forms | Cognator F | Lingpy F | **Δ F** | Cognator P/R | Lingpy P/R | Time cog → lpy |")
	fmt.Fprintln(f, "|---|---|---|---|---|---|---|---|---|")
	for _, r := range j {
		if r.cSkipped != "" || r.lSkipped != "" {
			continue
		}
		if r.cF == 0 || r.lF == 0 {
			continue
		}
		delta := r.cF - r.lF
		arrow := "="
		switch {
		case delta > 0.02:
			arrow = "↑"
		case delta < -0.02:
			arrow = "↓"
		}
		fmt.Fprintf(f, "| %s | %d | %d | %.3f | %.3f | **%+.3f** %s | %.3f/%.3f | %.3f/%.3f | %s → %s |\n",
			r.dataset, r.lects, r.forms, r.cF, r.lF, delta, arrow,
			r.cP, r.cR, r.lP, r.lR,
			formatMs(r.cElapsed), formatMs(r.lElapsed))
	}

	// "Partial" section — dataset appears on one side but not the
	// other (either skipped, or the run hasn't completed yet).
	var skipped []joined
	for _, r := range j {
		onlyCog := (r.cF > 0 && r.lF == 0 && r.lSkipped == "")
		onlyLpy := (r.lF > 0 && r.cF == 0 && r.cSkipped == "")
		if r.cSkipped != "" || r.lSkipped != "" || onlyCog || onlyLpy {
			skipped = append(skipped, r)
		}
	}
	if len(skipped) > 0 {
		fmt.Fprintln(f)
		fmt.Fprintln(f, "## Datasets not completed on one side")
		fmt.Fprintln(f)
		fmt.Fprintln(f, "| Dataset | Lects | Forms | Cognator | Lingpy |")
		fmt.Fprintln(f, "|---|---|---|---|---|")
		for _, r := range skipped {
			cStatus := fmt.Sprintf("F=%.3f", r.cF)
			if r.cSkipped != "" {
				cStatus = "skipped: " + r.cSkipped
			}
			lStatus := fmt.Sprintf("F=%.3f", r.lF)
			if r.lSkipped != "" {
				lStatus = "skipped: " + r.lSkipped
			}
			fmt.Fprintf(f, "| %s | %d | %d | %s | %s |\n", r.dataset, r.lects, r.forms, cStatus, lStatus)
		}
	}
	return nil
}

func formatMs(ms int64) string {
	if ms <= 0 {
		return "—"
	}
	if ms < 1000 {
		return fmt.Sprintf("%dms", ms)
	}
	if ms < 60_000 {
		return fmt.Sprintf("%.1fs", float64(ms)/1000)
	}
	return fmt.Sprintf("%dm%02ds", ms/60_000, (ms%60_000)/1000)
}
