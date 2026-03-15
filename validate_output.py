#!/usr/bin/env python3
"""
Validate tick-feeder-binance output files and metadata consistency.

Checks:
  1. File size consistency (all .bin files have matching record counts)
  2. Metadata match (candle_counts vs actual file sizes)
  3. Trade ID continuity (no reversals, gap warnings)
  4. Keys monotonically increasing
  5. OHLCV integrity (high >= low, volume > 0, etc.)
"""

import argparse
import json
import struct
import sys
from pathlib import Path

# All column files and their element sizes.
# keys.bin is handled separately.
FIELD_FILES = [
    ("open.bin", 8, "d"),
    ("high.bin", 8, "d"),
    ("low.bin", 8, "d"),
    ("close.bin", 8, "d"),
    ("volume.bin", 8, "d"),
    ("first_trade_id.bin", 8, "Q"),
    ("last_trade_id.bin", 8, "Q"),
]

ALL_BIN_FILES = ["keys.bin"] + [f[0] for f in FIELD_FILES]

GAP_WARN_THRESHOLD = 100000


class ValidationResult:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.warnings = 0
        self.errors = []

    def ok(self, msg):
        self.passed += 1

    def fail(self, msg):
        self.failed += 1
        self.errors.append(f"FAIL: {msg}")
        print(f"  FAIL: {msg}")

    def warn(self, msg):
        self.warnings += 1
        print(f"  WARN: {msg}")


def read_bin(path, fmt):
    """Read a binary file as an array of values with the given struct format."""
    data = path.read_bytes()
    elem_size = struct.calcsize(fmt)
    if len(data) % elem_size != 0:
        return None, len(data)
    count = len(data) // elem_size
    values = struct.unpack(f"<{count}{fmt}", data)
    return values, len(data)


