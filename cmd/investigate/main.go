// Command investigate is a throwaway debug utility for probing
// specific correspondence phenomena. Rewrite per milestone; keep
// checked in so narrative failures can be reproduced.
package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/tresoldi/cognator/internal/align"
	"github.com/tresoldi/cognator/internal/phon"
	"github.com/tresoldi/cognator/internal/score"
	"github.com/tresoldi/cognator/internal/wordlist"
)

func main() {
	var (
		path       = flag.String("data", "/tmp/arcaverborum_corecog/arcaverborum-A-corecog-20251008/forms.csv", "arcaverborum forms.csv")
		merkmalDir = flag.String("merkmal-dir", "tests/fixtures/merkmal-models", "merkmal data root (contains models/ and geometries/)")
		model      = flag.String("model", "descriptive", "merkmal model name")
	)
	flag.Parse()
	b, err := phon.LoadDir(*merkmalDir, *model)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	recs, err := wordlist.ReadTable(*path)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	src := "walworthpolynesian_Samoan"
	tgt := "walworthpolynesian_Hawaiian"
	fps := score.PairByConcept(recs, src, tgt, b)
	fmt.Printf("%d Samoan-Hawaiian form-pairs\n", len(fps))

	// Probe 1: how often does 'k' appear in Samoan forms, and what is
	// the Hawaiian segment at the same concept-level?
	kInSamoan := 0
	pairsWithSamoanK := 0
	for _, fp := range fps {
		hasK := false
		for _, g := range fp.Source {
			if g == "k" {
				kInSamoan++
				hasK = true
			}
		}
		if hasK {
			pairsWithSamoanK++
			if pairsWithSamoanK <= 5 {
				fmt.Printf("  sample pair: %v → %v\n", fp.Source, fp.Target)
			}
		}
	}
	fmt.Printf("Samoan 'k' occurrences: %d across %d form-pairs\n", kInSamoan, pairsWithSamoanK)

	fmt.Printf("merkmal distance k~ʔ = %.3f, k~k = %.3f, k~t = %.3f\n",
		b.Distance("k", "ʔ"), b.Distance("k", "k"), b.Distance("k", "t"))

	// Probe 2: align a canonical pair under prior vs. learned scorer.
	prior := align.PriorScorer(b)
	tab1 := score.Train(src, tgt, fps, prior)
	tab3 := score.TrainIterated(src, tgt, fps, b, 5, 0.9)
	learned := score.LearnedScorerOf(b, tab3, 0.9)

	for _, fp := range fps {
		// Find a sample pair where source has 'k' and target has 'ʔ'.
		hasK := false
		hasGlot := false
		for _, g := range fp.Source {
			if g == "k" {
				hasK = true
			}
		}
		for _, g := range fp.Target {
			if g == "ʔ" {
				hasGlot = true
			}
		}
		if hasK && hasGlot {
			a1 := align.NeedlemanWunsch(fp.Source, fp.Target, prior)
			a2 := align.NeedlemanWunsch(fp.Source, fp.Target, learned)
			fmt.Printf("\n%v  →  %v\n", fp.Source, fp.Target)
			fmt.Printf("  prior: %v / %v\n", a1.Source, a1.Target)
			fmt.Printf("  learn: %v / %v\n", a2.Source, a2.Target)
			break
		}
	}

	fmt.Printf("\nAttested counts:\n")
	fmt.Printf("  1-iter k→ʔ: %d   5-iter k→ʔ: %d\n", tab1.Count("k", "ʔ"), tab3.Count("k", "ʔ"))
	fmt.Printf("  1-iter ʔ→ʔ: %d   5-iter ʔ→ʔ: %d\n", tab1.Count("ʔ", "ʔ"), tab3.Count("ʔ", "ʔ"))
	fmt.Printf("  1-iter k→k: %d   5-iter k→k: %d\n", tab1.Count("k", "k"), tab3.Count("k", "k"))
}
