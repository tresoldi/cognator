package pipeline

import (
	"testing"

	"github.com/tresoldi/cognator/internal/cluster"
)

func TestRefinePartitionCoreCCSplitsWeakBridge(t *testing.T) {
	g := cluster.NewGraph(4, []cluster.Edge{
		{A: 0, B: 1, Weight: 0.95},
		{A: 2, B: 3, Weight: 0.96},
		{A: 1, B: 2, Weight: 0.40},
	})
	part := []int{0, 0, 0, 0}
	got := refinePartition(g, part, Spec{RefineMode: "core-cc", RefineCoreWeight: 0.75})
	if got[0] != got[1] {
		t.Fatalf("expected 0 and 1 to remain together, got %v", got)
	}
	if got[2] != got[3] {
		t.Fatalf("expected 2 and 3 to remain together, got %v", got)
	}
	if got[1] == got[2] {
		t.Fatalf("expected weak bridge to be split, got %v", got)
	}
}

func TestRefinePartitionDisabledLeavesPartitionUnchanged(t *testing.T) {
	g := cluster.NewGraph(3, []cluster.Edge{{A: 0, B: 1, Weight: 0.9}})
	part := []int{0, 0, 1}
	got := refinePartition(g, part, Spec{RefineMode: "none", RefineCoreWeight: 0.75})
	for i := range part {
		if got[i] != part[i] {
			t.Fatalf("partition changed with refinement disabled: got=%v want=%v", got, part)
		}
	}
}

func TestRefinePartitionReassignMovesWeakMemberToStrongerAltCluster(t *testing.T) {
	g := cluster.NewGraph(4, []cluster.Edge{
		{A: 0, B: 1, Weight: 0.30},
		{A: 0, B: 2, Weight: 0.92},
		{A: 2, B: 3, Weight: 0.94},
	})
	part := []int{0, 0, 1, 1}
	got := refinePartition(g, part, Spec{
		RefineMode:                "reassign",
		RefineConfidenceThreshold: 0.6,
		RefineAltRatio:            1.1,
	})
	if got[0] != got[2] || got[2] != got[3] {
		t.Fatalf("expected node 0 to reassign to the stronger alternative cluster, got %v", got)
	}
}

func TestRefinePartitionReassignLeavesConfidentNodeInPlace(t *testing.T) {
	g := cluster.NewGraph(4, []cluster.Edge{
		{A: 0, B: 1, Weight: 0.90},
		{A: 0, B: 2, Weight: 0.30},
		{A: 2, B: 3, Weight: 0.94},
	})
	part := []int{0, 0, 1, 1}
	got := refinePartition(g, part, Spec{
		RefineMode:                "reassign",
		RefineConfidenceThreshold: 0.6,
		RefineAltRatio:            1.1,
	})
	if got[0] != got[1] {
		t.Fatalf("expected confident node 0 to stay in its cluster, got %v", got)
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

func TestRefinePartitionLocalReclusterSplitsLowConfidenceCluster(t *testing.T) {
	g := cluster.NewGraph(4, []cluster.Edge{
		{A: 0, B: 1, Weight: 0.95},
		{A: 1, B: 2, Weight: 0.35},
		{A: 2, B: 3, Weight: 0.95},
	})
	part := []int{0, 0, 0, 0}
	got := refinePartition(g, part, Spec{
		RefineMode:                       "local-recluster",
		RefineClusterConfidenceThreshold: 1.1,
		RefineLocalCoreWeight:            0.8,
	})
	if got[0] != got[1] || got[2] != got[3] {
		t.Fatalf("expected strong local pairs to survive reclustering, got %v", got)
	}
	if got[1] == got[2] {
		t.Fatalf("expected weakly connected cluster to split under local reclustering, got %v", got)
	}
}
