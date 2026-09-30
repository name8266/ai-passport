#!/usr/bin/env python3
"""Reproducible static page compression; no extra build dependencies."""
import gzip
from pathlib import Path
import sys

if __name__ == "__main__":
    source, target = map(Path, sys.argv[1:])
    target.write_bytes(gzip.compress(source.read_bytes(), compresslevel=9, mtime=0))
