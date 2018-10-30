#ifndef X22I_H
#define X22I_H

#include "miner.h"

#define X22I_SCRATCHBUF_SIZE (8 * 8 * 4 + 8 * 4  * 4 /*+ 1024 * 8*/)

extern int x22i_test(unsigned char *pdata, const unsigned char *ptarget,
			uint32_t nonce);
extern void x22i_regenhash(struct work *work);

#endif /* X22I_H */