package merkmal

import (
	"os"
	"path/filepath"
	"testing"
)

const fixtureClassfeat = "../../tests/fixtures/merkmal/classfeat"
const fixtureDescriptive = "../../tests/fixtures/merkmal/descriptive"

func TestLoadClassfeatBundle(t *testing.T) {
	b, err := LoadBundle(fixtureClassfeat)
	if err != nil {
		t.Fatalf("LoadBundle: %v", err)
	}
	if b.System != "classfeat" {
		t.Errorf("System = %q, want classfeat", b.System)
	}
	if b.Manifest.GraphemeCount != 110 {
		t.Errorf("GraphemeCount = %d, want 110", b.Manifest.GraphemeCount)
	}
	if got, want := len(b.prosody), 110; got != want {
		t.Errorf("len(prosody) = %d, want %d", got, want)
	}
}

func TestDistanceIdentityIsZero(t *testing.T) {
	b, err := LoadBundle(fixtureClassfeat)
	if err != nil {
		t.Fatalf("LoadBundle: %v", err)
	}
	for _, g := range []string{"a", "b", "p", "t", "k", "m", "j"} {
		if d := b.Distance(g, g); d != 0.0 {
			t.Errorf("Distance(%q,%q) = %f, want 0.0", g, g, d)
		}
	}
}

func TestDistanceKnownInRange(t *testing.T) {
	b, err := LoadBundle(fixtureClassfeat)
	if err != nil {
		t.Fatalf("LoadBundle: %v", err)
	}
	d := b.Distance("a", "b")
	if d <= 0.0 || d > 1.0 {
		t.Errorf("Distance(a,b) = %f, want in (0, 1]", d)
	}
	// Symmetry on a known symmetric system.
	if b.Distance("a", "b") != b.Distance("b", "a") {
		t.Error("expected symmetric distance for classfeat")
	}
}

func TestDistanceOOVReturnsMax(t *testing.T) {
	b, err := LoadBundle(fixtureClassfeat)
	if err != nil {
		t.Fatalf("LoadBundle: %v", err)
	}
	if d := b.Distance("☃", "a"); d != 1.0 {
		t.Errorf("OOV-known: got %f, want 1.0", d)
	}
	if d := b.Distance("a", "☃"); d != 1.0 {
		t.Errorf("known-OOV: got %f, want 1.0", d)
	}
	if d := b.Distance("☃", "☃"); d != 0.0 {
		t.Errorf("OOV identity: got %f, want 0.0", d)
	}
	if d := b.Distance("☃", "⛄"); d != 1.0 {
		t.Errorf("OOV-OOV different: got %f, want 1.0", d)
	}
}

func TestRoleLookup(t *testing.T) {
	b, err := LoadBundle(fixtureClassfeat)
	if err != nil {
		t.Fatalf("LoadBundle: %v", err)
	}
	cases := map[string]Role{
		"a":  RoleV,
		"b":  RoleC,
		"p":  RoleC,
		"m":  RoleR,
		"j":  RoleG,
		"☃":  RoleUnknown,
	}
	for g, want := range cases {
		if got := b.Role(g); got != want {
			t.Errorf("Role(%q) = %c, want %c", g, got, want)
		}
	}
}

func TestClassesPresentForClassfeat(t *testing.T) {
	b, err := LoadBundle(fixtureClassfeat)
	if err != nil {
		t.Fatalf("LoadBundle: %v", err)
	}
	cls, ok := b.Class("a")
	if !ok || cls != "A" {
		t.Errorf("Class(a) = %q, %v; want A, true", cls, ok)
	}
}

func TestClassesAbsentForDescriptive(t *testing.T) {
	b, err := LoadBundle(fixtureDescriptive)
	if err != nil {
		t.Fatalf("LoadBundle: %v", err)
	}
	if _, ok := b.Class("a"); ok {
		t.Error("classfeat-only classes file should not be present for descriptive")
	}
}

func TestManifestHashVerification(t *testing.T) {
	dir := t.TempDir()
	for _, name := range []string{"distances.tsv", "prosody.tsv", "fallback.tsv", "manifest.json", "classes.tsv", "partitions.tsv"} {
		src := filepath.Join(fixtureClassfeat, name)
		dst := filepath.Join(dir, name)
		data, err := os.ReadFile(src)
		if err != nil {
			continue
		}
		if err := os.WriteFile(dst, data, 0o644); err != nil {
			t.Fatal(err)
		}
	}
	if _, err := LoadBundle(dir); err != nil {
		t.Fatalf("clean copy should load: %v", err)
	}
	// Now corrupt distances.tsv and expect a hash failure.
	path := filepath.Join(dir, "distances.tsv")
	data, _ := os.ReadFile(path)
	data[0] = '!'
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := LoadBundle(dir); err == nil {
		t.Fatal("expected hash mismatch error")
	}
}

func TestLoadDescriptiveSize(t *testing.T) {
	b, err := LoadBundle(fixtureDescriptive)
	if err != nil {
		t.Fatalf("LoadBundle: %v", err)
	}
	if b.Manifest.GraphemeCount != 778 {
		t.Errorf("GraphemeCount = %d, want 778", b.Manifest.GraphemeCount)
	}
	if !b.Known("a") || !b.Known("p") {
		t.Error("descriptive bundle should know common IPA")
	}
}
