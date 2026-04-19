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
	alpha := fs.Float64("alpha", 1.0, "Dirichlet pseudocount strength")
	threshold := fs.Float64("threshold", 0, "distance threshold (0 = auto via Otsu)")
	threshMin := fs.Float64("threshold-min", 0.2, "Otsu clamp floor")
	threshMax := fs.Float64("threshold-max", 0.8, "Otsu clamp ceiling")
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
		Iterations:     *iters,
		Lambda:         *lambda,
		Alpha:          *alpha,
		Threshold:      *threshold,
		ThresholdMin:   *threshMin,
		ThresholdMax:   *threshMax,
		Bootstrap:      *boot,
		BootstrapSigma: *bootSigma,
		Seed:           *seed,
		Jobs:           *jobs,
		ClusterScope:   *scope,
		AlignmentMode:  *alignMode,
		ClassReduce:    *classReduce,
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
