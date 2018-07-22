/*
 * Copyright (C) 2018 vt@altlinux.org. All Rights Reserved.
 *
 * Contents licensed under the terms of the OpenSSL license
 * See https://www.openssl.org/source/license.html for details
 */

#include "gost_grasshopper_cipher.h"
#include "gost_grasshopper_defines.h"
#include "gost_grasshopper_math.h"
#include "gost_grasshopper_core.h"
#include "e_gost_err.h"
#include "gost_lcl.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/asn1.h>
#include <string.h>

#define T(e) if (!(e)) {\
	ERR_print_errors_fp(stderr);\
	OpenSSLDie(__FILE__, __LINE__, #e);\
    }

#define cRED	"\033[1;31m"
#define cGREEN	"\033[1;32m"
#define cNORM	"\033[m"
#define TEST_ASSERT(e) {if ((test = (e))) \
		 printf(cRED "Test FAILED\n" cNORM); \
	     else \
		 printf(cGREEN "Test passed\n" cNORM);}

enum e_mode {
    E_ECB = 0,
    E_CTR
};

/* Test key from both GOST R 34.12-2015 and GOST R 34.13-2015. */
static const unsigned char K[] = {
    0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
    0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10,0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
};

/* Plaintext from GOST R 34.13-2015 A.1.
 * First 16 bytes is vector (a) from GOST R 34.12-2015 A.1. */
static const unsigned char P[] = {
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x00,0xff,0xee,0xdd,0xcc,0xbb,0xaa,0x99,0x88,
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xee,0xff,0x0a,
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xee,0xff,0x0a,0x00,
    0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xee,0xff,0x0a,0x00,0x11,
};

static void hexdump(const void *ptr, size_t len)
{
    const unsigned char *p = ptr;
    size_t i, j;

    for (i = 0; i < len; i += j) {
	for (j = 0; j < 16 && i + j < len; j++)
	    printf("%s%02x", j? " " : "\t", p[i + j]);
	printf("\n");
    }
}

/* Test vectors from GOST R 34.13-2015 A.1 which* includes vectors
 * from GOST R 34.12-2015 A.1 as first block of ecb mode. */
