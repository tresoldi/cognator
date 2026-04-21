package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"sort"
	"time"

	"github.com/tresoldi/cognator/internal/merkmal"
	"github.com/tresoldi/cognator/internal/pipeline"
	"github.com/tresoldi/cognator/internal/wordlist"
)

func detectCmd(args []string) {
	fs := flag.NewFlagSet("detect", flag.ExitOnError)
	in := fs.String("in", "", "input wordlist (arcaverborum-shape TSV/CSV)")
	out := fs.String("out", "", "output sidecar TSV")
	bundle := fs.String("bundle", "tests/fixtures/merkmal/descriptive", "merkmal bundle directory")
	iters := fs.Int("iterations", 3, "EM iterations per ordered lect-pair")
	lambda := fs.Float64("lambda", 0.5, "learned-vs-prior blend in [0, 1]")
	threshold := fs.Float64("threshold", 0, "distance threshold (0 = auto via Otsu)")
	thresholdMode := fs.String("threshold-mode", "family", "'family' (default) or 'concept-shrink'")
	threshMin := fs.Float64("threshold-min", 0.2, "Otsu clamp floor")
	threshMax := fs.Float64("threshold-max", 0.8, "Otsu clamp ceiling")
	thresholdShrink := fs.Float64("threshold-shrink", 0.5, "shrinkage toward family prior in concept-shrink mode")
	graphMode := fs.String("graph-mode", "threshold", "'threshold' (default) or 'support'")
	graphExpansionMode := fs.String("graph-expansion-mode", "either", "'either' (default), 'mutual', or 'adaptive' top-k expansion in support graph mode")
	graphTopK := fs.Int("graph-top-k", 2, "nearest-neighbor candidate expansion in support graph mode")
	graphAdaptiveMargin := fs.Float64("graph-adaptive-margin", 0.1, "minimum top1-vs-top2 support gap needed to keep unilateral expansion edges in adaptive mode")
	graphAdaptiveEitherMax := fs.Float64("graph-adaptive-either-max", 0.15, "if ambiguity share is at or below this, adaptive mode falls back to either expansion for the concept graph")
	graphAdaptiveAmbiguity := fs.Float64("graph-adaptive-ambiguity", 0.5, "if this share of nodes is ambiguous, adaptive mode falls back to mutual expansion for the concept graph")
	graphMutualBoost := fs.Float64("graph-mutual-boost", 0.2, "weight boost for mutual top-k edges in support graph mode")
	graphTriangleBoost := fs.Float64("graph-triangle-boost", 0.3, "weight boost for triangle-supported edges in support graph mode")
	graphContrastivePenalty := fs.Float64("graph-contrastive-penalty", 0.0, "penalty for edges dominated by stronger local rivals")
	graphContrastiveFloor := fs.Float64("graph-contrastive-floor", 0.05, "ignore rival gaps up to this size before contrastive penalty activates")
	graphRiskRankWeight := fs.Float64("graph-risk-rank-weight", 0.5, "weight of endpoint-rank risk in calibrated edge-risk score")
	graphRiskGapWeight := fs.Float64("graph-risk-gap-weight", 0.5, "weight of best-rival gap risk in calibrated edge-risk score")
	graphRiskTriangleWeight := fs.Float64("graph-risk-triangle-weight", 0.0, "weight of low triangle-support risk in calibrated edge-risk score")
	graphRiskUnilateralWeight := fs.Float64("graph-risk-unilateral-weight", 0.0, "weight of non-mutual expansion risk in calibrated edge-risk score")
	graphRiskDensityWeight := fs.Float64("graph-risk-density-weight", 0.0, "weight of low local-neighborhood density risk in calibrated edge-risk score")
	graphRiskDispersionWeight := fs.Float64("graph-risk-dispersion-weight", 0.0, "weight of concept-level ambiguity/dispersion risk in calibrated edge-risk score")
	graphUnilateralPenalty := fs.Float64("graph-unilateral-penalty", 0.0, "penalty on non-mutual expanded edges when endpoints have stronger rivals")
	graphRivalFloor := fs.Float64("graph-rival-floor", 0.6, "rival-support floor at which unilateral penalty activates")
	refineMode := fs.String("refine-mode", "none", "'none' (default), 'core-cc', 'reassign', 'alt-reassign', 'bridge-cut', 'bridge-alt-reassign', 'alt-reassign-bridge', 'alt-bridge-cut', or 'local-recluster'")
	refineCoreWeight := fs.Float64("refine-core-weight", 0.75, "minimum internal edge weight for core-connectivity refinement")
	refineConfidenceThreshold := fs.Float64("refine-confidence-threshold", 0.55, "only reassign nodes below this confidence")
	refineAltRatio := fs.Float64("refine-alt-ratio", 1.1, "alternative cluster support must exceed in-cluster support by this ratio for reassignment")
	refineAltClusterThreshold := fs.Float64("refine-alt-cluster-threshold", 0.0, "minimum target-cluster internal cohesion for alternative-cluster reassignment (0 = disabled)")
	refineBridgeRatio := fs.Float64("refine-bridge-ratio", 0.7, "cut weak in-cluster edges incident to low-confidence nodes below this fraction of node mean support")
	refineClusterConfidenceThreshold := fs.Float64("refine-cluster-confidence-threshold", 0.75, "only locally recluster clusters whose mean confidence falls below this value")
	refineLocalCoreWeight := fs.Float64("refine-local-core-weight", 0.8, "edge floor used during selective local reclustering")
	boot := fs.Int("bootstrap", 0, "bootstrap runs for confidence (0 = off)")
	bootSigma := fs.Float64("bootstrap-sigma", 0.05, "stddev of distance perturbation")
	seed := fs.Uint64("seed", 1, "RNG seed")
	jobs := fs.Int("jobs", 0, "concurrent workers (0 = GOMAXPROCS)")
	oovOut := fs.String("oov-out", "", "optional path to oov_report.tsv")
	scope := fs.String("cluster-scope", "concept", "'concept' (default) or 'global' (cross-concept)")
	alignMode := fs.String("alignment-mode", "combined", "'global' or 'combined' (default; global+local+overlap)")
	classReduce := fs.Bool("class-reduce", true, "strip IPA modifiers upstream for SCA-style densification (default true)")
	if err := fs.Parse(args); err != nil {
		os.Exit(2)
	}
	if *in == "" || *out == "" {
		fmt.Fprintln(os.Stderr, "detect: --in and --out are required")
		fs.Usage()
		os.Exit(2)
	}

	start := time.Now()
	b, err := merkmal.LoadBundle(*bundle)
	if err != nil {
		log.Fatalf("merkmal bundle: %v", err)
	}
	recs, err := wordlist.ReadTable(*in)
	if err != nil {
		log.Fatalf("read %s: %v", *in, err)
	}
	log.Printf("loaded %d records from %s; merkmal system=%s (%d graphemes)",
		len(recs), *in, b.System, b.Manifest.GraphemeCount)

	spec := pipeline.Spec{
		Iterations:                       *iters,
		Lambda:                           *lambda,
		Threshold:                        *threshold,
		ThresholdMode:                    *thresholdMode,
		ThresholdMin:                     *threshMin,
		ThresholdMax:                     *threshMax,
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
		Bootstrap:                        *boot,
		BootstrapSigma:                   *bootSigma,
		Seed:                             *seed,
		Jobs:                             *jobs,
		ClusterScope:                     *scope,
		AlignmentMode:                    *alignMode,
		ClassReduce:                      *classReduce,
	}
	res := pipeline.Detect(recs, b, spec)
	log.Printf("detection complete: %d rows, threshold=%.3f, %d OOV types, %s elapsed",
		len(res.Rows), res.Threshold, len(res.OOV), time.Since(start))

	if err := wordlist.WriteSidecar(*out, res.Rows); err != nil {
		log.Fatalf("write sidecar: %v", err)
	}
	log.Printf("wrote %s", *out)

	if *oovOut != "" {
		if err := writeOOVReport(*oovOut, res.OOV); err != nil {
			log.Fatalf("write oov report: %v", err)
		}
		log.Printf("wrote %s", *oovOut)
	}
}

func writeOOVReport(path string, oov map[string]int) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	fmt.Fprintln(f, "grapheme\tcount")
	type kv struct {
		g string
		n int
	}
	rows := make([]kv, 0, len(oov))
	for g, n := range oov {
		rows = append(rows, kv{g, n})
	}
	sort.Slice(rows, func(i, j int) bool { return rows[i].n > rows[j].n })
	for _, r := range rows {
		fmt.Fprintf(f, "%s\t%d\n", r.g, r.n)
	}
	return nil
}
