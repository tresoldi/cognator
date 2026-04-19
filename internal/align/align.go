// Package align implements direction-aware Needleman-Wunsch
// alignment over grapheme sequences. Scores are produced by a
// Scorer (see PriorScorer for the merkmal-distance-based default);
// gap costs are per-direction so that source insertions and target
// insertions can be priced differently when a learned asymmetric
// scorer is introduced in M3.
package align

import "github.com/tresoldi/cognator/internal/merkmal"

// Scorer supplies similarity scores between source and target
// graphemes and direction-specific gap costs. Score may return any
// real; gap costs are subtracted as positive values (i.e.
// GapInTarget=0.5 subtracts 0.5 from the alignment score for each
// column where source has a grapheme and target has a gap).
type Scorer interface {
	Score(source, target string) float64
	GapInTarget() float64 // penalty for advancing source while target waits
	GapInSource() float64 // penalty for advancing target while source waits
}

// Alignment is the result of NeedlemanWunsch. Source[i] == "" means
// a gap (insertion in target); Target[i] == "" means the opposite.
type Alignment struct {
	Source []string
	Target []string
	Score  float64
}

// NeedlemanWunsch computes the global alignment of source against
// target under scorer s. Runs in O(|source|·|target|) time and
// memory.
func NeedlemanWunsch(source, target []string, s Scorer) Alignment {
	m, n := len(source), len(target)
	dp := make([][]float64, m+1)
	for i := range dp {
		dp[i] = make([]float64, n+1)
	}
	gapT := s.GapInTarget()
	gapS := s.GapInSource()

	for i := 1; i <= m; i++ {
		dp[i][0] = dp[i-1][0] - gapT
	}
	for j := 1; j <= n; j++ {
		dp[0][j] = dp[0][j-1] - gapS
	}

	for i := 1; i <= m; i++ {
		for j := 1; j <= n; j++ {
			match := dp[i-1][j-1] + s.Score(source[i-1], target[j-1])
			del := dp[i-1][j] - gapT
			ins := dp[i][j-1] - gapS
			dp[i][j] = max3(match, del, ins)
		}
	}

	al := Alignment{Score: dp[m][n]}
	i, j := m, n
	for i > 0 || j > 0 {
		switch {
		case i > 0 && j > 0 && dp[i][j] == dp[i-1][j-1]+s.Score(source[i-1], target[j-1]):
			al.Source = append([]string{source[i-1]}, al.Source...)
			al.Target = append([]string{target[j-1]}, al.Target...)
			i--
			j--
		case i > 0 && dp[i][j] == dp[i-1][j]-gapT:
			al.Source = append([]string{source[i-1]}, al.Source...)
			al.Target = append([]string{""}, al.Target...)
			i--
		default:
			al.Source = append([]string{""}, al.Source...)
			al.Target = append([]string{target[j-1]}, al.Target...)
			j--
		}
	}
	return al
}

// Distance is the Downey-normalized pair distance derived from
// NeedlemanWunsch scores. It is clamped to [0, 1].
//
// D(a, b) = 1 - 2·s(a,b) / (s(a,a) + s(b,b))
//
// Self-alignment score s(x, x) is computed analytically as the sum
// of per-segment identity scores (no gaps). This assumes that the
// scorer prefers diagonal over gap for identical sequences, which
// holds as long as gap costs are not negative.
func Distance(a, b []string, s Scorer) float64 {
	if len(a) == 0 && len(b) == 0 {
		return 0
	}
	if len(a) == 0 || len(b) == 0 {
		return 1
	}
	ab := NeedlemanWunsch(a, b, s).Score
	aa := selfScore(a, s)
	bb := selfScore(b, s)
	denom := aa + bb
	if denom <= 0 {
		return 1
	}
	d := 1.0 - (2.0*ab)/denom
	if d < 0 {
		return 0
	}
	if d > 1 {
		return 1
	}
	return d
}

func selfScore(xs []string, s Scorer) float64 {
	sum := 0.0
	for _, x := range xs {
		sum += s.Score(x, x)
	}
	return sum
}

// LocalScore returns the best local (Smith-Waterman) alignment score
// between source and target under s. Start and end positions are
// free; any subsequence-on-subsequence match is considered.
func LocalScore(source, target []string, s Scorer) float64 {
	m, n := len(source), len(target)
	if m == 0 || n == 0 {
		return 0
	}
	dp := make([][]float64, m+1)
	for i := range dp {
		dp[i] = make([]float64, n+1)
	}
	gapT := s.GapInTarget()
	gapS := s.GapInSource()
	best := 0.0
	for i := 1; i <= m; i++ {
		for j := 1; j <= n; j++ {
			match := dp[i-1][j-1] + s.Score(source[i-1], target[j-1])
			del := dp[i-1][j] - gapT
			ins := dp[i][j-1] - gapS
			v := max3(match, del, ins)
			if v < 0 {
				v = 0
			}
			dp[i][j] = v
			if v > best {
				best = v
			}
		}
	}
	return best
}

