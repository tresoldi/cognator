// Package evidence turns pairwise distances into weighted graph edges.
// It keeps the legacy threshold-only mode and adds a support-aware mode
// that preserves a more permissive candidate graph, then boosts edges
// with local structural support.
package evidence

import (
	"sort"

	"github.com/tresoldi/cognator/internal/cluster"
)

// Pair is one undirected pairwise distance between form indices.
type Pair struct {
	A, B     int
	Distance float64
}

// Spec controls graph construction from pairwise distances.
type Spec struct {
	Mode                 string  // "threshold" (default) | "support"
	ExpansionMode        string  // "either" (default) | "mutual" | "adaptive"
	TopK                 int     // nearest-neighbor candidate expansion in support mode
	AdaptiveMargin       float64 // minimum top1-vs-top2 support gap needed to sponsor unilateral expansion in adaptive mode
	AdaptiveEitherMax    float64 // if ambiguity share is at or below this, adaptive mode falls back to either expansion
	AdaptiveAmbiguity    float64 // if this share of active nodes is ambiguous, adaptive mode falls back to mutual expansion
	MutualBoost          float64 // additive weight boost for mutual top-k edges
	TriangleBoost        float64 // additive weight boost scaled by triangle support
	ContrastivePenalty   float64 // penalty applied to edges dominated by stronger local rivals
	ContrastiveFloor     float64 // ignore rival gaps up to this size before contrastive penalty activates
	RiskRankWeight       float64 // weight of endpoint-rank risk in calibrated edge-risk score
	RiskGapWeight        float64 // weight of best-rival gap risk in calibrated edge-risk score
	RiskTriangleWeight   float64 // weight of low triangle-support risk in calibrated edge-risk score
	RiskUnilateralWeight float64 // weight of non-mutual expansion risk in calibrated edge-risk score
	RiskDensityWeight    float64 // weight of low local-neighborhood density risk in calibrated edge-risk score
	RiskDispersionWeight float64 // weight of concept-level ambiguity/dispersion risk in calibrated edge-risk score
	UnilateralPenalty    float64 // penalty applied to expanded non-mutual edges when both endpoints have strong alternatives
	RivalFloor           float64 // minimum rival-support level before unilateral penalty activates
}

type rankedNeighbor struct {
	node     int
	distance float64
}

type nodeProfile struct {
	bestWeight   float64
	secondWeight float64
}

type conceptContext struct {
	densityRisk    []float64
	dispersionRisk float64
}

// BuildGraphEdges converts pairwise distances into weighted graph edges.
func BuildGraphEdges(n int, pairs []Pair, threshold float64, spec Spec) []cluster.Edge {
	if spec.Mode == "" || spec.Mode == "threshold" {
		return thresholdEdges(pairs, threshold)
	}
	return supportEdges(n, pairs, threshold, spec)
}

func thresholdEdges(pairs []Pair, threshold float64) []cluster.Edge {
	edges := make([]cluster.Edge, 0, len(pairs))
	for _, p := range pairs {
		if p.Distance >= threshold {
			continue
		}
		w := similarity(p.Distance)
		if w <= 0 {
			continue
		}
		edges = append(edges, cluster.Edge{A: p.A, B: p.B, Weight: w})
	}
	return edges
}

