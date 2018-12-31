#ifndef LYRA2REV3_H
#define LYRA2REV3_H

#include "miner.h"
#define LYRA_SCRATCHBUF_SIZE (8 * 4 + 32 * 4)
#define LYRA_SECBUF_SIZE (4) // (not used)
extern int lyra2rev3_test(unsigned char *pdata, const unsigned char *ptarget,
			uint32_t nonce);
extern void lyra2rev3_regenhash(struct work *work);

#endif /* LYRA2REV3_H */