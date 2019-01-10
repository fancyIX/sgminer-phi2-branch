#ifndef LYRA2ZZ_H
#define LYRA2ZZ_H

#include "miner.h"
//#define LYRA2Z_SCRATCHBUF_SIZE (24576) // matrix size [12][4][4] uint64_t or equivalent
#define LYRA2ZZ_SCRATCHBUF_SIZE (12*8*8)
// #define LYRA_SCRATCHBUF_SIZE (1536)
#define LYRA_SECBUF_SIZE (4) // (not used)
extern int lyra2Zz_test(unsigned char *pdata, const unsigned char *ptarget,
			uint32_t nonce);
extern void lyra2Zz_regenhash(struct work *work);

#endif /* LYRA2ZZ_H */