static int test_modes(const EVP_CIPHER *type, const char *mode, enum e_mode t)
{
    EVP_CIPHER_CTX ctx;
    unsigned char e[4][sizeof(P)] = {
	{ /* ECB test vectors from GOST R 34.13-2015  A.1.1 */
	    /* first 16 bytes is vector (b) from GOST R 34.12-2015 A.1 */
	    0x7f,0x67,0x9d,0x90,0xbe,0xbc,0x24,0x30,0x5a,0x46,0x8d,0x42,0xb9,0xd4,0xed,0xcd,
	    0xb4,0x29,0x91,0x2c,0x6e,0x00,0x32,0xf9,0x28,0x54,0x52,0xd7,0x67,0x18,0xd0,0x8b,
	    0xf0,0xca,0x33,0x54,0x9d,0x24,0x7c,0xee,0xf3,0xf5,0xa5,0x31,0x3b,0xd4,0xb1,0x57,
	    0xd0,0xb0,0x9c,0xcd,0xe8,0x30,0xb9,0xeb,0x3a,0x02,0xc4,0xc5,0xaa,0x8a,0xda,0x98,
	},
	{ /* CTR test vectors from GOST R 34.13-2015  A.1.2 */
	    0xf1,0x95,0xd8,0xbe,0xc1,0x0e,0xd1,0xdb,0xd5,0x7b,0x5f,0xa2,0x40,0xbd,0xa1,0xb8,
	    0x85,0xee,0xe7,0x33,0xf6,0xa1,0x3e,0x5d,0xf3,0x3c,0xe4,0xb3,0x3c,0x45,0xde,0xe4,
	    0xa5,0xea,0xe8,0x8b,0xe6,0x35,0x6e,0xd3,0xd5,0xe8,0x77,0xf1,0x35,0x64,0xa3,0xa5,
	    0xcb,0x91,0xfa,0xb1,0xf2,0x0c,0xba,0xb6,0xd1,0xc6,0xd1,0x58,0x20,0xbd,0xba,0x73,
	},
    };
    unsigned char iv_ctr[] = { 0x12,0x34,0x56,0x78,0x90,0xab,0xce,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
    unsigned char *iv[4] = { NULL, iv_ctr, };
    unsigned char c[sizeof(P)];
    int outlen, tmplen;
    int ret = 0, test;

    printf("Encryption test from GOST R 34.13-2015 [%s] \n", mode);
    if (!t) {
	printf("  p[%zu] =\n", sizeof(P));
	hexdump(P, sizeof(P));
    }
    EVP_CIPHER_CTX_init(&ctx);
    T(EVP_CipherInit_ex(&ctx, type, NULL, K, iv[t], 1));
    T(EVP_CIPHER_CTX_set_padding(&ctx, 0));
    memset(c, 0, sizeof(c));
    T(EVP_CipherUpdate(&ctx, c, &outlen, P, sizeof(P)));
    T(EVP_CipherFinal_ex(&ctx, c + outlen, &tmplen));
    EVP_CIPHER_CTX_cleanup(&ctx);
    printf("  c[%d] =\n", outlen);
    hexdump(c, outlen);

    TEST_ASSERT(outlen != sizeof(P) ||
	memcmp(c, e[t], sizeof(P)));
    ret |= test;

    if (t == E_CTR) {
	int i;

	printf("Stream encryption test from GOST R 34.13-2015 [%s] \n", mode);
	EVP_CIPHER_CTX_init(&ctx);
	EVP_CipherInit_ex(&ctx, type, NULL, K, iv[t], 1);
	EVP_CIPHER_CTX_set_padding(&ctx, 0);
	memset(c, 0, sizeof(c));
	for (i = 0; i < sizeof(P); i++) {
	    EVP_CipherUpdate(&ctx, c + i, &outlen, P + i, 1);
	    OPENSSL_assert(outlen == 1);
	}
	outlen = i;
	EVP_CipherFinal_ex(&ctx, c + outlen, &tmplen);
	EVP_CIPHER_CTX_cleanup(&ctx);
	printf("  c[%d] =\n", outlen);
	hexdump(c, outlen);

	TEST_ASSERT(outlen != sizeof(P) ||
	    memcmp(c, e[t], sizeof(P)));
#if 0
	ret |= test;
#endif
    }

    printf("Decryption test from GOST R 34.13-2015 [%s] \n", mode);
    EVP_CIPHER_CTX_init(&ctx);
    T(EVP_CipherInit_ex(&ctx, type, NULL, K, iv[t], 0));
    T(EVP_CIPHER_CTX_set_padding(&ctx, 0));
    memset(c, 0, sizeof(c));
    T(EVP_CipherUpdate(&ctx, c, &outlen, e[t], sizeof(P)));
    T(EVP_CipherFinal_ex(&ctx, c + outlen, &tmplen));
    EVP_CIPHER_CTX_cleanup(&ctx);
    printf("  d[%d] =\n", outlen);
    hexdump(c, outlen);

    TEST_ASSERT(outlen != sizeof(P) ||
	memcmp(c, P, sizeof(P)));
    ret |= test;

    return ret;
}

static int test_omac()
{
    EVP_MD_CTX ctx;
    unsigned char e[] = { 0x33,0x6f,0x4d,0x29,0x60,0x59,0xfb,0xe3 };
    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len;
    int test;

    printf("OMAC test from GOST R 34.13-2015\n");
    EVP_MD_CTX_init(&ctx);
    /* preload cbc cipher for omac set key */
    EVP_add_cipher(cipher_gost_grasshopper_cbc());
    T(EVP_DigestInit_ex(&ctx, grasshopper_omac(), NULL));
    if (EVP_MD_CTX_size(&ctx) != 8) {
	/* strip const out of EVP_MD_CTX_md() to
	 * overwrite output size, as test vector is 8 bytes */
	T(EVP_MD_meth_set_result_size((EVP_MD *)EVP_MD_CTX_md(&ctx), 8));
    }
    T(EVP_MD_meth_get_ctrl(EVP_MD_CTX_md(&ctx))(&ctx, EVP_MD_CTRL_SET_KEY, sizeof(K), (void *)K));
    T(EVP_DigestUpdate(&ctx, P, sizeof(P)));
    T(EVP_DigestFinal_ex(&ctx, md_value, &md_len));
    EVP_MD_CTX_cleanup(&ctx);
    printf("  MAC[%u] =\n", md_len);
    hexdump(md_value, md_len);

    TEST_ASSERT(md_len != sizeof(e) ||
	memcmp(e, md_value, md_len));

    return test;
}

int main(int argc, char **argv)
{
    int ret = 0;

    ret |= test_modes(cipher_gost_grasshopper_ecb(), "ecb", E_ECB);
    ret |= test_modes(cipher_gost_grasshopper_ctr(), "ctr", E_CTR);
    /*
     * Other modes (ofb, cbc, cfb) is impossible to test to match GOST R
     * 34.13-2015 test vectors due to these vectors having exceeding IV
     * length value (m) = 256 bits, while openssl have hardcoded limit
     * of maximum IV length of 128 bits (EVP_MAX_IV_LENGTH).
     * Also, current grasshopper code having fixed IV length of 128 bits.
     */

    ret |= test_omac();

    return ret;
}