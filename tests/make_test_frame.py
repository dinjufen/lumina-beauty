# Writes a synthetic I420 frame (gradients plus a skin-colored ellipse) for smoke tests.
# usage: python make_test_frame.py <out.i420> <width> <height>
import math
import sys

path, w, h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
y = bytearray(w * h)
u = bytearray((w // 2) * (h // 2))
v = bytearray((w // 2) * (h // 2))
for r in range(h):
    for c in range(w):
        inside = ((c - w / 2) / (w * 0.2)) ** 2 + ((r - h / 2) / (h * 0.3)) ** 2 < 1
        y[r * w + c] = 170 if inside else int(40 + 150 * c / w + 20 * math.sin(r * 0.3))
for r in range(h // 2):
    for c in range(w // 2):
        inside = ((c - w / 4) / (w * 0.1)) ** 2 + ((r - h / 4) / (h * 0.15)) ** 2 < 1
        u[r * (w // 2) + c] = 110 if inside else 128
        v[r * (w // 2) + c] = 150 if inside else 128
with open(path, "wb") as f:
    f.write(y + u + v)
