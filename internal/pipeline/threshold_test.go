package pipeline

import "testing"

func TestResolveConceptThresholdUsesFamilyModeByDefault(t *testing.T) {
	spec := Spec{
		ThresholdMode:  "family",
		ThresholdMin:   0.2,
		ThresholdMax:   0.8,
		ThresholdShrink: 0.5,
	}
	got := resolveConceptThreshold([]float64{0.05, 0.10, 0.85, 0.90}, 0.67, spec)
	if got != 0.67 {
		t.Fatalf("got %f, want family threshold 0.67", got)
	}
}

func TestResolveConceptThresholdShrinksTowardFamilyPrior(t *testing.T) {
	spec := Spec{
		ThresholdMode:  "concept-shrink",
		ThresholdMin:   0.2,
		ThresholdMax:   0.8,
		ThresholdShrink: 0.5,
	}
	// This local distribution pushes Otsu low; shrinkage should move it
	// upward toward the family prior.
	localOnly := resolveConceptThreshold([]float64{0.05, 0.10, 0.15, 0.20}, 0.80, Spec{
		ThresholdMode:  "concept-shrink",
		ThresholdMin:   0.2,
		ThresholdMax:   0.8,
		ThresholdShrink: 0.0,
	})
	got := resolveConceptThreshold([]float64{0.05, 0.10, 0.15, 0.20}, 0.80, spec)
	if got <= localOnly {
		t.Fatalf("got %f, want > local-only %f due to shrinkage", got, localOnly)
	}
	if got >= 0.80 {
		t.Fatalf("got %f, want < family prior 0.80 with partial shrinkage", got)
	}
}

func TestResolveConceptThresholdHonorsManualOverride(t *testing.T) {
	spec := Spec{
		Threshold:      0.61,
		ThresholdMode:  "concept-shrink",
		ThresholdMin:   0.2,
		ThresholdMax:   0.8,
		ThresholdShrink: 0.5,
	}
	got := resolveConceptThreshold([]float64{0.05, 0.95}, 0.40, spec)
	if got != 0.61 {
		t.Fatalf("got %f, want manual threshold 0.61", got)
	}
}