def check_file_sizes(candle_dir, result):
    """Check that all .bin files have consistent record counts."""
    sizes = {}
    for name in ALL_BIN_FILES:
        p = candle_dir / name
        if not p.exists():
            result.fail(f"{candle_dir}: missing {name}")
            return -1
        sz = p.stat().st_size
        sizes[name] = sz
        if sz % 8 != 0:
            result.fail(
                f"{candle_dir}/{name}: size {sz} not a multiple of 8"
            )
            return -1

    record_counts = {name: sz // 8 for name, sz in sizes.items()}
    unique = set(record_counts.values())
    if len(unique) != 1:
        result.fail(
            f"{candle_dir}: inconsistent record counts: {record_counts}"
        )
        return -1

    count = unique.pop()
    result.ok(f"{candle_dir}: {count} records, all files consistent")
    return count


def check_metadata_counts(candle_dir, symbol, candle_name,
                          metadata, record_count, result):
    """Check metadata candle_counts matches actual file record count."""
    if symbol not in metadata:
        result.fail(f"{symbol}: not found in metadata")
        return
    sym_meta = metadata[symbol]
    counts = sym_meta.get("candle_counts", {})
    if candle_name not in counts:
        result.fail(
            f"{symbol}/{candle_name}: not found in metadata candle_counts"
        )
        return
    meta_count = counts[candle_name]
    if meta_count != record_count:
        result.fail(
            f"{symbol}/{candle_name}: metadata says {meta_count}, "
            f"file has {record_count} records"
        )
    else:
        result.ok(f"{symbol}/{candle_name}: metadata count matches")


def check_trade_continuity(candle_dir, result):
    """Check trade ID continuity between consecutive candles."""
    ftid, _ = read_bin(candle_dir / "first_trade_id.bin", "Q")
    ltid, _ = read_bin(candle_dir / "last_trade_id.bin", "Q")
    if ftid is None or ltid is None:
        result.fail(f"{candle_dir}: cannot read trade_id files")
        return

    n = len(ftid)
    if n == 0:
        return

    # Within each candle: first_trade_id <= last_trade_id
    for i in range(n):
        if ftid[i] > ltid[i]:
            result.fail(
                f"{candle_dir}[{i}]: first_trade_id {ftid[i]} > "
                f"last_trade_id {ltid[i]}"
            )
            return

    # Between consecutive candles
    for i in range(n - 1):
        if ltid[i] >= ftid[i + 1]:
            result.fail(
                f"{candle_dir}[{i}→{i+1}]: last_trade_id {ltid[i]} >= "
                f"next first_trade_id {ftid[i+1]}"
            )
            return
        gap = ftid[i + 1] - ltid[i]
        if gap > GAP_WARN_THRESHOLD:
            result.warn(
                f"{candle_dir}[{i}→{i+1}]: trade_id gap = {gap}"
            )

    result.ok(f"{candle_dir}: trade_id continuity OK")


def check_keys_monotonic(candle_dir, result):
    """Check that keys.bin values are strictly increasing."""
    keys, _ = read_bin(candle_dir / "keys.bin", "Q")
    if keys is None:
        result.fail(f"{candle_dir}: cannot read keys.bin")
        return

    for i in range(len(keys) - 1):
        if keys[i] >= keys[i + 1]:
            result.fail(
                f"{candle_dir}[{i}]: key {keys[i]} >= next {keys[i+1]}"
            )
            return

    result.ok(f"{candle_dir}: keys strictly increasing")


def check_ohlcv_integrity(candle_dir, result):
    """Check OHLCV basic constraints."""
    o, _ = read_bin(candle_dir / "open.bin", "d")
    h, _ = read_bin(candle_dir / "high.bin", "d")
    l, _ = read_bin(candle_dir / "low.bin", "d")
    c, _ = read_bin(candle_dir / "close.bin", "d")
    v, _ = read_bin(candle_dir / "volume.bin", "d")

    if any(x is None for x in [o, h, l, c, v]):
        result.fail(f"{candle_dir}: cannot read OHLCV files")
        return

    for i in range(len(o)):
        if h[i] < l[i]:
            result.fail(
                f"{candle_dir}[{i}]: high {h[i]} < low {l[i]}"
            )
            return
        if h[i] < o[i] or h[i] < c[i]:
            result.fail(
                f"{candle_dir}[{i}]: high {h[i]} < open {o[i]} "
                f"or close {c[i]}"
            )
            return
        if l[i] > o[i] or l[i] > c[i]:
            result.fail(
                f"{candle_dir}[{i}]: low {l[i]} > open {o[i]} "
                f"or close {c[i]}"
            )
            return
        if v[i] <= 0:
            result.fail(
                f"{candle_dir}[{i}]: volume {v[i]} <= 0"
            )
            return

    result.ok(f"{candle_dir}: OHLCV integrity OK")


def check_last_trade_id(symbol, metadata, output_dir,
                        candle_names, result):
    """Check metadata last_closed_trade_id vs last last_trade_id.bin value."""
    if symbol not in metadata:
        return
    meta_ltid = metadata[symbol].get("last_closed_trade_id", 0)
    if meta_ltid == 0:
        result.fail(f"{symbol}: last_closed_trade_id is 0")
        return

    # Find the maximum last_trade_id across all candle types
    max_ltid = 0
    for candle_name in candle_names:
        p = output_dir / symbol / candle_name / "last_trade_id.bin"
        if not p.exists():
            continue
        ltid, _ = read_bin(p, "Q")
        if ltid and len(ltid) > 0:
            max_ltid = max(max_ltid, ltid[-1])

    if max_ltid == 0:
        result.fail(f"{symbol}: no last_trade_id data found in files")
        return

    if meta_ltid != max_ltid:
        result.fail(
            f"{symbol}: metadata last_closed_trade_id={meta_ltid} "
            f"!= max file last_trade_id={max_ltid}"
        )
    else:
        result.ok(f"{symbol}: last_closed_trade_id matches files")


def main():
    parser = argparse.ArgumentParser(
        description="Validate tick-feeder-binance output"
    )
    parser.add_argument(
        "--output-dir", required=True,
        help="Path to the output directory"
    )
    parser.add_argument(
        "--metadata", required=True,
        help="Path to metadata.json"
    )
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    metadata_path = Path(args.metadata)

    if not output_dir.is_dir():
        print(f"Error: output dir not found: {output_dir}")
        sys.exit(1)

    metadata = {}
    if metadata_path.exists():
        with open(metadata_path) as f:
            metadata = json.load(f)
    else:
        print(f"Warning: metadata file not found: {metadata_path}")

    result = ValidationResult()

    # Discover symbols and candle types from directory structure
    symbol_dirs = sorted(
        [d for d in output_dir.iterdir() if d.is_dir()]
    )
    if not symbol_dirs:
        print("No symbol directories found.")
        sys.exit(0)

    for sym_dir in symbol_dirs:
        symbol = sym_dir.name
        print(f"\n=== {symbol} ===")
        candle_dirs = sorted(
            [d for d in sym_dir.iterdir() if d.is_dir()]
        )
        candle_names = [d.name for d in candle_dirs]

        for candle_dir in candle_dirs:
            candle_name = candle_dir.name
            print(f"\n--- {symbol}/{candle_name} ---")

            # 1. File size consistency
            count = check_file_sizes(candle_dir, result)
            if count < 0:
                continue

            # 2. Metadata counts
            if metadata:
                check_metadata_counts(
                    candle_dir, symbol, candle_name,
                    metadata, count, result
                )

            if count == 0:
                continue

            # 3. Trade ID continuity
            check_trade_continuity(candle_dir, result)

            # 4. Keys monotonic
            check_keys_monotonic(candle_dir, result)

            # 5. OHLCV integrity
            check_ohlcv_integrity(candle_dir, result)

        # Check per-symbol last_closed_trade_id
        if metadata:
            check_last_trade_id(
                symbol, metadata, output_dir, candle_names, result
            )

    # Summary
    print(f"\n{'=' * 50}")
    print(f"PASSED: {result.passed}")
    print(f"FAILED: {result.failed}")
    print(f"WARNINGS: {result.warnings}")
    if result.errors:
        print(f"\nFailures:")
        for e in result.errors:
            print(f"  {e}")
        sys.exit(1)
    else:
        print("\nAll checks passed.")


if __name__ == "__main__":
    main()
