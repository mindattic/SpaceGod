#!/usr/bin/env python3
"""Pixel diff of the two tracks' screenshots (US-T45).

    python tools/imgdiff.py                      # every docs/images/cpp-*.png against its csharp-*.png
    python tools/imgdiff.py a.png b.png          # one pair
    python tools/imgdiff.py --threshold 8 --out Saved/ImageDiff

A pixel differs when any channel differs by more than --threshold (0-255; default 24, above GPU and dithering
noise). For each pair it prints the share of differing pixels and writes <out>/<name>-diff.png: the C++ shot dimmed to a
quarter, differing pixels in red (brighter the bigger the difference), so where the shots part is visible at a glance.
Needs Pillow (pip install pillow). Run tools/play.ps1 -Track Cpp -Mode shots and -Track CSharp -Mode shots first:
both start at the same fixed moment with the same seed, so the shots are meant to match pixel for pixel.
"""
import argparse
import os
import sys

from PIL import Image, ImageChops

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGES = os.path.join(REPO, "docs", "images")


def diff(a_path, b_path, out_path, threshold):
    a = Image.open(a_path).convert("RGB")
    b = Image.open(b_path).convert("RGB")
    if a.size != b.size:
        b = b.resize(a.size, Image.BILINEAR)
    d = ImageChops.difference(a, b)
    r, g, bl = d.split()
    peak = ImageChops.lighter(ImageChops.lighter(r, g), bl)
    mask = peak.point(lambda v: 255 if v > threshold else 0)
    total = a.size[0] * a.size[1]
    differing = mask.histogram()[255]
    if out_path:
        base = a.point(lambda v: v // 4)
        red = Image.merge("RGB", (peak.point(lambda v: min(255, 96 + v * 2)), Image.new("L", a.size, 0), Image.new("L", a.size, 0)))
        Image.composite(red, base, mask).save(out_path)
    return differing * 100.0 / total, differing, total


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pair", nargs="*", help="two images (default: every docs/images/cpp-*.png with its csharp-*.png)")
    ap.add_argument("--threshold", type=int, default=24, help="per-channel difference (0-255) a pixel must exceed to count")
    ap.add_argument("--out", default=os.path.join(REPO, "Saved", "ImageDiff"), help="folder for the diff images ('' for none)")
    args = ap.parse_args()

    if args.pair and len(args.pair) != 2:
        ap.error("give two images, or none")
    pairs = []
    if args.pair:
        pairs.append((os.path.splitext(os.path.basename(args.pair[0]))[0], args.pair[0], args.pair[1]))
    else:
        for name in sorted(os.listdir(IMAGES)):
            if name.startswith("cpp-") and name.endswith(".png"):
                other = os.path.join(IMAGES, "csharp-" + name[len("cpp-"):])
                if os.path.exists(other):
                    pairs.append((name[len("cpp-"):-4], os.path.join(IMAGES, name), other))
    if not pairs:
        print("no pairs to compare", file=sys.stderr)
        return 1
    if args.out:
        os.makedirs(args.out, exist_ok=True)
    print(f"{'shot':<24} {'differing':>10}  (threshold {args.threshold})")
    for name, a, b in pairs:
        out = os.path.join(args.out, name + "-diff.png") if args.out else None
        pct, n, total = diff(a, b, out, args.threshold)
        print(f"{name:<24} {pct:>9.2f}%  {n} of {total} px" + (f"  -> {out}" if out else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
