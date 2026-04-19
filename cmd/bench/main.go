// Command bench runs cognator's pipeline on every Family in an
// arcaverborum-shape corpus and emits per-dataset B-cubed P/R/F,
// threshold, cluster count, OOV types, and timing. The output is a
// TSV plus a markdown table ready to paste into a comparison paper.
//
// Families are independent so we run them in parallel.
package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/tresoldi/cognator/internal/eval"
	"github.com/tresoldi/cognator/internal/merkmal"
	"github.com/tresoldi/cognator/internal/pipeline"
	"github.com/tresoldi/cognator/internal/wordlist"
)

type result struct {
	Dataset       string
	Lects         int
	Concepts      int
	Forms         int
	CognacyForms  int
	OOVTypes      int
	Threshold     float64
	Clusters      int
	Precision     float64
	Recall        float64
	F             float64
	ElapsedMillis int64
	Skipped       string // non-empty if the dataset was skipped (reason)
}

func main() {
	var (
		in            = flag.String("in", "/tmp/arcaverborum_corecog/arcaverborum-A-corecog-20251008/forms.csv", "arcaverborum-shape CSV/TSV")
		bundle        = flag.String("bundle", "tests/fixtures/merkmal/descriptive", "merkmal bundle directory")
		outTSV        = flag.String("out-tsv", "benchmark/results.tsv", "per-dataset TSV output")
		outMD         = flag.String("out-md", "benchmark/results.md", "markdown summary output")
		iters         = flag.Int("iterations", 3, "EM iterations per lect-pair")
		lambda        = flag.Float64("lambda", 0.5, "learned/prior blend (0.0=prior only, 1.0=learned only)")
		alpha         = flag.Float64("alpha", 1.0, "Dirichlet pseudocount")
		scope         = flag.String("cluster-scope", "concept", "'concept' or 'global'")
		thresholdOver = flag.Float64("threshold", 0, "override threshold (0 = Otsu per-dataset)")
		thresholdMin  = flag.Float64("threshold-min", 0, "Otsu clamp floor (0 = default 0.2)")
		thresholdMax  = flag.Float64("threshold-max", 0, "Otsu clamp ceiling (0 = default 0.8)")
		seed          = flag.Uint64("seed", 1, "RNG seed")
		jobs          = flag.Int("jobs", 0, "workers per family (0 = GOMAXPROCS)")
		parallelFam   = flag.Int("parallel-families", 2, "concurrent families")
		alignMode     = flag.String("alignment-mode", "combined", "'global' or 'combined' (global+local+overlap, default)")
		classReduce   = flag.Bool("class-reduce", true, "NFD-strip IPA modifiers upstream (fallback when --partition-level is empty)")
		partitionLvl  = flag.String("partition-level", "", "merkmal partition level to use ('coarse'/'medium'/'fine'); empty = use class-reduce NFD strip")
		nullMode      = flag.String("null-mode", "marginal", "'marginal' (default) or 'shuffle' (permutation-based null, slower but more accurate)")
		nullMinRuns   = flag.Int("null-min-runs", 0, "floor on shuffle runs before adaptive stopping can kick in (0 = default 20)")
		nullMaxRuns   = flag.Int("null-max-runs", 200, "cap on shuffle runs per lect-pair; adaptive stopping may end earlier")
		filter        = flag.String("datasets", "", "comma list of families; empty = all")
		minCognacy    = flag.Int("min-cognacy", 50, "skip families with fewer than N Cognacy-annotated forms")
		maxLects      = flag.Int("max-lects", 250, "skip families with more than N distinct lects (0 = no limit)")
		maxForms      = flag.Int("max-forms", 0, "skip families with more than N forms (0 = no limit)")
	)
	flag.Parse()

	b, err := merkmal.LoadBundle(*bundle)
	if err != nil {
		log.Fatalf("load bundle: %v", err)
	}
	start := time.Now()
	recs, err := wordlist.ReadTable(*in)
	if err != nil {
		log.Fatalf("read corpus: %v", err)
	}
	log.Printf("loaded %d records in %s", len(recs), time.Since(start))

	byFam := map[string][]wordlist.Record{}
	for _, r := range recs {
		byFam[r.Family] = append(byFam[r.Family], r)
	}
	famNames := make([]string, 0, len(byFam))
	allowed := filterSet(*filter)
	for f := range byFam {
		if len(allowed) > 0 && !allowed[f] {
			continue
		}
		famNames = append(famNames, f)
	}
	sort.Strings(famNames)
	log.Printf("%d families scheduled", len(famNames))

	results := make([]result, len(famNames))
	var wg sync.WaitGroup
	workers := *parallelFam
	if workers <= 0 {
		workers = runtime.GOMAXPROCS(0)
	}
	sem := make(chan struct{}, workers)
	for i, name := range famNames {
		wg.Add(1)
		sem <- struct{}{}
		go func(i int, name string) {
			defer wg.Done()
			defer func() { <-sem }()
			results[i] = runFamily(name, byFam[name], b, pipeline.Spec{
				Iterations:    *iters,
				Lambda:        *lambda,
				Alpha:         *alpha,
				Threshold:     *thresholdOver,
				ThresholdMin:  *thresholdMin,
				ThresholdMax:  *thresholdMax,
				Seed:          *seed,
				Jobs:          *jobs,
				ClusterScope:  *scope,
				AlignmentMode: *alignMode,
				ClassReduce:    *classReduce,
				PartitionLevel: *partitionLvl,
				NullMode:       *nullMode,
				NullMinRuns:    *nullMinRuns,
				NullMaxRuns:    *nullMaxRuns,
			}, *minCognacy, *maxLects, *maxForms)
			r := results[i]
			if r.Skipped != "" {
				log.Printf("[%s] skipped: %s", r.Dataset, r.Skipped)
			} else {
				log.Printf("[%s] lects=%d forms=%d P=%.3f R=%.3f F=%.3f threshold=%.3f clusts=%d (%dms)",
					r.Dataset, r.Lects, r.Forms, r.Precision, r.Recall, r.F, r.Threshold, r.Clusters, r.ElapsedMillis)
			}
		}(i, name)
	}
	wg.Wait()

	if err := ensureDir(*outTSV); err != nil {
		log.Fatalf("mkdir: %v", err)
	}
	if err := writeTSV(*outTSV, results); err != nil {
		log.Fatalf("write tsv: %v", err)
	}
	log.Printf("wrote %s", *outTSV)
	if err := writeMD(*outMD, results, time.Since(start)); err != nil {
		log.Fatalf("write md: %v", err)
	}
	log.Printf("wrote %s", *outMD)
}

