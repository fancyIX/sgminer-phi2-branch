#ifndef ALGORITHM_H
#define ALGORITHM_H

#ifdef __APPLE_CC__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

#include <inttypes.h>
#include <stdbool.h>
#include "ocl/build_kernel.h"   // For the build_kernel_data type

typedef enum {
  ALGO_MTP,
  ALGO_HEAVYHASH,
  ALGO_UNK,
  ALGO_ALLIUM,
  ALGO_ALLIUM_NAVI,
  ALGO_CRE,
  ALGO_SCRYPT,
  ALGO_NSCRYPT,
  ALGO_PASCAL,
  ALGO_X11,
  ALGO_0X10,
  ALGO_X13,
  ALGO_X14,
  ALGO_X15,
  ALGO_X22I,
  ALGO_X25X,
  ALGO_KECCAK,
  ALGO_QUARK,
  ALGO_TWE,
  ALGO_FUGUE,
  ALGO_NIST,
  ALGO_FRESH,
  ALGO_WHIRL,
  ALGO_NEOSCRYPT,
  ALGO_NEOSCRYPT_XAYA,
  ALGO_NEOSCRYPT_NAVI,
  ALGO_NEOSCRYPT_XAYA_NAVI,
  ALGO_WHIRLPOOLX,
  ALGO_LYRA2RE,
  ALGO_LYRA2REV2,
  ALGO_LYRA2REV3,
  ALGO_LYRA2Z,
  ALGO_LYRA2Z_NAVI,
  ALGO_LYRA2ZZ,
  ALGO_LYRA2H,
  ALGO_PLUCK,
  ALGO_YESCRYPT,
  ALGO_YESCRYPT_MULTI,
  ALGO_YESCRYPT_NAVI,
  ALGO_BLAKECOIN,
  ALGO_BLAKE,
  ALGO_SIA,
  ALGO_DECRED,
  ALGO_VANILLA,
  ALGO_ETHASH,
  ALGO_LBRY,
  ALGO_PHI,
  ALGO_PHI2,
  ALGO_PHI2_NAVI,
  ALGO_SIBCOIN,
  ALGO_ARGON2D
} algorithm_type_t;

extern const char *algorithm_type_str[];

extern void gen_hash(const unsigned char *data, unsigned int len, unsigned char *hash);

struct __clState;
struct _dev_blk_ctx;
struct _build_kernel_data;
struct cgpu_info;
struct work;

/* Describes the Scrypt parameters and hashing functions used to mine
 * a specific coin.
 */
typedef struct _algorithm_t {
  char     name[20]; /* Human-readable identifier */
  algorithm_type_t type; //algorithm type
  const char *kernelfile; /* alternate kernel file */
  uint32_t n;        /* N (CPU/Memory tradeoff parameter) */
  uint8_t  nfactor;  /* Factor of N above (n = 2^nfactor) */
  double   diff_multiplier1;
  double   diff_multiplier2;
  double   share_diff_multiplier;
  uint32_t xintensity_shift;
  uint32_t intensity_shift;
  uint32_t found_idx;
  unsigned long long   diff_numerator;
  uint32_t diff1targ;
  size_t n_extra_kernels;
  long rw_buffer_size;
  cl_command_queue_properties cq_properties;
  void(*regenhash)(struct work *);
  void(*calc_midstate)(struct work *);
  void(*prepare_work)(struct _dev_blk_ctx *, uint32_t *, uint32_t *);
  cl_int(*queue_kernel)(struct __clState *, struct _dev_blk_ctx *, cl_uint);
  void(*gen_hash)(const unsigned char *, unsigned int, unsigned char *);
  void(*set_compile_options)(struct _build_kernel_data *, struct cgpu_info *, struct _algorithm_t *);
} algorithm_t;

typedef struct _algorithm_settings_t
{
	const char *name;
	algorithm_type_t type;
	const char *kernelfile;
	double   diff_multiplier1;
	double   diff_multiplier2;
	double   share_diff_multiplier;
	uint32_t xintensity_shift;
	uint32_t intensity_shift;
	uint32_t found_idx;
	unsigned long long   diff_numerator;
	uint32_t diff1targ;
	size_t n_extra_kernels;
	long rw_buffer_size;
	cl_command_queue_properties cq_properties;
	void     (*regenhash)(struct work *);
	void     (*calc_midstate)(struct work *);
	void     (*prepare_work)(struct _dev_blk_ctx *, uint32_t *, uint32_t *);
	cl_int   (*queue_kernel)(struct __clState *, struct _dev_blk_ctx *, cl_uint);
	void     (*gen_hash)(const unsigned char *, unsigned int, unsigned char *);
	void     (*set_compile_options)(build_kernel_data *, struct cgpu_info *, algorithm_t *);
} algorithm_settings_t;

/* Set default parameters based on name. */
void set_algorithm(algorithm_t* algo, const char* name);

/* Set to specific N factor. */
void set_algorithm_nfactor(algorithm_t* algo, const uint8_t nfactor);

/* Compare two algorithm parameters */
bool cmp_algorithm(const algorithm_t* algo1, const algorithm_t* algo2);

#endif /* ALGORITHM_H */
