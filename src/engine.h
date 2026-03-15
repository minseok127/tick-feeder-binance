#ifndef FEEDER_ENGINE_H
#define FEEDER_ENGINE_H

#include "config.h"
#include "output_writer.h"
#include "trcache.h"

/*
 * Initialize the trcache engine with the given configuration.
 *
 * @param config   Feeder configuration.
 * @param writer   Output writer context (used for batch flush).
 *
 * @return trcache handle or NULL on failure.
 */
struct trcache *engine_init(const feeder_config &config,
	output_writer_ctx *writer);

/*
 * Destroy the trcache engine and flush remaining data.
 */
void engine_destroy(struct trcache *cache);

#endif /* FEEDER_ENGINE_H */
