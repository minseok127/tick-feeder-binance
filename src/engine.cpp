/*
 * engine.cpp
 *
 * trcache initialization using the slot template system.
 * Simplified from validator/core/engine.cpp:
 * - No LatencyCodec (historical data, timestamps used as-is)
 * - No trade flush (raw trades not persisted)
 * - batch_flush wired to output_writer
 */

#include "engine.h"
#include "types.h"
#include "output_writer.h"

#include <iostream>
#include <vector>
#include <cstring>

#define MAX_CANDLE_SLOTS 8

/* Per-slot config storage for template callbacks. */
static candle_config g_slot_configs[MAX_CANDLE_SLOTS];

/* -----------------------------------------------------------
 * Callback Logic
 * ----------------------------------------------------------- */

static void init_common(trcache_candle_base *c,
	trcache_trade_data *d, feeder_candle *candle)
{
	double price = d->price.as_double;

	candle->open = price;
	candle->high = price;
	candle->low = price;
	candle->close = price;
	candle->volume = d->volume.as_double;
	candle->last_trade_id = d->trade_id;

	c->is_closed = false;
}

static void update_common(feeder_candle *candle,
	trcache_trade_data *d)
{
	double price = d->price.as_double;

	if (price > candle->high) candle->high = price;
	if (price < candle->low)  candle->low = price;
	candle->close = price;
	candle->volume += d->volume.as_double;
	candle->last_trade_id = d->trade_id;
}

/* -----------------------------------------------------------
 * Slot Template System
 * ----------------------------------------------------------- */

/* TICK_MODULO: candle closes when (trade_id + 1) % threshold == 0 */
template <int SLOT>
void init_tick_modulo(trcache_candle_base *c, void *data,
	const void *)
{
	trcache_trade_data *d = (trcache_trade_data *)data;
	feeder_candle *candle = (feeder_candle *)c;
	int threshold = g_slot_configs[SLOT].threshold;

	init_common(c, d, candle);
	c->key.trade_id =
		d->trade_id - (d->trade_id % threshold);
}

template <int SLOT>
bool update_tick_modulo(trcache_candle_base *c, void *data,
	const void *)
{
	trcache_trade_data *d = (trcache_trade_data *)data;
	feeder_candle *candle = (feeder_candle *)c;
	int threshold = g_slot_configs[SLOT].threshold;

	if (d->trade_id >= c->key.trade_id +
	    (uint64_t)threshold) {
		c->is_closed = true;
		return false;
	}

	update_common(candle, d);

	if ((d->trade_id + 1) % threshold == 0) {
		c->is_closed = true;
	}
	return true;
}

/* TIME_FIXED: candle closes when timestamp crosses window */
template <int SLOT>
void init_time_fixed(trcache_candle_base *c, void *data,
	const void *)
{
	trcache_trade_data *d = (trcache_trade_data *)data;
	feeder_candle *candle = (feeder_candle *)c;
	int threshold = g_slot_configs[SLOT].threshold;

	init_common(c, d, candle);
	c->key.timestamp =
		d->timestamp - (d->timestamp % threshold);
}

template <int SLOT>
bool update_time_fixed(trcache_candle_base *c, void *data,
	const void *)
{
	trcache_trade_data *d = (trcache_trade_data *)data;
	feeder_candle *candle = (feeder_candle *)c;
	int threshold = g_slot_configs[SLOT].threshold;

	if (d->timestamp >=
	    c->key.timestamp + (uint64_t)threshold) {
		c->is_closed = true;
		return false;
	}

	update_common(candle, d);
	return true;
}

/* Function pointer lookup tables */
typedef void (*init_func_t)(trcache_candle_base *, void *,
	const void *);
typedef bool (*update_func_t)(trcache_candle_base *, void *,
	const void *);

static const init_func_t INIT_TICK_OPS[] = {
	init_tick_modulo<0>, init_tick_modulo<1>,
	init_tick_modulo<2>, init_tick_modulo<3>,
	init_tick_modulo<4>, init_tick_modulo<5>,
	init_tick_modulo<6>, init_tick_modulo<7>
};

static const update_func_t UPDATE_TICK_OPS[] = {
	update_tick_modulo<0>, update_tick_modulo<1>,
	update_tick_modulo<2>, update_tick_modulo<3>,
	update_tick_modulo<4>, update_tick_modulo<5>,
	update_tick_modulo<6>, update_tick_modulo<7>
};

static const init_func_t INIT_TIME_OPS[] = {
	init_time_fixed<0>, init_time_fixed<1>,
	init_time_fixed<2>, init_time_fixed<3>,
	init_time_fixed<4>, init_time_fixed<5>,
	init_time_fixed<6>, init_time_fixed<7>
};

static const update_func_t UPDATE_TIME_OPS[] = {
	update_time_fixed<0>, update_time_fixed<1>,
	update_time_fixed<2>, update_time_fixed<3>,
	update_time_fixed<4>, update_time_fixed<5>,
	update_time_fixed<6>, update_time_fixed<7>
};

/* -----------------------------------------------------------
 * Engine Init / Destroy
 * ----------------------------------------------------------- */

struct trcache *engine_init(const feeder_config &config,
	output_writer_ctx *writer)
{
	int num_configs = (int)config.candles.size();
	if (num_configs > MAX_CANDLE_SLOTS) {
		std::cerr << "[Engine] Too many candle configs. "
			  << "Max: " << MAX_CANDLE_SLOTS << "\n";
		return nullptr;
	}

	trcache_batch_flush_ops flush_ops =
		output_writer_get_ops(writer);

	std::vector<trcache_candle_config> tr_configs;
	tr_configs.reserve(num_configs);

	for (int i = 0; i < num_configs; i++) {
		const auto &cc = config.candles[i];
		g_slot_configs[i] = cc;

		trcache_candle_update_ops u_ops = {};

		if (cc.type == "TICK_MODULO") {
			u_ops.init = INIT_TICK_OPS[i];
			u_ops.update = UPDATE_TICK_OPS[i];
		} else if (cc.type == "TIME_FIXED") {
			u_ops.init = INIT_TIME_OPS[i];
			u_ops.update = UPDATE_TIME_OPS[i];
		} else {
			std::cerr << "[Engine] Unknown candle type: "
				  << cc.type << "\n";
			return nullptr;
		}

		trcache_candle_config c_conf = {
			sizeof(feeder_candle),
			feeder_candle_fields,
			num_feeder_candle_fields,
			u_ops,
			flush_ops
		};
		tr_configs.push_back(c_conf);
	}

	size_t mem_bytes =
		(size_t)config.memory_limit_mb * (1ULL << 20);
	int max_symbols =
		(int)config.symbols.size() + 10;

	trcache_init_ctx ctx;
	memset(&ctx, 0, sizeof(ctx));

	ctx.candle_configs = tr_configs.data();
	ctx.num_candle_configs = num_configs;
	ctx.batch_candle_count_pow2 = config.batch_size_pow2;
	ctx.cached_batch_count_pow2 =
		config.cached_batch_count_pow2;
	ctx.total_memory_limit = mem_bytes;
	ctx.num_worker_threads = config.worker_threads;
	ctx.max_symbols = max_symbols;
	ctx.trade_data_size = sizeof(trcache_trade_data);

	struct trcache *cache = trcache_init(&ctx);
	if (!cache) {
		std::cerr << "[Engine] trcache_init failed\n";
	}
	return cache;
}

void engine_destroy(struct trcache *cache)
{
	if (cache) {
		trcache_destroy(cache);
	}
}
