from __future__ import annotations

import argparse


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="aoe-clone",
        description="Provisional CLI for the Age of Empires clone",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    play = sub.add_parser("play", help="start a match")
    play.add_argument("--cols", type=int, default=40, help="map width in cells")
    play.add_argument("--rows", type=int, default=40, help="map height in cells")
    play.add_argument("--tile", type=int, default=64, help="isometric tile width in px")
    play.add_argument("--seed", type=int, default=None, help="seed for world generation")
    play.add_argument("--frames", type=int, default=None, help="auto-close after N frames (for testing)")
    play.add_argument("--screenshot", type=str, default=None, help="save a screenshot of the world and exit (no window)")

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.command == "play":
        from .game import run_match

        run_match(
            cols=args.cols,
            rows=args.rows,
            tile_w=args.tile,
            seed=args.seed,
            frames=args.frames,
            screenshot_path=args.screenshot,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
