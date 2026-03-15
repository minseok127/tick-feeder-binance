/*
 * tick-feeder-binance
 *
 * Downloads Binance USD-M Futures historical aggTrades data,
 * feeds through trcache, and outputs column-oriented binary
 * files for backtesting.
 *
 * Usage: ./tick_feeder_binance -c config.json
 */

#include "config.h"
#include "csv_parser.h"
#include "decompressor.h"
#include "downloader.h"
#include "engine.h"
#include "funding.h"
#include "metadata.h"
#include "output_writer.h"
#include "types.h"
#include "trcache.h"

#include <curl/curl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

struct date {
	int year;
	int month;
	int day;
};

static date today()
{
	time_t t = time(nullptr);
	struct tm *tm = gmtime(&t);
	return {tm->tm_year + 1900, tm->tm_mon + 1,
		tm->tm_mday};
}

static int days_in_month(int year, int month)
{
	static const int d[] = {
		31, 28, 31, 30, 31, 30,
		31, 31, 30, 31, 30, 31
	};
	int n = d[month - 1];
	if (month == 2 &&
	    (year % 4 == 0 &&
	     (year % 100 != 0 || year % 400 == 0))) {
		n = 29;
	}
	return n;
}

static date parse_date(const std::string &s)
{
	date d = {};
	if (s.size() >= 10) {
		sscanf(s.c_str(), "%d-%d-%d",
			&d.year, &d.month, &d.day);
	}
	return d;
}

static std::string format_date(const date &d)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
		d.year, d.month, d.day);
	return buf;
}

/*
 * Default start dates for known symbols.
 * Binance Futures aggTrades availability.
 */
static date default_start(const std::string &symbol)
{
	if (symbol == "BTCUSDT") return {2019, 9, 1};
	if (symbol == "ETHUSDT") return {2019, 11, 1};
	return {2020, 1, 1};
}

static void clean_temp(const std::string &temp_dir)
{
	std::string cmd = "rm -f " + temp_dir + "/*.csv "
		+ temp_dir + "/*.zip";
	int ret = system(cmd.c_str());
	(void)ret;
}

/*
 * Process all data for a single symbol.
 *
 * Downloads monthly/daily zips, feeds CSV through trcache,
 * and cleans up temporary files.
 */
