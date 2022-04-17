/*-
 * Copyright 2013,2014 Alexander Peslyak
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "algorithm/yescrypt_core.h"

#define BYTES2CHARS(bytes) \
	((((bytes) * 8) + 5) / 6)

#define HASH_SIZE 32 /* bytes */
#define HASH_LEN BYTES2CHARS(HASH_SIZE) /* base-64 chars */
#define YESCRYPT_FLAGS (YESCRYPT_RW | YESCRYPT_PWXFORM)
static const char * const itoa64 =
	"./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static uint8_t * encode64_uint32(uint8_t * dst, size_t dstlen,
    uint32_t src, uint32_t srcbits)
{
	uint32_t bit;

	for (bit = 0; bit < srcbits; bit += 6) {
		if (dstlen < 1)
			return NULL;
		*dst++ = itoa64[src & 0x3f];
		dstlen--;
		src >>= 6;
	}

	return dst;
}

static uint8_t * encode64(uint8_t * dst, size_t dstlen,
    const uint8_t * src, size_t srclen)
{
	size_t i;

	for (i = 0; i < srclen; ) {
		uint8_t * dnext;
		uint32_t value = 0, bits = 0;
		do {
			value |= (uint32_t)src[i++] << bits;
			bits += 8;
		} while (bits < 24 && i < srclen);
		dnext = encode64_uint32(dst, dstlen, value, bits);
		if (!dnext)
			return NULL;
		dstlen -= dnext - dst;
		dst = dnext;
	}

	return dst;
}

static int decode64_one(uint32_t * dst, uint8_t src)
{
	const char * ptr = strchr(itoa64, src);
	if (ptr) {
		*dst = ptr - itoa64;
		return 0;
	}
	*dst = 0;
	return -1;
}

static const uint8_t * decode64_uint32(uint32_t * dst, uint32_t dstbits,
    const uint8_t * src)
{
	uint32_t bit;
	uint32_t value;

	value = 0;
	for (bit = 0; bit < dstbits; bit += 6) {
		uint32_t one;
		if (decode64_one(&one, *src)) {
			*dst = 0;
			return NULL;
		}
		src++;
		value |= one << bit;
	}

	*dst = value;
	return src;
}

uint8_t *
yescrypt_r(const yescrypt_shared_t * shared, yescrypt_local_t * local,
    const uint8_t * passwd, size_t passwdlen,
    const uint8_t * setting,
    uint8_t * buf, size_t buflen)
{
	uint8_t hash[HASH_SIZE];
	const uint8_t * src, * salt;
	uint8_t * dst;
	size_t prefixlen, saltlen, need;
	uint8_t version;
	uint64_t N;
	uint32_t r, p;
	yescrypt_flags_t flags = YESCRYPT_WORM;
          fflush(stdout);
	if (setting[0] != '$' || setting[1] != '7')
		{
            fflush(stdout);
		return NULL; 
        }
          fflush(stdout);  
	src = setting + 2;
          fflush(stdout); 
	switch ((version = *src)) {
	case '$':
                fflush(stdout);
		break;
	case 'X':
		src++;
		flags = YESCRYPT_RW;
                fflush(stdout);
		break;
	default:
		{
            fflush(stdout);
		return NULL; 
        }
	}

          fflush(stdout);
	if (*src != '$') {
		uint32_t decoded_flags;
		if (decode64_one(&decoded_flags, *src))

		{
		fflush(stdout);
		return NULL; 
		}
		flags = (yescrypt_flags_t)decoded_flags;
		if (*++src != '$')
	    {	
        fflush(stdout);
		return NULL; 
        }
	}
	src++;

	{
		uint32_t N_log2;
		if (decode64_one(&N_log2, *src))
			{
		         return NULL; 
            }
		src++;
		N = (uint64_t)1 << N_log2;
	}

	src = decode64_uint32(&r, 30, src);
	if (!src)
          {
		return NULL; 
          }

	src = decode64_uint32(&p, 30, src);
	if (!src)
         {
		return NULL; 
         }

	prefixlen = src - setting;

	salt = src;
	src = (uint8_t *)strrchr((char *)salt, '$');
	if (src)
		saltlen = src - salt;
	else
		saltlen = strlen((char *)salt);

	need = prefixlen + saltlen + 1 + HASH_LEN + 1;
	if (need > buflen || need < saltlen)
                            
		 {
        fflush(stdout);
		return NULL; 
         }

fflush(stdout);
#ifndef WIN32
	if (yescrypt_kdf(shared, local, passwd, passwdlen, salt, saltlen,
	    N, r, p, 0, flags, hash, sizeof(hash)))
	    {
        fflush(stdout);
		return NULL; 
        }
#endif

	dst = buf;
	memcpy(dst, setting, prefixlen + saltlen);
	dst += prefixlen + saltlen;
	*dst++ = '$';

	dst = encode64(dst, buflen - (dst - buf), hash, sizeof(hash));
	/* Could zeroize hash[] here, but yescrypt_kdf() doesn't zeroize its
	 * memory allocations yet anyway. */
	if (!dst || dst >= buf + buflen) /* Can't happen */
		{
		return NULL; 
        }

	*dst = 0; /* NUL termination */
           fflush(stdout);
	return buf;
}

