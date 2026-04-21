// Command bench runs cognator's pipeline on every Family in an
// arcaverborum-shape corpus and emits per-dataset B-cubed P/R/F,
// threshold, cluster count, OOV types, and timing. The output is a
// TSV plus a markdown table ready to paste into a comparison paper.
//
// Families are independent so we run them in parallel.
package main

import (
	"encoding/csv"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"runtime/pprof"
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

type prediction struct {
	EvalID        string
	Dataset       string
	Concept       string
	Gold          string
	Pred          string
	Confidence    float64
	Bootstrap     float64
	BootstrapLow  float64
	BootstrapHigh float64
}

func main() {
	var (
		in                               = flag.String("in", "/tmp/arcaverborum_corecog/arcaverborum-A-corecog-20251008/forms.csv", "arcaverborum-shape CSV/TSV")
		evalTable                        = flag.String("eval-table", "", "benchmark evaluation TSV exported by scripts/export_arcaverborum_eval.py")
		splitFile                        = flag.String("split-file", "", "optional file with one dataset/family per line")
		track                            = flag.String("track", "", "optional track label for logging/metadata (informational)")
		predOut                          = flag.String("pred-out", "", "optional per-form prediction TSV output")
		runLabel                         = flag.String("run-label", "", "optional run label written to prediction output")
		bundle                           = flag.String("bundle", "tests/fixtures/merkmal/descriptive", "merkmal bundle directory")
		outTSV                           = flag.String("out-tsv", "benchmark/results.tsv", "per-dataset TSV output")
		outMD                            = flag.String("out-md", "benchmark/results.md", "markdown summary output")
		iters                            = flag.Int("iterations", 3, "EM iterations per lect-pair")
		lambda                           = flag.Float64("lambda", 0.5, "learned/prior blend (0.0=prior only, 1.0=learned only)")
		scope                            = flag.String("cluster-scope", "concept", "'concept' or 'global'")
		thresholdOver                    = flag.Float64("threshold", 0, "override threshold (0 = Otsu per-dataset)")
		thresholdMode                    = flag.String("threshold-mode", "family", "'family' (default) or 'concept-shrink'")
		thresholdMin                     = flag.Float64("threshold-min", 0, "Otsu clamp floor (0 = default 0.2)")
		thresholdMax                     = flag.Float64("threshold-max", 0, "Otsu clamp ceiling (0 = default 0.8)")
		thresholdShrink                  = flag.Float64("threshold-shrink", 0.5, "shrinkage toward family prior in concept-shrink mode")
		graphMode                        = flag.String("graph-mode", "threshold", "'threshold' (default) or 'support'")
		graphExpansionMode               = flag.String("graph-expansion-mode", "either", "'either' (default), 'mutual', or 'adaptive' top-k expansion in support graph mode")
		graphTopK                        = flag.Int("graph-top-k", 2, "nearest-neighbor candidate expansion in support graph mode")
		graphAdaptiveMargin              = flag.Float64("graph-adaptive-margin", 0.1, "minimum top1-vs-top2 support gap needed to keep unilateral expansion edges in adaptive mode")
		graphAdaptiveEitherMax           = flag.Float64("graph-adaptive-either-max", 0.15, "if ambiguity share is at or below this, adaptive mode falls back to either expansion for the concept graph")
		graphAdaptiveAmbiguity           = flag.Float64("graph-adaptive-ambiguity", 0.5, "if this share of nodes is ambiguous, adaptive mode falls back to mutual expansion for the concept graph")
		graphMutualBoost                 = flag.Float64("graph-mutual-boost", 0.2, "weight boost for mutual top-k edges in support graph mode")
		graphTriangleBoost               = flag.Float64("graph-triangle-boost", 0.3, "weight boost for triangle-supported edges in support graph mode")
		graphContrastivePenalty          = flag.Float64("graph-contrastive-penalty", 0.0, "penalty for edges dominated by stronger local rivals")
		graphContrastiveFloor            = flag.Float64("graph-contrastive-floor", 0.05, "ignore rival gaps up to this size before contrastive penalty activates")
		graphRiskRankWeight              = flag.Float64("graph-risk-rank-weight", 0.5, "weight of endpoint-rank risk in calibrated edge-risk score")
		graphRiskGapWeight               = flag.Float64("graph-risk-gap-weight", 0.5, "weight of best-rival gap risk in calibrated edge-risk score")
		graphRiskTriangleWeight          = flag.Float64("graph-risk-triangle-weight", 0.0, "weight of low triangle-support risk in calibrated edge-risk score")
		graphRiskUnilateralWeight        = flag.Float64("graph-risk-unilateral-weight", 0.0, "weight of non-mutual expansion risk in calibrated edge-risk score")
		graphRiskDensityWeight           = flag.Float64("graph-risk-density-weight", 0.0, "weight of low local-neighborhood density risk in calibrated edge-risk score")
		graphRiskDispersionWeight        = flag.Float64("graph-risk-dispersion-weight", 0.0, "weight of concept-level ambiguity/dispersion risk in calibrated edge-risk score")
		graphUnilateralPenalty           = flag.Float64("graph-unilateral-penalty", 0.0, "penalty on non-mutual expanded edges when endpoints have stronger rivals")
		graphRivalFloor                  = flag.Float64("graph-rival-floor", 0.6, "rival-support floor at which unilateral penalty activates")
		refineMode                       = flag.String("refine-mode", "none", "'none' (default), 'core-cc', 'reassign', 'alt-reassign', 'bridge-cut', 'bridge-alt-reassign', 'alt-reassign-bridge', 'alt-bridge-cut', or 'local-recluster'")
		refineCoreWeight                 = flag.Float64("refine-core-weight", 0.75, "minimum internal edge weight for core-connectivity refinement")
		refineConfidenceThreshold        = flag.Float64("refine-confidence-threshold", 0.55, "only reassign nodes below this confidence")
		refineAltRatio                   = flag.Float64("refine-alt-ratio", 1.1, "alternative cluster support must exceed in-cluster support by this ratio for reassignment")
		refineAltClusterThreshold        = flag.Float64("refine-alt-cluster-threshold", 0.0, "minimum target-cluster internal cohesion for alternative-cluster reassignment (0 = disabled)")
		refineBridgeRatio                = flag.Float64("refine-bridge-ratio", 0.7, "cut weak in-cluster edges incident to low-confidence nodes below this fraction of node mean support")
		refineClusterConfidenceThreshold = flag.Float64("refine-cluster-confidence-threshold", 0.75, "only locally recluster clusters whose mean confidence falls below this value")
		refineLocalCoreWeight            = flag.Float64("refine-local-core-weight", 0.8, "edge floor used during selective local reclustering")
		bootstrap                        = flag.Int("bootstrap", 0, "bootstrap runs for confidence intervals (0 = off)")
		bootstrapSigma                   = flag.Float64("bootstrap-sigma", 0.05, "stddev of distance perturbation for bootstrap confidence")
		seed                             = flag.Uint64("seed", 1, "RNG seed")
		jobs                             = flag.Int("jobs", 0, "workers per family (0 = GOMAXPROCS)")
		parallelFam                      = flag.Int("parallel-families", 2, "concurrent families")
		alignMode                        = flag.String("alignment-mode", "combined", "'global' or 'combined' (global+local+overlap, default)")
		classReduce                      = flag.Bool("class-reduce", true, "NFD-strip IPA modifiers upstream")
		nullMode                         = flag.String("null-mode", "marginal", "'marginal' (default) or 'shuffle' (permutation-based null, slower but more accurate)")
		nullMaxRuns                      = flag.Int("null-max-runs", 200, "cap on shuffle runs per lect-pair; adaptive stopping may end earlier")
		filter                           = flag.String("datasets", "", "comma list of families; empty = all")
		minCognacy                       = flag.Int("min-cognacy", 50, "skip families with fewer than N Cognacy-annotated forms")
		maxLects                         = flag.Int("max-lects", 250, "skip families with more than N distinct lects (0 = no limit; memory guard)")
		maxForms                         = flag.Int("max-forms", 15000, "skip families with more than N forms (0 = no limit; memory guard — mid-family NW edge transients can balloon 30x sys on L²-heavy families)")
		memProfile                       = flag.String("mem-profile", "", "write post-run heap profile to this file (go tool pprof)")
		memProfileEach                   = flag.String("mem-profile-each", "", "write a heap profile after EACH family to this dir (filename = family.prof)")
	)
	flag.Parse()

	b, err := merkmal.LoadBundle(*bundle)
	if err != nil {
		log.Fatalf("load bundle: %v", err)
	}
	start := time.Now()
	recs, err := loadBenchRecords(*in, *evalTable)
	if err != nil {
		log.Fatalf("read corpus/eval table: %v", err)
	}
	log.Printf("loaded %d records in %s", len(recs), time.Since(start))
	if *track != "" {
		log.Printf("benchmark track: %s", *track)
	}

	byFam := map[string][]wordlist.Record{}
	for _, r := range recs {
		byFam[r.Family] = append(byFam[r.Family], r)
	}
	famNames := make([]string, 0, len(byFam))
	allowed := filterSet(*filter)
	if *splitFile != "" {
		fromFile, err := loadNameList(*splitFile)
		if err != nil {
			log.Fatalf("read split file: %v", err)
		}
		if allowed == nil {
			allowed = fromFile
		} else {
			for name := range allowed {
				if !fromFile[name] {
					delete(allowed, name)
				}
			}
		}
	}
	for f := range byFam {
		if len(allowed) > 0 && !allowed[f] {
			continue
		}
		famNames = append(famNames, f)
	}
	sort.Strings(famNames)
	log.Printf("%d families scheduled", len(famNames))

	results := make([]result, len(famNames))
	predictionsByFamily := make([][]prediction, len(famNames))
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
			results[i], predictionsByFamily[i] = runFamily(name, byFam[name], b, pipeline.Spec{
				Iterations:                       *iters,
				Lambda:                           *lambda,
				Threshold:                        *thresholdOver,
				ThresholdMode:                    *thresholdMode,
				ThresholdMin:                     *thresholdMin,
				ThresholdMax:                     *thresholdMax,
				ThresholdShrink:                  *thresholdShrink,
				GraphMode:                        *graphMode,
				GraphExpansionMode:               *graphExpansionMode,
				GraphTopK:                        *graphTopK,
				GraphAdaptiveMargin:              *graphAdaptiveMargin,
				GraphAdaptiveEitherMax:           *graphAdaptiveEitherMax,
				GraphAdaptiveAmbiguity:           *graphAdaptiveAmbiguity,
				GraphMutualBoost:                 *graphMutualBoost,
				GraphTriangleBoost:               *graphTriangleBoost,
				GraphContrastivePenalty:          *graphContrastivePenalty,
				GraphContrastiveFloor:            *graphContrastiveFloor,
				GraphRiskRankWeight:              *graphRiskRankWeight,
				GraphRiskGapWeight:               *graphRiskGapWeight,
				GraphRiskTriangleWeight:          *graphRiskTriangleWeight,
				GraphRiskUnilateralWeight:        *graphRiskUnilateralWeight,
				GraphRiskDensityWeight:           *graphRiskDensityWeight,
				GraphRiskDispersionWeight:        *graphRiskDispersionWeight,
				GraphUnilateralPenalty:           *graphUnilateralPenalty,
				GraphRivalFloor:                  *graphRivalFloor,
				RefineMode:                       *refineMode,
				RefineCoreWeight:                 *refineCoreWeight,
				RefineConfidenceThreshold:        *refineConfidenceThreshold,
				RefineAltRatio:                   *refineAltRatio,
				RefineAltClusterThreshold:        *refineAltClusterThreshold,
				RefineBridgeRatio:                *refineBridgeRatio,
				RefineClusterConfidenceThreshold: *refineClusterConfidenceThreshold,
				RefineLocalCoreWeight:            *refineLocalCoreWeight,
				Bootstrap:                        *bootstrap,
				BootstrapSigma:                   *bootstrapSigma,
				Seed:                             *seed,
				Jobs:                             *jobs,
				ClusterScope:                     *scope,
				AlignmentMode:                    *alignMode,
				ClassReduce:                      *classReduce,
				NullMode:                         *nullMode,
				NullMaxRuns:                      *nullMaxRuns,
			}, *minCognacy, *maxLects, *maxForms)
			r := results[i]
			if r.Skipped != "" {
				log.Printf("[%s] skipped: %s", r.Dataset, r.Skipped)
			} else {
				var ms runtime.MemStats
				runtime.ReadMemStats(&ms)
				log.Printf("[%s] lects=%d forms=%d P=%.3f R=%.3f F=%.3f threshold=%.3f clusts=%d (%dms) alloc=%dMB sys=%dMB",
					r.Dataset, r.Lects, r.Forms, r.Precision, r.Recall, r.F, r.Threshold, r.Clusters, r.ElapsedMillis,
					ms.Alloc/(1<<20), ms.Sys/(1<<20))
				if *memProfileEach != "" {
					runtime.GC()
					f, err := os.Create(filepath.Join(*memProfileEach, name+".prof"))
					if err == nil {
						_ = pprof.WriteHeapProfile(f)
						_ = f.Close()
					}
				}
			}
		}(i, name)
	}
	wg.Wait()

	if *memProfile != "" {
		runtime.GC()
		f, err := os.Create(*memProfile)
		if err == nil {
			_ = pprof.WriteHeapProfile(f)
			_ = f.Close()
			log.Printf("wrote heap profile %s", *memProfile)
		}
	}

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
	if *predOut != "" {
		if err := ensureDir(*predOut); err != nil {
			log.Fatalf("mkdir pred-out: %v", err)
		}
		var predictions []prediction
		for _, rows := range predictionsByFamily {
			predictions = append(predictions, rows...)
		}
		sort.Slice(predictions, func(i, j int) bool {
			if predictions[i].Dataset != predictions[j].Dataset {
				return predictions[i].Dataset < predictions[j].Dataset
			}
			if predictions[i].Concept != predictions[j].Concept {
				return predictions[i].Concept < predictions[j].Concept
			}
			return predictions[i].EvalID < predictions[j].EvalID
		})
		if err := writePredictions(*predOut, predictions, *runLabel); err != nil {
			log.Fatalf("write predictions: %v", err)
		}
		log.Printf("wrote %s", *predOut)
	}
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

