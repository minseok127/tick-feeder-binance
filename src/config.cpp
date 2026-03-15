#include "config.h"
#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>

using json = nlohmann::json;

bool config_load(const char *path, feeder_config &cfg)
{
	std::ifstream ifs(path);
	if (!ifs.is_open()) {
		std::cerr << "[Config] Cannot open: " << path << "\n";
		return false;
	}

	json j;
	try {
		ifs >> j;
	} catch (const json::parse_error &e) {
		std::cerr << "[Config] Parse error: " << e.what() << "\n";
		return false;
	}

	/* Symbols */
	for (const auto &s : j.at("symbols")) {
		cfg.symbols.push_back(s.get<std::string>());
	}

	/* Candle configs */
	for (const auto &c : j.at("candles")) {
		candle_config cc;
		cc.name = c.at("name").get<std::string>();
		cc.type = c.at("type").get<std::string>();
		cc.threshold = c.at("threshold").get<int>();
		cfg.candles.push_back(cc);
	}

	/* Paths */
	cfg.output_dir = j.value("output_dir", "./output");
	cfg.temp_dir = j.value("temp_dir", "./tmp");
	cfg.metadata_path = j.value("metadata_path",
		"./metadata.json");
	cfg.temp_disk_limit_mb = j.value("temp_disk_limit_mb",
		25600);

	/* trcache settings */
	const auto &tc = j.at("trcache");
	cfg.memory_limit_mb = tc.value("memory_limit_mb", 2048);
	cfg.worker_threads = tc.value("worker_threads", 3);
	cfg.batch_size_pow2 = tc.value("batch_size_pow2", 12);
	cfg.cached_batch_count_pow2 =
		tc.value("cached_batch_count_pow2", 0);

	return true;
}
