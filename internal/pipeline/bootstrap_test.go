package pipeline

import "testing"

func TestBootstrapStableClusterHighConfidence(t *testing.T) {
	// Two tight cliques, one weak bridge. The in-clique assignment
	// should be stable under small perturbation; confidences for
	// in-clique members should approach 1.0.
	edges := []cEdge{
		{0, 1, 0.05}, {0, 2, 0.08}, {1, 2, 0.06},
		{3, 4, 0.05}, {3, 5, 0.07}, {4, 5, 0.04},
		{2, 3, 0.55}, // weak bridge above threshold
	}
	canon := runClustering(6, edges, 0.3, 1)
	conf := bootstrapConfidences(6, edges, canon, 0.3, 50, 0.03, 1)
	for i := 0; i < 6; i++ {
		if conf[i] < 0.8 {
			t.Errorf("confidence[%d] = %f; expected ≥0.8 for stable clique", i, conf[i])
		}
	}
}

func TestBootstrapFragileEdgeLowConfidence(t *testing.T) {
	// Two nodes barely under threshold; perturbation should knock the
	// edge out of the graph in some runs, yielding lower stability.
	edges := []cEdge{
		{0, 1, 0.29}, // right at the boundary of threshold=0.30
	}
	canon := runClustering(2, edges, 0.30, 7)
	conf := bootstrapConfidences(2, edges, canon, 0.30, 100, 0.05, 7)
	if conf[0] > 0.95 {
		t.Errorf("borderline pair should have volatile confidence, got %f", conf[0])
	}
}
