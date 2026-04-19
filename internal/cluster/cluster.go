// Package cluster provides graph-community detection primitives
// used by cognator's per-concept cognate-set inference. Two pieces:
//
//  1. Infomap — a greedy optimizer for the two-level map equation
//     (Rosvall & Bergstrom 2008). Input is an undirected, weighted
//     graph; output is a partition of node indices into modules.
//
//  2. OtsuThreshold — the one-dimensional histogram-based
//     bimodality cut (Otsu 1979), used here to pick a distance
//     threshold without hard-coding.
//
// The implementation favors clarity over micro-optimization; the
// per-concept graphs cognator builds are small (typically <30 nodes)
// so an O(V²·M) map-equation recomputation per trial move is fine.
// If global-scope clustering or very large families land later,
// profile before porting incremental bookkeeping.
package cluster

import (
	"math"
	"math/rand/v2"
	"sort"
)

// Edge represents one undirected, weighted link. Callers should
// ensure A < B and that the same pair doesn't appear twice.
type Edge struct {
	A, B   int
	Weight float64
}

type adjEntry struct {
	Target int
	Weight float64
}

// Graph is an undirected, weighted graph built from an edge list.
type Graph struct {
	N        int
	Adj      [][]adjEntry
	Strength []float64
	W        float64 // 2 × total edge weight
}

// NewGraph constructs a Graph. Edges with non-positive weight are
// dropped. Self-loops are ignored.
func NewGraph(n int, edges []Edge) *Graph {
	g := &Graph{
		N:        n,
		Adj:      make([][]adjEntry, n),
		Strength: make([]float64, n),
	}
	for _, e := range edges {
		if e.A == e.B || e.Weight <= 0 {
			continue
		}
		if e.A < 0 || e.B < 0 || e.A >= n || e.B >= n {
			continue
		}
		g.Adj[e.A] = append(g.Adj[e.A], adjEntry{Target: e.B, Weight: e.Weight})
		g.Adj[e.B] = append(g.Adj[e.B], adjEntry{Target: e.A, Weight: e.Weight})
		g.Strength[e.A] += e.Weight
		g.Strength[e.B] += e.Weight
		g.W += 2 * e.Weight
	}
	return g
}

// MapEquation computes the two-level map equation L(M) for the
// partition p (one module id per node). Lower is better.
//
// L(M) = q_↷·H(Q) + Σ_i p_↻_i·H(P_i)
//
// For isolated nodes (strength 0) we treat the node as its own
// module with zero mass; it contributes nothing to L.
func (g *Graph) MapEquation(p []int) float64 {
	if len(p) != g.N {
		panic("partition length != graph N")
	}
	if g.W == 0 {
		return 0
	}
	type moduleAcc struct {
		p float64   // stationary mass in module
		q float64   // exit probability from module
		pi []float64 // per-node stationary masses
	}
	mods := map[int]*moduleAcc{}
	for v := 0; v < g.N; v++ {
		pv := g.Strength[v] / g.W
		m := p[v]
		acc, ok := mods[m]
		if !ok {
			acc = &moduleAcc{}
			mods[m] = acc
		}
		acc.p += pv
		acc.pi = append(acc.pi, pv)
		for _, a := range g.Adj[v] {
			if p[a.Target] != m {
				acc.q += a.Weight / g.W
			}
		}
	}

	var qSum float64
	for _, m := range mods {
		qSum += m.q
	}

	var hQ float64
	if qSum > 0 {
		for _, m := range mods {
			hQ += plogp(m.q / qSum)
		}
		hQ = -hQ
	}

	var hModules float64
	for _, m := range mods {
		usage := m.q + m.p
		if usage <= 0 {
			continue
		}
		var hi float64
		hi += plogp(m.q / usage)
		for _, pv := range m.pi {
			hi += plogp(pv / usage)
		}
		hModules += -usage * hi
	}

	return qSum*hQ + hModules
}

