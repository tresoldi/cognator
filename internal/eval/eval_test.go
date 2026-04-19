package eval

import (
	"math"
	"testing"
)

func TestBCubedIdentity(t *testing.T) {
	truth := map[string]string{"a": "X", "b": "X", "c": "Y", "d": "Y"}
	pred := map[string]string{"a": "X", "b": "X", "c": "Y", "d": "Y"}
	p, r, f := BCubed(pred, truth)
	if p != 1.0 || r != 1.0 || f != 1.0 {
		t.Errorf("identity partition should score 1.0: P=%f R=%f F=%f", p, r, f)
	}
}

func TestBCubedAllOneCluster(t *testing.T) {
	truth := map[string]string{"a": "X", "b": "X", "c": "Y", "d": "Y"}
	pred := map[string]string{"a": "M", "b": "M", "c": "M", "d": "M"}
	p, r, f := BCubed(pred, truth)
	// Recall = 1 (every true pair is in the same predicted cluster).
	// Precision = 0.5 (half of each predicted cluster's members share
	// the true class).
	if math.Abs(r-1.0) > 1e-9 {
		t.Errorf("Recall = %f, want 1.0", r)
	}
	if math.Abs(p-0.5) > 1e-9 {
		t.Errorf("Precision = %f, want 0.5", p)
	}
	if f <= 0 || f >= 1 {
		t.Errorf("F out of expected (0,1) range: %f", f)
	}
}

func TestBCubedAllSingletons(t *testing.T) {
	truth := map[string]string{"a": "X", "b": "X", "c": "Y", "d": "Y"}
	pred := map[string]string{"a": "1", "b": "2", "c": "3", "d": "4"}
	p, r, f := BCubed(pred, truth)
	// Precision = 1 (each singleton has 1/1 shared with itself).
	// Recall = 0.5 (each item shares truth with one other it doesn't cluster with).
	if math.Abs(p-1.0) > 1e-9 {
		t.Errorf("Precision = %f, want 1.0", p)
	}
	if math.Abs(r-0.5) > 1e-9 {
		t.Errorf("Recall = %f, want 0.5", r)
	}
	_ = f
}

func TestBCubedMissingKeysDontCrash(t *testing.T) {
	truth := map[string]string{"a": "X", "b": "X"}
	pred := map[string]string{"a": "M"}
	_, _, _ = BCubed(pred, truth)
}

func TestBCubedEmptyInput(t *testing.T) {
	p, r, f := BCubed(nil, nil)
	if p != 0 || r != 0 || f != 0 {
		t.Errorf("empty inputs should yield zeros: %f %f %f", p, r, f)
	}
}
