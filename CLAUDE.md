# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**tick-feeder-binance** is a C++17 application that downloads Binance Futures historical aggregated trade data (`aggTrades`), feeds it through the **trcache** lock-free candle aggregation engine, and outputs column-oriented binary files for backtesting. It supports incremental resume via persistent metadata.

## Build Commands

```bash
# Build everything (trcache library + main binary)
make

# Build just the trcache library
make -C trcache

# Build trcache in debug mode
make -C trcache BUILD_MODE=debug

# Clean all artifacts
make clean

# Run (requires trcache shared library on LD_LIBRARY_PATH)
LD_LIBRARY_PATH=./trcache ./tick_feeder_binance -c config.json
```

**Dependencies:** `libcurl`, `pthread`, `unzip` (system utility). JSON parsing uses vendored `nlohmann/json.hpp` in `third_party/`.

## Architecture

### Data Flow

```
config.json → main.cpp orchestration
  → downloader: fetch monthly/daily ZIP archives from Binance Vision
  → decompressor: extract CSV via system unzip
  → csv_parser: parse trades, feed to trcache via trcache_feed_trade_data()
  → trcache engine: lock-free candle aggregation (tick-based or time-based)
  → output_writer: flush_cb writes column-oriented binary files via pwrite
  → metadata: save incremental state (last_processed_date, last_closed_trade_id)
  → funding: fetch funding rate history from Binance REST API
```

### Candle Slot Template System (engine.cpp)

The engine uses C++ template instantiation to generate per-slot callback functions (up to `MAX_CANDLE_SLOTS = 8`). Two candle strategies:
- **TICK_MODULO** — closes when `(trade_id + 1) % threshold == 0`
- **TIME_FIXED** — closes when timestamp crosses a fixed window boundary

Each slot's `init`/`update`/`close` callbacks are generated at compile time. The custom candle struct extends `trcache_candle_base` with OHLCV fields plus `last_trade_id` for resume tracking.

### Output Format

Binary column files per (symbol, candle_type):
```
output_dir/SYMBOL/CANDLE_NAME/{keys.bin, open.bin, high.bin, low.bin, close.bin, volume.bin}
```

### Incremental Resume

`metadata.json` tracks per-symbol state: `last_closed_trade_id`, `last_processed_date`, and per-candle-type counts. Metadata writes use temp file + atomic rename.

## Configuration (config.json)

Key fields: `symbols` (list), `candles` (name/type/threshold), `output_dir`, `temp_dir`, `metadata_path`, `temp_disk_limit_mb`, and `trcache` block (`memory_limit_mb`, `worker_threads`, `batch_size_pow2`, `cached_batch_count_pow2`).

## trcache Submodule

The `trcache/` directory is a git submodule containing a C library with its own build system and `CLAUDE.md`. See `trcache/CLAUDE.md` for its architecture details (lock-free concurrency, pipeline stages, storage layout). Key integration points: `trcache.h` public API, `libtrcache.a` static library.

## Testing

No unit test suite. Correctness is validated through:
- **trcache benchmarks** (`trcache/benchmark/`) — throughput stress tests
- **trcache validator** (`trcache/validator/`) — live Binance data integrity checks

## Code Style

- C++17 with `-O2 -Wall -Wextra`
- Follow trcache conventions: 80-char line limit, always use braces for control flow
