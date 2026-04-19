package merkmal

import (
	"bufio"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

type Role byte

const (
	RoleUnknown Role = 'X'
	RoleC       Role = 'C'
	RoleR       Role = 'R'
	RoleV       Role = 'V'
	RoleG       Role = 'G'
	RoleT       Role = 'T'
	RoleS       Role = 'S'
)

type FileMeta struct {
	Present bool   `json:"present"`
	SHA256  string `json:"sha256"`
	Rows    int    `json:"rows"`
	Bytes   int    `json:"bytes"`
}

type DistanceNormalization struct {
	Method      string  `json:"method"`
	DMaxRaw     float64 `json:"d_max_raw"`
	Description string  `json:"description"`
}

type Manifest struct {
	SchemaVersion         int                   `json:"schema_version"`
	MerkmalVersion        string                `json:"merkmal_version"`
	System                string                `json:"system"`
	ExportDate            string                `json:"export_date"`
	DistanceNormalization DistanceNormalization `json:"distance_normalization"`
	GraphemeCount         int                   `json:"grapheme_count"`
	Files                 map[string]FileMeta   `json:"files"`
}

type Bundle struct {
	System    string
	Manifest  Manifest
	distances map[string]map[string]float64
	prosody   map[string]Role
	classes   map[string]string
	// partitions[level][grapheme] = class_code. Levels are whatever
	// merkmal ships (prosody/coarse/medium/fine for current exports).
	partitions map[string]map[string]string
}

func LoadBundle(dir string) (*Bundle, error) {
	manifestPath := filepath.Join(dir, "manifest.json")
	mdata, err := os.ReadFile(manifestPath)
	if err != nil {
		return nil, fmt.Errorf("read manifest: %w", err)
	}
	var m Manifest
	if err := json.Unmarshal(mdata, &m); err != nil {
		return nil, fmt.Errorf("parse manifest: %w", err)
	}
	if m.SchemaVersion != 1 {
		return nil, fmt.Errorf("unsupported manifest schema_version %d (need 1)", m.SchemaVersion)
	}

	b := &Bundle{
		System:     m.System,
		Manifest:   m,
		distances:  make(map[string]map[string]float64),
		prosody:    make(map[string]Role),
		classes:    make(map[string]string),
		partitions: make(map[string]map[string]string),
	}

	for _, name := range []string{"distances.tsv", "prosody.tsv", "fallback.tsv", "classes.tsv", "partitions.tsv"} {
		meta, ok := m.Files[name]
		if !ok || !meta.Present {
			continue
		}
		path := filepath.Join(dir, name)
		if err := verifySHA256(path, meta.SHA256); err != nil {
			return nil, fmt.Errorf("%s: %w", name, err)
		}
	}

	if err := b.loadDistances(filepath.Join(dir, "distances.tsv")); err != nil {
		return nil, fmt.Errorf("distances: %w", err)
	}
	if err := b.loadProsody(filepath.Join(dir, "prosody.tsv")); err != nil {
		return nil, fmt.Errorf("prosody: %w", err)
	}
	if meta, ok := m.Files["classes.tsv"]; ok && meta.Present {
		if err := b.loadClasses(filepath.Join(dir, "classes.tsv")); err != nil {
			return nil, fmt.Errorf("classes: %w", err)
		}
	}
	if meta, ok := m.Files["partitions.tsv"]; ok && meta.Present {
		if err := b.loadPartitions(filepath.Join(dir, "partitions.tsv")); err != nil {
			return nil, fmt.Errorf("partitions: %w", err)
		}
	}
	return b, nil
}

func (b *Bundle) loadPartitions(path string) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	s.Buffer(make([]byte, 1<<20), 1<<20)
	if !s.Scan() {
		return fmt.Errorf("empty partitions file")
	}
	if s.Text() != "grapheme\tlevel\tclass_code\tclass_full" {
		return fmt.Errorf("unexpected partitions header: %q", s.Text())
	}
	for s.Scan() {
		parts := strings.SplitN(s.Text(), "\t", 4)
		if len(parts) < 3 {
			return fmt.Errorf("bad partitions row: %q", s.Text())
		}
		grapheme, level, code := parts[0], parts[1], parts[2]
		inner, ok := b.partitions[level]
		if !ok {
			inner = make(map[string]string)
			b.partitions[level] = inner
		}
		inner[grapheme] = code
	}
	return s.Err()
}