func filterSet(s string) map[string]bool {
	if s == "" {
		return nil
	}
	out := map[string]bool{}
	for _, p := range strings.Split(s, ",") {
		p = strings.TrimSpace(p)
		if p != "" {
			out[p] = true
		}
	}
	return out
}

func runFamily(name string, recs []wordlist.Record, b *merkmal.Bundle, spec pipeline.Spec, minCognacy, maxLects, maxForms int) result {
	r := result{Dataset: name, Forms: len(recs)}
	cognacyForms := 0
	lects := map[string]struct{}{}
	concepts := map[string]struct{}{}
	for _, rec := range recs {
		lects[rec.LanguageID] = struct{}{}
		concepts[rec.Concept] = struct{}{}
		if rec.Cognacy != "" {
			cognacyForms++
		}
	}
	r.Lects = len(lects)
	r.Concepts = len(concepts)
	r.CognacyForms = cognacyForms

	if cognacyForms < minCognacy {
		r.Skipped = fmt.Sprintf("only %d Cognacy-annotated forms (<%d)", cognacyForms, minCognacy)
		return r
	}
	if r.Lects < 2 {
		r.Skipped = "fewer than 2 lects"
		return r
	}
	if maxLects > 0 && r.Lects > maxLects {
		r.Skipped = fmt.Sprintf("%d lects exceeds max-lects=%d (memory guard)", r.Lects, maxLects)
		return r
	}
	if maxForms > 0 && r.Forms > maxForms {
		r.Skipped = fmt.Sprintf("%d forms exceeds max-forms=%d (memory guard)", r.Forms, maxForms)
		return r
	}

	start := time.Now()
	res := pipeline.Detect(recs, b, spec)
	r.ElapsedMillis = time.Since(start).Milliseconds()
	r.Threshold = res.Threshold
	r.OOVTypes = len(res.OOV)

	pred := map[string]string{}
	for _, row := range res.Rows {
		pred[row.ID] = row.CognatorID
	}
	clusters := map[string]struct{}{}
	for _, v := range pred {
		clusters[v] = struct{}{}
	}
	r.Clusters = len(clusters)

	truth := map[string]string{}
	for _, rec := range recs {
		if rec.Cognacy != "" {
			truth[rec.ID] = rec.Concept + ":" + rec.Cognacy
		}
	}
	restricted := map[string]string{}
	for id, c := range pred {
		if _, ok := truth[id]; ok {
			restricted[id] = c
		}
	}
	r.Precision, r.Recall, r.F = eval.BCubed(restricted, truth)
	return r
}

