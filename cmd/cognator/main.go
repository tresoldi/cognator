package main

import (
	"fmt"
	"os"
)

const usage = `cognator — framework-native cognate detection

usage:
  cognator <subcommand> [flags]

subcommands:
  detect    cluster forms into cognate sets
  eval      score predicted cognate assignments against ground truth
  version   print version
  help      show this message
`

const version = "0.0.0-dev"

func main() {
	if len(os.Args) < 2 {
		fmt.Fprint(os.Stderr, usage)
		os.Exit(2)
	}
	switch os.Args[1] {
	case "version":
		fmt.Println(version)
	case "help", "-h", "--help":
		fmt.Print(usage)
	case "detect":
		detectCmd(os.Args[2:])
	case "eval":
		evalCmd(os.Args[2:])
	default:
		fmt.Fprintf(os.Stderr, "unknown subcommand: %s\n\n%s", os.Args[1], usage)
		os.Exit(2)
	}
}
