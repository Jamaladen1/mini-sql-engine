#!/usr/bin/env python3
"""Generate a deterministic users CSV for minisql.

Uses the same splitmix64 generator and draw order as src/datagen.cpp, so
the CSV matches the in-process data used by minisql_bench for the same
--rows and --seed.

    python3 scripts/generate_data.py --rows 100000 --out data/users_100k.csv
"""
import argparse
import sys

MASK = (1 << 64) - 1
CITIES = ["Columbus", "Austin", "Boston", "Chicago", "Denver",
          "Seattle", "Portland", "Atlanta", "Phoenix", "Miami"]


class SplitMix64:
    def __init__(self, seed):
        self.state = seed & MASK

    def next(self):
        self.state = (self.state + 0x9E3779B97F4A7C15) & MASK
        z = self.state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
        return z ^ (z >> 31)


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--rows", type=int, default=10000)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--out", default="-", help="output path, '-' for stdout")
    a = p.parse_args()
    if a.rows < 0:
        p.error("--rows must be nonnegative")

    rng = SplitMix64(a.seed)
    out = sys.stdout if a.out == "-" else open(a.out, "w", newline="")
    try:
        out.write("id,name,age,city\n")
        for i in range(a.rows):
            age = 18 + rng.next() % 63
            city = CITIES[rng.next() % len(CITIES)]
            out.write(f"{i},user_{i},{age},{city}\n")
    finally:
        if out is not sys.stdout:
            out.close()


if __name__ == "__main__":
    main()
