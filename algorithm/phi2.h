#ifndef PHI2_H
#define PHI2_H

#include "miner.h"

extern int phi2_test(unsigned char *pdata, const unsigned char *ptarget,
			uint32_t nonce);
extern void phi2_regenhash(struct work *work);

#endif /* PHI2_H */
