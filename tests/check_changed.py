# Fails unless the output frame differs from the input (the effects actually ran).
# usage: python check_changed.py <in.i420> <out.i420>
import sys

a = open(sys.argv[1], "rb").read()
b = open(sys.argv[2], "rb").read()
if len(a) != len(b):
    sys.exit("size mismatch")
diff = sum(abs(x - y) for x, y in zip(a, b)) / len(a)
print(f"mean change {diff:.2f}")
if diff < 0.5:
    sys.exit("output is unchanged")