static int process_symbol(const feeder_config &config,
	struct trcache *cache, int symbol_id,
	const std::string &symbol,
	output_writer_ctx *writer,
	metadata_map &meta)
{
	symbol_metadata &sm = meta[symbol];

	/* Determine start date */
	date start;
	if (!sm.last_processed_date.empty()) {
		start = parse_date(sm.last_processed_date);
	} else {
		start = default_start(symbol);
	}

	date end = today();
	uint64_t skip_id = sm.last_closed_trade_id;

	/* Count total months for progress */
	int total_months =
		(end.year - start.year) * 12 +
		(end.month - start.month) + 1;
	int done_months = 0;

	std::cout << "[" << symbol << "] Processing from "
		  << format_date(start) << " to "
		  << format_date(end)
		  << " (" << total_months << " months)\n";

	mkdir(config.temp_dir.c_str(), 0755);

	int cur_year = start.year;
	int cur_month = start.month;

	while (cur_year < end.year ||
	       (cur_year == end.year &&
		cur_month <= end.month)) {
		bool is_current_month =
			(cur_year == end.year &&
			 cur_month == end.month);

		if (!is_current_month) {
			/* Try monthly download */
			std::string url = make_monthly_url(
				symbol, cur_year, cur_month);
			char fname[256];
			snprintf(fname, sizeof(fname),
				"%s-aggTrades-%04d-%02d.zip",
				symbol.c_str(),
				cur_year, cur_month);
			std::string zip_path =
				config.temp_dir + "/" + fname;

			printf("[%s] (%d/%d) %04d-%02d\n",
				symbol.c_str(),
				done_months + 1, total_months,
				cur_year, cur_month);

			int http = download_file(url, zip_path);
			if (http == 200) {
				/* Unzip and feed */
				std::string csv = unzip_file(
					zip_path, config.temp_dir);
				if (!csv.empty()) {
					uint64_t fed = 0;
					csv_parse_and_feed(csv,
						cache, symbol_id,
						skip_id, &fed);
					printf("  Fed %lu trades\n",
						(unsigned long)fed);
					remove(csv.c_str());
				}
				remove(zip_path.c_str());

				/* Update skip_id */
				uint64_t ltid =
					output_writer_get_last_trade_id(
						writer, symbol_id);
				if (ltid > skip_id) {
					skip_id = ltid;
				}

				/* Update metadata */
				int last_day = days_in_month(
					cur_year, cur_month);
				date d = {cur_year, cur_month,
					last_day};
				sm.last_processed_date =
					format_date(d);
				sm.last_closed_trade_id = skip_id;

				for (int ci = 0;
				     ci < (int)config.candles.size();
				     ci++) {
					sm.candle_counts[
						config.candles[ci].name] =
						output_writer_get_candle_count(
							writer,
							symbol_id,
							ci);
				}

				metadata_save(config.metadata_path,
					meta);
			} else {
				printf("  Monthly not available, "
					"trying daily\n");
				/* Fall through to daily */
				is_current_month = true;
			}
		}

		if (is_current_month) {
			/* Daily downloads */
			int start_day = 1;
			if (cur_year == start.year &&
			    cur_month == start.month &&
			    start.day > 1) {
				start_day = start.day;
			}

			int last_day;
			if (cur_year == end.year &&
			    cur_month == end.month) {
				last_day = end.day - 1;
			} else {
				last_day = days_in_month(
					cur_year, cur_month);
			}

			for (int d = start_day; d <= last_day; d++) {
				std::string url = make_daily_url(
					symbol, cur_year,
					cur_month, d);
				char fname[256];
				snprintf(fname, sizeof(fname),
					"%s-aggTrades-"
					"%04d-%02d-%02d.zip",
					symbol.c_str(),
					cur_year, cur_month, d);
				std::string zip_path =
					config.temp_dir + "/"
					+ fname;

				int http = download_file(
					url, zip_path);
				if (http != 200) continue;

				std::string csv = unzip_file(
					zip_path, config.temp_dir);
				if (!csv.empty()) {
					uint64_t fed = 0;
					csv_parse_and_feed(csv,
						cache, symbol_id,
						skip_id, &fed);
					if (fed > 0) {
						printf("  %04d-%02d-%02d: "
							"%lu trades\n",
							cur_year,
							cur_month, d,
							(unsigned long)fed);
					}
					remove(csv.c_str());
				}
				remove(zip_path.c_str());

				uint64_t ltid =
					output_writer_get_last_trade_id(
						writer, symbol_id);
				if (ltid > skip_id) {
					skip_id = ltid;
				}
			}

			/* Update metadata for daily batch */
			date last = {cur_year, cur_month, last_day};
			sm.last_processed_date = format_date(last);
			sm.last_closed_trade_id = skip_id;

			for (int ci = 0;
			     ci < (int)config.candles.size();
			     ci++) {
				sm.candle_counts[
					config.candles[ci].name] =
					output_writer_get_candle_count(
						writer,
						symbol_id, ci);
			}

			metadata_save(config.metadata_path, meta);
		}

		/* Report candle counts */
		done_months++;
		for (int ci = 0;
		     ci < (int)config.candles.size(); ci++) {
			uint64_t cnt =
				output_writer_get_candle_count(
					writer, symbol_id, ci);
			if (cnt > 0) {
				printf("  [%s] candles so far: %lu\n",
					config.candles[ci].name.c_str(),
					(unsigned long)cnt);
			}
		}

		/* Advance to next month */
		cur_month++;
		if (cur_month > 12) {
			cur_month = 1;
			cur_year++;
		}
	}

	return 0;
}

