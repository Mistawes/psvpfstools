#pragma once

#include <stdint.h>

//############## LEVEL 2 - CRYPTO WRAPPER SELECTORS ###############

#define PFS_CRYPTO_USE_CMAC   0x0001 //1
#define PFS_CRYPTO_USE_KEYGEN 0x0002 //2

//#### GROUP 1, GROUP 2 (hw dec/enc) ####

int pfs_decrypt_hw(const unsigned char* key, const unsigned char* iv_xor_key, int tweak_key0, int tweak_key1, uint32_t size, uint32_t block_size, const unsigned char* src, unsigned char* dst, uint16_t flag, uint16_t key_id);

int pfs_encrypt_hw(const unsigned char* key, const unsigned char* iv_xor_key, int tweak_key0, int tweak_key1, uint32_t size, uint32_t block_size, const unsigned char* src, unsigned char* dst, uint16_t flag, uint16_t key_id);

int pfs_decrypt_sw(const unsigned char* key, const unsigned char* subkey_key, uint32_t keysize, int tweak_key0, int tweak_key1, uint32_t size, uint32_t block_size, const unsigned char* src, unsigned char* dst, uint16_t flag);

int pfs_encrypt_sw(const unsigned char* key, const unsigned char* subkey_key, uint32_t keysize, int tweak_key0, int tweak_key1, uint32_t size, uint32_t block_size, const unsigned char* src, unsigned char* dst, uint16_t flag);