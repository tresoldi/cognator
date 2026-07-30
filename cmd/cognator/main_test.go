package main

import "testing"

func TestVersionConstantNonEmpty(t *testing.T) {
	if version == "" {
		t.Fatal("version string must not be empty")
	}
}

func TestGoArchiveWarning(t *testing.T) {
	if goArchiveWarning == "" {
		t.Fatal("archive warning must not be empty")
	}
	if got := goArchiveWarning; got[len(got)-1] != '\n' {
		t.Fatalf("archive warning must end with newline: %q", got)
	}
}
