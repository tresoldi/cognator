// cognatorwasm is the browser entry point. It compiles to a single
// WebAssembly module that embeds the descriptive merkmal bundle at
// compile time (via //go:embed) and exposes one JS-callable entry
// point: globalThis.cognatorDetect(csvText, optionsJSON).
//
// Build:
//
//	GOOS=js GOARCH=wasm go build -o web/cognator.wasm ./cmd/cognatorwasm
//
// The Go runtime needs to stay alive after init so the browser can
// call cognatorDetect repeatedly without reinitialising the engine;
// main() therefore blocks on a channel that is never closed.
//
//go:build js && wasm

package main

import (
	"bytes"
	"embed"
	"encoding/csv"
	"encoding/json"
	"fmt"
	"io/fs"
	"strings"
	"syscall/js"
	"time"

	"github.com/tresoldi/cognator/internal/confidence"
	"github.com/tresoldi/cognator/internal/eval"
	"github.com/tresoldi/cognator/internal/phon"
	"github.com/tresoldi/cognator/internal/pipeline"
	"github.com/tresoldi/cognator/internal/wordlist"
)

//go:embed embedded/models/descriptive
var modelFS embed.FS

//go:embed embedded/geometries
var geomFS embed.FS

func loadEmbeddedPhon() (*phon.Phon, error) {
	mFS, err := fs.Sub(modelFS, "embedded/models/descriptive")
	if err != nil {
		return nil, fmt.Errorf("sub model fs: %w", err)
	}
	gFS, err := fs.Sub(geomFS, "embedded/geometries")
	if err != nil {
		return nil, fmt.Errorf("sub geom fs: %w", err)
	}
	return phon.Load(mFS, gFS, "clements-hume")
}

// detectOptions mirrors the Spec fields that the browser UI exposes.
// Unset fields fall back to pipeline.ResolveDefaults behaviour, so
// the JS side can omit anything it does not care about. Unknown
// fields in the incoming JSON are ignored.
type detectOptions struct {
	Preset string `json:"preset"`

	// Scoring
	ScorerMode             string  `json:"scorer_mode"`
	Iterations             int     `json:"iterations"`
	Lambda                 float64 `json:"lambda"`
	DirichletConcentration float64 `json:"dirichlet_concentration"`
	DirichletSharpness     float64 `json:"dirichlet_sharpness"`
	DirichletScaleMode     string  `json:"dirichlet_scale"`
	ClassReduce            *bool   `json:"class_reduce"`
	AlignmentMode          string  `json:"alignment_mode"`
	NullMode               string  `json:"null_mode"`
	NullMaxRuns            int     `json:"null_max_runs"`
	MinPairs               int     `json:"min_pairs"`

	// Threshold
	Threshold       float64 `json:"threshold"`
	ThresholdMode   string  `json:"threshold_mode"`
	ThresholdMin    float64 `json:"threshold_min"`
	ThresholdMax    float64 `json:"threshold_max"`
	ThresholdShrink float64 `json:"threshold_shrink"`

	// Graph
	GraphMode               string  `json:"graph_mode"`
	GraphExpansionMode      string  `json:"graph_expansion_mode"`
	GraphTopK               int     `json:"graph_top_k"`
	GraphAdaptiveMargin     float64 `json:"graph_adaptive_margin"`
	GraphAdaptiveEitherMax  float64 `json:"graph_adaptive_either_max"`
	GraphAdaptiveAmbiguity  float64 `json:"graph_adaptive_ambiguity"`
	GraphMutualBoost        float64 `json:"graph_mutual_boost"`
	GraphTriangleBoost      float64 `json:"graph_triangle_boost"`
	GraphContrastivePenalty float64 `json:"graph_contrastive_penalty"`
	GraphContrastiveFloor   float64 `json:"graph_contrastive_floor"`
	GraphRiskRankWeight     float64 `json:"graph_risk_rank_weight"`
	GraphRiskGapWeight      float64 `json:"graph_risk_gap_weight"`
	GraphUnilateralPenalty  float64 `json:"graph_unilateral_penalty"`
	GraphRivalFloor         float64 `json:"graph_rival_floor"`

	// Refinement
	RefineMode                string  `json:"refine_mode"`
	RefineConfidenceThreshold float64 `json:"refine_confidence_threshold"`
	RefineBridgeRatio         float64 `json:"refine_bridge_ratio"`

	// Bootstrap / clustering scope
	Bootstrap      int     `json:"bootstrap"`
	BootstrapSigma float64 `json:"bootstrap_sigma"`
	Seed           uint64  `json:"seed"`
	ClusterScope   string  `json:"cluster_scope"`
}