// Partition returns the partition class code at the given level for
// the grapheme, or "" if the level is absent or the grapheme is
// unknown at that level. Levels in current merkmal exports:
// "prosody", "coarse", "medium", "fine".
func (b *Bundle) Partition(level, grapheme string) string {
	inner, ok := b.partitions[level]
	if !ok {
		return ""
	}
	return inner[grapheme]
}

// PartitionLevels returns the level names present in this bundle,
// in no particular order. Use for sanity-checking a requested level.
func (b *Bundle) PartitionLevels() []string {
	out := make([]string, 0, len(b.partitions))
	for k := range b.partitions {
		out = append(out, k)
	}
	return out
}

func verifySHA256(path, want string) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	sum := sha256.Sum256(data)
	got := hex.EncodeToString(sum[:])
	if got != want {
		return fmt.Errorf("sha256 mismatch: got %s, want %s", got, want)
	}
	return nil
}

func (b *Bundle) loadDistances(path string) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	s.Buffer(make([]byte, 1<<20), 1<<20)
	if !s.Scan() {
		return fmt.Errorf("empty distances file")
	}
	if s.Text() != "grapheme_a\tgrapheme_b\tdistance" {
		return fmt.Errorf("unexpected distances header: %q", s.Text())
	}
	for s.Scan() {
		parts := strings.SplitN(s.Text(), "\t", 3)
		if len(parts) != 3 {
			return fmt.Errorf("bad distances row: %q", s.Text())
		}
		d, err := strconv.ParseFloat(parts[2], 64)
		if err != nil {
			return fmt.Errorf("bad distance %q: %w", parts[2], err)
		}
		inner, ok := b.distances[parts[0]]
		if !ok {
			inner = make(map[string]float64)
			b.distances[parts[0]] = inner
		}
		inner[parts[1]] = d
	}
	return s.Err()
}

func (b *Bundle) loadProsody(path string) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	if !s.Scan() {
		return fmt.Errorf("empty prosody file")
	}
	if s.Text() != "grapheme\trole" {
		return fmt.Errorf("unexpected prosody header: %q", s.Text())
	}
	for s.Scan() {
		parts := strings.SplitN(s.Text(), "\t", 2)
		if len(parts) != 2 || len(parts[1]) != 1 {
			return fmt.Errorf("bad prosody row: %q", s.Text())
		}
		b.prosody[parts[0]] = Role(parts[1][0])
	}
	return s.Err()
}

func (b *Bundle) loadClasses(path string) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	if !s.Scan() {
		return fmt.Errorf("empty classes file")
	}
	if s.Text() != "grapheme\tclass" {
		return fmt.Errorf("unexpected classes header: %q", s.Text())
	}
	for s.Scan() {
		parts := strings.SplitN(s.Text(), "\t", 2)
		if len(parts) != 2 {
			return fmt.Errorf("bad classes row: %q", s.Text())
		}
		b.classes[parts[0]] = parts[1]
	}
	return s.Err()
}

// Distance returns the normalized distance between graphemes a and b,
// in [0, 1]. OOV policy: identical strings return 0, otherwise 1.
func (b *Bundle) Distance(a, bg string) float64 {
	if a == bg {
		return 0.0
	}
	inner, ok := b.distances[a]
	if !ok {
		return 1.0
	}
	d, ok := inner[bg]
	if !ok {
		return 1.0
	}
	return d
}

// Role returns the prosodic role of grapheme g, or RoleUnknown if absent.
func (b *Bundle) Role(g string) Role {
	r, ok := b.prosody[g]
	if !ok {
		return RoleUnknown
	}
	return r
}

// Class returns the sound-class of grapheme g, or "", false if the
// bundle system has no class reduction or the grapheme is absent.
func (b *Bundle) Class(g string) (string, bool) {
	c, ok := b.classes[g]
	return c, ok
}

// Known reports whether grapheme g is in the merkmal inventory.
func (b *Bundle) Known(g string) bool {
	_, ok := b.prosody[g]
	return ok
}
