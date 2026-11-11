/*
 * Compact AES-128 (FIPS-197), encrypt and decrypt of single blocks.
 * SPDX-License-Identifier: MIT
 */
#ifndef AES128_H
#define AES128_H
#include <stdint.h>

struct aes128 {
    uint8_t rk[176];        /* expanded key */
};

void aes128_init(struct aes128 *ctx, const uint8_t key[16]);
void aes128_encrypt(const struct aes128 *ctx, uint8_t *block);
void aes128_decrypt(const struct aes128 *ctx, uint8_t *block);
#endif
