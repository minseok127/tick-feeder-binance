#ifndef FEEDER_TYPES_H
#define FEEDER_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include "trcache.h"

/*
 * Column index definitions.
 * Maps to the order of fields in feeder_candle_fields[].
 */
enum {
	FEED_COL_OPEN = 0,
	FEED_COL_HIGH,
	FEED_COL_LOW,
	FEED_COL_CLOSE,
	FEED_COL_VOLUME,
	FEED_COL_FIRST_TRADE_ID,
	FEED_COL_LAST_TRADE_ID,
	FEED_COL_COUNT
};

/*
 * feeder_candle - Candle structure for historical data processing.
 *
 * trcache_candle_base must be the first member.
 * OHLCV fields are stored as doubles.
 * first_trade_id: trade that opened this candle.
 * last_trade_id:  trade that closed this candle (or the most
 *                 recent trade if still open). Used for resume
 *                 tracking and continuity verification.
 */
typedef struct {
	trcache_candle_base base;

	double open;
	double high;
	double low;
	double close;
	double volume;

	uint64_t first_trade_id;
	uint64_t last_trade_id;
} feeder_candle;

/*
 * Field definitions for trcache Convert stage.
 * Order must match the FEED_COL_* enum above.
 */
static const trcache_field_def feeder_candle_fields[] = {
	{offsetof(feeder_candle, open),
		sizeof(double), FIELD_TYPE_DOUBLE},
	{offsetof(feeder_candle, high),
		sizeof(double), FIELD_TYPE_DOUBLE},
	{offsetof(feeder_candle, low),
		sizeof(double), FIELD_TYPE_DOUBLE},
	{offsetof(feeder_candle, close),
		sizeof(double), FIELD_TYPE_DOUBLE},
	{offsetof(feeder_candle, volume),
		sizeof(double), FIELD_TYPE_DOUBLE},
	{offsetof(feeder_candle, first_trade_id),
		sizeof(uint64_t), FIELD_TYPE_UINT64},
	{offsetof(feeder_candle, last_trade_id),
		sizeof(uint64_t), FIELD_TYPE_UINT64},
};

static const int num_feeder_candle_fields =
	sizeof(feeder_candle_fields) / sizeof(trcache_field_def);

#endif /* FEEDER_TYPES_H */
