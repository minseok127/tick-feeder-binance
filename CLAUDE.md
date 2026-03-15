# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**tick-feeder-binance** is a C++17 application that downloads Binance Futures historical aggregated trade data (`aggTrades`), feeds it through the **trcache** lock-free candle aggregation engine, and outputs column-oriented binary files for backtesting. It supports incremental resume via persistent metadata.

## Build Commands

```bash
# Build and run (preferred)
./run.sh                          # builds with make -j, runs with default config.json
./run.sh path/to/config.json      # builds and runs with custom config

# Build everything (trcache library + main binary)
make

# Build just the trcache library
make -C trcache

# Build trcache in debug mode
make -C trcache BUILD_MODE=debug

# Clean all artifacts
make clean

# Run manually (requires trcache shared library on LD_LIBRARY_PATH)
LD_LIBRARY_PATH=./trcache ./tick_feeder_binance -c config.json

# Reset metadata to re-download from scratch (backs up existing metadata)
./reset_metadata.sh
```

**Dependencies:** `libcurl`, `pthread`, `unzip` (system utility). JSON parsing uses vendored `nlohmann/json.hpp` in `third_party/`.

## Architecture

### Data Flow

```
config.json → main.cpp orchestration (10-step pipeline)
  → downloader: fetch daily ZIP archives from Binance Vision (libcurl)
  → decompressor: extract CSV via system unzip
  → csv_parser: parse trades, feed to trcache via trcache_feed_trade_data()
  → trcache engine: lock-free candle aggregation (tick-based or time-based)
  → output_writer: flush_cb writes column-oriented binary files via pwrite
  → metadata: save incremental state (last_processed_date, last_closed_trade_id)
  → funding: fetch funding rate history from Binance REST API
```

### Candle Slot Template System (engine.cpp)

The engine uses C++ template instantiation to generate per-slot callback functions (up to `MAX_CANDLE_SLOTS = 8`). This avoids runtime parameter passing through trcache's opaque callback interface — each slot's threshold is stored in a global array `g_slot_configs[slot]` and read by its template instantiation.

Two candle strategies:
- **TICK_MODULO** — closes when `(trade_id + 1) % threshold == 0`. Implements gap detection: if a trade arrives beyond the current window, the candle is force-closed without consuming the trade.
- **TIME_FIXED** — closes when timestamp crosses a fixed window boundary.

The `update_callback` returns `true` if the trade was applied, `false` if the trade belongs to the next candle (triggering close).

### Custom Candle Struct (types.h)

`feeder_candle` extends `trcache_candle_base` with OHLCV fields plus `first_trade_id` and `last_trade_id`. Field definitions use offset-based metadata for SoA column extraction. Output includes 8 binary columns: keys + 5 OHLCV + 2 trade IDs.

### Output Format

Binary column files per (symbol, candle_type), written via `pwrite()` with tracked offsets (supports resume without re-reading file positions):
```
output_dir/SYMBOL/CANDLE_NAME/{keys.bin, open.bin, high.bin, low.bin, close.bin, volume.bin, first_trade_id.bin, last_trade_id.bin}
```

### Incremental Resume

`metadata.json` tracks per-symbol state: `last_closed_trade_id`, `last_processed_date`, and per-candle-type counts. Metadata writes use temp file + atomic rename.

**Split metadata save strategy:** `last_processed_date` is saved after each month (download checkpoint), while `last_closed_trade_id` is saved only after `trcache_destroy()` to ensure all async batches are flushed first.

### Graceful Shutdown

A global `g_shutdown` flag (set by SIGINT/SIGTERM) lets the current file finish processing, then proceeds to engine teardown and final metadata save.

### Daily-Only Download Strategy

Despite coding support for monthly archive URLs, only daily archives are used because Binance monthly ZIPs can have missing dates within the file.

## Configuration (config.json)

Key fields: `symbols` (list), `candles` (name/type/threshold), `output_dir`, `temp_dir`, `metadata_path`, `temp_disk_limit_mb`, and `trcache` block (`memory_limit_mb`, `worker_threads`, `batch_size_pow2`, `cached_batch_count_pow2`).

- `threshold` in candle config: milliseconds for TIME_FIXED, trade count for TICK_MODULO
- `batch_size_pow2`: log2 of candles per column batch (e.g., 12 = 4096 candles)
- `cached_batch_count_pow2`: log2 of batches to keep before flush (0 = flush immediately)

## trcache Submodule

The `trcache/` directory is a git submodule containing a C library with its own build system and `CLAUDE.md`. See `trcache/CLAUDE.md` for its architecture details (lock-free concurrency, pipeline stages, storage layout). Key integration points: `trcache.h` public API, `libtrcache.a` static library.

## Testing

No unit test suite. Correctness is validated through:
- **trcache benchmarks** (`trcache/benchmark/`) — throughput stress tests
- **trcache validator** (`trcache/validator/`) — live Binance data integrity checks
- **validate_output.py** — output validation script

## Code Style

- C++17 with `-O2 -Wall -Wextra`
- Follow trcache conventions: 80-char line limit, always use braces for control flow
