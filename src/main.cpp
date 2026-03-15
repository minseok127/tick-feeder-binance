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

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

/*
 * Shutdown flag set by SIGINT/SIGTERM handler.
 * Checked in processing loops to allow graceful exit:
 * finish the current file, then proceed to engine teardown
 * and final metadata save.
 */
static volatile sig_atomic_t g_shutdown = 0;

static void shutdown_handler(int /*sig*/)
{
	g_shutdown = 1;
}

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
 * These correspond to when each symbol's aggTrades data
 * first became available on Binance Vision.
 * Unknown symbols default to 2020-01-01.
 */
static date default_start(const std::string &symbol)
{
	if (symbol == "BTCUSDT") return {2019, 9, 1};
	if (symbol == "ETHUSDT") return {2019, 11, 1};
	return {2020, 1, 1};
}

static std::string format_bytes(uint64_t bytes)
{
	char buf[64];
	if (bytes >= (uint64_t)1024 * 1024 * 1024) {
		snprintf(buf, sizeof(buf), "%.2f GB",
			bytes / (1024.0 * 1024.0 * 1024.0));
	} else if (bytes >= 1024 * 1024) {
		snprintf(buf, sizeof(buf), "%.2f MB",
			bytes / (1024.0 * 1024.0));
	} else if (bytes >= 1024) {
		snprintf(buf, sizeof(buf), "%.2f KB",
			bytes / 1024.0);
	} else {
		snprintf(buf, sizeof(buf), "%lu B",
			(unsigned long)bytes);
	}
	return buf;
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
 * Strategy: iterate month by month from start to today.
 *   - For completed months: try monthly ZIP first (one large file).
 *   - For the current month (or if monthly is unavailable):
 *     fall back to daily ZIPs.
 *
 * Saves last_processed_date as a checkpoint after each month so
 * that a crash-restart skips already-downloaded data. The accurate
 * last_closed_trade_id and candle_counts are saved later, after
 * engine_destroy has flushed all pending batches.
 */
static int process_symbol(const feeder_config &config,
	struct trcache *cache, int symbol_id,
	const std::string &symbol,
	metadata_map &meta)
{
	symbol_metadata &sm = meta[symbol];

	/*
	 * Resume from last_processed_date if available,
	 * otherwise start from the symbol's default start date.
	 */
	date start;
	if (!sm.last_processed_date.empty()) {
		start = parse_date(sm.last_processed_date);
	} else {
		start = default_start(symbol);
	}

	date end = today();
	/*
	 * skip_id: trades with id <= this value are skipped.
	 * Loaded once from metadata and not updated during
	 * processing — the accurate value is saved after
	 * engine_destroy when all batches have been flushed.
	 */
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

	while (!g_shutdown &&
	       (cur_year < end.year ||
	        (cur_year == end.year &&
		 cur_month <= end.month))) {
		bool is_current_month =
			(cur_year == end.year &&
			 cur_month == end.month);

		if (!is_current_month) {
			/*
			 * Try monthly download first — one file
			 * per month is faster than 28-31 daily files.
			 * If the monthly ZIP returns non-200 (not yet
			 * published), fall through to daily downloads.
			 */
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

				/*
				 * Save last_processed_date as a
				 * checkpoint so we can skip this
				 * month on restart after a crash.
				 */
				int last_day = days_in_month(
					cur_year, cur_month);
				date d = {cur_year, cur_month,
					last_day};
				sm.last_processed_date =
					format_date(d);
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
			/*
			 * Daily downloads: used for the current
			 * month or when the monthly archive is not
			 * yet available on Binance Vision.
			 */
			int start_day = 1;
			if (cur_year == start.year &&
			    cur_month == start.month &&
			    start.day > 1) {
				start_day = start.day;
			}

			/*
			 * Stop at yesterday (end.day - 1) for the
			 * current month since today's data is
			 * still accumulating.
			 */
			int last_day;
			if (cur_year == end.year &&
			    cur_month == end.month) {
				last_day = end.day - 1;
			} else {
				last_day = days_in_month(
					cur_year, cur_month);
			}

			int last_done_day = start_day - 1;
			for (int d = start_day;
			     d <= last_day && !g_shutdown; d++) {
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

				printf("  [Daily] %04d-%02d-%02d\n",
					cur_year, cur_month, d);
				int http = download_file(
					url, zip_path);
				if (http != 200) {
					printf("    Not available "
						"(HTTP %d)\n", http);
					continue;
				}

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
				last_done_day = d;
			}

			/*
			 * Save last_processed_date checkpoint.
			 * Use the actual last day processed, not
			 * last_day, in case of early exit via
			 * shutdown.
			 */
			if (last_done_day >= start_day) {
				date last = {cur_year, cur_month,
					last_done_day};
				sm.last_processed_date =
					format_date(last);
				metadata_save(
					config.metadata_path, meta);
			}
		}

		done_months++;

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

	/* Register signal handlers for graceful shutdown */
	signal(SIGINT, shutdown_handler);
	signal(SIGTERM, shutdown_handler);

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

	/* Show current progress per symbol */
	for (const auto &sym : config.symbols) {
		auto it = meta.find(sym);
		if (it != meta.end() &&
		    !it->second.last_processed_date.empty()) {
			date s = default_start(sym);
			printf("[%s] Already processed: %s ~ %s"
				" (last_trade_id: %lu)\n",
				sym.c_str(),
				format_date(s).c_str(),
				it->second.last_processed_date
					.c_str(),
				(unsigned long)
					it->second
					.last_closed_trade_id);
			for (const auto &[name, cnt] :
			     it->second.candle_counts) {
				printf("  [%s] %lu candles\n",
					name.c_str(),
					(unsigned long)cnt);
			}
		} else {
			printf("[%s] No previous data\n",
				sym.c_str());
		}
	}

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
	for (size_t i = 0;
	     i < config.symbols.size() && !g_shutdown; i++) {
		process_symbol(config, cache, symbol_ids[i],
			config.symbols[i], meta);
	}

	if (g_shutdown) {
		printf("\nShutdown requested, finishing up...\n");
	}

	/*
	 * 7. Destroy engine.
	 * trcache_destroy flushes all remaining candle batches
	 * (including partially filled ones) via the flush
	 * callback before freeing resources.
	 */
	engine_destroy(cache);

	/*
	 * 8. Final metadata save.
	 * Now that engine_destroy has flushed all pending batches,
	 * output_writer's last_trade_id values are fully accurate.
	 * Save metadata before destroying the writer.
	 */
	for (size_t i = 0; i < config.symbols.size(); i++) {
		const std::string &sym = config.symbols[i];
		int sid = symbol_ids[i];
		symbol_metadata &sm = meta[sym];

		uint64_t ltid =
			output_writer_get_last_trade_id(
				writer, sid);
		if (ltid > sm.last_closed_trade_id) {
			sm.last_closed_trade_id = ltid;
		}

		for (int ci = 0;
		     ci < (int)config.candles.size(); ci++) {
			sm.candle_counts[
				config.candles[ci].name] =
				output_writer_get_candle_count(
					writer, sid, ci);
		}
	}
	metadata_save(config.metadata_path, meta);

	/* Report total output size */
	uint64_t total_bytes =
		output_writer_get_total_bytes(writer, -1);
	printf("Total output size: %s\n",
		format_bytes(total_bytes).c_str());

	output_writer_destroy(writer);

	/* 9. Fetch funding rates (skip on shutdown) */
	if (!g_shutdown) {
		curl_global_init(CURL_GLOBAL_DEFAULT);
		for (const auto &sym : config.symbols) {
			funding_fetch(sym, config.output_dir);
		}
		curl_global_cleanup();
	}

	/* 10. Clean temp dir */
	clean_temp(config.temp_dir);

	std::cout << "Done.\n";
	return 0;
}
