#ifndef HEAVYHASH_GATE_H__
#define HEAVYHASH_GATE_H__ 1

#include <stdint.h>

struct xoshiro_state {
    uint64_t s[4];
};

extern uint64_t le64dec(const void *pp);
extern void generate_matrix(uint32_t matrix[64][64], struct xoshiro_state *state);
extern int heavyhash_test(unsigned char *pdata, const unsigned char *ptarget,
			uint32_t nonce);
extern void heavyhash_regenhash(struct work *work);

#endif
