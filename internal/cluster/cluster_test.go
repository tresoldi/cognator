package cluster

import (
	"math"
	"math/rand/v2"
	"sort"
	"testing"
)

// ----- Graph tests -----

func TestTwoCliquesRecoverTwoModules(t *testing.T) {
	// Nodes 0..3 form one clique, 4..7 another, with a single weak
	// edge bridging 3 and 4.
	n := 8
	var edges []Edge
	addClique := func(from, to int) {
		for i := from; i < to; i++ {
			for j := i + 1; j < to; j++ {
				edges = append(edges, Edge{A: i, B: j, Weight: 1.0})
			}
		}
	}
	addClique(0, 4)
	addClique(4, 8)
	edges = append(edges, Edge{A: 3, B: 4, Weight: 0.1})
	g := NewGraph(n, edges)

	p := Infomap(g, 42)
	// Nodes 0..3 should share one module; 4..7 share another.
	if p[0] != p[1] || p[1] != p[2] || p[2] != p[3] {
		t.Errorf("left clique should be one module: %v", p)
	}
	if p[4] != p[5] || p[5] != p[6] || p[6] != p[7] {
		t.Errorf("right clique should be one module: %v", p)
	}
	if p[0] == p[4] {
		t.Errorf("cliques should be separate modules: %v", p)
	}
}

func TestCompleteGraphOneModule(t *testing.T) {
	// A complete graph of 5 nodes with equal weights should not
	// partition into sub-modules.
	n := 5
	var edges []Edge
	for i := 0; i < n; i++ {
		for j := i + 1; j < n; j++ {
			edges = append(edges, Edge{A: i, B: j, Weight: 1.0})
		}
	}
	g := NewGraph(n, edges)
	p := Infomap(g, 1)
	modID := p[0]
	for i := 1; i < n; i++ {
		if p[i] != modID {
			t.Errorf("complete graph should be one module, got %v", p)
			break
		}
	}
}

func TestIsolatedNodesAreSeparate(t *testing.T) {
	// Two nodes, no edges. Each is its own module.
	g := NewGraph(2, nil)
	p := Infomap(g, 1)
	if p[0] == p[1] {
		t.Errorf("isolated nodes should be distinct modules, got %v", p)
	}
}

func TestSingleEdgeTwoNodes(t *testing.T) {
	g := NewGraph(2, []Edge{{A: 0, B: 1, Weight: 1.0}})
	p := Infomap(g, 1)
	if p[0] != p[1] {
		t.Errorf("directly connected pair should merge, got %v", p)
	}
}

func TestDeterministicUnderSeed(t *testing.T) {
	// Same graph + same seed → identical partition.
	var edges []Edge
	rng := rand.New(rand.NewPCG(7, 7))
	n := 20
	for i := 0; i < n; i++ {
		for j := i + 1; j < n; j++ {
			if rng.Float64() < 0.3 {
				edges = append(edges, Edge{A: i, B: j, Weight: rng.Float64()})
			}
		}
	}
	g := NewGraph(n, edges)
	p1 := Infomap(g, 99)
	p2 := Infomap(g, 99)
	for i := range p1 {
		if p1[i] != p2[i] {
			t.Errorf("non-deterministic at node %d: %d vs %d", i, p1[i], p2[i])
			break
		}
	}
}

// ----- Map equation invariants -----

func TestMapEquationNonNegative(t *testing.T) {
	g := NewGraph(4, []Edge{
		{A: 0, B: 1, Weight: 1.0},
		{A: 2, B: 3, Weight: 1.0},
	})
	for _, part := range [][]int{
		{0, 0, 0, 0},
		{0, 1, 2, 3},
		{0, 0, 1, 1},
	} {
		if l := g.MapEquation(part); l < 0 {
			t.Errorf("map equation %f < 0 for %v", l, part)
		}
	}
}

func TestMapEquationPrefersMeaningfulPartition(t *testing.T) {
	// Two disjoint dense clusters. The two-module partition should
	// have a smaller L than the one-module partition.
	g := NewGraph(6, []Edge{
		{A: 0, B: 1, Weight: 1.0}, {A: 0, B: 2, Weight: 1.0}, {A: 1, B: 2, Weight: 1.0},
		{A: 3, B: 4, Weight: 1.0}, {A: 3, B: 5, Weight: 1.0}, {A: 4, B: 5, Weight: 1.0},
		{A: 2, B: 3, Weight: 0.05},
	})
	flat := []int{0, 0, 0, 0, 0, 0}
	split := []int{0, 0, 0, 1, 1, 1}
	if g.MapEquation(split) >= g.MapEquation(flat) {
		t.Errorf("expected split (%f) < flat (%f)", g.MapEquation(split), g.MapEquation(flat))
	}
}

// ----- Otsu -----

func TestOtsuBimodalRecoversValley(t *testing.T) {
	// Distribution with mass near 0.1 and 0.9 — the valley is around 0.5.
	var d []float64
	for i := 0; i < 400; i++ {
		d = append(d, 0.05+0.10*float64(i%5)/5.0) // 0.05..0.13
	}
	for i := 0; i < 400; i++ {
		d = append(d, 0.85+0.10*float64(i%5)/5.0) // 0.85..0.93
	}
	th := OtsuThreshold(d, 0.0, 1.0, 64)
	if th < 0.3 || th > 0.7 {
		t.Errorf("Otsu threshold %f; expected valley between modes (~0.5)", th)
	}
}

func TestOtsuUnimodalYieldsInteriorThreshold(t *testing.T) {
	d := make([]float64, 500)
	for i := range d {
		d[i] = 0.4 + 0.01*math.Sin(float64(i))
	}
	th := OtsuThreshold(d, 0.0, 1.0, 64)
	if th <= 0 || th >= 1 {
		t.Errorf("Otsu threshold %f on unimodal data; want interior value", th)
	}
}

func TestOtsuEmptyInputSafe(t *testing.T) {
	if th := OtsuThreshold(nil, 0, 1, 32); th <= 0 || th >= 1 {
		t.Errorf("empty Otsu should return a sane midpoint, got %f", th)
	}
}

// ----- Helpers -----

func partitionSizes(p []int) []int {
	counts := map[int]int{}
	for _, m := range p {
		counts[m]++
	}
	sizes := make([]int, 0, len(counts))
	for _, c := range counts {
		sizes = append(sizes, c)
	}
	sort.Ints(sizes)
	return sizes
}
