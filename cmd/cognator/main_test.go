package main

import "testing"

func TestVersionConstantNonEmpty(t *testing.T) {
	if version == "" {
		t.Fatal("version string must not be empty")
	}
}
