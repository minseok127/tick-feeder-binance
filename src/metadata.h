#ifndef FEEDER_METADATA_H
#define FEEDER_METADATA_H

#include <map>
#include <string>
#include <cstdint>

struct symbol_metadata {
	uint64_t last_closed_trade_id;
	std::string last_processed_date;
	std::map<std::string, uint64_t> candle_counts;
};

/*
 * Per-symbol metadata for incremental updates.
 * Key: symbol string (e.g. "BTCUSDT")
 */
using metadata_map = std::map<std::string, symbol_metadata>;

/*
 * Load metadata from a JSON file.
 * Returns empty map if file doesn't exist (first run).
 */
metadata_map metadata_load(const std::string &path);

/*
 * Save metadata to a JSON file (atomic write via rename).
 */
bool metadata_save(const std::string &path,
	const metadata_map &meta);

#endif /* FEEDER_METADATA_H */
