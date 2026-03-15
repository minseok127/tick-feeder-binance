#include "funding.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using json = nlohmann::json;

static size_t string_write_cb(void *ptr, size_t size,
	size_t nmemb, void *userdata)
{
	auto *s = (std::string *)userdata;
	s->append((char *)ptr, size * nmemb);
	return size * nmemb;
}

static std::string http_get(const std::string &url)
{
	std::string body;
	CURL *curl = curl_easy_init();
	if (!curl) return body;

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		string_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		std::cerr << "[Funding] HTTP error: "
			  << curl_easy_strerror(res) << "\n";
		return "";
	}
	return body;
}

static void mkdirs(const std::string &path)
{
	std::string tmp;
	for (size_t i = 0; i < path.size(); i++) {
		tmp += path[i];
		if (path[i] == '/' || i == path.size() - 1) {
			mkdir(tmp.c_str(), 0755);
		}
	}
}

int funding_fetch(const std::string &symbol,
	const std::string &output_dir)
{
	std::string dir = output_dir + "/" + symbol;
	mkdirs(dir);

	std::string time_path = dir + "/funding_time.bin";
	std::string rate_path = dir + "/funding_rate.bin";

	/* Determine start time from existing data */
	uint64_t start_time = 0;
	{
		struct stat st;
		if (stat(time_path.c_str(), &st) == 0 &&
		    st.st_size > 0) {
			int fd = open(time_path.c_str(), O_RDONLY);
			if (fd >= 0) {
				uint64_t last_ts;
				off_t off = st.st_size -
					(off_t)sizeof(uint64_t);
				if (pread(fd, &last_ts,
				    sizeof(uint64_t), off) ==
				    (ssize_t)sizeof(uint64_t)) {
					start_time = last_ts + 1;
				}
				close(fd);
			}
		}
	}

	int time_fd = open(time_path.c_str(),
		O_WRONLY | O_CREAT | O_APPEND, 0644);
	int rate_fd = open(rate_path.c_str(),
		O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (time_fd < 0 || rate_fd < 0) {
		std::cerr << "[Funding] Cannot open output files\n";
		if (time_fd >= 0) close(time_fd);
		if (rate_fd >= 0) close(rate_fd);
		return -1;
	}

	int total = 0;

	while (true) {
		char url[512];
		snprintf(url, sizeof(url),
			"https://fapi.binance.com/fapi/v1/"
			"fundingRate?symbol=%s&limit=1000"
			"&startTime=%lu",
			symbol.c_str(),
			(unsigned long)start_time);

		std::string body = http_get(url);
		if (body.empty()) break;

		json arr;
		try {
			arr = json::parse(body);
		} catch (...) {
			std::cerr << "[Funding] JSON parse error\n";
			break;
		}

		if (!arr.is_array() || arr.empty()) break;

		for (const auto &item : arr) {
			uint64_t ft =
				item["fundingTime"].get<uint64_t>();
			double fr = std::stod(
				item["fundingRate"].get<std::string>());

			ssize_t w1 = write(time_fd, &ft,
				sizeof(ft));
			ssize_t w2 = write(rate_fd, &fr,
				sizeof(fr));
			(void)w1; (void)w2;
			total++;

			start_time = ft + 1;
		}

		if ((int)arr.size() < 1000) break;
	}

	close(time_fd);
	close(rate_fd);

	std::cout << "[Funding] " << symbol << ": "
		  << total << " records fetched\n";
	return total;
}
