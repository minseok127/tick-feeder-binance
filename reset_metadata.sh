#!/usr/bin/env bash
#
# Reset metadata.json so tick_feeder_binance re-downloads from scratch.
# Reads config.json to build the initial metadata structure.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG="${1:-${SCRIPT_DIR}/config.json}"
META_PATH=$(python3 -c "
import json, sys
cfg = json.load(open('$CONFIG'))
print(cfg.get('metadata_path', './metadata.json'))
")

# Resolve relative path against config location
if [[ "$META_PATH" != /* ]]; then
	META_PATH="${SCRIPT_DIR}/${META_PATH#./}"
fi

if [[ -f "$META_PATH" ]]; then
	echo "Backing up existing metadata to ${META_PATH}.bak"
	cp "$META_PATH" "${META_PATH}.bak"
fi

python3 -c "
import json, sys

cfg = json.load(open('$CONFIG'))
symbols = cfg.get('symbols', [])
candles = cfg.get('candles', [])

meta = {}
for sym in symbols:
    meta[sym] = {
        'last_closed_trade_id': 0,
        'last_processed_date': '',
        'candle_counts': {c['name']: 0 for c in candles}
    }

with open('$META_PATH', 'w') as f:
    json.dump(meta, f, indent=2)
    f.write('\n')

print('Metadata reset: ' + '$META_PATH')
for sym in symbols:
    print(f'  {sym}: {list(meta[sym][\"candle_counts\"].keys())}')
"
