#ifndef FEEDER_OUTPUT_WRITER_H
#define FEEDER_OUTPUT_WRITER_H

#include "trcache.h"
#include <string>

/*
 * Opaque context for the binary file output writer.
 *
 * Manages per-(symbol, candle_type, field) file descriptors and
 * tracks the last closed trade ID per symbol for resume support.
 */
struct output_writer_ctx;

/*
 * Create an output writer context.
 *
 * @param max_symbols      Maximum number of symbols.
 * @param num_candle_types  Number of candle configurations.
 * @param num_fields       Number of user fields (excluding base).
 * @param candle_names     Array of candle type names (e.g. "5tick").
 * @param output_dir       Root output directory.
 *
 * @return Context pointer or NULL on failure.
 */
output_writer_ctx *output_writer_create(int max_symbols,
	int num_candle_types, int num_fields,
	const std::string *candle_names,
	const std::string &output_dir);

void output_writer_destroy(output_writer_ctx *ctx);

/*
 * Get trcache batch_flush_ops wired to this writer.
 * The returned ops should be used for all candle configs.
 */
trcache_batch_flush_ops output_writer_get_ops(
	output_writer_ctx *ctx);

/*
 * Get the last closed trade ID for a symbol.
 * Returns 0 if no candles have been flushed for this symbol.
 */
uint64_t output_writer_get_last_trade_id(
	const output_writer_ctx *ctx, int symbol_id);

/*
 * Get the number of candles written for a (symbol, candle_type).
 */
uint64_t output_writer_get_candle_count(
	const output_writer_ctx *ctx,
	int symbol_id, int candle_idx);

/*
 * Set the symbol string for a given symbol_id.
 * Must be called after trcache_register_symbol() so the writer
 * knows which directory to write to.
 */
void output_writer_set_symbol(output_writer_ctx *ctx,
	int symbol_id, const std::string &symbol_str);

#endif /* FEEDER_OUTPUT_WRITER_H */
