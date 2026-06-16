// SPDX-License-Identifier: MIT
/*
 * hash_chain_minimal.c
 *
 * Minimal hash-chain example for Root of Trust / Measured Boot learning.
 *
 * Model:
 *
 *   chain_0 = 32 bytes of zero
 *
 *   image_hash_i = SHA256(image_i)
 *   chain_i      = SHA256(chain_{i-1} || image_hash_i)
 *
 * This is similar in spirit to TPM PCR extend:
 *
 *   PCR_new = H(PCR_old || measurement)
 *
 * This example does NOT verify signatures.
 */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#define SHA256_LEN 32
#define READ_CHUNK 4096

static void print_hex(const char *label, const uint8_t *buf, size_t len)
{
	size_t i;

	printf("%s", label);

	for (i = 0; i < len; i++)
		printf("%02x", buf[i]);

	printf("\n");
}

static int sha256_file(const char *path, uint8_t out[SHA256_LEN])
{
	EVP_MD_CTX *ctx;
	FILE *fp;
	uint8_t buf[READ_CHUNK];
	size_t n;
	unsigned int out_len = 0;
	int ret = -1;

	fp = fopen(path, "rb");
	if (!fp) {
		fprintf(stderr, "failed to open %s: %s\n",
			path, strerror(errno));
		return -1;
	}

	ctx = EVP_MD_CTX_new();
	if (!ctx) {
		fprintf(stderr, "EVP_MD_CTX_new failed\n");
		goto out_close;
	}

	if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) {
		fprintf(stderr, "EVP_DigestInit_ex failed\n");
		goto out_free;
	}

	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		if (EVP_DigestUpdate(ctx, buf, n) != 1) {
			fprintf(stderr, "EVP_DigestUpdate failed\n");
			goto out_free;
		}
	}

	if (ferror(fp)) {
		fprintf(stderr, "failed to read %s\n", path);
		goto out_free;
	}

	if (EVP_DigestFinal_ex(ctx, out, &out_len) != 1) {
		fprintf(stderr, "EVP_DigestFinal_ex failed\n");
		goto out_free;
	}

	if (out_len != SHA256_LEN) {
		fprintf(stderr, "unexpected sha256 length: %u\n", out_len);
		goto out_free;
	}

	ret = 0;

out_free:
	EVP_MD_CTX_free(ctx);

out_close:
	fclose(fp);
	return ret;
}

static int extend_chain(const uint8_t old_chain[SHA256_LEN],
			const uint8_t measurement[SHA256_LEN],
			uint8_t new_chain[SHA256_LEN])
{
	EVP_MD_CTX *ctx;
	unsigned int out_len = 0;
	int ret = -1;

	ctx = EVP_MD_CTX_new();
	if (!ctx) {
		fprintf(stderr, "EVP_MD_CTX_new failed\n");
		return -1;
	}

	if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1)
		goto out;

	if (EVP_DigestUpdate(ctx, old_chain, SHA256_LEN) != 1)
		goto out;

	if (EVP_DigestUpdate(ctx, measurement, SHA256_LEN) != 1)
		goto out;

	if (EVP_DigestFinal_ex(ctx, new_chain, &out_len) != 1)
		goto out;

	if (out_len != SHA256_LEN)
		goto out;

	ret = 0;

out:
	EVP_MD_CTX_free(ctx);
	return ret;
}

int main(int argc, char **argv)
{
	uint8_t chain[SHA256_LEN] = {0};
	uint8_t measurement[SHA256_LEN];
	uint8_t new_chain[SHA256_LEN];
	int i;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <component> [component...]\n", argv[0]);
		fprintf(stderr, "\n");
		fprintf(stderr, "example:\n");
		fprintf(stderr, "  %s tfa.bin uboot.bin Image board.dtb\n", argv[0]);
		return 1;
	}

	print_hex("initial chain: ", chain, SHA256_LEN);
	printf("\n");

	for (i = 1; i < argc; i++) {
		printf("component[%d]: %s\n", i, argv[i]);

		if (sha256_file(argv[i], measurement))
			return 1;

		print_hex("  measurement: ", measurement, SHA256_LEN);

		if (extend_chain(chain, measurement, new_chain)) {
			fprintf(stderr, "failed to extend hash chain\n");
			return 1;
		}

		memcpy(chain, new_chain, SHA256_LEN);
		print_hex("  chain:       ", chain, SHA256_LEN);
		printf("\n");
	}

	print_hex("final chain digest: ", chain, SHA256_LEN);

	printf("\n");
	printf("Important:\n");
	printf("  This digest only proves the measured sequence.\n");
	printf("  It becomes a security boundary only if protected or signed.\n");

	return 0;
}