// OverlapScore returns the best semi-global (overlap) alignment
// score: leading and trailing gaps on either string are free, but
// the interior of the overlap must be fully aligned.
func OverlapScore(source, target []string, s Scorer) float64 {
	m, n := len(source), len(target)
	if m == 0 || n == 0 {
		return 0
	}
	dp := make([][]float64, m+1)
	for i := range dp {
		dp[i] = make([]float64, n+1)
	}
	gapT := s.GapInTarget()
	gapS := s.GapInSource()
	for i := 1; i <= m; i++ {
		for j := 1; j <= n; j++ {
			match := dp[i-1][j-1] + s.Score(source[i-1], target[j-1])
			del := dp[i-1][j] - gapT
			ins := dp[i][j-1] - gapS
			dp[i][j] = max3(match, del, ins)
		}
	}
	best := dp[m][n]
	for i := 0; i <= m; i++ {
		if dp[i][n] > best {
			best = dp[i][n]
		}
	}
	for j := 0; j <= n; j++ {
		if dp[m][j] > best {
			best = dp[m][j]
		}
	}
	return best
}

// DistanceCombined is Downey-normalized distance using the sum of
// three alignment modes (global NW, local SW, semi-global overlap),
// the same triple used by lingpy's LexStat. Captures partial-stem
// matches that global-only misses.
func DistanceCombined(a, b []string, s Scorer) float64 {
	if len(a) == 0 && len(b) == 0 {
		return 0
	}
	if len(a) == 0 || len(b) == 0 {
		return 1
	}
	sg := NeedlemanWunsch(a, b, s).Score
	sl := LocalScore(a, b, s)
	so := OverlapScore(a, b, s)
	sum := sg + sl + so
	aa := selfScore(a, s)
	bb := selfScore(b, s)
	denom := 3.0 * (aa + bb)
	if denom <= 0 {
		return 1
	}
	d := 1.0 - (2.0*sum)/denom
	if d < 0 {
		return 0
	}
	if d > 1 {
		return 1
	}
	return d
}

// DefaultGapCost is the per-column penalty for an unpaired grapheme
// in either direction. Score() returns values in [-1, +1] (identity
// = +1); a gap costing 0.5 is painful enough to discourage
// free-floating gaps yet cheap enough to prefer over the worst
// substitutions.
const DefaultGapCost = 0.5

// RoleMismatchPenalty subtracts from the raw similarity score when
// two graphemes have known but different prosodic roles (e.g., a
// consonant aligned to a vowel). Chosen so that a C↔V column
// discourages pairing even when merkmal's feature distance happens
// to be small, while still permitting it in cases like glide↔vowel
// where roles differ but sound change is attested.
const RoleMismatchPenalty = 0.3

// priorScorer wraps a merkmal.Bundle as a Scorer. Similarity is
// 1 - 2·d, i.e. identity = +1, maximally distant = -1.
type priorScorer struct {
	b    *merkmal.Bundle
	gapT float64
	gapS float64
}

// PriorScorer returns a Scorer backed by the bundle's distance
// table. Gaps default to DefaultGapCost; use WithGaps to override.
// Role (prosody) awareness is NOT applied here — wrap with
// ProsodyAwareScorer for framework-native skeleton sensitivity.
func PriorScorer(b *merkmal.Bundle) Scorer {
	return &priorScorer{b: b, gapT: DefaultGapCost, gapS: DefaultGapCost}
}

// roleAware wraps a Scorer with merkmal prosody-role awareness: when
// two graphemes have known, distinct roles the column is penalized by
// RoleMismatchPenalty. Gaps are unaffected (the gap cost is the
// column-level price of skipping a grapheme, independent of role).
type roleAware struct {
	inner   Scorer
	bundle  *merkmal.Bundle
	penalty float64
}

// ProsodyAwareScorer returns a role-aware wrapper over PriorScorer(b).
// Cheap wrapper that costs one bundle.Role lookup per Score() call.
func ProsodyAwareScorer(b *merkmal.Bundle) Scorer {
	return &roleAware{
		inner:   PriorScorer(b),
		bundle:  b,
		penalty: RoleMismatchPenalty,
	}
}

// WrapWithProsody wraps any Scorer with role-awareness using the
// given bundle. Used when a LearnedScorer (or other composed scorer)
// needs the same penalty applied on top of its internal blending.
func WrapWithProsody(s Scorer, b *merkmal.Bundle) Scorer {
	return &roleAware{inner: s, bundle: b, penalty: RoleMismatchPenalty}
}

func (r *roleAware) Score(a, b string) float64 {
	s := r.inner.Score(a, b)
	ra := r.bundle.Role(a)
	rb := r.bundle.Role(b)
	if ra != merkmal.RoleUnknown && rb != merkmal.RoleUnknown && ra != rb {
		s -= r.penalty
	}
	return s
}

func (r *roleAware) GapInTarget() float64 { return r.inner.GapInTarget() }
func (r *roleAware) GapInSource() float64 { return r.inner.GapInSource() }

// WithGaps returns a copy of the scorer with the given gap costs.
// A Scorer must be of concrete *priorScorer type; otherwise the
// call is a no-op that returns s unchanged.
func WithGaps(s Scorer, gapInTarget, gapInSource float64) Scorer {
	ps, ok := s.(*priorScorer)
	if !ok {
		return s
	}
	c := *ps
	c.gapT = gapInTarget
	c.gapS = gapInSource
	return &c
}

func (p *priorScorer) Score(a, b string) float64 {
	return 1.0 - 2.0*p.b.Distance(a, b)
}

func (p *priorScorer) GapInTarget() float64 { return p.gapT }
func (p *priorScorer) GapInSource() float64 { return p.gapS }

func max3(a, b, c float64) float64 {
	m := a
	if b > m {
		m = b
	}
	if c > m {
		m = c
	}
	return m
}