// specFromOptions merges preset defaults and explicit overrides into
// a pipeline.Spec. Presets are applied first; any option explicitly
// present in the JSON overrides the preset value, mirroring the CLI
// semantics (flag.Visit checks).
func specFromOptions(opts detectOptions) pipeline.Spec {
	spec := pipeline.Spec{}
	if opts.Preset != "" {
		for name, val := range pipeline.Preset(opts.Preset) {
			applyPresetField(&spec, name, val)
		}
	}
	applyExplicitOptions(&spec, opts)
	return spec
}

// applyPresetField decodes a single (flag-name, value) override into
// the corresponding Spec field. Only the flags that are actually
// defined in Preset() are handled; everything else is a no-op.
func applyPresetField(spec *pipeline.Spec, name, val string) {
	switch name {
	case "threshold":
		spec.Threshold = parseFloat(val)
	case "graph-mode":
		spec.GraphMode = val
	case "graph-expansion-mode":
		spec.GraphExpansionMode = val
	case "graph-top-k":
		spec.GraphTopK = parseInt(val)
	case "graph-contrastive-penalty":
		spec.GraphContrastivePenalty = parseFloat(val)
	case "refine-mode":
		spec.RefineMode = val
	}
}

// applyExplicitOptions overlays the JSON-supplied values on top of
// the preset baseline. Zero-valued fields (numeric 0, empty string,
// nil pointer) are treated as "unset" and leave the preset untouched.
func applyExplicitOptions(spec *pipeline.Spec, opts detectOptions) {
	if opts.ScorerMode != "" {
		spec.ScorerMode = opts.ScorerMode
	}
	if opts.Iterations > 0 {
		spec.Iterations = opts.Iterations
	}
	if opts.Lambda > 0 {
		spec.Lambda = opts.Lambda
	}
	if opts.DirichletConcentration > 0 {
		spec.DirichletConcentration = opts.DirichletConcentration
	}
	if opts.DirichletSharpness > 0 {
		spec.DirichletSharpness = opts.DirichletSharpness
	}
	if opts.DirichletScaleMode != "" {
		spec.DirichletScaleMode = opts.DirichletScaleMode
	}
	if opts.ClassReduce != nil {
		spec.ClassReduce = *opts.ClassReduce
	} else {
		spec.ClassReduce = true // CLI default
	}
	if opts.AlignmentMode != "" {
		spec.AlignmentMode = opts.AlignmentMode
	}
	if opts.NullMode != "" {
		spec.NullMode = opts.NullMode
	}
	if opts.NullMaxRuns > 0 {
		spec.NullMaxRuns = opts.NullMaxRuns
	}
	if opts.MinPairs > 0 {
		spec.MinPairs = opts.MinPairs
	}
	if opts.Threshold > 0 {
		spec.Threshold = opts.Threshold
	}
	if opts.ThresholdMode != "" {
		spec.ThresholdMode = opts.ThresholdMode
	}
	if opts.ThresholdMin > 0 {
		spec.ThresholdMin = opts.ThresholdMin
	}
	if opts.ThresholdMax > 0 {
		spec.ThresholdMax = opts.ThresholdMax
	}
	if opts.ThresholdShrink > 0 {
		spec.ThresholdShrink = opts.ThresholdShrink
	}
	if opts.GraphMode != "" {
		spec.GraphMode = opts.GraphMode
	}
	if opts.GraphExpansionMode != "" {
		spec.GraphExpansionMode = opts.GraphExpansionMode
	}
	if opts.GraphTopK > 0 {
		spec.GraphTopK = opts.GraphTopK
	}
	if opts.GraphAdaptiveMargin > 0 {
		spec.GraphAdaptiveMargin = opts.GraphAdaptiveMargin
	}
	if opts.GraphAdaptiveEitherMax > 0 {
		spec.GraphAdaptiveEitherMax = opts.GraphAdaptiveEitherMax
	}
	if opts.GraphAdaptiveAmbiguity > 0 {
		spec.GraphAdaptiveAmbiguity = opts.GraphAdaptiveAmbiguity
	}
	if opts.GraphMutualBoost > 0 {
		spec.GraphMutualBoost = opts.GraphMutualBoost
	}
	if opts.GraphTriangleBoost > 0 {
		spec.GraphTriangleBoost = opts.GraphTriangleBoost
	}
	if opts.GraphContrastivePenalty > 0 {
		spec.GraphContrastivePenalty = opts.GraphContrastivePenalty
	}
	if opts.GraphContrastiveFloor > 0 {
		spec.GraphContrastiveFloor = opts.GraphContrastiveFloor
	}
	if opts.GraphRiskRankWeight > 0 {
		spec.GraphRiskRankWeight = opts.GraphRiskRankWeight
	}
	if opts.GraphRiskGapWeight > 0 {
		spec.GraphRiskGapWeight = opts.GraphRiskGapWeight
	}
	if opts.GraphUnilateralPenalty > 0 {
		spec.GraphUnilateralPenalty = opts.GraphUnilateralPenalty
	}
	if opts.GraphRivalFloor > 0 {
		spec.GraphRivalFloor = opts.GraphRivalFloor
	}
	if opts.RefineMode != "" {
		spec.RefineMode = opts.RefineMode
	}
	if opts.RefineConfidenceThreshold > 0 {
		spec.RefineConfidenceThreshold = opts.RefineConfidenceThreshold
	}
	if opts.RefineBridgeRatio > 0 {
		spec.RefineBridgeRatio = opts.RefineBridgeRatio
	}
	if opts.Bootstrap > 0 {
		spec.Bootstrap = opts.Bootstrap
	}
	if opts.BootstrapSigma > 0 {
		spec.BootstrapSigma = opts.BootstrapSigma
	}
	if opts.Seed > 0 {
		spec.Seed = opts.Seed
	} else {
		spec.Seed = 1
	}
	if opts.ClusterScope != "" {
		spec.ClusterScope = opts.ClusterScope
	}
}

