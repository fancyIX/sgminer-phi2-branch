#ifndef MTP_ALGO_H
#define MTP_ALGO_H

#include "miner.h"
#define MTP_MEMCOST 4*1024*1024
#define MTP_SCRATCH 256 * MTP_MEMCOST * 4
#define MTP_TREE_SIZE 2 * 1048576 * 4 * 8

//#define LYRA2H_SCRATCHBUF_SIZE (24576) // matrix size [12][16][16] uint64_t or equivalent
#define LYRA2H_SCRATCHBUF_SIZE (12*16*16)
// #define LYRA_SCRATCHBUF_SIZE (1536)
#define LYRA_SECBUF_SIZE (4) // (not used)
extern int mtp_test(unsigned char *pdata, const unsigned char *ptarget,
			uint32_t nonce);
extern void mtp_regenhash(struct work *work);

#endif /* LYRA2H_H */