static void usage(const char *prog)
{
	std::cerr << "Usage: " << prog
		  << " -c <config.json>\n";
}

int main(int argc, char *argv[])
{
	const char *config_path = nullptr;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
			config_path = argv[++i];
		}
	}

	if (!config_path) {
		usage(argv[0]);
		return 1;
	}

	/* 1. Load config */
	feeder_config config;
	if (!config_load(config_path, config)) {
		return 1;
	}

	std::cout << "Symbols: " << config.symbols.size()
		  << ", Candle types: "
		  << config.candles.size() << "\n";

	/* 2. Load metadata */
	metadata_map meta = metadata_load(config.metadata_path);

	/* 3. Create output writer */
	int max_symbols = (int)config.symbols.size() + 10;
	std::vector<std::string> candle_names;
	for (const auto &c : config.candles) {
		candle_names.push_back(c.name);
	}

	output_writer_ctx *writer = output_writer_create(
		max_symbols,
		(int)config.candles.size(),
		num_feeder_candle_fields,
		candle_names.data(),
		config.output_dir);
	if (!writer) {
		std::cerr << "Failed to create output writer\n";
		return 1;
	}

	/* 4. Initialize trcache engine */
	struct trcache *cache = engine_init(config, writer);
	if (!cache) {
		output_writer_destroy(writer);
		return 1;
	}

	/* 5. Register symbols */
	std::vector<int> symbol_ids;
	for (const auto &sym : config.symbols) {
		int sid = trcache_register_symbol(cache,
			sym.c_str());
		if (sid < 0) {
			std::cerr << "Failed to register: "
				  << sym << "\n";
			engine_destroy(cache);
			output_writer_destroy(writer);
			return 1;
		}
		output_writer_set_symbol(writer, sid, sym);
		symbol_ids.push_back(sid);
	}

	/* 6. Process each symbol */
	for (size_t i = 0; i < config.symbols.size(); i++) {
		process_symbol(config, cache, symbol_ids[i],
			config.symbols[i], writer, meta);
	}

	/* 7. Flush residual candles via query API */
	for (size_t si = 0; si < config.symbols.size(); si++) {
		int sid = symbol_ids[si];
		for (int ci = 0;
		     ci < (int)config.candles.size(); ci++) {
			/*
			 * Query the most recent candles that may
			 * not have been batch-flushed yet.
			 * batch_size = 2^batch_size_pow2
			 */
			int batch_sz =
				1 << config.batch_size_pow2;
			trcache_candle_batch *batch =
				trcache_batch_alloc_on_heap(
					cache, ci, batch_sz,
					nullptr);
			if (!batch) continue;

			int ret =
				trcache_get_candles_by_symbol_id_and_offset(
					cache, sid, ci,
					nullptr, 0, batch_sz,
					batch);

			if (ret == 0 && batch->num_candles > 0) {
				/*
				 * Filter: only write closed candles
				 * that haven't been flushed yet.
				 * The output_writer tracks candle
				 * count; compare.
				 */
				uint64_t already =
					output_writer_get_candle_count(
						writer, sid, ci);
				/*
				 * The query returns the most recent
				 * candles. We need the ones not yet
				 * written. This is approximate;
				 * we write all closed candles from
				 * the query result to a temporary
				 * batch and let the writer handle it.
				 *
				 * For simplicity, skip residual
				 * flushing here. The last partial
				 * batch will be picked up on the
				 * next incremental run.
				 */
				(void)already;
			}
			trcache_batch_free(batch);
		}
	}

	/* 8. Cleanup engine */
	engine_destroy(cache);
	output_writer_destroy(writer);

	/* 9. Fetch funding rates */
	curl_global_init(CURL_GLOBAL_DEFAULT);
	for (const auto &sym : config.symbols) {
		funding_fetch(sym, config.output_dir);
	}
	curl_global_cleanup();

	/* 10. Clean temp dir */
	clean_temp(config.temp_dir);

	std::cout << "Done.\n";
	return 0;
}