func parseFloat(s string) float64 {
	var f float64
	fmt.Sscanf(s, "%f", &f)
	return f
}

func parseInt(s string) int {
	var n int
	fmt.Sscanf(s, "%d", &n)
	return n
}

// parseRecords reads a CSV or TSV wordlist from a string. Comma vs
// tab is inferred from the first header line; both arcaverborum and
// cognator canonical columns are supported via wordlist.ReadTable's
// column aliases.
func parseRecords(input string) ([]wordlist.Record, error) {
	trimmed := strings.TrimSpace(input)
	if trimmed == "" {
		return nil, fmt.Errorf("empty input")
	}
	delim := ','
	if firstLineHas(trimmed, "\t") {
		delim = '\t'
	}
	r := csv.NewReader(bytes.NewBufferString(trimmed))
	r.Comma = delim
	r.FieldsPerRecord = -1
	r.LazyQuotes = true
	rows, err := r.ReadAll()
	if err != nil {
		return nil, fmt.Errorf("csv parse: %w", err)
	}
	if len(rows) < 2 {
		return nil, fmt.Errorf("need a header plus at least one data row")
	}
	return recordsFromRows(rows)
}

func firstLineHas(input, needle string) bool {
	nl := strings.IndexByte(input, '\n')
	head := input
	if nl > 0 {
		head = input[:nl]
	}
	return strings.Contains(head, needle)
}

