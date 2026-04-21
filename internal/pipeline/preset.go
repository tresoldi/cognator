package pipeline

import "sort"

// Preset returns the CLI-flag overrides for a named configuration, or
// nil if the name is unknown. Commands apply these overrides to flags
// the user did not explicitly set, so a preset is a sensible
// default-bundle rather than a lock-in.
func Preset(name string) map[string]string {
	switch name {
	case "support-adaptive-bridge":
		// Promoted research baseline: support-aware graph, adaptive
		// top-k expansion, contrastive penalty on dominated edges,
		// bridge-cut refinement. Wins Dev-Full on mean F against
		// LingPy; Dev-Core and Holdout are close but not consistently
		// above LingPy yet. Other values (mutual-boost 0.2,
		// triangle-boost 0.3, risk-rank 0.5, risk-gap 0.5,
		// confidence-threshold 0.55, bridge-ratio 0.7, etc.) are
		// already defaults; this preset only carries the overrides.
		return map[string]string{
			"threshold":                 "0.68",
			"graph-mode":                "support",
			"graph-expansion-mode":      "adaptive",
			"graph-top-k":               "3",
			"graph-contrastive-penalty": "0.20",
			"refine-mode":               "bridge-cut",
		}
	}
	return nil
}

// PresetNames returns the defined preset names, sorted.
func PresetNames() []string {
	names := []string{"support-adaptive-bridge"}
	sort.Strings(names)
	return names
}
