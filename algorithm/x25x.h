#ifndef X25X_H
#define X25X_H

#include "miner.h"

#define X25X_SCRATCHBUF_SIZE (8 * 8 * 24 + 8 * 4  * 4 /*+ 1024 * 8*/)

extern int x25x_test(unsigned char *pdata, const unsigned char *ptarget,
			uint32_t nonce);
extern void x25x_regenhash(struct work *work);

#endif /* X25X_H */