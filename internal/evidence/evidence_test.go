package evidence

import "testing"

func TestSupportModeKeepsTopKBoundaryEdge(t *testing.T) {
	pairs := []Pair{
		{A: 0, B: 1, Distance: 0.20},
		{A: 0, B: 2, Distance: 0.32},
		{A: 1, B: 2, Distance: 0.70},
	}
	edges := BuildGraphEdges(3, pairs, 0.30, Spec{
		Mode:          "support",
		TopK:          1,
		MutualBoost:   0.2,
		TriangleBoost: 0.2,
	})
	if len(edges) != 2 {
		t.Fatalf("len(edges) = %d, want 2", len(edges))
	}
	foundBoundary := false
	for _, e := range edges {
		if (e.A == 0 && e.B == 2) || (e.A == 2 && e.B == 0) {
			foundBoundary = true
		}
	}
	if !foundBoundary {
		t.Fatal("support mode should keep top-k boundary edge 0-2")
	}
}

func TestSupportModeBoostsMutualTriangleEdge(t *testing.T) {
	pairs := []Pair{
		{A: 0, B: 1, Distance: 0.20},
		{A: 0, B: 2, Distance: 0.22},
		{A: 1, B: 2, Distance: 0.21},
	}
	plain := BuildGraphEdges(3, pairs, 0.30, Spec{Mode: "threshold"})
	boosted := BuildGraphEdges(3, pairs, 0.30, Spec{
		Mode:          "support",
		TopK:          2,
		MutualBoost:   0.2,
		TriangleBoost: 0.3,
	})
	if len(plain) != len(boosted) {
		t.Fatalf("edge count mismatch plain=%d boosted=%d", len(plain), len(boosted))
	}
	if boosted[0].Weight <= plain[0].Weight {
		t.Fatalf("expected support mode to boost edge weight: plain=%f boosted=%f", plain[0].Weight, boosted[0].Weight)
	}
}

func TestSupportModeMutualExpansionDropsUnilateralTopKEdge(t *testing.T) {
	pairs := []Pair{
		{A: 0, B: 1, Distance: 0.24},
		{A: 0, B: 2, Distance: 0.90},
		{A: 1, B: 3, Distance: 0.10},
		{A: 1, B: 4, Distance: 0.11},
	}
	either := BuildGraphEdges(5, pairs, 0.20, Spec{
		Mode:          "support",
		ExpansionMode: "either",
		TopK:          1,
	})
	mutual := BuildGraphEdges(5, pairs, 0.20, Spec{
		Mode:          "support",
		ExpansionMode: "mutual",
		TopK:          1,
	})
	if len(either) <= len(mutual) {
		t.Fatalf("expected unilateral expansion to keep more edges: either=%d mutual=%d", len(either), len(mutual))
	}
}

func TestSupportModePenalizesExpandedUnilateralBridge(t *testing.T) {
	pairs := []Pair{
		{A: 0, B: 1, Distance: 0.10},
		{A: 0, B: 2, Distance: 0.11},
		{A: 2, B: 3, Distance: 0.10},
		{A: 2, B: 4, Distance: 0.11},
		{A: 0, B: 3, Distance: 0.31},
	}
	base := BuildGraphEdges(5, pairs, 0.30, Spec{
		Mode:          "support",
		ExpansionMode: "either",
		TopK:          2,
		MutualBoost:   0.2,
		TriangleBoost: 0.3,
	})
	penalized := BuildGraphEdges(5, pairs, 0.30, Spec{
		Mode:              "support",
		ExpansionMode:     "either",
		TopK:              2,
		MutualBoost:       0.2,
		TriangleBoost:     0.3,
		UnilateralPenalty: 0.5,
		RivalFloor:        0.6,
	})
	var baseBridge, penalizedBridge float64
	for _, e := range base {
		if (e.A == 0 && e.B == 3) || (e.A == 3 && e.B == 0) {
			baseBridge = e.Weight
		}
	}
	for _, e := range penalized {
		if (e.A == 0 && e.B == 3) || (e.A == 3 && e.B == 0) {
			penalizedBridge = e.Weight
		}
	}
	if penalizedBridge >= baseBridge {
		t.Fatalf("expected unilateral bridge penalty to reduce weight: base=%f penalized=%f", baseBridge, penalizedBridge)
	}
}

func TestSupportModeAdaptiveKeepsClearUnilateralTop1Edge(t *testing.T) {
	pairs := []Pair{
		{A: 0, B: 1, Distance: 0.23},
		{A: 0, B: 2, Distance: 0.80},
		{A: 1, B: 3, Distance: 0.10},
		{A: 1, B: 4, Distance: 0.11},
	}
	adaptive := BuildGraphEdges(5, pairs, 0.20, Spec{
		Mode:              "support",
		ExpansionMode:     "adaptive",
		TopK:              1,
		AdaptiveMargin:    0.1,
		AdaptiveEitherMax: 0.4,
	})
	found := false
	for _, e := range adaptive {
		if (e.A == 0 && e.B == 1) || (e.A == 1 && e.B == 0) {
			found = true
		}
	}
	if !found {
		t.Fatal("adaptive mode should keep a clear unilateral top-1 edge")
	}
}

