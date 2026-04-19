// Command benchmd renders a benchmark results TSV into a markdown
// summary with aggregate statistics and a sorted per-dataset table.
// Used when the TSV has been hand-patched after a bench run.
package main

import (
	"encoding/csv"
	"flag"
	"fmt"
	"log"
	"os"
	"sort"
	"strconv"
)

type row struct {
	dataset       string
	lects         int
	concepts      int
	forms         int
	cognacyForms  int
	oovTypes      int
	threshold     float64
	clusters      int
	precision     float64
	recall        float64
	f1            float64
	elapsedMillis int64
	skipped       string
}

func main() {
	in := flag.String("in", "benchmark/results.tsv", "input TSV")
	out := flag.String("out", "benchmark/results.md", "output markdown")
	flag.Parse()
	f, err := os.Open(*in)
	if err != nil {
		log.Fatal(err)
	}
	defer f.Close()
	r := csv.NewReader(f)
	r.Comma = '\t'
	r.FieldsPerRecord = -1
	r.LazyQuotes = true
	records, err := r.ReadAll()
	if err != nil {
		log.Fatal(err)
	}
	var rows []row
	for i, rec := range records {
		if i == 0 {
			continue
		}
		if len(rec) < 13 {
			continue
		}
		rows = append(rows, row{
			dataset:       rec[0],
			lects:         atoi(rec[1]),
			concepts:      atoi(rec[2]),
			forms:         atoi(rec[3]),
			cognacyForms:  atoi(rec[4]),
			oovTypes:      atoi(rec[5]),
			threshold:     atof(rec[6]),
			clusters:      atoi(rec[7]),
			precision:     atof(rec[8]),
			recall:        atof(rec[9]),
			f1:            atof(rec[10]),
			elapsedMillis: int64(atoi(rec[11])),
			skipped:       rec[12],
		})
	}

	scored := 0
	var sumF, sumP, sumR, weightedF float64
	totalForms := 0
	for _, r := range rows {
		if r.skipped != "" {
			continue
		}
		scored++
		sumF += r.f1
		sumP += r.precision
		sumR += r.recall
		weightedF += r.f1 * float64(r.cognacyForms)
		totalForms += r.cognacyForms
	}
	meanF, meanP, meanR, wF := 0.0, 0.0, 0.0, 0.0
	if scored > 0 {
		meanF = sumF / float64(scored)
		meanP = sumP / float64(scored)
		meanR = sumR / float64(scored)
	}
	if totalForms > 0 {
		wF = weightedF / float64(totalForms)
	}
	sortedScored := make([]row, 0, scored)
	for _, r := range rows {
		if r.skipped == "" {
			sortedScored = append(sortedScored, r)
		}
	}
	sort.Slice(sortedScored, func(i, j int) bool { return sortedScored[i].f1 > sortedScored[j].f1 })
	median := 0.0
	if len(sortedScored) > 0 {
		median = sortedScored[len(sortedScored)/2].f1
	}

	o, err := os.Create(*out)
	if err != nil {
		log.Fatal(err)
	}
	defer o.Close()
	fmt.Fprintf(o, "# Cognator benchmark results\n\n")
	fmt.Fprintf(o, "Datasets: %d scored, %d skipped\n\n", scored, len(rows)-scored)
	fmt.Fprintf(o, "- **Mean F** across datasets: **%.4f**\n", meanF)
	fmt.Fprintf(o, "- Median F: %.4f\n", median)
	fmt.Fprintf(o, "- Form-weighted F: %.4f\n", wF)
	fmt.Fprintf(o, "- Mean Precision: %.4f  Mean Recall: %.4f\n\n", meanP, meanR)
	fmt.Fprintln(o, "## Per-dataset results (sorted by F, descending)")
	fmt.Fprintln(o)
	fmt.Fprintln(o, "| Dataset | Lects | Concepts | Forms | Cognacy | OOV | Threshold | Clusters | P | R | **F** | Time |")
	fmt.Fprintln(o, "|---|---|---|---|---|---|---|---|---|---|---|---|")
	for _, r := range sortedScored {
		fmt.Fprintf(o, "| %s | %d | %d | %d | %d | %d | %.3f | %d | %.3f | %.3f | **%.3f** | %s |\n",
			r.dataset, r.lects, r.concepts, r.forms, r.cognacyForms, r.oovTypes,
			r.threshold, r.clusters, r.precision, r.recall, r.f1, formatMs(r.elapsedMillis))
	}
	skipped := []row{}
	for _, r := range rows {
		if r.skipped != "" {
			skipped = append(skipped, r)
		}
	}
	if len(skipped) > 0 {
		fmt.Fprintln(o, "")
		fmt.Fprintln(o, "## Skipped datasets")
		fmt.Fprintln(o, "")
		fmt.Fprintln(o, "| Dataset | Lects | Forms | Cognacy | Reason |")
		fmt.Fprintln(o, "|---|---|---|---|---|")
		for _, r := range skipped {
			fmt.Fprintf(o, "| %s | %d | %d | %d | %s |\n", r.dataset, r.lects, r.forms, r.cognacyForms, r.skipped)
		}
	}
	log.Printf("wrote %s", *out)
}

func atoi(s string) int {
	n, _ := strconv.Atoi(s)
	return n
}
func atof(s string) float64 {
	f, _ := strconv.ParseFloat(s, 64)
	return f
}
func formatMs(ms int64) string {
	if ms < 1000 {
		return fmt.Sprintf("%dms", ms)
	}
	if ms < 60_000 {
		return fmt.Sprintf("%.1fs", float64(ms)/1000)
	}
	return fmt.Sprintf("%dm%02ds", ms/60_000, (ms%60_000)/1000)
}
