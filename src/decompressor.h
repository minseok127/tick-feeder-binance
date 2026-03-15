#ifndef FEEDER_DECOMPRESSOR_H
#define FEEDER_DECOMPRESSOR_H

#include <string>

/*
 * Extract a zip file into the given directory.
 *
 * @param zip_path  Path to the zip file.
 * @param dest_dir  Directory to extract into.
 *
 * @return Path to the extracted CSV file, or empty string
 *         on failure.
 */
std::string unzip_file(const std::string &zip_path,
	const std::string &dest_dir);

#endif /* FEEDER_DECOMPRESSOR_H */
