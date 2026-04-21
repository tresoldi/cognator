// Package wordlist reads arcaverborum-shaped tabular input and
// writes cognator's sidecar output. Supports both cognator's
// canonical column names and arcaverborum's CLDF-derived aliases
// (Concepticon_Gloss → Concept, Dataset → Family). CSV and TSV are
// auto-detected by file extension.
package wordlist

import (
	"encoding/csv"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

// Record is one input form. The optional fields may be empty strings
// when the source file does not provide them.
type Record struct {
	ID         string
	LanguageID string
	Concept    string
	Segments   string
	Family     string
	Cognacy    string
}

// SidecarRow is one output form — clustering result with confidence
// diagnostics. Confidence is the support-ratio confidence; ClusterSize
// and CandidateNeighbors are structural signals that disambiguate the
// support-ratio metric's conf=1.0 collapse between stable singletons
// and unsupported singletons with competitors (see docs/roadmap.md
// Model §2/§4).
type SidecarRow struct {
	ID                 string
	CognatorID         string
	Confidence         float64
	AssignedSupport    float64
	AlternativeSupport float64
	SupportMargin      float64
	ClusterSize        int
	CandidateNeighbors int
	Bootstrap          float64
	BootstrapLow       float64
	BootstrapHigh      float64
}

// ReadTable loads a wordlist from path. The delimiter is chosen from
// the extension (.csv → comma, .tsv/.txt → tab).
func ReadTable(path string) ([]Record, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	r := csv.NewReader(f)
	r.Comma = delimiterFor(path)
	r.FieldsPerRecord = -1
	r.LazyQuotes = true

	header, err := r.Read()
	if err != nil {
		return nil, fmt.Errorf("read header: %w", err)
	}
	idx, err := resolveColumns(header)
	if err != nil {
		return nil, err
	}

	var out []Record
	for {
		row, err := r.Read()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, fmt.Errorf("read row: %w", err)
		}
		rec := idx.extract(row)
		if rec.ID == "" || rec.LanguageID == "" || rec.Segments == "" {
			continue
		}
		if rec.Cognacy != "" {
			rec.Cognacy = firstCognacy(rec.Cognacy)
		}
		out = append(out, rec)
	}
	return out, nil
}

func delimiterFor(path string) rune {
	switch strings.ToLower(filepath.Ext(path)) {
	case ".csv":
		return ','
	default:
		return '\t'
	}
}

type columnIndex struct {
	id, language, concept, segments int
	family, cognacy                 int // -1 when absent
}

func (c columnIndex) extract(row []string) Record {
	get := func(i int) string {
		if i < 0 || i >= len(row) {
			return ""
		}
		return strings.TrimSpace(row[i])
	}
	return Record{
		ID:         get(c.id),
		LanguageID: get(c.language),
		Concept:    get(c.concept),
		Segments:   get(c.segments),
		Family:     get(c.family),
		Cognacy:    get(c.cognacy),
	}
}

func resolveColumns(header []string) (columnIndex, error) {
	idx := columnIndex{id: -1, language: -1, concept: -1, segments: -1, family: -1, cognacy: -1}
	pos := make(map[string]int, len(header))
	for i, name := range header {
		pos[name] = i
	}
	pick := func(names ...string) int {
		for _, n := range names {
			if i, ok := pos[n]; ok {
				return i
			}
		}
		return -1
	}
	idx.id = pick("ID")
	idx.language = pick("Language_ID")
	idx.concept = pick("Concept", "Concepticon_Gloss", "Parameter_ID")
	idx.segments = pick("Segments")
	idx.family = pick("Family", "Dataset")
	idx.cognacy = pick("Cognacy")
	var missing []string
	if idx.id < 0 {
		missing = append(missing, "ID")
	}
	if idx.language < 0 {
		missing = append(missing, "Language_ID")
	}
	if idx.concept < 0 {
		missing = append(missing, "Concept|Concepticon_Gloss|Parameter_ID")
	}
	if idx.segments < 0 {
		missing = append(missing, "Segments")
	}
	if len(missing) > 0 {
		return idx, fmt.Errorf("missing required column(s): %s", strings.Join(missing, ", "))
	}
	return idx, nil
}

func firstCognacy(s string) string {
	if i := strings.IndexByte(s, ';'); i >= 0 {
		return strings.TrimSpace(s[:i])
	}
	return strings.TrimSpace(s)
}

// WriteSidecar writes the clustering result next to the input. The
// output is a TSV with a stable column order regardless of map
// iteration order.
func WriteSidecar(path string, rows []SidecarRow) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	w := csv.NewWriter(f)
	w.Comma = '\t'
	if err := w.Write([]string{
		"ID",
		"Cognator_ID",
		"Cognator_Confidence",
		"Cognator_Assigned_Support",
		"Cognator_Alternative_Support",
		"Cognator_Support_Margin",
		"Cognator_Cluster_Size",
		"Cognator_Candidate_Neighbors",
		"Cognator_Bootstrap",
		"Cognator_Bootstrap_Low",
		"Cognator_Bootstrap_High",
	}); err != nil {
		return err
	}
	for _, row := range rows {
		if err := w.Write([]string{
			row.ID,
			row.CognatorID,
			fmt.Sprintf("%.6f", row.Confidence),
			fmt.Sprintf("%.6f", row.AssignedSupport),
			fmt.Sprintf("%.6f", row.AlternativeSupport),
			fmt.Sprintf("%.6f", row.SupportMargin),
			fmt.Sprintf("%d", row.ClusterSize),
			fmt.Sprintf("%d", row.CandidateNeighbors),
			fmt.Sprintf("%.6f", row.Bootstrap),
			fmt.Sprintf("%.6f", row.BootstrapLow),
			fmt.Sprintf("%.6f", row.BootstrapHigh),
		}); err != nil {
			return err
		}
	}
	w.Flush()
	return w.Error()
}
