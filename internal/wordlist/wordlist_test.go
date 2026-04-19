package wordlist

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func writeTemp(t *testing.T, name, content string) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), name)
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestReadCognatorCanonicalTSV(t *testing.T) {
	content := strings.Join([]string{
		"ID\tLanguage_ID\tConcept\tSegments\tFamily\tCognacy",
		"w1\tlat\tNIGHT\tn o k t e\tromance\t42",
		"w2\tita\tNIGHT\tn o t t e\tromance\t42",
		"",
	}, "\n")
	path := writeTemp(t, "small.tsv", content)

	recs, err := ReadTable(path)
	if err != nil {
		t.Fatalf("ReadTable: %v", err)
	}
	if len(recs) != 2 {
		t.Fatalf("len(recs) = %d, want 2", len(recs))
	}
	r := recs[0]
	if r.ID != "w1" || r.LanguageID != "lat" || r.Concept != "NIGHT" ||
		r.Segments != "n o k t e" || r.Family != "romance" || r.Cognacy != "42" {
		t.Errorf("row 0 = %+v", r)
	}
}

func TestReadArcaverborumCSVAliasColumns(t *testing.T) {
	// Arcaverborum columns: Concepticon_Gloss (→ Concept), Dataset (→ Family when Family absent)
	content := strings.Join([]string{
		"ID,Dataset,Language_ID,Glottocode,Parameter_ID,Concepticon_Gloss,Segments,Cognacy",
		"wp_haw_night,walworthpolynesian,walworthpolynesian_Hawaiian,hawa1245,p1,NIGHT,p o,wp_1;wp_1",
		"wp_ton_night,walworthpolynesian,walworthpolynesian_Tongan,tong1325,p1,NIGHT,p o,wp_1",
		"",
	}, "\n")
	path := writeTemp(t, "arca.csv", content)

	recs, err := ReadTable(path)
	if err != nil {
		t.Fatalf("ReadTable: %v", err)
	}
	if len(recs) != 2 {
		t.Fatalf("got %d rows, want 2", len(recs))
	}
	if recs[0].Concept != "NIGHT" {
		t.Errorf("Concept alias failed: %q", recs[0].Concept)
	}
	if recs[0].Family != "walworthpolynesian" {
		t.Errorf("Dataset alias failed: %q", recs[0].Family)
	}
	if recs[0].Cognacy != "wp_1" {
		t.Errorf("Cognacy should be first of semicolon list, got %q", recs[0].Cognacy)
	}
}

func TestReadMissingRequiredColumnErrors(t *testing.T) {
	content := strings.Join([]string{
		"ID\tLanguage_ID\tSegments", // missing Concept/Concepticon_Gloss
		"w1\tlat\tp o",
		"",
	}, "\n")
	path := writeTemp(t, "missing.tsv", content)
	if _, err := ReadTable(path); err == nil {
		t.Fatal("expected error for missing concept column")
	}
}

func TestReadSkipsEmptySegments(t *testing.T) {
	content := strings.Join([]string{
		"ID\tLanguage_ID\tConcept\tSegments",
		"w1\tlat\tNIGHT\tn o k t e",
		"w2\tlat\tDAY\t", // empty segments — skipped with warning
		"",
	}, "\n")
	path := writeTemp(t, "emptyseg.tsv", content)
	recs, err := ReadTable(path)
	if err != nil {
		t.Fatalf("ReadTable: %v", err)
	}
	if len(recs) != 1 {
		t.Errorf("len(recs) = %d, want 1 (empty-segments row should be skipped)", len(recs))
	}
}

func TestWriteSidecarTSV(t *testing.T) {
	path := filepath.Join(t.TempDir(), "out.tsv")
	rows := []SidecarRow{
		{ID: "w1", CognatorID: "romance:NIGHT:1", Confidence: 0.91, Bootstrap: 0.88},
		{ID: "w2", CognatorID: "romance:NIGHT:1", Confidence: 0.91, Bootstrap: 0.88},
	}
	if err := WriteSidecar(path, rows); err != nil {
		t.Fatalf("WriteSidecar: %v", err)
	}
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	got := string(data)
	wantHeader := "ID\tCognator_ID\tCognator_Confidence\tCognator_Bootstrap\n"
	if !strings.HasPrefix(got, wantHeader) {
		t.Errorf("header wrong, got %q", got)
	}
	if !strings.Contains(got, "\tromance:NIGHT:1\t0.910000\t0.880000\n") {
		t.Errorf("row format wrong: %q", got)
	}
}

// Linguistic check: load the real walworth Polynesian subset from the
// arcaverborum CoreCog CSV, assert shape. Skipped if the data file
// isn't present (common in CI / for contributors without the dump).
func TestLoadWalworthPolynesianFromArcaverborum(t *testing.T) {
	path := "/tmp/arcaverborum_corecog/arcaverborum-A-corecog-20251008/forms.csv"
	if _, err := os.Stat(path); os.IsNotExist(err) {
		t.Skipf("arcaverborum dump not present at %s; download from DOI 10.5281/zenodo.17294927", path)
	}
	recs, err := ReadTable(path)
	if err != nil {
		t.Fatalf("ReadTable: %v", err)
	}
	walworth := filterByFamily(recs, "walworthpolynesian")
	if len(walworth) < 2000 {
		t.Errorf("expected >2000 walworthpolynesian rows, got %d", len(walworth))
	}
	lects := uniqueLanguages(walworth)
	if len(lects) < 7 {
		t.Errorf("expected ≥7 Polynesian lects, got %d: %v", len(lects), lects)
	}
	// Regulae's own experiments report 329 cognate sets on walworthpolynesian.
	// We count the unique first-semicolon Cognacy values.
	cogSets := uniqueCognacies(walworth)
	if len(cogSets) < 300 {
		t.Errorf("expected ≈329 cognate sets, got %d", len(cogSets))
	}
}

func filterByFamily(recs []Record, family string) []Record {
	out := make([]Record, 0, len(recs)/4)
	for _, r := range recs {
		if r.Family == family {
			out = append(out, r)
		}
	}
	return out
}

func uniqueLanguages(recs []Record) []string {
	seen := map[string]struct{}{}
	for _, r := range recs {
		seen[r.LanguageID] = struct{}{}
	}
	out := make([]string, 0, len(seen))
	for k := range seen {
		out = append(out, k)
	}
	return out
}

func uniqueCognacies(recs []Record) map[string]struct{} {
	seen := map[string]struct{}{}
	for _, r := range recs {
		if r.Cognacy != "" {
			seen[r.Cognacy] = struct{}{}
		}
	}
	return seen
}
