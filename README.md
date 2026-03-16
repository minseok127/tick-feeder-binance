# tick-feeder-binance

Downloads Binance Futures historical aggTrades and outputs column-oriented binary candle files for backtesting.

## Prerequisites

- GCC with C++17 support
- libcurl
- unzip

## Build & Run

```bash
./run.sh                        # build and run with default config.json
./run.sh path/to/config.json    # build and run with custom config
```

## Configuration

Edit `config.json`:

```json
{
  "symbols": ["BTCUSDT", "ETHUSDT"],
  "candles": [
    {"name": "10sec", "type": "TIME_FIXED", "threshold": 10000},
    {"name": "5tick", "type": "TICK_MODULO", "threshold": 5}
  ],
  "output_dir": "./output",
  "temp_dir": "./tmp",
  "metadata_path": "./metadata.json",
  "temp_disk_limit_mb": 25600,
  "trcache": {
    "memory_limit_mb": 2048,
    "worker_threads": 3,
    "batch_size_pow2": 12,
    "cached_batch_count_pow2": 0
  }
}
```

- `TIME_FIXED` threshold: milliseconds (e.g., 10000 = 10s)
- `TICK_MODULO` threshold: trade count

## Output

Binary column files are written to:

```
output_dir/SYMBOL/CANDLE_NAME/{keys,open,high,low,close,volume,first_trade_id,last_trade_id}.bin
```

## Validate Output

```bash
python3 validate_output.py --output-dir ./output --metadata ./metadata.json --config ./config.json
```

Checks file size consistency, metadata match, trade ID continuity, key monotonicity, and OHLCV integrity.

## Resume & Reset

The process resumes automatically from where it left off using `metadata.json`.

To re-download from scratch:

```bash
./reset_metadata.sh
```
