#ifndef SIBCOIN_H
#define SIBCOIN_H

#include "miner.h"

extern int sibcoin_test(unsigned char *pdata, const unsigned char *ptarget,
			uint32_t nonce);
extern void sibcoin_regenhash(struct work *work);

#endif /* SIBCOIN_H */
