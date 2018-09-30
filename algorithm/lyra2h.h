#ifndef LYRA2H_H
#define LYRA2H_H

#include "miner.h"
//#define LYRA2H_SCRATCHBUF_SIZE (24576) // matrix size [12][16][16] uint64_t or equivalent
#define LYRA2H_SCRATCHBUF_SIZE (12*16*16*8)
// #define LYRA_SCRATCHBUF_SIZE (1536)
extern int lyra2h_test(unsigned char *pdata, const unsigned char *ptarget,
			uint32_t nonce);
extern void lyra2h_regenhash(struct work *work);

#endif /* LYRA2H_H */
