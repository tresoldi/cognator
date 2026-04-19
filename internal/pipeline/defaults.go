package pipeline

// Pipeline defaults. Each value is an empirical choice tuned on
// CoreCog; none is derived from the input corpus at runtime. The
// Spec fields accept overrides; these are the fallbacks.
const (
	// DefaultIterations is EM passes per lect-pair. 1 is too weak (learned
	// signal barely diverges from prior); 5+ adds little. 3 is the plateau.
	DefaultIterations = 3

	// DefaultLambda blends prior similarity and learned log-odds in
	// LearnedScorer. 0.5 = equal weight; per-family learning was rejected
	// to avoid supervised tuning.
	DefaultLambda = 0.5

	// DefaultThresholdMin/Max clamp Otsu's bimodal split. Raw Otsu
	// occasionally lands at ~0 or ~1 on pathological distance histograms;
	// [0.2, 0.8] keeps the threshold in a regime that produces non-
	// degenerate clusters.
	DefaultThresholdMin = 0.2
	DefaultThresholdMax = 0.8

	// DefaultBootstrapSigma is the stddev of Gaussian noise added to edge
	// distances (∈ [0,1]) during confidence bootstrap. 5% is large enough
	// to exercise boundary-adjacent edges without destroying strong signal.
	DefaultBootstrapSigma = 0.05

	// DefaultMinPairs skips training a correspondence table when a lect
	// pair has fewer than N concept-sharing form-pairs; such pairs fall
	// back to the prior scorer.
	DefaultMinPairs = 5

	// DefaultNullMaxRuns caps the shuffle null's permutation budget per
	// lect-pair. Adaptive stopping usually halts far earlier.
	DefaultNullMaxRuns = 200
)

// Default string values (const-incompatible via Go's zero-value rules,
// kept as plain variables).
var (
	DefaultClusterScope  = "concept"
	DefaultAlignmentMode = "combined"
	DefaultNullMode      = "marginal"
)
