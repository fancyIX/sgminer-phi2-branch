#ifndef LYRA2REV2_H
#define LYRA2REV2_H

#include "miner.h"
#define LYRA_SCRATCHBUF_SIZE (8 * 4 + 32 * 4)
#define LYRA_SECBUF_SIZE (4) // (not used)
extern int lyra2rev2_test(unsigned char *pdata, const unsigned char *ptarget,
			uint32_t nonce);
extern void lyra2rev2_regenhash(struct work *work);

#endif /* LYRA2REV2_H */
