#!/bin/bash
set -e

cd "$(dirname "$0")"

make -j$(nproc)

LD_LIBRARY_PATH=./trcache ./tick_feeder_binance -c "${1:-config.json}"
