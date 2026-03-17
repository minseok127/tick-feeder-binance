#!/bin/bash
set -e

cd "$(dirname "$0")"

make -j$(nproc)

./funding_fetcher -c "${1:-config.json}"
