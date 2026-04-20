package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func writeTempFile(t *testing.T, name, content string) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), name)
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestReadEvalTableUsesEvalIDAndDataset(t *testing.T) {
	path := writeTempFile(t, "track_a.tsv", strings.Join([]string{
		"Eval_ID\tRaw_ID\tDataset\tLanguage_ID\tConcept\tSegments\tGold_Cognacy\tTrack\tHas_Partial\tHas_Multi_Cognacy\tRow_Number",
		"fam:w1#1\tw1\tfam\tlang1\tHAND\th a n d\tc1\tA\t0\t0\t2",
		"fam:w1#2\tw1\tfam\tlang2\tHAND\th a n d\tc1\tA\t0\t0\t3",
		"",
	}, "\n"))

	recs, err := readEvalTable(path)
	if err != nil {
		t.Fatalf("readEvalTable: %v", err)
	}
	if len(recs) != 2 {
		t.Fatalf("len(recs) = %d, want 2", len(recs))
	}
	if recs[0].ID != "fam:w1#1" {
		t.Fatalf("record ID = %q, want Eval_ID", recs[0].ID)
	}
	if recs[0].Family != "fam" {
		t.Fatalf("Family = %q, want dataset name", recs[0].Family)
	}
	if recs[0].Cognacy != "c1" {
		t.Fatalf("Cognacy = %q, want Gold_Cognacy", recs[0].Cognacy)
	}
}

func TestLoadNameListIgnoresCommentsAndBlankLines(t *testing.T) {
	path := writeTempFile(t, "split.txt", strings.Join([]string{
		"# comment",
		"alpha",
		"",
		"beta",
		"",
	}, "\n"))
	got, err := loadNameList(path)
	if err != nil {
		t.Fatalf("loadNameList: %v", err)
	}
	if !got["alpha"] || !got["beta"] {
		t.Fatalf("expected alpha and beta in split set, got %#v", got)
	}
	if got["# comment"] {
		t.Fatalf("comment line should be ignored: %#v", got)
	}
}
