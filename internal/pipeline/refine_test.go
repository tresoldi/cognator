package pipeline

import (
	"testing"

	"github.com/tresoldi/cognator/internal/cluster"
)

func TestRefinePartitionDisabledLeavesPartitionUnchanged(t *testing.T) {
	g := cluster.NewGraph(3, []cluster.Edge{{A: 0, B: 1, Weight: 0.9}})
	part := []int{0, 0, 1}
	got := refinePartition(g, part, Spec{RefineMode: "none"})
	for i := range part {
		if got[i] != part[i] {
			t.Fatalf("partition changed with refinement disabled: got=%v want=%v", got, part)
		}
	}
}

func TestMembershipDiagnosticsExposeAssignedAndAlternativeSupport(t *testing.T) {
	g := cluster.NewGraph(4, []cluster.Edge{
		{A: 0, B: 1, Weight: 0.80},
		{A: 0, B: 2, Weight: 0.30},
		{A: 0, B: 3, Weight: 0.20},
		{A: 2, B: 3, Weight: 0.90},
	})
	part := []int{0, 0, 1, 1}
	got := membershipDiagnostics(g, part)
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
}

func closeFloat(a, b float64) bool {
	if a > b {
		return a-b < 1e-9
	}
	return b-a < 1e-9
}

func TestRefinePartitionBridgeCutSplitsWeakMemberBridge(t *testing.T) {
	g := cluster.NewGraph(4, []cluster.Edge{
		{A: 0, B: 1, Weight: 0.95},
		{A: 1, B: 2, Weight: 0.20},
		{A: 2, B: 3, Weight: 0.95},
	})
	part := []int{0, 0, 0, 0}
	got := refinePartition(g, part, Spec{
		RefineMode:                "bridge-cut",
		RefineConfidenceThreshold: 0.6,
		RefineBridgeRatio:         0.7,
	})
	if got[0] == got[2] || got[1] == got[2] {
		t.Fatalf("expected weak bridge incident to low-confidence node to be cut, got %v", got)
	}
}
