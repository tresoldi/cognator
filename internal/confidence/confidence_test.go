package confidence

import (
	"testing"

	"github.com/tresoldi/cognator/internal/cluster"
)

func closeFloat(a, b float64) bool {
	if a > b {
		return a-b < 1e-9
	}
	return b-a < 1e-9
}

func TestDiagnosticsExposeAssignedAndAlternativeSupport(t *testing.T) {
	g := cluster.NewGraph(4, []cluster.Edge{
		{A: 0, B: 1, Weight: 0.80},
		{A: 0, B: 2, Weight: 0.30},
		{A: 0, B: 3, Weight: 0.20},
		{A: 2, B: 3, Weight: 0.90},
	})
	part := []int{0, 0, 1, 1}
	got := Diagnostics(g, part)
	if !closeFloat(got[0].AssignedSupport, 0.80) {
		t.Fatalf("assigned support = %f, want 0.80", got[0].AssignedSupport)
	}
	if !closeFloat(got[0].AlternativeSupport, 0.50) {
		t.Fatalf("alternative support = %f, want 0.50", got[0].AlternativeSupport)
	}
	if !closeFloat(got[0].SupportMargin, 0.30) {
		t.Fatalf("support margin = %f, want 0.30", got[0].SupportMargin)
	}
	if got[0].Confidence <= 0 || got[0].Confidence >= 1 {
		t.Fatalf("confidence should be bounded and non-degenerate, got %f", got[0].Confidence)
	}
	if got[0].ClusterSize != 2 {
		t.Errorf("node 0 lives in a 2-member cluster, got ClusterSize=%d", got[0].ClusterSize)
	}
	if got[0].CandidateNeighbors != 3 {
		t.Errorf("node 0 has 3 candidate-graph neighbors, got CandidateNeighbors=%d", got[0].CandidateNeighbors)
	}
}

func TestDiagnosticsEmptyInputsReturnZeroLengthSlice(t *testing.T) {
	if got := Diagnostics(nil, nil); len(got) != 0 {
		t.Errorf("Diagnostics(nil, nil) = %v, want empty", got)
	}
	g := cluster.NewGraph(0, nil)
	if got := Diagnostics(g, nil); len(got) != 0 {
		t.Errorf("Diagnostics(empty graph, nil part) = %v, want empty", got)
	}
}

func TestFromSupportEdgeCases(t *testing.T) {
	// Zero in-support with zero alternative → stable singleton = 1.
	if got := FromSupport(0, 0); got != 1.0 {
		t.Errorf("FromSupport(0, 0) = %v, want 1.0", got)
	}
	// Zero in-support with nonzero alternative → 1 − alt.
	if got := FromSupport(0, 0.4); !closeFloat(got, 0.6) {
		t.Errorf("FromSupport(0, 0.4) = %v, want 0.6", got)
	}
	// Nonzero in with zero alt → 1.
	if got := FromSupport(0.7, 0); got != 1.0 {
		t.Errorf("FromSupport(0.7, 0) = %v, want 1.0", got)
	}
	// Both nonzero: ratio.
	if got := FromSupport(0.8, 0.2); !closeFloat(got, 0.8) {
		t.Errorf("FromSupport(0.8, 0.2) = %v, want 0.8", got)
	}
}

func TestMembershipSetsGroupsByPartition(t *testing.T) {
	part := []int{0, 0, 1, 2, 1}
	got := MembershipSets(part)
	if len(got) != 3 {
		t.Fatalf("expected 3 clusters, got %d", len(got))
	}
	for id, want := range map[int][]int{0: {0, 1}, 1: {2, 4}, 2: {3}} {
		if len(got[id]) != len(want) {
			t.Errorf("cluster %d: size = %d, want %d", id, len(got[id]), len(want))
		}
		for _, idx := range want {
			if _, ok := got[id][idx]; !ok {
				t.Errorf("cluster %d: missing node %d", id, idx)
			}
		}
	}
}

func TestNodeProfileCapturesBestAlternative(t *testing.T) {
	g := cluster.NewGraph(4, []cluster.Edge{
		{A: 0, B: 1, Weight: 0.90},
		{A: 0, B: 2, Weight: 0.40},
		{A: 0, B: 3, Weight: 0.10},
	})
	// Node 0 is in cluster 0; partners 2 and 3 are in separate alt
	// clusters. Cluster containing 2 has total weight 0.4 (single
	// edge), which is the best alternative.
	part := []int{0, 0, 1, 2}
	p := NodeProfile(g, part, 0)
	if !closeFloat(p.InTotal, 0.90) {
		t.Errorf("InTotal = %v, want 0.90", p.InTotal)
	}
	if p.BestAltMod != 1 {
		t.Errorf("BestAltMod = %d, want 1", p.BestAltMod)
	}
	if !closeFloat(p.BestAltTotal, 0.40) {
		t.Errorf("BestAltTotal = %v, want 0.40", p.BestAltTotal)
	}
}

func TestNodeProfileNoAlternativeYieldsSentinel(t *testing.T) {
	g := cluster.NewGraph(2, []cluster.Edge{{A: 0, B: 1, Weight: 0.5}})
	p := NodeProfile(g, []int{0, 0}, 0)
	if p.BestAltMod != -1 {
		t.Errorf("BestAltMod with no alt = %d, want -1", p.BestAltMod)
	}
	if p.BestAltTotal != 0 {
		t.Errorf("BestAltTotal with no alt = %v, want 0", p.BestAltTotal)
	}
}
