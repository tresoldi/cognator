// Package confidence summarizes node-level cluster-membership
// confidence from a clustering partition. It consumes cluster.Graph
// plus a partition slice; outputs are consumed by the pipeline's
// sidecar writer and by refinement modes.
//
// Confidence semantics mirror the sidecar schema described in
// docs/roadmap.md Model §2. The package is state-free; all functions
// are pure.
package confidence

import "github.com/tresoldi/cognator/internal/cluster"

// Diagnostic holds the per-form confidence signals emitted in the
// sidecar. Confidence is the support-ratio score in [0, 1];
// ClusterSize and CandidateNeighbors are structural signals used by
// scripts/calibrate_confidence.py stratification.
type Diagnostic struct {
	Confidence         float64
	AssignedSupport    float64
	AlternativeSupport float64
	SupportMargin      float64
	ClusterSize        int // # forms in this node's predicted cluster, including self
	CandidateNeighbors int // # candidate-graph neighbors for this node, regardless of cluster
}

// SupportProfile is the raw per-node support data from which a
// Diagnostic is derived. Exposed for refinement modes that need the
// per-cluster breakdown (mean vs total, best-alternative cluster id).
type SupportProfile struct {
	InMean       float64
	InTotal      float64
	InCount      int
	BestAltMod   int
	BestAltMean  float64
	BestAltTotal float64
	BestAltCount int
}

// Diagnostics computes one Diagnostic per node under the supplied
// graph and partition.
func Diagnostics(g *cluster.Graph, part []int) []Diagnostic {
	out := make([]Diagnostic, len(part))
	if g == nil || len(part) == 0 {
		return out
	}
	clusterSize := map[int]int{}
	for _, p := range part {
		clusterSize[p]++
	}
	for i := range part {
		profile := NodeProfile(g, part, i)
		neighbors := 0
		if i < len(g.Adj) {
			neighbors = len(g.Adj[i])
		}
		out[i] = Diagnostic{
			Confidence:         profile.ConfidenceTotal(),
			AssignedSupport:    profile.InTotal,
			AlternativeSupport: profile.BestAltTotal,
			SupportMargin:      profile.InTotal - profile.BestAltTotal,
			ClusterSize:        clusterSize[part[i]],
			CandidateNeighbors: neighbors,
		}
	}
	return out
}

// NodeProfile returns the support profile for one node. BestAltMod is
// -1 when the node has no out-of-cluster neighbors.
func NodeProfile(g *cluster.Graph, part []int, node int) SupportProfile {
	var profile SupportProfile
	profile.BestAltMod = -1
	alt := map[int]float64{}
	altCount := map[int]int{}
	for _, adj := range g.Adj[node] {
		if part[adj.Target] == part[node] {
			profile.InTotal += adj.Weight
			profile.InCount++
			continue
		}
		alt[part[adj.Target]] += adj.Weight
		altCount[part[adj.Target]]++
	}
	if profile.InCount > 0 {
		profile.InMean = profile.InTotal / float64(profile.InCount)
	}
	for mod, total := range alt {
		if total > profile.BestAltTotal {
			profile.BestAltTotal = total
			profile.BestAltMod = mod
			profile.BestAltCount = altCount[mod]
		}
	}
	if profile.BestAltCount > 0 {
		profile.BestAltMean = profile.BestAltTotal / float64(profile.BestAltCount)
	}
	return profile
}

// FromSupport collapses (in-mean, alternative-weight) to a [0, 1]
// confidence score. Edge cases: zero-in-support with nonzero
// alternative yields 1 − altWeight, clamped to [0, 1]; zero-alt with
// nonzero in-support yields 1; all-zero yields 1 (a stable singleton
// with no context).
func FromSupport(inMean, altWeight float64) float64 {
	if inMean <= 0 {
		if altWeight <= 0 {
			return 1.0
		}
		return 1.0 - altWeight
	}
	if altWeight <= 0 {
		return 1.0
	}
	return inMean / (inMean + altWeight)
}

// ConfidenceMean uses the per-cluster mean edge weights.
func (p SupportProfile) ConfidenceMean() float64 {
	return FromSupport(p.InMean, p.BestAltMean)
}

// ConfidenceTotal uses the per-cluster summed edge weights. Heavier
// clusters contribute more to the numerator and denominator; in
// practice this is the more stable signal for sidecar output.
func (p SupportProfile) ConfidenceTotal() float64 {
	if p.InTotal <= 0 {
		if p.BestAltTotal <= 0 {
			return 1.0
		}
		conf := 1.0 - p.BestAltTotal
		if conf < 0 {
			return 0
		}
		return conf
	}
	if p.BestAltTotal <= 0 {
		return 1.0
	}
	return p.InTotal / (p.InTotal + p.BestAltTotal)
}

// MembershipSets groups node indices by their partition label.
// Returns a map from cluster id to the set of node indices in that
// cluster. Useful for bootstrap stability, bridge-cut refinement,
// and anywhere else that needs to walk cluster membership.
func MembershipSets(part []int) map[int]map[int]struct{} {
	out := map[int]map[int]struct{}{}
	for i, m := range part {
		s, ok := out[m]
		if !ok {
			s = map[int]struct{}{}
			out[m] = s
		}
		s[i] = struct{}{}
	}
	return out
}