uint8_t *
yescrypt(const uint8_t * passwd, const uint8_t * setting)
{
	static uint8_t buf[4 + 1 + 5 + 5 + BYTES2CHARS(32) + 1 + HASH_LEN + 1];
	yescrypt_shared_t shared;
	yescrypt_local_t local;
	uint8_t * retval;
#ifndef WIN32
	if (yescrypt_init_shared(&shared, NULL, 0,
	    0, 0, 0, YESCRYPT_SHARED_DEFAULTS, 0, NULL, 0))
		return NULL;
	if (yescrypt_init_local(&local)) {
		yescrypt_free_shared(&shared);
		return NULL;
	}
#endif
	retval = yescrypt_r(&shared, &local,
	    passwd, 80, setting, buf, sizeof(buf));
        // printf("hashse='%s'\n", (char *)retval);
#ifndef WIN32
	if (yescrypt_free_local(&local)) {
		yescrypt_free_shared(&shared);
		return NULL;
	}
	if (yescrypt_free_shared(&shared))
		return NULL;
#endif
	return retval;
      
}

uint8_t *
yescrypt_gensalt_r(uint32_t N_log2, uint32_t r, uint32_t p,
    yescrypt_flags_t flags,
    const uint8_t * src, size_t srclen,
    uint8_t * buf, size_t buflen)
{
	uint8_t * dst;
	size_t prefixlen = 3 + 1 + 5 + 5;
	size_t saltlen = BYTES2CHARS(srclen);
	size_t need;

	if (p == 1)
		flags = (yescrypt_flags_t)(flags & ~YESCRYPT_PARALLEL_SMIX);

	if (flags) {
		if (flags & ~0x3f)
			return NULL;

		prefixlen++;
		if (flags != YESCRYPT_RW)
			prefixlen++;
	}

	need = prefixlen + saltlen + 1;
	if (need > buflen || need < saltlen || saltlen < srclen)
		return NULL;

	if (N_log2 > 63 || ((uint64_t)r * (uint64_t)p >= (1U << 30)))
		return NULL;

	dst = buf;
	*dst++ = '$';
	*dst++ = '7';
	if (flags) {
		*dst++ = 'X'; /* eXperimental, subject to change */
		if (flags != YESCRYPT_RW)
			*dst++ = itoa64[flags];
	}
	*dst++ = '$';

	*dst++ = itoa64[N_log2];

	dst = encode64_uint32(dst, buflen - (dst - buf), r, 30);
	if (!dst) /* Can't happen */
		return NULL;

	dst = encode64_uint32(dst, buflen - (dst - buf), p, 30);
	if (!dst) /* Can't happen */
		return NULL;

	dst = encode64(dst, buflen - (dst - buf), src, srclen);
	if (!dst || dst >= buf + buflen) /* Can't happen */
		return NULL;

	*dst = 0; /* NUL termination */

	return buf;
}

uint8_t *
yescrypt_gensalt(uint32_t N_log2, uint32_t r, uint32_t p,
    yescrypt_flags_t flags,
    const uint8_t * src, size_t srclen)
{
	static uint8_t buf[4 + 1 + 5 + 5 + BYTES2CHARS(32) + 1];
	return yescrypt_gensalt_r(N_log2, r, p, flags, src, srclen,
	    buf, sizeof(buf));
}

static int
yescrypt_bsty(const uint8_t * passwd, size_t passwdlen,
    const uint8_t * salt, size_t saltlen, uint64_t N, uint32_t r, uint32_t p,
    uint8_t * buf, size_t buflen)
{
	static  int initialized = 0;
	static  yescrypt_shared_t shared;
	static  yescrypt_local_t local;

//		static __declspec(thread) int initialized = 0;
//		static __declspec(thread) yescrypt_shared_t shared;
//		static  __declspec(thread) yescrypt_local_t local;

	int retval;
	if (!initialized) {
/* "shared" could in fact be shared, but it's simpler to keep it private
 * along with "local".  It's dummy and tiny anyway. */
#ifndef WIN32
		if (yescrypt_init_shared(&shared, NULL, 0,
		    0, 0, 0, YESCRYPT_SHARED_DEFAULTS, 0, NULL, 0))
			return -1;
		if (yescrypt_init_local(&local)) {
			yescrypt_free_shared(&shared);
			return -1;
		}
#endif
		initialized = 1;
 	}
#ifndef WIN32
	retval = yescrypt_kdf(&shared, &local,
		passwd, passwdlen, salt, saltlen, N, r, p, 0, (yescrypt_flags_t)YESCRYPT_FLAGS,
	    buf, buflen);	
#endif	

	return retval;
}

void yescrypt_hash(const unsigned char *input, unsigned char *output)
{

   yescrypt_bsty((const uint8_t *)input, 80, (const uint8_t *) input, 80, 2048, 8, 1, (uint8_t *)output, 32);
}

void yescryptr16_hash(const unsigned char *input, unsigned char *output)
{
	/*
	if (client_key_len == 0xff)
	{
		client_key = "Client Key";
		client_key_len = 10;
	}*/
   yescrypt_bsty((const uint8_t *)input, 80, (const uint8_t *) input, 80, 4096, 16, 1, (uint8_t *)output, 32);
}