func supportEdges(n int, pairs []Pair, threshold float64, spec Spec) []cluster.Edge {
	topK := spec.TopK
	if topK <= 0 {
		topK = 2
	}
	mutualBoost := spec.MutualBoost
	if mutualBoost < 0 {
		mutualBoost = 0
	}
	triangleBoost := spec.TriangleBoost
	if triangleBoost < 0 {
		triangleBoost = 0
	}
	contrastivePenalty := spec.ContrastivePenalty
	if contrastivePenalty < 0 {
		contrastivePenalty = 0
	}
	if contrastivePenalty > 1 {
		contrastivePenalty = 1
	}
	contrastiveFloor := spec.ContrastiveFloor
	if contrastiveFloor < 0 {
		contrastiveFloor = 0
	}
	riskRankWeight := spec.RiskRankWeight
	riskGapWeight := spec.RiskGapWeight
	riskTriangleWeight := spec.RiskTriangleWeight
	riskUnilateralWeight := spec.RiskUnilateralWeight
	riskDensityWeight := spec.RiskDensityWeight
	riskDispersionWeight := spec.RiskDispersionWeight
	if riskRankWeight < 0 {
		riskRankWeight = 0
	}
	if riskGapWeight < 0 {
		riskGapWeight = 0
	}
	if riskTriangleWeight < 0 {
		riskTriangleWeight = 0
	}
	if riskUnilateralWeight < 0 {
		riskUnilateralWeight = 0
	}
	if riskDensityWeight < 0 {
		riskDensityWeight = 0
	}
	if riskDispersionWeight < 0 {
		riskDispersionWeight = 0
	}
	if riskRankWeight+riskGapWeight+riskTriangleWeight+riskUnilateralWeight+riskDensityWeight+riskDispersionWeight == 0 {
		riskRankWeight = 0.5
		riskGapWeight = 0.5
	}
	unilateralPenalty := spec.UnilateralPenalty
	if unilateralPenalty < 0 {
		unilateralPenalty = 0
	}
	if unilateralPenalty > 1 {
		unilateralPenalty = 1
	}
	rivalFloor := spec.RivalFloor
	if rivalFloor <= 0 {
		rivalFloor = 0.6
	}
	expansionMode := spec.ExpansionMode
	if expansionMode == "" {
		expansionMode = "either"
	}
	adaptiveMargin := spec.AdaptiveMargin
	if adaptiveMargin <= 0 {
		adaptiveMargin = 0.1
	}
	adaptiveAmbiguity := spec.AdaptiveAmbiguity
	if adaptiveAmbiguity <= 0 {
		adaptiveAmbiguity = 0.5
	}
	adaptiveEitherMax := spec.AdaptiveEitherMax
	if adaptiveEitherMax < 0 {
		adaptiveEitherMax = 0
	}
	if adaptiveEitherMax > adaptiveAmbiguity {
		adaptiveEitherMax = adaptiveAmbiguity
	}

	neighbors := make([][]rankedNeighbor, n)
	profiles := make([]nodeProfile, n)
	for _, p := range pairs {
		neighbors[p.A] = append(neighbors[p.A], rankedNeighbor{node: p.B, distance: p.Distance})
		neighbors[p.B] = append(neighbors[p.B], rankedNeighbor{node: p.A, distance: p.Distance})
	}
	for i := range neighbors {
		sort.Slice(neighbors[i], func(a, b int) bool {
			if neighbors[i][a].distance == neighbors[i][b].distance {
				return neighbors[i][a].node < neighbors[i][b].node
			}
			return neighbors[i][a].distance < neighbors[i][b].distance
		})
		if len(neighbors[i]) > 0 {
			best := similarity(neighbors[i][0].distance)
			second := 0.0
			if len(neighbors[i]) > 1 {
				second = similarity(neighbors[i][1].distance)
			}
			profiles[i] = nodeProfile{bestWeight: best, secondWeight: second}
		}
		if len(neighbors[i]) > topK {
			neighbors[i] = neighbors[i][:topK]
		}
	}

	topKSet := make([]map[int]struct{}, n)
	topKRank := make([]map[int]int, n)
	fullRank := make([]map[int]int, n)
	bestMutual := make([]float64, n)
	bestAnyTopK := make([]float64, n)
	for i := 0; i < n; i++ {
		if len(neighbors[i]) == 0 {
			continue
		}
		fullRank[i] = make(map[int]int, len(neighbors[i]))
		for rank, nb := range neighbors[i] {
			fullRank[i][nb.node] = rank + 1
		}
		topKSet[i] = make(map[int]struct{}, len(neighbors[i]))
		topKRank[i] = make(map[int]int, len(neighbors[i]))
		for _, nb := range neighbors[i] {
			topKSet[i][nb.node] = struct{}{}
			w := similarity(nb.distance)
			if w > bestAnyTopK[i] {
				bestAnyTopK[i] = w
			}
		}
		for rank, nb := range neighbors[i] {
			topKRank[i][nb.node] = rank + 1
		}
	}
	for _, p := range pairs {
		if contains(topKSet[p.A], p.B) && contains(topKSet[p.B], p.A) {
			w := similarity(p.Distance)
			if w > bestMutual[p.A] {
				bestMutual[p.A] = w
			}
			if w > bestMutual[p.B] {
				bestMutual[p.B] = w
			}
		}
	}
	context := summarizeConceptContext(n, neighbors, topKSet, profiles, adaptiveMargin)

	policy := expansionMode
	if expansionMode == "adaptive" {
		ambShare := ambiguousShare(profiles, adaptiveMargin)
		switch {
		case ambShare <= adaptiveEitherMax:
			policy = "either"
		case ambShare >= adaptiveAmbiguity:
			policy = "mutual"
		default:
			policy = "adaptive"
		}
	}

	candidateAdj := make([]map[int]float64, n)
	for _, p := range pairs {
		mutualTopK := contains(topKSet[p.A], p.B) && contains(topKSet[p.B], p.A)
		inTopK := mutualTopK
		switch policy {
		case "mutual":
			// keep mutual-only
		case "adaptive":
			inTopK = mutualTopK || adaptiveAllowsUnilateral(p, topKSet, topKRank, profiles, adaptiveMargin)
		default:
			inTopK = contains(topKSet[p.A], p.B) || contains(topKSet[p.B], p.A)
		}
		if p.Distance >= threshold && !inTopK {
			continue
		}
		w := similarity(p.Distance)
		if w <= 0 {
			continue
		}
		if candidateAdj[p.A] == nil {
			candidateAdj[p.A] = map[int]float64{}
		}
		if candidateAdj[p.B] == nil {
			candidateAdj[p.B] = map[int]float64{}
		}
		candidateAdj[p.A][p.B] = w
		candidateAdj[p.B][p.A] = w
	}

	edges := make([]cluster.Edge, 0, len(pairs))
	for _, p := range pairs {
		base, ok := candidateAdj[p.A][p.B]
		if !ok {
			continue
		}
		mutualTopK := contains(topKSet[p.A], p.B) && contains(topKSet[p.B], p.A)
		expandedOnly := p.Distance >= threshold
		weight := base
		if mutualTopK {
			weight += mutualBoost * (1.0 - base)
		}
		tri := triangleSupport(candidateAdj, p.A, p.B)
		if tri > 0 {
			weight += triangleBoost * tri * (1.0 - weight)
		}
		if contrastivePenalty > 0 {
			risk := calibratedEdgeRisk(
				p.A, p.B, base, tri, expandedOnly, mutualTopK, fullRank, profiles, topK, contrastiveFloor, context,
				riskRankWeight, riskGapWeight, riskTriangleWeight, riskUnilateralWeight, riskDensityWeight, riskDispersionWeight,
			)
			if risk > 0 {
				weight *= 1.0 - contrastivePenalty*risk
			}
		}
		if expandedOnly && !mutualTopK && unilateralPenalty > 0 {
			rivalA := rivalSupport(bestMutual[p.A], bestAnyTopK[p.A], base)
			rivalB := rivalSupport(bestMutual[p.B], bestAnyTopK[p.B], base)
			edgeRival := minFloat(rivalA, rivalB)
			if edgeRival > rivalFloor {
				strength := (edgeRival - rivalFloor) / (1.0 - rivalFloor)
				if strength < 0 {
					strength = 0
				}
				if strength > 1 {
					strength = 1
				}
				weight *= 1.0 - unilateralPenalty*strength
			}
		}
		if weight > 1 {
			weight = 1
		}
		edges = append(edges, cluster.Edge{A: p.A, B: p.B, Weight: weight})
	}
	return edges
}