func recordsFromRows(rows [][]string) ([]wordlist.Record, error) {
	header := rows[0]
	pos := map[string]int{}
	for i, name := range header {
		pos[strings.TrimSpace(name)] = i
	}
	pick := func(names ...string) int {
		for _, n := range names {
			if i, ok := pos[n]; ok {
				return i
			}
		}
		return -1
	}
	id := pick("ID")
	lang := pick("Language_ID")
	concept := pick("Concept", "Concepticon_Gloss", "Parameter_ID")
	segs := pick("Segments")
	family := pick("Family", "Dataset")
	cognacy := pick("Cognacy")
	if id < 0 || lang < 0 || concept < 0 || segs < 0 {
		return nil, fmt.Errorf("missing required column (need ID, Language_ID, Concept|Concepticon_Gloss|Parameter_ID, Segments)")
	}
	get := func(row []string, i int) string {
		if i < 0 || i >= len(row) {
			return ""
		}
		return strings.TrimSpace(row[i])
	}
	out := make([]wordlist.Record, 0, len(rows)-1)
	for _, row := range rows[1:] {
		rec := wordlist.Record{
			ID:         get(row, id),
			LanguageID: get(row, lang),
			Concept:    get(row, concept),
			Segments:   get(row, segs),
			Family:     get(row, family),
			Cognacy:    get(row, cognacy),
		}
		if rec.ID == "" || rec.LanguageID == "" || rec.Segments == "" {
			continue
		}
		if rec.Family == "" {
			rec.Family = "demo"
		}
		if rec.Cognacy != "" {
			if idx := strings.IndexByte(rec.Cognacy, ';'); idx >= 0 {
				rec.Cognacy = strings.TrimSpace(rec.Cognacy[:idx])
			}
		}
		out = append(out, rec)
	}
	if len(out) == 0 {
		return nil, fmt.Errorf("no data rows had required columns populated")
	}
	return out, nil
}

// detect wraps the full pipeline in a JSON-in / JSON-out contract
// suitable for syscall/js. Errors surface via {ok:false, error:"..."};
// success returns {ok:true, predictions:[...], summary:{...}, eval?:{...}}.
func detect(bundle *phon.Phon, csvText string, opts detectOptions) map[string]any {
	start := time.Now()
	recs, err := parseRecords(csvText)
	if err != nil {
		return map[string]any{"ok": false, "error": err.Error()}
	}
	spec := specFromOptions(opts)
	res := pipeline.Detect(recs, bundle, spec)

	// Predictions. We re-derive cluster_size and candidate_neighbors
	// from the sidecar rows even though the sidecar carries them —
	// the UI wants concept and gold alongside, so we zip with the
	// original records here.
	byID := map[string]wordlist.Record{}
	for _, r := range recs {
		byID[r.ID] = r
	}
	predictions := make([]map[string]any, 0, len(res.Rows))
	for _, row := range res.Rows {
		rec := byID[row.ID]
		gold := ""
		if rec.Cognacy != "" {
			gold = rec.Concept + ":" + rec.Cognacy
		}
		predictions = append(predictions, map[string]any{
			"eval_id":             row.ID,
			"dataset":             rec.Family,
			"language_id":         rec.LanguageID,
			"concept":             rec.Concept,
			"segments":            rec.Segments,
			"gold":                gold,
			"pred":                row.CognatorID,
			"confidence":          row.Confidence,
			"assigned_support":    row.AssignedSupport,
			"alternative_support": row.AlternativeSupport,
			"support_margin":      row.SupportMargin,
			"cluster_size":        row.ClusterSize,
			"candidate_neighbors": row.CandidateNeighbors,
			"bootstrap":           row.Bootstrap,
			"bootstrap_low":       row.BootstrapLow,
			"bootstrap_high":      row.BootstrapHigh,
		})
	}

	concepts := map[string]struct{}{}
	lects := map[string]struct{}{}
	clusters := map[string]struct{}{}
	for _, r := range recs {
		concepts[r.Concept] = struct{}{}
		lects[r.LanguageID] = struct{}{}
	}
	for _, p := range res.Rows {
		clusters[p.CognatorID] = struct{}{}
	}
	summary := map[string]any{
		"forms":      len(recs),
		"concepts":   len(concepts),
		"lects":      len(lects),
		"clusters":   len(clusters),
		"threshold":  res.Threshold,
		"oov_types":  len(res.OOV),
		"elapsed_ms": time.Since(start).Milliseconds(),
	}

	result := map[string]any{
		"ok":          true,
		"predictions": predictions,
		"summary":     summary,
	}

	// Evaluation only when gold is available.
	gold := map[string]string{}
	for _, r := range recs {
		if r.Cognacy != "" {
			gold[r.ID] = r.Concept + ":" + r.Cognacy
		}
	}
	if len(gold) > 0 {
		pred := map[string]string{}
		for _, row := range res.Rows {
			if _, ok := gold[row.ID]; ok {
				pred[row.ID] = row.CognatorID
			}
		}
		p, rcl, f := eval.BCubed(pred, gold)
		result["eval"] = map[string]any{
			"scored_forms": len(gold),
			"precision":    p,
			"recall":       rcl,
			"f1":           f,
		}
	}
	_ = confidence.Diagnostic{} // keep import linked; compiler cannot see transitive use
	return result
}

