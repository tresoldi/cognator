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

	// DefaultThresholdShrink controls how much a concept-local threshold
	// is pulled toward the family-level prior in "concept-shrink" mode.
	// 0 = pure concept-local Otsu; 1 = pure family prior.
	DefaultThresholdShrink = 0.5

	// DefaultGraphTopK expands support-mode candidate graphs with the
	// k nearest neighbors of each node even when they sit above the
	// family threshold.
	DefaultGraphTopK = 2

	// DefaultGraphAdaptiveMargin is the minimum support gap between a
	// node's best and second-best top-k neighbors required for that
	// node to sponsor a unilateral expansion edge in adaptive mode.
	DefaultGraphAdaptiveMargin = 0.1

	// DefaultGraphAdaptiveEitherMax is the ambiguity-share ceiling
	// below which adaptive mode simply reuses the legacy either-endpoint
	// expansion policy for the whole concept graph.
	DefaultGraphAdaptiveEitherMax = 0.15

	// DefaultGraphAdaptiveAmbiguity is the share of active nodes that
	// must look ambiguous before adaptive mode falls back to mutual
	// expansion for the whole concept graph.
	DefaultGraphAdaptiveAmbiguity = 0.5

	// DefaultGraphContrastivePenalty scales the optional edge penalty
	// applied when an edge is locally dominated by stronger rivals.
	// Zero keeps legacy behavior.
	DefaultGraphContrastivePenalty = 0.0

	// DefaultGraphContrastiveFloor ignores small best-rival gaps before
	// the contrastive penalty starts to activate.
	DefaultGraphContrastiveFloor = 0.05

	// DefaultGraphRiskRankWeight and DefaultGraphRiskGapWeight define
	// the calibrated edge-risk blend used by the contrastive penalty.
	// Rank and rival-gap are the only two risk features that earned
	// promotion; triangle/unilateral/density/dispersion variants
	// regressed on Dev-Full and were removed.
	DefaultGraphRiskRankWeight = 0.5
	DefaultGraphRiskGapWeight  = 0.5

	// DefaultGraphMutualBoost raises the weight of mutual top-k edges
	// toward 1.0 in support mode.
	DefaultGraphMutualBoost = 0.2

	// DefaultGraphTriangleBoost raises the weight of edges that are
	// supported by shared neighbors in the candidate graph.
	DefaultGraphTriangleBoost = 0.3

	// DefaultGraphUnilateralPenalty reduces the weight of non-mutual
	// top-k expansion edges when both endpoints already have stronger
	// local alternatives. Default 0 keeps legacy support behavior.
	DefaultGraphUnilateralPenalty = 0.0

	// DefaultGraphRivalFloor is the support level above which the
	// unilateral-edge penalty starts to activate.
	DefaultGraphRivalFloor = 0.6

	// DefaultRefineConfidenceThreshold gates bridge-cut to only the
	// least confident cluster members.
	DefaultRefineConfidenceThreshold = 0.55

	// DefaultRefineBridgeRatio removes weak in-cluster edges incident
	// to low-confidence nodes when the edge falls below this fraction
	// of the node's mean in-cluster support.
	DefaultRefineBridgeRatio = 0.7
)

// Default string values (const-incompatible via Go's zero-value rules,
// kept as plain variables).
var (
	DefaultClusterScope       = "concept"
	DefaultAlignmentMode      = "combined"
	DefaultNullMode           = "marginal"
	DefaultThresholdMode      = "family"
	DefaultGraphMode          = "threshold"
	DefaultGraphExpansionMode = "either"
	DefaultRefineMode         = "none"
)
