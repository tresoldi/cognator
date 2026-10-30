from __future__ import annotations

import argparse
from typing import TYPE_CHECKING

from .api import load

if TYPE_CHECKING:
    from collections.abc import Sequence


def main(argv: Sequence[str] | None = None) -> int:
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
    detect.add_argument("--pairs-out", default=None)
    detect.add_argument("--pairs-format", choices=("tsv", "json"), default=None)
    detect.add_argument("--pairs-unit", choices=("words", "morphemes"), default=None)
    detect.add_argument("--morpheme-ids", action="store_true")
    detect.add_argument("--null-samples", type=int, default=None)
    detect.add_argument("--null-min", type=int, default=None)
    detect.add_argument("--null-seed", type=int, default=None)

    args = parser.parse_args(argv)
    if args.command == "detect":
        pair_options: dict[str, object] = {}
        if args.pairs_out is not None or args.morpheme_ids:
            if args.morpheme_ids and args.pairs_unit == "words":
                parser.error("--morpheme-ids clusters morphemes and needs --pairs-unit morphemes")
            pair_options = {
                "pairs": "morphemes" if args.morpheme_ids else (args.pairs_unit or "words"),
                "morpheme_ids": args.morpheme_ids,
                "null_samples": args.null_samples,
                "null_min": args.null_min,
                "null_seed": args.null_seed,
            }
        elif args.pairs_unit is not None or args.pairs_format is not None:
            parser.error("--pairs-format and --pairs-unit need --pairs-out")
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
            **pair_options,
        )
        result.write_sidecar(args.out)
        if args.pairs_out is not None:
            result.write_pairs(args.pairs_out, kind=args.pairs_format)
        return 0
    return 2
