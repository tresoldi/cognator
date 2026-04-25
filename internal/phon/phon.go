package phon

import (
	"io/fs"
	"os"
	"path/filepath"

	merkmal "github.com/tresoldi/merkmal/go"
)

type Role = merkmal.Role

const (
	RoleUnknown = merkmal.RoleUnknown
	RoleC       = merkmal.RoleC
	RoleV       = merkmal.RoleV
	RoleR       = merkmal.RoleR
	RoleG       = merkmal.RoleG
	RoleT       = merkmal.RoleT
	RoleS       = merkmal.RoleS
)

// Phon wraps a merkmal System with precomputed roles and partitions.
// A nil *Phon is safe to call methods on — they return zero-value defaults.
type Phon struct {
	sys   merkmal.System
	geom  *merkmal.Geometry
	pt    *merkmal.PartitionTable
	roles map[string]Role

	Name          string
	GraphemeCount int
}

// Load creates a Phon from a model fs.FS and a geometry fs.FS.
func Load(modelFS, geomFS fs.FS, geomName string) (*Phon, error) {
	geom, err := merkmal.LoadGeometry(geomFS, geomName)
	if err != nil {
		return nil, err
	}
	sys, err := merkmal.LoadModel(modelFS, geom)
	if err != nil {
		return nil, err
	}
	pt := merkmal.ComputePartitions(sys, geom)
	graphemes := sys.ListGraphemes()
	roles := make(map[string]Role, len(graphemes))
	for _, g := range graphemes {
		roles[g] = merkmal.DeriveRole(g, sys)
	}
	return &Phon{
		sys:           sys,
		geom:          geom,
		pt:            pt,
		roles:         roles,
		Name:          sys.Name(),
		GraphemeCount: len(graphemes),
	}, nil
}

// LoadDir loads a Phon from a merkmal repository root directory.
// It expects {root}/models/{modelName}/ and {root}/geometries/.
func LoadDir(root, modelName string) (*Phon, error) {
	modelFS := os.DirFS(filepath.Join(root, "models", modelName))
	geomFS := os.DirFS(filepath.Join(root, "geometries"))
	return Load(modelFS, geomFS, "clements-hume")
}

// Distance returns the normalized distance between graphemes a and b.
// Identical strings return 0. Unknown graphemes return 1.
func (p *Phon) Distance(a, b string) float64 {
	if p == nil {
		if a == b {
			return 0.0
		}
		return 1.0
	}
	if a == b {
		return 0.0
	}
	return p.sys.SegmentDistance(a, b)
}

// Role returns the prosodic role of grapheme g, or RoleUnknown if absent.
func (p *Phon) Role(g string) Role {
	if p == nil {
		return RoleUnknown
	}
	r, ok := p.roles[g]
	if !ok {
		return RoleUnknown
	}
	return r
}

// Known reports whether grapheme g is in the inventory.
func (p *Phon) Known(g string) bool {
	if p == nil {
		return false
	}
	return merkmal.Known(p.sys, g)
}

// Partition returns the partition class code at the given level.
func (p *Phon) Partition(level, grapheme string) string {
	if p == nil {
		return ""
	}
	return p.pt.Partition(level, grapheme)
}

// PartitionLevels returns the available partition levels.
func (p *Phon) PartitionLevels() []string {
	if p == nil {
		return nil
	}
	return p.pt.Levels()
}

// LevelClassCount returns the number of distinct classes at a partition level.
func (p *Phon) LevelClassCount(level string) int {
	if p == nil {
		return 0
	}
	return p.pt.ClassCount(level)
}

// System returns the underlying merkmal System.
func (p *Phon) System() merkmal.System {
	if p == nil {
		return nil
	}
	return p.sys
}
