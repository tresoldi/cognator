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
