package pipeline

import "testing"

func TestPresetSupportAdaptiveBridgeOverridesPromotedValues(t *testing.T) {
	got := Preset("support-adaptive-bridge")
	if got == nil {
		t.Fatal("expected preset to be defined")
	}
	want := map[string]string{
		"threshold":                 "0.68",
		"graph-mode":                "support",
		"graph-expansion-mode":      "adaptive",
		"graph-top-k":               "3",
		"graph-contrastive-penalty": "0.20",
		"refine-mode":               "bridge-cut",
	}
	if len(got) != len(want) {
		t.Fatalf("override count = %d, want %d", len(got), len(want))
	}
	for k, v := range want {
		if got[k] != v {
			t.Errorf("override %q = %q, want %q", k, got[k], v)
		}
	}
}

func TestPresetUnknownNameReturnsNil(t *testing.T) {
	if Preset("not-a-preset") != nil {
		t.Fatal("expected unknown preset to return nil")
	}
}

func TestPresetNamesListsSupportAdaptiveBridge(t *testing.T) {
	names := PresetNames()
	found := false
	for _, n := range names {
		if n == "support-adaptive-bridge" {
			found = true
		}
	}
	if !found {
		t.Fatalf("PresetNames() = %v, missing support-adaptive-bridge", names)
	}
}
