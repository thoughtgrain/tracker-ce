#!/usr/bin/env python3
"""Generate the TORO synth badges (L2 bitmaps) for the Tracker's synth model list and patch editor.

    python3 core/libs/images/gen_toro_badges.py            # writes the two headers next to this script
    python3 core/libs/images/gen_toro_badges.py --png DIR  # also writes 8x previews for checking by eye

How it works:
- The pill silhouette is lifted from the stock CHIP badges (same sizes, same anti-aliased edge), so TORO sits in
  the model list exactly like the others: everything connected to the border at level 0 is outside, everything
  else is the pill, and the original grey edge pixels are kept.
- The lettering is drawn at 8x with plain polygons and ellipses, box-filtered down and quantized to the four
  L2 levels. The T's crossbar sweeps up into a pair of horns: toro is Spanish for bull.

L2 format: 2 bits per pixel, MSB first, 4 grey levels (0 black .. 3 white), rows padded to `stride` bytes.

Copyright (c) 2026 briannhinton. MIT.
"""
import argparse
import os
import re
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SS = 8  # supersampling factor


def read_l2(path, w, h, stride):
    src = open(path).read()
    body = src[src.index('{', src.index('const uint8_t')) + 1:src.index('}')]
    body = re.sub(r'/\*.*?\*/|//[^\n]*', '', body, flags=re.S)
    data = [int(t, 0) for t in re.findall(r'0x[0-9A-Fa-f]+|\d+', body)]
    assert len(data) == h * stride, f'{path}: {len(data)} bytes, expected {h * stride}'
    return [[(data[y * stride + x // 4] >> (6 - 2 * (x % 4))) & 3 for x in range(w)] for y in range(h)]


def pill_from(px):
    """Outside = level-0 pixels connected to the border. Returns (inside mask, edge levels to keep)."""
    h, w = len(px), len(px[0])
    outside = [[False] * w for _ in range(h)]
    stack = [(x, y) for x in range(w) for y in (0, h - 1)] + [(x, y) for y in range(h) for x in (0, w - 1)]
    while stack:
        x, y = stack.pop()
        if 0 <= x < w and 0 <= y < h and not outside[y][x] and px[y][x] == 0:
            outside[y][x] = True
            stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    keep = {}
    for y in range(h):
        for x in range(w):
            if not outside[y][x] and 0 < px[y][x] < 3:
                if any(0 <= x + dx < w and 0 <= y + dy < h and outside[y + dy][x + dx]
                       for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    keep[(x, y)] = px[y][x]
    return outside, keep


def draw_toro(w, h, box):
    """Black lettering on white, rendered at SS x and returned as an 8-bit image of size w x h.

    box = (x0, y0, x1, y1): the cap-height box for the word, in badge pixels (floats are fine)."""
    img = Image.new('L', (w * SS, h * SS), 255)
    d = ImageDraw.Draw(img)
    x0, y0, x1, y1 = (v * SS for v in box)
    cap = y1 - y0
    stroke = cap * 0.27
    gap = cap * 0.16
    widths = [cap * 0.95, cap * 0.92, cap * 0.80, cap * 0.92]  # T O R O
    total = sum(widths) + gap * 3
    x = x0 + ((x1 - x0) - total) / 2
    ink = 0

    # T: horned crossbar. Flat across the middle so it still reads as a T; only the outer part of each arm
    # curls up and tapers to a point, like a bull's horns.
    tw = widths[0]
    cx = x + tw / 2
    horn = cap * 0.24                      # how far the tips rise above the cap line
    curl = 0.55                            # the arm stays flat until this fraction of its length
    pts_top, pts_bot = [], []
    for i in range(61):
        t = i / 60 * 2 - 1                 # -1 .. 1 across the bar
        u = max(0.0, (abs(t) - curl) / (1.0 - curl))   # 0 on the flat part, 0..1 along the curl
        lift = horn * u ** 1.8
        thick = stroke * (1.0 - 0.8 * u ** 1.5)        # full weight, then tapering to the tip
        px = cx + t * tw * 0.62
        top = y0 - lift
        pts_top.append((px, top))
        pts_bot.append((px, top + thick))
    d.polygon(pts_top + pts_bot[::-1], fill=ink)
    d.rectangle([cx - stroke / 2, y0, cx + stroke / 2, y1], fill=ink)
    x += tw + gap

    def o(xl, wd):
        d.ellipse([xl, y0, xl + wd, y1], fill=ink)
        d.ellipse([xl + stroke, y0 + stroke, xl + wd - stroke, y1 - stroke], fill=255)

    # O
    o(x, widths[1])
    x += widths[1] + gap

    # R: stem, rounded bowl, straight leg
    rw = widths[2]
    bowl_h = cap * 0.56
    d.rectangle([x, y0, x + stroke, y1], fill=ink)
    d.rounded_rectangle([x, y0, x + rw, y0 + bowl_h], radius=bowl_h / 2, fill=ink)
    d.rounded_rectangle([x + stroke, y0 + stroke, x + rw - stroke, y0 + bowl_h - stroke],
                        radius=max(1, (bowl_h - 2 * stroke) / 2), fill=255)
    d.rectangle([x + stroke, y0 + stroke, x + rw * 0.45, y0 + bowl_h - stroke], fill=255)
    d.rectangle([x, y0, x + stroke, y1], fill=ink)
    leg = [(x + rw * 0.38, y0 + bowl_h - stroke * 0.6), (x + rw * 0.38 + stroke * 1.1, y0 + bowl_h - stroke * 0.6),
           (x + rw, y1), (x + rw - stroke * 1.15, y1)]
    d.polygon(leg, fill=ink)
    x += rw + gap

    # O
    o(x, widths[3])

    return img.resize((w, h), Image.BOX)


def compose(template, w, h, stride, box):
    outside, keep = pill_from(read_l2(template, w, h, stride))
    text = draw_toro(w, h, box)
    out = []
    for y in range(h):
        row = []
        for x in range(w):
            if outside[y][x]:
                v = 0
            elif (x, y) in keep:
                v = keep[(x, y)]
            else:
                v = min(3, max(0, int(round(text.getpixel((x, y)) / 85.0))))
            row.append(v)
        out.append(row)
    return out


def pack(px, stride):
    data = []
    for row in px:
        b = [0] * stride
        for x, v in enumerate(row):
            b[x // 4] |= v << (6 - 2 * (x % 4))
        data += b
    return data


def write_header(path, name, px, w, h, stride):
    data = pack(px, stride)
    lines = [f'// Generated by gen_toro_badges.py: TORO badge, L2 bitmap ({w}x{h}, stride {stride}).',
             '// Pill silhouette from the stock CHIP badge; lettering drawn by the script. Do not edit by hand.',
             f'const uint8_t {name}[{len(data)}] = {{']
    for i in range(0, len(data), 16):
        lines.append('    ' + ', '.join(f'0x{b:02X}' for b in data[i:i + 16]) + ',')
    lines.append('};')
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


def preview(px, path, scale=8):
    h, w = len(px), len(px[0])
    img = Image.new('L', (w, h))
    for y in range(h):
        for x in range(w):
            img.putpixel((x, y), px[y][x] * 85)
    img.resize((w * scale, h * scale), Image.NEAREST).save(path)


BADGES = [
    # template, output, symbol, w, h, stride, cap-height box (x0, y0, x1, y1)
    ('chip_mini_nomark_80x28_L2.h', 'toro_mini_nomark_80x28_L2.h', 'toro_mini_nomark_80x28_L2', 80, 28, 20,
     (14.5, 7.8, 65.5, 20.2)),
    ('chip_med_108x40_L2.h', 'toro_med_108x40_L2.h', 'toro_med_108x40_L2', 108, 40, 27,
     (20.0, 11.0, 88.0, 28.5)),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--png', metavar='DIR', help='also write 8x PNG previews into DIR')
    args = ap.parse_args()
    for template, out, sym, w, h, stride, box in BADGES:
        px = compose(os.path.join(HERE, template), w, h, stride, box)
        write_header(os.path.join(HERE, out), sym, px, w, h, stride)
        if args.png:
            os.makedirs(args.png, exist_ok=True)
            preview(px, os.path.join(args.png, out.replace('.h', '.png')))
        print(f'wrote {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