func contains(m map[int]struct{}, target int) bool {
	if m == nil {
		return false
	}
	_, ok := m[target]
	return ok
}

func triangleSupport(adj []map[int]float64, a, b int) float64 {
	na := adj[a]
	nb := adj[b]
	if len(na) == 0 || len(nb) == 0 {
		return 0
	}
	var inter int
	denom := min(len(na)-1, len(nb)-1)
	if denom <= 0 {
		return 0
	}
	if len(na) > len(nb) {
		na, nb = nb, na
	}
	for k := range na {
		if k == b || k == a {
			continue
		}
		if _, ok := nb[k]; ok {
			inter++
		}
	}
	if inter == 0 {
		return 0
	}
	return float64(inter) / float64(denom)
}

func similarity(distance float64) float64 {
	if distance < 0 {
		distance = 0
	}
	if distance > 1 {
		distance = 1
	}
	return 1.0 - distance
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func minFloat(a, b float64) float64 {
	if a < b {
		return a
	}
	return b
}

func rivalSupport(bestMutual, bestAny, current float64) float64 {
	if bestMutual > current {
		return bestMutual
	}
	if bestAny > current {
		return bestAny
	}
	return 0
}

func adaptiveAllowsUnilateral(p Pair, topKSet []map[int]struct{}, topKRank []map[int]int, profiles []nodeProfile, margin float64) bool {
	return sponsorsUnilateral(p.A, p.B, topKSet, topKRank, profiles, margin) ||
		sponsorsUnilateral(p.B, p.A, topKSet, topKRank, profiles, margin)
}

func sponsorsUnilateral(src, dst int, topKSet []map[int]struct{}, topKRank []map[int]int, profiles []nodeProfile, margin float64) bool {
	if !contains(topKSet[src], dst) || contains(topKSet[dst], src) {
		return false
	}
	if topKRank[src] == nil || topKRank[src][dst] != 1 {
		return false
	}
	return profileMargin(profiles[src]) >= margin
}

func profileMargin(p nodeProfile) float64 {
	margin := p.bestWeight - p.secondWeight
	if margin < 0 {
		return 0
	}
	return margin
}

func ambiguousShare(profiles []nodeProfile, margin float64) float64 {
	var active, ambiguous int
	for _, p := range profiles {
		if p.bestWeight <= 0 {
			continue
		}
		active++
		if profileMargin(p) < margin {
			ambiguous++
		}
	}
	if active == 0 {
		return 0
	}
	return float64(ambiguous) / float64(active)
}

func summarizeConceptContext(n int, neighbors [][]rankedNeighbor, topKSet []map[int]struct{}, profiles []nodeProfile, margin float64) conceptContext {
	ctx := conceptContext{densityRisk: make([]float64, n)}
	for i := 0; i < n; i++ {
		if len(neighbors[i]) == 0 {
			ctx.densityRisk[i] = 1
			continue
		}
		var sum float64
		for _, nb := range neighbors[i] {
			sum += similarity(nb.distance)
		}
		mean := sum / float64(len(neighbors[i]))
		risk := 1.0 - mean
		if risk < 0 {
			risk = 0
		}
		if risk > 1 {
			risk = 1
		}
		ctx.densityRisk[i] = risk
	}
	ambRisk := ambiguousShare(profiles, margin)
	mutualRisk := 1.0 - mutualTopKShare(topKSet)
	ctx.dispersionRisk = 0.5*ambRisk + 0.5*mutualRisk
	if ctx.dispersionRisk < 0 {
		ctx.dispersionRisk = 0
	}
	if ctx.dispersionRisk > 1 {
		ctx.dispersionRisk = 1
	}
	return ctx
}

func mutualTopKShare(topKSet []map[int]struct{}) float64 {
	var total, mutual int
	for src, peers := range topKSet {
		for dst := range peers {
			total++
			if dst < 0 || dst >= len(topKSet) {
				continue
			}
			if contains(topKSet[dst], src) {
				mutual++
			}
		}
	}
	if total == 0 {
		return 1
	}
	return float64(mutual) / float64(total)
}

func calibratedEdgeRisk(a, b int, base, tri float64, expandedOnly, mutualTopK bool, fullRank []map[int]int, profiles []nodeProfile, topK int, floor float64, context conceptContext, rankW, gapW, triW, unilateralW, densityW, dispersionW float64) float64 {
	rankRisk, gapRisk := endpointRiskFeatures(a, b, base, fullRank, profiles, topK, floor)
	rankRiskB, gapRiskB := endpointRiskFeatures(b, a, base, fullRank, profiles, topK, floor)
	rankMean := 0.5 * (rankRisk + rankRiskB)
	gapMean := 0.5 * (gapRisk + gapRiskB)
	triangleRisk := 1.0 - tri
	if triangleRisk < 0 {
		triangleRisk = 0
	}
	unilateralRisk := 0.0
	if expandedOnly && !mutualTopK {
		unilateralRisk = 1.0
	}
	densityRisk := 0.0
	if len(context.densityRisk) > 0 {
		densityRisk = 0.5 * (context.densityRisk[a] + context.densityRisk[b])
	}
	dispersionRisk := context.dispersionRisk
	totalW := rankW + gapW + triW + unilateralW + densityW + dispersionW
	if totalW <= 0 {
		return 0
	}
	score := rankW*rankMean + gapW*gapMean + triW*triangleRisk + unilateralW*unilateralRisk + densityW*densityRisk + dispersionW*dispersionRisk
	risk := score / totalW
	if risk < 0 {
		return 0
	}
	if risk > 1 {
		return 1
	}
	return risk
}

func endpointRiskFeatures(src, dst int, base float64, fullRank []map[int]int, profiles []nodeProfile, topK int, floor float64) (float64, float64) {
	rank := 1
	if fullRank[src] != nil {
		if r, ok := fullRank[src][dst]; ok {
			rank = r
		}
	}
	rankRisk := 0.0
	if rank > 1 {
		denom := float64(maxInt(1, topK))
		rankRisk = float64(min(rank-1, topK)) / denom
	}
	gapRisk := 0.0
	gap := profiles[src].bestWeight - base - floor
	if gap > 0 {
		gapRisk = gap / maxFloat(1e-9, 1.0-floor)
		if gapRisk > 1 {
			gapRisk = 1
		}
	}
	return rankRisk, gapRisk
}

func maxInt(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func maxFloat(a, b float64) float64 {
	if a > b {
		return a
	}
	return b
}
