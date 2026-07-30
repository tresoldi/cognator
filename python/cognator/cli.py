import argparse

from .api import load


def main(argv=None):
    parser = argparse.ArgumentParser(prog="python -m cognator")
    sub = parser.add_subparsers(dest="command", required=True)
    detect = sub.add_parser("detect")
    detect.add_argument("--in", dest="input", required=True)
    detect.add_argument("--out", required=True)
    detect.add_argument("--preset", default="support-adaptive-bridge")
    detect.add_argument("--threshold", type=float, default=0.70)
    detect.add_argument("--bootstrap", type=int, default=0)
    detect.add_argument("--seed", type=int, default=1)
    detect.add_argument("--min-pairs", type=int, default=1)

    args = parser.parse_args(argv)
    if args.command == "detect":
        result = load(args.input).detect(
            preset=args.preset,
            threshold=args.threshold,
            bootstrap=args.bootstrap,
            seed=args.seed,
            min_pairs=args.min_pairs,
        )
        result.write_sidecar(args.out)
        return 0
    return 2
