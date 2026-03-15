#ifndef FEEDER_CSV_PARSER_H
#define FEEDER_CSV_PARSER_H

#include "trcache.h"
#include <cstdint>
#include <string>

/*
 * Parse a Binance aggTrades CSV file and feed trades to trcache.
 *
 * CSV columns (no header):
 *   agg_trade_id, price, quantity, first_trade_id,
 *   last_trade_id, timestamp, is_buyer_maker
 *
 * Rows with agg_trade_id <= skip_until_id are skipped
 * (for incremental resume).
 *
 * @param csv_path       Path to the CSV file.
 * @param cache          trcache handle.
 * @param symbol_id      Registered symbol ID.
 * @param skip_until_id  Skip trades with id <= this value.
 *                       Pass 0 to process all.
 * @param trades_fed     Output: number of trades fed.
 *
 * @return 0 on success, -1 on error.
 */
int csv_parse_and_feed(const std::string &csv_path,
	struct trcache *cache, int symbol_id,
	uint64_t skip_until_id, uint64_t *trades_fed);

#endif /* FEEDER_CSV_PARSER_H */
