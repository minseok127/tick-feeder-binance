#include "metadata.h"
#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

metadata_map metadata_load(const std::string &path)
{
	metadata_map meta;

	std::ifstream ifs(path);
	if (!ifs.is_open()) {
		return meta;
	}

	json j;
	try {
		ifs >> j;
	} catch (const json::parse_error &e) {
		std::cerr << "[Metadata] Parse error: "
			  << e.what() << "\n";
		return meta;
	}

	for (auto it = j.begin(); it != j.end(); ++it) {
		symbol_metadata sm;
		const auto &v = it.value();
		sm.last_closed_trade_id =
			v.value("last_closed_trade_id",
				(uint64_t)0);
		sm.last_processed_date =
			v.value("last_processed_date",
				std::string(""));

		if (v.contains("candle_counts")) {
			for (auto &cc : v["candle_counts"].items()) {
				sm.candle_counts[cc.key()] =
					cc.value().get<uint64_t>();
			}
		}
		meta[it.key()] = sm;
	}
	return meta;
}

bool metadata_save(const std::string &path,
	const metadata_map &meta)
{
	json j;
	for (const auto &[sym, sm] : meta) {
		json entry;
		entry["last_closed_trade_id"] =
			sm.last_closed_trade_id;
		entry["last_processed_date"] =
			sm.last_processed_date;

		json counts;
		for (const auto &[name, cnt] : sm.candle_counts) {
			counts[name] = cnt;
		}
		entry["candle_counts"] = counts;
		j[sym] = entry;
	}

	/* Atomic write: write to tmp, then rename */
	std::string tmp_path = path + ".tmp";
	std::ofstream ofs(tmp_path);
	if (!ofs.is_open()) {
		std::cerr << "[Metadata] Cannot write: "
			  << tmp_path << "\n";
		return false;
	}

	ofs << j.dump(2) << "\n";
	ofs.close();

	if (rename(tmp_path.c_str(), path.c_str()) != 0) {
		std::cerr << "[Metadata] rename failed\n";
		return false;
	}
	return true;
}
