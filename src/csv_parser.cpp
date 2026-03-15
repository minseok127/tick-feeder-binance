#include "csv_parser.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

/*
 * Fast comma-delimited field extraction.
 * Returns pointer to next field (after comma) or NULL.
 */
static inline const char *next_field(const char *p)
{
	while (*p && *p != ',') p++;
	return (*p == ',') ? p + 1 : nullptr;
}

int csv_parse_and_feed(const std::string &csv_path,
	struct trcache *cache, int symbol_id,
	uint64_t skip_until_id, uint64_t *trades_fed)
{
	FILE *fp = fopen(csv_path.c_str(), "r");
	if (!fp) {
		std::cerr << "[CSV] Cannot open: " << csv_path
			  << "\n";
		return -1;
	}

	char line[512];
	uint64_t fed = 0;
	trcache_trade_data td;

	while (fgets(line, sizeof(line), fp)) {
		/* Column 0: agg_trade_id */
		const char *p = line;
		uint64_t trade_id = strtoull(p, nullptr, 10);

		if (trade_id <= skip_until_id) {
			continue;
		}

		/* Column 1: price */
		p = next_field(p);
		if (!p) continue;
		double price = strtod(p, nullptr);

		/* Column 2: quantity */
		p = next_field(p);
		if (!p) continue;
		double quantity = strtod(p, nullptr);

		/* Column 3: first_trade_id (skip) */
		p = next_field(p);
		if (!p) continue;

		/* Column 4: last_trade_id (skip) */
		p = next_field(p);
		if (!p) continue;

		/* Column 5: timestamp */
		p = next_field(p);
		if (!p) continue;
		uint64_t timestamp = strtoull(p, nullptr, 10);

		/* Column 6: is_buyer_maker (skip) */

		td.timestamp = timestamp;
		td.trade_id = trade_id;
		td.price.as_double = price;
		td.volume.as_double = quantity;

		int ret = trcache_feed_trade_data(cache, &td,
			symbol_id);
		if (ret < 0) {
			std::cerr << "[CSV] feed failed at trade_id="
				  << trade_id << "\n";
			fclose(fp);
			if (trades_fed) *trades_fed = fed;
			return -1;
		}
		fed++;
	}

	fclose(fp);
	if (trades_fed) *trades_fed = fed;
	return 0;
}
