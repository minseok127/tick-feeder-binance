#include "output_writer.h"
#include "types.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

/* Field file names in order matching feeder_candle_fields[]. */
static const char *FIELD_NAMES[] = {
	"open", "high", "low", "close", "volume"
};
static const int NUM_OUTPUT_FIELDS = 5;

/*
 * Per-(symbol, candle_type) file descriptor set.
 *
 * key_fd:       fd for keys.bin (candle key — trade_id or timestamp)
 * field_fds[i]: fd for each OHLCV column file
 * *_offset:     current write offset for pwrite; tracks how much
 *               data has been written, enabling append without lseek
 * candle_count: number of candles written so far; used by main
 *               to populate metadata for incremental resume
 */
struct file_set {
	int key_fd;
	int field_fds[5];
	size_t key_offset;
	size_t field_offsets[5];
	uint64_t candle_count;
};

struct output_writer_ctx {
	int max_symbols;
	int num_candle_types;
	std::string output_dir;
	std::vector<std::string> candle_names;
	std::vector<std::string> symbol_names;

	/*
	 * file_sets[symbol_id * num_candle_types + candle_idx]
	 */
	std::vector<file_set> file_sets;

	/*
	 * Per-symbol last closed trade ID.
	 * Updated during flush from the last_trade_id column.
	 */
	std::vector<uint64_t> last_trade_ids;
};

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

static int open_bin(const std::string &dir, const char *name)
{
	std::string path = dir + "/" + name + ".bin";
	int fd = open(path.c_str(),
		O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0) {
		std::cerr << "[OutputWriter] open failed: " << path
			  << " err=" << strerror(errno) << "\n";
	}
	return fd;
}

/*
 * Lazily open output files for a (symbol, candle_type) pair.
 * Creates the directory tree (e.g. output/BTCUSDT/5tick/) on first
 * access and sets offsets to current file sizes for correct append.
 */
static void ensure_files_open(output_writer_ctx *ctx,
	int symbol_id, int candle_idx)
{
	int idx = symbol_id * ctx->num_candle_types + candle_idx;
	file_set &fs = ctx->file_sets[idx];

	if (fs.key_fd >= 0) {
		return;
	}

	const std::string &sym = ctx->symbol_names[symbol_id];
	const std::string &candle = ctx->candle_names[candle_idx];
	std::string dir = ctx->output_dir + "/" + sym + "/" + candle;
	mkdirs(dir);

	fs.key_fd = open_bin(dir, "keys");
	for (int i = 0; i < NUM_OUTPUT_FIELDS; i++) {
		fs.field_fds[i] = open_bin(dir, FIELD_NAMES[i]);
	}

	/*
	 * Set offsets to current file sizes for append correctness.
	 */
	if (fs.key_fd >= 0) {
		fs.key_offset = (size_t)lseek(fs.key_fd, 0, SEEK_END);
		fs.candle_count =
			fs.key_offset / sizeof(uint64_t);
	}
	for (int i = 0; i < NUM_OUTPUT_FIELDS; i++) {
		if (fs.field_fds[i] >= 0) {
			fs.field_offsets[i] = (size_t)lseek(
				fs.field_fds[i], 0, SEEK_END);
		}
	}
}

/*
 * Write a contiguous range [start, start+count) from batch arrays.
 */
static void write_range(file_set &fs,
	trcache_candle_batch *batch, int start, int count)
{
	if (count <= 0) {
		return;
	}

	/* Write keys */
	if (fs.key_fd >= 0) {
		size_t sz = (size_t)count * sizeof(uint64_t);
		ssize_t w = pwrite(fs.key_fd,
			batch->key_array + start, sz,
			(off_t)fs.key_offset);
		(void)w;
		fs.key_offset += sz;
	}

	/* Write OHLCV columns */
	for (int i = 0; i < NUM_OUTPUT_FIELDS; i++) {
		if (fs.field_fds[i] < 0 ||
		    batch->column_arrays[i] == nullptr) {
			continue;
		}
		size_t elem_sz = (i < 5) ? sizeof(double)
					  : sizeof(uint64_t);
		size_t sz = (size_t)count * elem_sz;
		const char *base =
			(const char *)batch->column_arrays[i];
		ssize_t w = pwrite(fs.field_fds[i],
			base + (size_t)start * elem_sz,
			sz, (off_t)fs.field_offsets[i]);
		(void)w;
		fs.field_offsets[i] += sz;
	}

	fs.candle_count += (uint64_t)count;
}

/*
 * Batch flush callback — invoked by trcache when a candle batch
 * is ready (either a full batch or during trcache_destroy).
 *
 * Writes only closed candles to disk. Unclosed candles at the
 * end of a batch are in-progress aggregations that will be
 * flushed in a later batch or during engine teardown.
 */
