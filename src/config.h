#ifndef FEEDER_CONFIG_H
#define FEEDER_CONFIG_H

#include <string>
#include <vector>

struct candle_config {
	std::string name;
	std::string type;
	int threshold;
};

struct feeder_config {
	std::vector<std::string> symbols;
	std::vector<candle_config> candles;

	std::string output_dir;
	std::string temp_dir;
	std::string metadata_path;
	int temp_disk_limit_mb;

	int memory_limit_mb;
	int worker_threads;
	int batch_size_pow2;
	int cached_batch_count_pow2;
};

/*
 * Load configuration from a JSON file.
 * Returns true on success, false on error (prints to stderr).
 */
bool config_load(const char *path, feeder_config &cfg);

#endif /* FEEDER_CONFIG_H */