func ensureDir(path string) error {
	return os.MkdirAll(filepath.Dir(path), 0o755)
}

func writeTSV(path string, rs []result) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	fmt.Fprintln(f, "dataset\tlects\tconcepts\tforms\tcognacy_forms\toov_types\tthreshold\tclusters\tprecision\trecall\tf1\telapsed_ms\tskipped")
	for _, r := range rs {
		fmt.Fprintf(f, "%s\t%d\t%d\t%d\t%d\t%d\t%.4f\t%d\t%.4f\t%.4f\t%.4f\t%d\t%s\n",
			r.Dataset, r.Lects, r.Concepts, r.Forms, r.CognacyForms, r.OOVTypes,
			r.Threshold, r.Clusters, r.Precision, r.Recall, r.F, r.ElapsedMillis, r.Skipped)
	}
	return nil
}

func writeMD(path string, rs []result, total time.Duration) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	scored := 0
	var sumF, sumP, sumR float64
	weightedF, totalForms := 0.0, 0
	for _, r := range rs {
		if r.Skipped != "" {
			continue
		}
		scored++
		sumF += r.F
		sumP += r.Precision
		sumR += r.Recall
		weightedF += r.F * float64(r.CognacyForms)
		totalForms += r.CognacyForms
	}
	meanF := 0.0
	meanP := 0.0
	meanR := 0.0
	wF := 0.0
	if scored > 0 {
		meanF = sumF / float64(scored)
		meanP = sumP / float64(scored)
		meanR = sumR / float64(scored)
	}
	if totalForms > 0 {
		wF = weightedF / float64(totalForms)
	}
	// Identify best / worst by F.
	sorted := make([]result, 0, scored)
	for _, r := range rs {
		if r.Skipped == "" {
			sorted = append(sorted, r)
		}
	}
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].F > sorted[j].F })
	median := 0.0
	if len(sorted) > 0 {
		median = sorted[len(sorted)/2].F
	}

	fmt.Fprintf(f, "# Cognator benchmark results\n\n")
	fmt.Fprintf(f, "Total datasets scheduled: %d  scored: %d  skipped: %d\n\n", len(rs), scored, len(rs)-scored)
	fmt.Fprintf(f, "- **Mean F** across datasets: **%.4f**\n", meanF)
	fmt.Fprintf(f, "- Median F: %.4f\n", median)
	fmt.Fprintf(f, "- Form-weighted F: %.4f\n", wF)
	fmt.Fprintf(f, "- Mean Precision: %.4f   Mean Recall: %.4f\n", meanP, meanR)
	fmt.Fprintf(f, "- Total wall time: %s\n\n", total.Round(time.Millisecond))

	fmt.Fprintln(f, "## Per-dataset results (sorted by F, descending)")
	fmt.Fprintln(f, "")
	fmt.Fprintln(f, "| Dataset | Lects | Concepts | Forms | Cognacy | OOV | Threshold | Clusters | P | R | **F** | Time |")
	fmt.Fprintln(f, "|---|---|---|---|---|---|---|---|---|---|---|---|")
	for _, r := range sorted {
		fmt.Fprintf(f, "| %s | %d | %d | %d | %d | %d | %.3f | %d | %.3f | %.3f | **%.3f** | %s |\n",
			r.Dataset, r.Lects, r.Concepts, r.Forms, r.CognacyForms, r.OOVTypes,
			r.Threshold, r.Clusters, r.Precision, r.Recall, r.F, formatMs(r.ElapsedMillis))
	}
	var skipped []result
	for _, r := range rs {
		if r.Skipped != "" {
			skipped = append(skipped, r)
		}
	}
	if len(skipped) > 0 {
		fmt.Fprintln(f, "")
		fmt.Fprintln(f, "## Skipped datasets")
		fmt.Fprintln(f, "")
		fmt.Fprintln(f, "| Dataset | Lects | Forms | Cognacy | Reason |")
		fmt.Fprintln(f, "|---|---|---|---|---|")
		for _, r := range skipped {
			fmt.Fprintf(f, "| %s | %d | %d | %d | %s |\n",
				r.Dataset, r.Lects, r.Forms, r.CognacyForms, r.Skipped)
		}
	}
	return nil
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
