// Command sweep runs Detect at multiple thresholds on one input and
// prints B-cubed P/R/F at each, using the input's Cognacy as gold.
// Used during development to answer "is Otsu's threshold optimal?".
package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/tresoldi/cognator/internal/eval"
	"github.com/tresoldi/cognator/internal/merkmal"
	"github.com/tresoldi/cognator/internal/pipeline"
	"github.com/tresoldi/cognator/internal/wordlist"
)

func main() {
	var (
		in        = flag.String("in", "", "arcaverborum-shape TSV/CSV")
		bundle    = flag.String("bundle", "tests/fixtures/merkmal/descriptive", "merkmal bundle")
		list      = flag.String("thresholds", "auto,0.30,0.40,0.45,0.50,0.55,0.60,0.65,0.70,0.75", "comma list; 'auto' = Otsu")
		iters     = flag.Int("iterations", 3, "EM iterations")
		lambda    = flag.Float64("lambda", 0.5, "blend")
	)
	flag.Parse()
	if *in == "" {
		fmt.Fprintln(os.Stderr, "usage: sweep --in=PATH [--thresholds=...] [--iterations=N]")
		os.Exit(2)
	}
	b, err := merkmal.LoadBundle(*bundle)
	if err != nil {
		log.Fatal(err)
	}
	recs, err := wordlist.ReadTable(*in)
	if err != nil {
		log.Fatal(err)
	}
	truth := map[string]string{}
	for _, r := range recs {
		if r.Cognacy == "" {
			continue
		}
		truth[r.ID] = r.Concept + ":" + r.Cognacy
	}

	fmt.Printf("forms: %d  with-cognacy: %d\n", len(recs), len(truth))
	fmt.Printf("%-10s  %-8s  %-6s  %-6s  %-6s  %-7s  %s\n", "threshold", "time", "P", "R", "F", "clusts", "note")

	for _, tok := range strings.Split(*list, ",") {
		tok = strings.TrimSpace(tok)
		spec := pipeline.Spec{Iterations: *iters, Lambda: *lambda, Seed: 1}
		note := ""
		if tok == "auto" {
			note = "Otsu"
		} else {
			v, err := strconv.ParseFloat(tok, 64)
			if err != nil {
				fmt.Printf("skip %q: %v\n", tok, err)
				continue
			}
			spec.Threshold = v
		}
		start := time.Now()
		res := pipeline.Detect(recs, b, spec)
		elapsed := time.Since(start)
		pred := map[string]string{}
		for _, r := range res.Rows {
			pred[r.ID] = r.CognatorID
		}
		p, r, f := eval.BCubed(pred, truth)
		clusts := map[string]struct{}{}
		for _, v := range pred {
			clusts[v] = struct{}{}
		}
		label := tok
		if tok == "auto" {
			label = fmt.Sprintf("%.3f", res.Threshold)
		}
		fmt.Printf("%-10s  %-8s  %.4f  %.4f  %.4f  %-7d  %s\n",
			label, elapsed.Round(10*time.Millisecond), p, r, f, len(clusts), note)
	}
}