// jsEntry is the thin syscall/js wrapper around detect(). It accepts
// (csvText, optionsObject) and returns the JSON-marshaled result as
// a string — the UI does JSON.parse on the other side, which is
// cheaper than plumbing every field through js.ValueOf piecewise.
func jsEntry(bundle *phon.Phon) js.Func {
	return js.FuncOf(func(this js.Value, args []js.Value) any {
		if len(args) < 1 {
			return failJSON("cognatorDetect: expected (csvText, options)")
		}
		csvText := args[0].String()
		var opts detectOptions
		if len(args) >= 2 && args[1].Type() == js.TypeObject {
			buf, err := json.Marshal(jsToGo(args[1]))
			if err == nil {
				_ = json.Unmarshal(buf, &opts)
			}
		}
		res := detect(bundle, csvText, opts)
		buf, err := json.Marshal(res)
		if err != nil {
			return failJSON("marshal: " + err.Error())
		}
		return string(buf)
	})
}

// jsToGo converts a js.Value options object to a generic Go map so
// encoding/json can unmarshal it into detectOptions. Only primitives
// are crossed; functions, symbols, and nested objects flatten as
// best-effort.
func jsToGo(v js.Value) any {
	switch v.Type() {
	case js.TypeUndefined, js.TypeNull:
		return nil
	case js.TypeBoolean:
		return v.Bool()
	case js.TypeNumber:
		return v.Float()
	case js.TypeString:
		return v.String()
	case js.TypeObject:
		// Arrays
		if v.InstanceOf(js.Global().Get("Array")) {
			length := v.Length()
			out := make([]any, length)
			for i := 0; i < length; i++ {
				out[i] = jsToGo(v.Index(i))
			}
			return out
		}
		// Plain object
		out := map[string]any{}
		keys := js.Global().Get("Object").Call("keys", v)
		for i := 0; i < keys.Length(); i++ {
			k := keys.Index(i).String()
			out[k] = jsToGo(v.Get(k))
		}
		return out
	default:
		return nil
	}
}

func failJSON(msg string) string {
	buf, _ := json.Marshal(map[string]any{"ok": false, "error": msg})
	return string(buf)
}

func main() {
	b, err := loadEmbeddedPhon()
	if err != nil {
		js.Global().Set("cognatorInitError", err.Error())
		return
	}
	js.Global().Set("cognatorDetect", jsEntry(b))
	js.Global().Set("cognatorReady", true)
	js.Global().Set("cognatorSystem", b.Name)
	js.Global().Set("cognatorGraphemeCount", b.GraphemeCount)

	// Block forever so the Go runtime stays alive for repeat calls
	// into cognatorDetect. Closing the channel would let main return
	// and the WASM would tear down.
	<-make(chan struct{})
}