static void flush_cb(trcache * /*cache*/,
	trcache_candle_batch *batch, void *ctx_ptr)
{
	output_writer_ctx *ctx = (output_writer_ctx *)ctx_ptr;
	int symbol_id = batch->symbol_id;
	int candle_idx = batch->candle_idx;
	int n = batch->num_candles;

	if (n <= 0) {
		return;
	}

	ensure_files_open(ctx, symbol_id, candle_idx);

	int idx = symbol_id * ctx->num_candle_types + candle_idx;
	file_set &fs = ctx->file_sets[idx];

	/*
	 * Only write closed candles. Unclosed candles at the end of
	 * a batch represent in-progress aggregation; writing them
	 * would corrupt resume since the trades that formed them
	 * would be skipped on restart.
	 *
	 * Closed candles are contiguous at the front of the batch
	 * (trcache closes candles in order). Scan for contiguous
	 * closed runs and write them. Stop at the first unclosed
	 * candle.
	 */
	int closed_count = 0;
	for (int i = 0; i < n; i++) {
		if (batch->is_closed_array[i]) {
			closed_count++;
		} else {
			break;
		}
	}

	write_range(fs, batch, 0, closed_count);

	/*
	 * Update last_trade_id from the last *closed* candle only.
	 */
	if (closed_count > 0) {
		uint64_t *ltid_col = (uint64_t *)
			batch->column_arrays[FEED_COL_LAST_TRADE_ID];
		if (ltid_col) {
			uint64_t last =
				ltid_col[closed_count - 1];
			if (last >
			    ctx->last_trade_ids[symbol_id]) {
				ctx->last_trade_ids[symbol_id] =
					last;
			}
		}
	}
}

/*
 * is_done callback: signals that flush_cb completed synchronously.
 * Always returns true since pwrite is blocking.
 */
static bool is_done_cb(trcache * /*cache*/,
	trcache_candle_batch * /*batch*/, void * /*ctx*/)
{
	return true;
}

/* Public API */

output_writer_ctx *output_writer_create(int max_symbols,
	int num_candle_types, int num_fields,
	const std::string *candle_names,
	const std::string &output_dir)
{
	(void)num_fields;

	auto *ctx = new (std::nothrow) output_writer_ctx();
	if (!ctx) {
		return nullptr;
	}

	ctx->max_symbols = max_symbols;
	ctx->num_candle_types = num_candle_types;
	ctx->output_dir = output_dir;

	for (int i = 0; i < num_candle_types; i++) {
		ctx->candle_names.push_back(candle_names[i]);
	}

	ctx->symbol_names.resize(max_symbols);
	ctx->last_trade_ids.resize(max_symbols, 0);

	int total = max_symbols * num_candle_types;
	ctx->file_sets.resize(total);
	for (int i = 0; i < total; i++) {
		ctx->file_sets[i].key_fd = -1;
		for (int j = 0; j < NUM_OUTPUT_FIELDS; j++) {
			ctx->file_sets[i].field_fds[j] = -1;
			ctx->file_sets[i].field_offsets[j] = 0;
		}
		ctx->file_sets[i].key_offset = 0;
		ctx->file_sets[i].candle_count = 0;
	}

	return ctx;
}

void output_writer_destroy(output_writer_ctx *ctx)
{
	if (!ctx) {
		return;
	}

	int total = ctx->max_symbols * ctx->num_candle_types;
	for (int i = 0; i < total; i++) {
		if (ctx->file_sets[i].key_fd >= 0) {
			close(ctx->file_sets[i].key_fd);
		}
		for (int j = 0; j < NUM_OUTPUT_FIELDS; j++) {
			if (ctx->file_sets[i].field_fds[j] >= 0) {
				close(ctx->file_sets[i].field_fds[j]);
			}
		}
	}

	delete ctx;
}

trcache_batch_flush_ops output_writer_get_ops(
	output_writer_ctx *ctx)
{
	trcache_batch_flush_ops ops = {};
	ops.flush = flush_cb;
	ops.is_done = is_done_cb;
	ops.ctx = ctx;
	return ops;
}

uint64_t output_writer_get_last_trade_id(
	const output_writer_ctx *ctx, int symbol_id)
{
	if (symbol_id < 0 ||
	    symbol_id >= ctx->max_symbols) {
		return 0;
	}
	return ctx->last_trade_ids[symbol_id];
}

uint64_t output_writer_get_candle_count(
	const output_writer_ctx *ctx,
	int symbol_id, int candle_idx)
{
	int idx = symbol_id * ctx->num_candle_types + candle_idx;
	return ctx->file_sets[idx].candle_count;
}

uint64_t output_writer_get_total_bytes(
	const output_writer_ctx *ctx, int symbol_id)
{
	uint64_t total = 0;
	int s_start = 0;
	int s_end = ctx->max_symbols;
	if (symbol_id >= 0) {
		s_start = symbol_id;
		s_end = symbol_id + 1;
	}
	for (int s = s_start; s < s_end; s++) {
		for (int c = 0; c < ctx->num_candle_types; c++) {
			int idx = s * ctx->num_candle_types + c;
			const file_set &fs = ctx->file_sets[idx];
			total += fs.key_offset;
			for (int i = 0; i < NUM_OUTPUT_FIELDS; i++) {
				total += fs.field_offsets[i];
			}
		}
	}
	return total;
}

void output_writer_set_symbol(output_writer_ctx *ctx,
	int symbol_id, const std::string &symbol_str)
{
	if (symbol_id >= 0 &&
	    symbol_id < ctx->max_symbols) {
		ctx->symbol_names[symbol_id] = symbol_str;
	}
}