func loadNameList(path string) (map[string]bool, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	out := map[string]bool{}
	for _, line := range strings.Split(string(data), "\n") {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		out[line] = true
	}
	return out, nil
}

func loadBenchRecords(rawPath, evalPath string) ([]wordlist.Record, error) {
	if evalPath != "" {
		return readEvalTable(evalPath)
	}
	return wordlist.ReadTable(rawPath)
}

func readEvalTable(path string) ([]wordlist.Record, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	r := csv.NewReader(f)
	r.Comma = '\t'
	r.FieldsPerRecord = -1
	header, err := r.Read()
	if err != nil {
		return nil, fmt.Errorf("read header: %w", err)
	}
	pos := map[string]int{}
	for i, name := range header {
		pos[name] = i
	}
	required := []string{"Eval_ID", "Dataset", "Language_ID", "Concept", "Segments", "Gold_Cognacy"}
	for _, name := range required {
		if _, ok := pos[name]; !ok {
			return nil, fmt.Errorf("missing required column %q in eval table", name)
		}
	}
	get := func(row []string, name string) string {
		i := pos[name]
		if i >= len(row) {
			return ""
		}
		return strings.TrimSpace(row[i])
	}
	var out []wordlist.Record
	for {
		row, err := r.Read()
		if err != nil {
			if err == io.EOF {
				break
			}
			return nil, fmt.Errorf("read row: %w", err)
		}
		rec := wordlist.Record{
			ID:         get(row, "Eval_ID"),
			LanguageID: get(row, "Language_ID"),
			Concept:    get(row, "Concept"),
			Segments:   get(row, "Segments"),
			Family:     get(row, "Dataset"),
			Cognacy:    get(row, "Gold_Cognacy"),
		}
		if rec.ID == "" || rec.LanguageID == "" || rec.Concept == "" || rec.Segments == "" || rec.Family == "" {
			continue
		}
		out = append(out, rec)
	}
	return out, nil
}