func TestSupportModeAdaptiveDropsAmbiguousUnilateralTop1Edge(t *testing.T) {
	pairs := []Pair{
		{A: 0, B: 1, Distance: 0.23},
		{A: 0, B: 2, Distance: 0.24},
		{A: 1, B: 3, Distance: 0.10},
		{A: 1, B: 4, Distance: 0.11},
	}
	either := BuildGraphEdges(5, pairs, 0.20, Spec{
		Mode:          "support",
		ExpansionMode: "either",
		TopK:          1,
	})
	adaptive := BuildGraphEdges(5, pairs, 0.20, Spec{
		Mode:              "support",
		ExpansionMode:     "adaptive",
		TopK:              1,
		AdaptiveMargin:    0.1,
		AdaptiveEitherMax: 0.0,
		AdaptiveAmbiguity: 1.0,
	})
	var eitherFound, adaptiveFound bool
	for _, e := range either {
		if (e.A == 0 && e.B == 1) || (e.A == 1 && e.B == 0) {
			eitherFound = true
		}
	}
	for _, e := range adaptive {
		if (e.A == 0 && e.B == 1) || (e.A == 1 && e.B == 0) {
			adaptiveFound = true
		}
	}
	if !eitherFound {
		t.Fatal("either mode should keep the unilateral edge in this setup")
	}
	if adaptiveFound {
		t.Fatal("adaptive mode should drop an ambiguous unilateral top-1 edge")
	}
}

func TestSupportModeAdaptiveFallsBackToMutualOnBroadAmbiguity(t *testing.T) {
	pairs := []Pair{
		{A: 0, B: 1, Distance: 0.23},
		{A: 0, B: 2, Distance: 0.24},
		{A: 1, B: 3, Distance: 0.10},
		{A: 1, B: 4, Distance: 0.11},
		{A: 2, B: 5, Distance: 0.10},
		{A: 2, B: 6, Distance: 0.11},
	}
	adaptive := BuildGraphEdges(7, pairs, 0.20, Spec{
		Mode:              "support",
		ExpansionMode:     "adaptive",
		TopK:              1,
		AdaptiveMargin:    0.1,
		AdaptiveEitherMax: 0.0,
		AdaptiveAmbiguity: 0.4,
	})
	for _, e := range adaptive {
		if (e.A == 0 && e.B == 1) || (e.A == 1 && e.B == 0) {
			t.Fatal("adaptive mode should fall back to mutual when ambiguity is widespread")
		}
	}
}

func TestSupportModeAdaptiveFallsBackToEitherOnLowAmbiguity(t *testing.T) {
	pairs := []Pair{
		{A: 0, B: 1, Distance: 0.23},
		{A: 0, B: 2, Distance: 0.80},
		{A: 1, B: 3, Distance: 0.10},
		{A: 1, B: 4, Distance: 0.11},
	}
	adaptive := BuildGraphEdges(5, pairs, 0.20, Spec{
		Mode:              "support",
		ExpansionMode:     "adaptive",
		TopK:              1,
		AdaptiveMargin:    0.1,
		AdaptiveEitherMax: 0.4,
		AdaptiveAmbiguity: 0.8,
	})
	found := false
	for _, e := range adaptive {
		if (e.A == 0 && e.B == 1) || (e.A == 1 && e.B == 0) {
			found = true
		}
	}
	if !found {
		t.Fatal("adaptive mode should fall back to either expansion on low-ambiguity concepts")
	}
}

func TestSupportModeContrastivePenaltyReducesDominatedEdge(t *testing.T) {
	pairs := []Pair{
		{A: 0, B: 1, Distance: 0.10},
		{A: 0, B: 2, Distance: 0.11},
		{A: 1, B: 3, Distance: 0.10},
		{A: 1, B: 4, Distance: 0.11},
		{A: 0, B: 3, Distance: 0.24},
	}
	base := BuildGraphEdges(5, pairs, 0.25, Spec{
		Mode:          "support",
		ExpansionMode: "either",
		TopK:          2,
	})
	penalized := BuildGraphEdges(5, pairs, 0.25, Spec{
		Mode:               "support",
		ExpansionMode:      "either",
		TopK:               2,
		ContrastivePenalty: 0.5,
		ContrastiveFloor:   0.05,
	})
	var baseBridge, penalizedBridge float64
	for _, e := range base {
		if (e.A == 0 && e.B == 3) || (e.A == 3 && e.B == 0) {
			baseBridge = e.Weight
		}
	}
	for _, e := range penalized {
		if (e.A == 0 && e.B == 3) || (e.A == 3 && e.B == 0) {
			penalizedBridge = e.Weight
		}
	}
	if penalizedBridge >= baseBridge {
		t.Fatalf("expected contrastive penalty to reduce dominated edge: base=%f penalized=%f", baseBridge, penalizedBridge)
	}
}

func TestSupportModeContrastivePenaltyPreservesMutualTop1Edge(t *testing.T) {
	pairs := []Pair{
		{A: 0, B: 1, Distance: 0.10},
		{A: 0, B: 2, Distance: 0.30},
		{A: 1, B: 2, Distance: 0.31},
	}
	base := BuildGraphEdges(3, pairs, 0.35, Spec{
		Mode:          "support",
		ExpansionMode: "either",
		TopK:          1,
	})
	penalized := BuildGraphEdges(3, pairs, 0.35, Spec{
		Mode:               "support",
		ExpansionMode:      "either",
		TopK:               1,
		ContrastivePenalty: 0.8,
		ContrastiveFloor:   0.05,
	})
	var baseMain, penalizedMain float64
	for _, e := range base {
		if (e.A == 0 && e.B == 1) || (e.A == 1 && e.B == 0) {
			baseMain = e.Weight
		}
	}
	for _, e := range penalized {
		if (e.A == 0 && e.B == 1) || (e.A == 1 && e.B == 0) {
			penalizedMain = e.Weight
		}
	}
	if penalizedMain != baseMain {
		t.Fatalf("mutual top-1 edge should be unchanged: base=%f penalized=%f", baseMain, penalizedMain)
	}
}