// Infomap greedily optimizes the two-level map equation starting
// from a singleton partition. Node visit order is a permutation
// derived from seed; same seed → identical output.
func Infomap(g *Graph, seed uint64) []int {
	p := make([]int, g.N)
	for i := range p {
		p[i] = i
	}
	rng := rand.New(rand.NewPCG(seed, seed^0x9E3779B97F4A7C15))
	order := make([]int, g.N)
	for i := range order {
		order[i] = i
	}

	improved := true
	for improved {
		improved = false
		rng.Shuffle(len(order), func(i, j int) { order[i], order[j] = order[j], order[i] })
		currentL := g.MapEquation(p)
		for _, v := range order {
			// Collect candidate destination modules: current + each neighbor's.
			cands := map[int]struct{}{p[v]: {}}
			for _, a := range g.Adj[v] {
				cands[p[a.Target]] = struct{}{}
			}
			bestL := currentL
			bestMod := p[v]
			orig := p[v]
			for c := range cands {
				if c == orig {
					continue
				}
				p[v] = c
				l := g.MapEquation(p)
				if l < bestL-1e-12 {
					bestL = l
					bestMod = c
				}
			}
			p[v] = bestMod
			if bestMod != orig {
				improved = true
				currentL = bestL
			}
		}
	}
	return renumber(p)
}

func renumber(p []int) []int {
	m := map[int]int{}
	for _, v := range p {
		if _, ok := m[v]; !ok {
			m[v] = len(m)
		}
	}
	out := make([]int, len(p))
	for i, v := range p {
		out[i] = m[v]
	}
	return out
}

func plogp(x float64) float64 {
	if x <= 0 {
		return 0
	}
	return x * math.Log2(x)
}

// OtsuThreshold picks the cutoff that maximizes between-class
// variance over a histogram of values in [lo, hi] using bins bins.
// Empty input returns the midpoint. Input may include values
// outside [lo, hi]; they are clipped in.
func OtsuThreshold(values []float64, lo, hi float64, bins int) float64 {
	if bins < 2 {
		bins = 64
	}
	if hi <= lo {
		hi = lo + 1
	}
	if len(values) == 0 {
		return (lo + hi) / 2
	}
	hist := make([]int, bins)
	width := (hi - lo) / float64(bins)
	total := 0
	for _, v := range values {
		if v < lo {
			v = lo
		} else if v > hi {
			v = hi
		}
		idx := int((v - lo) / width)
		if idx >= bins {
			idx = bins - 1
		}
		hist[idx]++
		total++
	}
	// Prefix sums: cumulative count and cumulative value-weighted count.
	var sumAll float64
	for i, c := range hist {
		binCenter := lo + (float64(i)+0.5)*width
		sumAll += binCenter * float64(c)
	}
	var bestVar float64
	bestLo := -1
	bestHi := -1
	var w0 float64
	var s0 float64
	for t := 0; t < bins-1; t++ {
		c := float64(hist[t])
		w0 += c
		if w0 == 0 {
			continue
		}
		binCenter := lo + (float64(t)+0.5)*width
		s0 += binCenter * c
		w1 := float64(total) - w0
		if w1 == 0 {
			break
		}
		m0 := s0 / w0
		m1 := (sumAll - s0) / w1
		bv := w0 * w1 * (m0 - m1) * (m0 - m1)
		if bv > bestVar+1e-15 {
			bestVar = bv
			bestLo = t
			bestHi = t
		} else if bv >= bestVar-1e-15 && bestLo >= 0 {
			bestHi = t
		}
	}
	if bestLo < 0 {
		return (lo + hi) / 2
	}
	// Midpoint of the plateau — robust to empty-gap bimodal cases where
	// between-class variance is constant across the void.
	return lo + (float64(bestLo+bestHi)/2.0+1.0)*width
}

// Clamp wraps a threshold value to [min, max]. Helper for callers
// who want Otsu plus safety rails.
func Clamp(v, min, max float64) float64 {
	if v < min {
		return min
	}
	if v > max {
		return max
	}
	return v
}

// compile-time check that sort is used
var _ = sort.Ints
