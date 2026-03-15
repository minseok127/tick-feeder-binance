#include "downloader.h"

#include <curl/curl.h>
#include <dirent.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>

static size_t write_cb(void *ptr, size_t size, size_t nmemb,
	void *stream)
{
	return fwrite(ptr, size, nmemb, (FILE *)stream);
}

static bool g_progress_shown = false;

static int progress_cb(void * /*clientp*/,
	curl_off_t dltotal, curl_off_t dlnow,
	curl_off_t /*ultotal*/, curl_off_t /*ulnow*/)
{
	if (dltotal > 0) {
		g_progress_shown = true;
		int pct = (int)(dlnow * 100 / dltotal);
		double mb = (double)dlnow / (1024.0 * 1024.0);
		double total_mb =
			(double)dltotal / (1024.0 * 1024.0);
		fprintf(stderr,
			"\r    [DL] %d%% (%.1f / %.1f MB)",
			pct, mb, total_mb);
	}
	return 0;
}

int download_file(const std::string &url,
	const std::string &dest_path)
{
	FILE *fp = fopen(dest_path.c_str(), "wb");
	if (!fp) {
		std::cerr << "[Download] Cannot create: "
			  << dest_path << "\n";
		return -1;
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		fclose(fp);
		return -1;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
		progress_cb);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

	g_progress_shown = false;
	CURLcode res = curl_easy_perform(curl);
	if (g_progress_shown) {
		fprintf(stderr, "\n");
	}

	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE,
		&http_code);

	curl_easy_cleanup(curl);
	fclose(fp);

	if (res != CURLE_OK) {
		std::cerr << "[Download] Failed: " << url
			  << " err=" << curl_easy_strerror(res)
			  << "\n";
		remove(dest_path.c_str());
		return -1;
	}

	if (http_code != 200) {
		remove(dest_path.c_str());
		return (int)http_code;
	}

	return 200;
}

std::string make_monthly_url(const std::string &symbol,
	int year, int month)
{
	char buf[512];
	snprintf(buf, sizeof(buf),
		"https://data.binance.vision/data/futures/um/"
		"monthly/aggTrades/%s/"
		"%s-aggTrades-%04d-%02d.zip",
		symbol.c_str(), symbol.c_str(), year, month);
	return buf;
}

std::string make_daily_url(const std::string &symbol,
	int year, int month, int day)
{
	char buf[512];
	snprintf(buf, sizeof(buf),
		"https://data.binance.vision/data/futures/um/"
		"daily/aggTrades/%s/"
		"%s-aggTrades-%04d-%02d-%02d.zip",
		symbol.c_str(), symbol.c_str(),
		year, month, day);
	return buf;
}

size_t get_dir_size(const std::string &dir)
{
	size_t total = 0;
	DIR *d = opendir(dir.c_str());
	if (!d) {
		return 0;
	}

	struct dirent *ent;
	while ((ent = readdir(d)) != nullptr) {
		if (ent->d_name[0] == '.') {
			continue;
		}
		std::string path = dir + "/" + ent->d_name;
		struct stat st;
		if (stat(path.c_str(), &st) == 0 &&
		    S_ISREG(st.st_mode)) {
			total += (size_t)st.st_size;
		}
	}
	closedir(d);
	return total;
}