func runFamily(name string, recs []wordlist.Record, b *merkmal.Bundle, spec pipeline.Spec, minCognacy, maxLects, maxForms int) (result, []prediction) {
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
		return r, nil
	}
	if r.Lects < 2 {
		r.Skipped = "fewer than 2 lects"
		return r, nil
	}
	if maxLects > 0 && r.Lects > maxLects {
		r.Skipped = fmt.Sprintf("%d lects exceeds max-lects=%d (memory guard)", r.Lects, maxLects)
		return r, nil
	}
	if maxForms > 0 && r.Forms > maxForms {
		r.Skipped = fmt.Sprintf("%d forms exceeds max-forms=%d (memory guard)", r.Forms, maxForms)
		return r, nil
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
	meta := map[string]wordlist.Record{}
	for _, rec := range recs {
		meta[rec.ID] = rec
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
	var predictions []prediction
	for _, row := range res.Rows {
		rec, ok := meta[row.ID]
		if !ok {
			continue
		}
		predictions = append(predictions, prediction{
			EvalID:        row.ID,
			Dataset:       name,
			Concept:       rec.Concept,
			Gold:          truth[row.ID],
			Pred:          row.CognatorID,
			Confidence:    row.Confidence,
			Bootstrap:     row.Bootstrap,
			BootstrapLow:  row.BootstrapLow,
			BootstrapHigh: row.BootstrapHigh,
		})
	}
	return r, predictions
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

func writePredictions(path string, rows []prediction, runLabel string) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	fmt.Fprintln(f, "eval_id\tdataset\tconcept\tgold\tpred\tconfidence\tbootstrap\tbootstrap_low\tbootstrap_high\trun_label")
	for _, row := range rows {
		fmt.Fprintf(f, "%s\t%s\t%s\t%s\t%s\t%.6f\t%.6f\t%.6f\t%.6f\t%s\n",
			row.EvalID, row.Dataset, row.Concept, row.Gold, row.Pred,
			row.Confidence, row.Bootstrap, row.BootstrapLow, row.BootstrapHigh, runLabel)
	}
	return nil
}
