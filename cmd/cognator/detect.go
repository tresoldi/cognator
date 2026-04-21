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
	graphUnilateralPenalty := fs.Float64("graph-unilateral-penalty", 0.0, "penalty on non-mutual expanded edges when endpoints have stronger rivals")
	graphRivalFloor := fs.Float64("graph-rival-floor", 0.6, "rival-support floor at which unilateral penalty activates")
	refineMode := fs.String("refine-mode", "none", "'none' (default) or 'bridge-cut'")
	refineConfidenceThreshold := fs.Float64("refine-confidence-threshold", 0.55, "bridge-cut activates for nodes below this confidence")
	refineBridgeRatio := fs.Float64("refine-bridge-ratio", 0.7, "cut weak in-cluster edges incident to low-confidence nodes below this fraction of node mean support")
	boot := fs.Int("bootstrap", 0, "bootstrap runs for confidence (0 = off)")
	bootSigma := fs.Float64("bootstrap-sigma", 0.05, "stddev of distance perturbation")
	seed := fs.Uint64("seed", 1, "RNG seed")
	jobs := fs.Int("jobs", 0, "concurrent workers (0 = GOMAXPROCS)")
	oovOut := fs.String("oov-out", "", "optional path to oov_report.tsv")
	scope := fs.String("cluster-scope", "concept", "'concept' (default) or 'global' (cross-concept)")
	alignMode := fs.String("alignment-mode", "combined", "'global' or 'combined' (default; global+local+overlap)")
	classReduce := fs.Bool("class-reduce", true, "strip IPA modifiers upstream for SCA-style densification (default true)")
	scorerMode := fs.String("scorer", "learned", "'learned' (default) or 'posterior' (Dirichlet-prior scorer, asymmetric-by-construction)")
	dirichletConcentration := fs.Float64("dirichlet-concentration", 0, "α₀ for --scorer=posterior (0 = default)")
	dirichletSharpness := fs.Float64("dirichlet-sharpness", 0, "k for --scorer=posterior softmax over merkmal distances (0 = default)")
	dirichletScale := fs.String("dirichlet-scale", "", "per-row α₀ scaling for --scorer=posterior: '' (default), 'sparse-boost', 'rich-boost'")
	preset := fs.String("preset", "", "apply a named preset (e.g. 'support-adaptive-bridge'); explicit flags override preset values")
	if err := fs.Parse(args); err != nil {
		os.Exit(2)
	}
	if *preset != "" {
		overrides := pipeline.Preset(*preset)
		if overrides == nil {
			log.Fatalf("unknown preset %q; known: %v", *preset, pipeline.PresetNames())
		}
		explicit := map[string]bool{}
		fs.Visit(func(f *flag.Flag) { explicit[f.Name] = true })
		for name, val := range overrides {
			if explicit[name] {
				continue
			}
			if err := fs.Set(name, val); err != nil {
				log.Fatalf("preset %q: failed to set --%s=%s: %v", *preset, name, val, err)
			}
		}
		log.Printf("applied preset %q", *preset)
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
		Iterations:                *iters,
		Lambda:                    *lambda,
		Threshold:                 *threshold,
		ThresholdMode:             *thresholdMode,
		ThresholdMin:              *threshMin,
		ThresholdMax:              *threshMax,
		ThresholdShrink:           *thresholdShrink,
		GraphMode:                 *graphMode,
		GraphExpansionMode:        *graphExpansionMode,
		GraphTopK:                 *graphTopK,
		GraphAdaptiveMargin:       *graphAdaptiveMargin,
		GraphAdaptiveEitherMax:    *graphAdaptiveEitherMax,
		GraphAdaptiveAmbiguity:    *graphAdaptiveAmbiguity,
		GraphMutualBoost:          *graphMutualBoost,
		GraphTriangleBoost:        *graphTriangleBoost,
		GraphContrastivePenalty:   *graphContrastivePenalty,
		GraphContrastiveFloor:     *graphContrastiveFloor,
		GraphRiskRankWeight:       *graphRiskRankWeight,
		GraphRiskGapWeight:        *graphRiskGapWeight,
		GraphUnilateralPenalty:    *graphUnilateralPenalty,
		GraphRivalFloor:           *graphRivalFloor,
		RefineMode:                *refineMode,
		RefineConfidenceThreshold: *refineConfidenceThreshold,
		RefineBridgeRatio:         *refineBridgeRatio,
		Bootstrap:                 *boot,
		BootstrapSigma:            *bootSigma,
		Seed:                      *seed,
		Jobs:                      *jobs,
		ClusterScope:              *scope,
		AlignmentMode:             *alignMode,
		ClassReduce:               *classReduce,
		ScorerMode:                *scorerMode,
		DirichletConcentration:    *dirichletConcentration,
		DirichletSharpness:        *dirichletSharpness,
		DirichletScaleMode:        *dirichletScale,
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
