#ifndef FEEDER_DOWNLOADER_H
#define FEEDER_DOWNLOADER_H

#include <string>

/*
 * Download a file from a URL to a local path.
 *
 * @return HTTP status code (200 on success), or -1 on error.
 */
int download_file(const std::string &url,
	const std::string &dest_path);

/*
 * Build the monthly aggTrades URL for a symbol.
 */
std::string make_monthly_url(const std::string &symbol,
	int year, int month);

/*
 * Build the daily aggTrades URL for a symbol.
 */
std::string make_daily_url(const std::string &symbol,
	int year, int month, int day);

/*
 * Get total size (in bytes) of all files in a directory.
 */
size_t get_dir_size(const std::string &dir);

#endif /* FEEDER_DOWNLOADER_H */
