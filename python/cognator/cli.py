from __future__ import annotations

import argparse
from typing import Optional, Sequence

from .api import load


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(prog="cognator")
    sub = parser.add_subparsers(dest="command", required=True)
    detect = sub.add_parser("detect")
    detect.add_argument("--in", dest="input", required=True)
    detect.add_argument("--out", required=True)
    detect.add_argument("--preset", default="support-adaptive-bridge")
    detect.add_argument("--threshold", type=float, default=0.0)
    detect.add_argument("--threshold-mode", default=None)
    detect.add_argument("--threshold-min", type=float, default=None)
    detect.add_argument("--threshold-max", type=float, default=None)
    detect.add_argument("--threshold-shrink", type=float, default=None)
    detect.add_argument("--bootstrap", type=int, default=0)
    detect.add_argument("--seed", type=int, default=1)
    detect.add_argument("--min-pairs", type=int, default=1)

    args = parser.parse_args(argv)
    if args.command == "detect":
        result = load(args.input).detect(
            preset=args.preset,
            threshold=args.threshold,
            threshold_mode=args.threshold_mode,
            threshold_min=args.threshold_min,
            threshold_max=args.threshold_max,
            threshold_shrink=args.threshold_shrink,
            bootstrap=args.bootstrap,
            seed=args.seed,
            min_pairs=args.min_pairs,
        )
        result.write_sidecar(args.out)
        return 0
    return 2
