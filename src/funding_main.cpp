/*
 * funding_fetcher
 *
 * Standalone binary that fetches Binance Futures funding rate
 * history and writes to binary files. Supports incremental
 * updates.
 *
 * Usage: ./funding_fetcher -c config.json
 */

#include "config.h"
#include "funding.h"

#include <curl/curl.h>

#include <cstring>
#include <iostream>

static void usage(const char *prog)
{
	std::cerr << "Usage: " << prog
		  << " -c <config.json>\n";
}

int main(int argc, char *argv[])
{
	const char *config_path = nullptr;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-c") == 0 &&
		    i + 1 < argc) {
			config_path = argv[++i];
		}
	}

	if (!config_path) {
		usage(argv[0]);
		return 1;
	}

	feeder_config config;
	if (!config_load(config_path, config)) {
		return 1;
	}

	curl_global_init(CURL_GLOBAL_DEFAULT);

	for (const auto &sym : config.symbols) {
		funding_fetch(sym, config.output_dir);
	}

	curl_global_cleanup();

	std::cout << "Done.\n";
	return 0;
}
