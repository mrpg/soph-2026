#!/usr/bin/env python3
"""Solve n * log10(n) = d for n, i.e. how many terms for d digits."""

import math
import sys

d = int(sys.argv[1])

n = d / math.log10(d)
for _ in range(50):
    n -= (n * math.log10(n) - d) / (math.log10(n) + 1 / math.log(10))

print(math.ceil(n))
