#ifndef FEEDER_FUNDING_H
#define FEEDER_FUNDING_H

#include <string>

/*
 * Fetch funding rate history for a symbol from Binance
 * and append to binary files:
 *   output_dir/symbol/funding_time.bin  (uint64_t[])
 *   output_dir/symbol/funding_rate.bin  (double[])
 *
 * Supports incremental updates (reads existing file length
 * to determine last fetched timestamp).
 *
 * @return Number of new records fetched, or -1 on error.
 */
int funding_fetch(const std::string &symbol,
	const std::string &output_dir);

#endif /* FEEDER_FUNDING_H */
