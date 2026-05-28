#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:
    print("Pillow is required to generate icon theme files. Install python3-pillow.", file=sys.stderr)
    raise


def parse_args():
    parser = argparse.ArgumentParser(description="Generate hicolor icon sizes from a source PNG.")
    parser.add_argument("--input", required=True, help="Source icon PNG path")
    parser.add_argument("--output-dir", required=True, help="Output base directory for generated icons")
    parser.add_argument("--name", default="spectra.png", help="Output filename")
    parser.add_argument("--sizes", nargs="+", type=int, required=True, help="Icon sizes to generate")
    return parser.parse_args()


def main():
    args = parse_args()
    src = Path(args.input)
    dst_root = Path(args.output_dir)
    if not src.is_file():
        print(f"Source icon not found: {src}", file=sys.stderr)
        return 1

    with Image.open(src) as img:
        img = img.convert("RGBA")
        width, height = img.size
        if width != height:
            print(
                f"Warning: source icon is not square ({width}x{height}); resizing will stretch to square.",
                file=sys.stderr,
            )
        for size in args.sizes:
            output_dir = dst_root / f"{size}x{size}" / "apps"
            output_dir.mkdir(parents=True, exist_ok=True)
            output_path = output_dir / args.name
            resized = img.resize((size, size), Image.LANCZOS)
            resized.save(output_path, format="PNG")
            print(f"Generated {output_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
