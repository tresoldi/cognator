package main

import (
	"encoding/csv"
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"

	"github.com/tresoldi/cognator/internal/eval"
	"github.com/tresoldi/cognator/internal/wordlist"
)

func evalCmd(args []string) {
	fs := flag.NewFlagSet("eval", flag.ExitOnError)
	pred := fs.String("pred", "", "predicted sidecar TSV (cognator detect output)")
	truth := fs.String("truth", "", "ground-truth wordlist (arcaverborum-shape with Cognacy)")
	truthCol := fs.String("truth-col", "Cognacy", "column in --truth to use as gold cluster id")
	conceptScope := fs.Bool("per-concept", true, "partition gold within concept (prepend Concept to Cognacy)")
	if err := fs.Parse(args); err != nil {
		os.Exit(2)
	}
	if *pred == "" || *truth == "" {
		fmt.Fprintln(os.Stderr, "eval: --pred and --truth are required")
		fs.Usage()
		os.Exit(2)
	}

	predMap, err := readSidecarPred(*pred)
	if err != nil {
		log.Fatalf("read pred %s: %v", *pred, err)
	}
	truthMap, err := readTruth(*truth, *truthCol, *conceptScope)
	if err != nil {
		log.Fatalf("read truth %s: %v", *truth, err)
	}

	// Only score items present in both maps; report coverage.
	restricted := map[string]string{}
	for id, c := range predMap {
		if _, ok := truthMap[id]; ok {
			restricted[id] = c
		}
	}
	p, r, f := eval.BCubed(restricted, truthMap)
	fmt.Printf("items scored: %d  (pred=%d  truth=%d)\n", len(restricted), len(predMap), len(truthMap))
	fmt.Printf("B-cubed Precision: %.4f\n", p)
	fmt.Printf("B-cubed Recall:    %.4f\n", r)
	fmt.Printf("B-cubed F1:        %.4f\n", f)
}

func readSidecarPred(path string) (map[string]string, error) {
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
		return nil, err
	}
	idCol, cCol := -1, -1
	for i, h := range header {
		switch h {
		case "ID":
			idCol = i
		case "Cognator_ID":
			cCol = i
		}
	}
	if idCol < 0 || cCol < 0 {
		return nil, fmt.Errorf("sidecar missing ID or Cognator_ID columns")
	}
	out := map[string]string{}
	for {
		row, err := r.Read()
		if err != nil {
			break
		}
		if idCol >= len(row) || cCol >= len(row) {
			continue
		}
		out[strings.TrimSpace(row[idCol])] = strings.TrimSpace(row[cCol])
	}
	return out, nil
}

func readTruth(path, col string, perConcept bool) (map[string]string, error) {
	recs, err := wordlist.ReadTable(path)
	if err != nil {
		return nil, err
	}
	out := map[string]string{}
	for _, rec := range recs {
		var v string
		switch col {
		case "Cognacy":
			v = rec.Cognacy
		default:
			return nil, fmt.Errorf("unsupported truth column %q (only Cognacy for now)", col)
		}
		if v == "" {
			continue
		}
		if perConcept {
			v = rec.Concept + ":" + v
		}
		out[rec.ID] = v
	}
	_ = filepath.Base // keep import stable for future use
	return out, nil
}
