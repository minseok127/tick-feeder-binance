#include "decompressor.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

std::string unzip_file(const std::string &zip_path,
	const std::string &dest_dir)
{
	mkdir(dest_dir.c_str(), 0755);

	char cmd[2048];
	snprintf(cmd, sizeof(cmd),
		"unzip -o -q \"%s\" -d \"%s\" 2>&1",
		zip_path.c_str(), dest_dir.c_str());

	int ret = system(cmd);
	if (ret != 0) {
		std::cerr << "[Decompress] unzip failed: "
			  << zip_path << " (exit " << ret << ")\n";
		return "";
	}

	/*
	 * Find the extracted CSV file. Each Binance aggTrades ZIP
	 * contains exactly one CSV file named like:
	 * BTCUSDT-aggTrades-2024-01.csv
	 */
	DIR *d = opendir(dest_dir.c_str());
	if (!d) {
		return "";
	}

	std::string csv_path;
	struct dirent *ent;
	while ((ent = readdir(d)) != nullptr) {
		const char *name = ent->d_name;
		size_t len = strlen(name);
		if (len > 4 &&
		    strcmp(name + len - 4, ".csv") == 0) {
			csv_path = dest_dir + "/" + name;
			break;
		}
	}
	closedir(d);

	if (csv_path.empty()) {
		std::cerr << "[Decompress] No CSV found in: "
			  << dest_dir << "\n";
	}
	return csv_path;
}
