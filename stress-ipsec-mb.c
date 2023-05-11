/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "stress-ng.h"
#include "core-arch.h"
#include "core-builtin.h"
#include "core-cpu.h"

#if defined(HAVE_INTEL_IPSEC_MB_H)
#include <intel-ipsec-mb.h>
#endif

typedef struct {
	double	ops;
	double	duration;
} ipsec_stats_t;

static const stress_help_t help[] = {
	{ NULL,	"ipsec-mb N",		"start N workers exercising the IPSec MB encoding" },
	{ NULL, "ipsec-mb-feature F",	"specify CPU feature F" },
	{ NULL,	"ipsec-mb-jobs N",	"specify number of jobs to run per round (default 1)" },
	{ NULL,	"ipsec-mb-method M",	"specify crypto/integrity method" },
	{ NULL,	"ipsec-mb-ops N",	"stop after N ipsec bogo encoding operations" },
	{ NULL,	NULL,		  	NULL }
};

static int stress_set_ipsec_mb_feature(const char *opt);
static int stress_set_ipsec_mb_method(const char *opt);

/*
 *  stress_set_ipsec_mb_jobs()
 *      set number of jobs per round
 */
static int stress_set_ipsec_mb_jobs(const char *opt)
{
	int ipsec_mb_jobs;

	ipsec_mb_jobs = (int)stress_get_int32(opt);
	stress_check_range("ipsec-mb-jobs", (uint64_t)ipsec_mb_jobs, 1, 65536);
	return stress_set_setting("ipsec-mb-jobs", TYPE_ID_INT, &ipsec_mb_jobs);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_ipsec_mb_feature,	stress_set_ipsec_mb_feature },
	{ OPT_ipsec_mb_jobs,	stress_set_ipsec_mb_jobs },
	{ OPT_ipsec_mb_method,	stress_set_ipsec_mb_method },
	{ 0,                    NULL }
};

#if defined(HAVE_INTEL_IPSEC_MB_H) &&	\
    defined(HAVE_LIB_IPSEC_MB) &&	\
    defined(STRESS_ARCH_X86_64) &&	\
    defined(IMB_FEATURE_SSE4_2) &&	\
    defined(IMB_FEATURE_CMOV) &&	\
    defined(IMB_FEATURE_AESNI) &&	\
    defined(IMB_FEATURE_AVX) &&		\
    defined(IMB_FEATURE_AVX2) &&	\
    defined(IMB_FEATURE_AVX512_SKX)

typedef void (*ipsec_func_t)(
        const stress_args_t *args,
        struct MB_MGR *mb_mgr,
        const uint8_t *data,
        const size_t data_len,
        const int jobs);

typedef struct {
	ipsec_func_t	func;
	char 		*name;
} stress_ipsec_funcs_t;

typedef struct {
	uint64_t	features;
	void 		(*init_func)(MB_MGR *p_mgr);
	char 		*name;
	bool		supported;
	ipsec_stats_t	stats;
} stress_ipsec_features_t;

static stress_ipsec_features_t mb_features[] = {
	{
		IMB_FEATURE_AVX | IMB_FEATURE_CMOV | IMB_FEATURE_AESNI,
		init_mb_mgr_avx,
		"avx",
		false,
		{ 0.0, 0.0 }
	},
	{
		IMB_FEATURE_AVX2 | IMB_FEATURE_AVX | IMB_FEATURE_CMOV |
		IMB_FEATURE_AESNI,
		init_mb_mgr_avx2,
		"avx2",
		false,
		{ 0.0, 0.0 }
	},
	{
		IMB_FEATURE_AVX512_SKX | IMB_FEATURE_AVX2 |IMB_FEATURE_AVX |
		IMB_FEATURE_CMOV | IMB_FEATURE_AESNI,
		init_mb_mgr_avx512,
		"avx512",
		false,
		{ 0.0, 0.0 }
	},
	{
		IMB_FEATURE_SSE4_2 | IMB_FEATURE_CMOV,
		init_mb_mgr_sse,
		"noaesni",
		false,
		{ 0.0, 0.0 }
	},
	{
		IMB_FEATURE_SSE4_2 | IMB_FEATURE_CMOV | IMB_FEATURE_AESNI,
		init_mb_mgr_sse,
		"sse",
		false,
		{ 0.0, 0.0 }
	},
};

static int stress_set_ipsec_mb_feature(const char *opt)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(mb_features); i++) {
		if (!strcmp(opt, mb_features[i].name))
			return stress_set_setting("ipsec-mb-feature", TYPE_ID_SIZE_T, &i);
	}

	(void)fprintf(stderr, "invalid ipsec-mb-feature '%s', allowed options are:", opt);
	for (i = 0; i < SIZEOF_ARRAY(mb_features); i++) {
		(void)fprintf(stderr, " %s", mb_features[i].name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_ipsec_mb_features()
 *	get list of CPU feature bits
 */
static uint64_t stress_ipsec_mb_features(const stress_args_t *args, const MB_MGR *p_mgr)
{
	const uint64_t features = p_mgr->features;

	if (args->instance == 0) {
		char str[256] = "";
		size_t i;

		for (i = 0; i < SIZEOF_ARRAY(mb_features); i++) {
			if ((features & mb_features[i].features) == mb_features[i].features) {
				shim_strlcat(str, " ", sizeof(str));
				shim_strlcat(str, mb_features[i].name, sizeof(str));
			}
		}

		pr_inf("%s: features:%s\n", args->name, str);
	}
	return features;
}

/*
 *  stress_ipsec_mb_supported()
 *	check if ipsec_mb is supported
 */
static int stress_ipsec_mb_supported(const char *name)
{
	/* Intel CPU? */
	if (!stress_cpu_is_x86()) {
		pr_inf_skip("%s stressor will be skipped, "
			"not a recognised Intel CPU\n", name);
		return -1;
	}
	return 0;
}

/*
 *  stress_rnd_fill()
 *	fill uint32_t aligned buf with n bytes of random data
 */
static void stress_rnd_fill(uint8_t *buf, const size_t n)
{
	register uint8_t *ptr = buf;
	register uint8_t *end = buf + n;

	while (ptr < end)
		*(ptr++) = stress_mwc8();
}

/*
 *  stress_job_empty()
 *	empty job queue
 */
static inline void stress_job_empty(struct MB_MGR *mb_mgr)
{
	while (IMB_FLUSH_JOB(mb_mgr))
		;
}

static inline struct JOB_AES_HMAC *stress_job_get_next(struct MB_MGR *mb_mgr)
{
	struct JOB_AES_HMAC *job = IMB_GET_NEXT_JOB(mb_mgr);

	(void)shim_memset(job, 0, sizeof(*job));
	return job;
}

/*
 *  stress_job_check_status()
 *	check if jobs has completed, report error if not
 */
static void stress_job_check_status(
	const stress_args_t *args,
	const char *name,
	const struct JOB_AES_HMAC *job,
	int *jobs_done)
{
	if (job->status != STS_COMPLETED) {
		pr_err("%s: %s: job not completed\n",
			args->name, name);
	} else {
		(*jobs_done)++;
		inc_counter(args);
	}
}

/*
 *  stress_jobs_done()
 *  	check if all the jobs have completed
 */
static void stress_jobs_done(
	const stress_args_t *args,
	const char *name,
	const int jobs,
	const int jobs_done)
{
	if (jobs_done != jobs)
		pr_err("%s: %s: only processed %d of %d jobs\n",
			args->name, name, jobs_done, jobs);
}

static void *stress_alloc_aligned(const size_t nmemb, const size_t size, const size_t alignment)
{
#if defined(HAVE_POSIX_MEMALIGN)
	const size_t sz = nmemb * size;
	void *ptr;

	if (posix_memalign(&ptr, alignment, sz) == 0)
		return ptr;
#elif defined(HAVE_ALIGNED_ALLOC)
	const size_t sz = nmemb * size;

	return aligned_alloc(alignment, sz);
#elif defined(HAVE_MEMALIGN)
	const size_t sz = nmemb * size;

	return memalign(aiglment, sz);
#endif
	return NULL;
}

#define SHA_DIGEST_SIZE		(64)

static void stress_ipsec_sha(
	const stress_args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs)
{
	int j, jobs_done = 0;
	struct JOB_AES_HMAC *job;
	uint8_t padding[16];
	const size_t alloc_len = SHA_DIGEST_SIZE + (sizeof(padding) * 2);
	uint8_t *auth;
	uint8_t *auth_data;
	static const char name[] = "sha";

	auth_data = (uint8_t *)stress_alloc_aligned((size_t)jobs, alloc_len, 16);
	if (!auth_data)
		return;

	stress_job_empty(mb_mgr);

	for (auth = auth_data, j = 0; j < jobs; j++, auth += alloc_len) {
		job = stress_job_get_next(mb_mgr);
		job->cipher_direction = ENCRYPT;
		job->chain_order = HASH_CIPHER;
		job->auth_tag_output = auth + sizeof(padding);
		job->auth_tag_output_len_in_bytes = SHA_DIGEST_SIZE;
		job->src = data;
		job->msg_len_to_hash_in_bytes = data_len;
		job->cipher_mode = NULL_CIPHER;
		job->hash_alg = PLAIN_SHA_512;
		job->user_data = auth;
		job = IMB_SUBMIT_JOB(mb_mgr);
		if (job)
			stress_job_check_status(args, name, job, &jobs_done);
	}

	while ((job = IMB_FLUSH_JOB(mb_mgr)) != NULL)
		stress_job_check_status(args, name, job, &jobs_done);

	stress_jobs_done(args, name, jobs, jobs_done);
	stress_job_empty(mb_mgr);
	free(auth_data);
}

static void stress_ipsec_des(
	const stress_args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs)
{
	int j, jobs_done = 0;
	struct JOB_AES_HMAC *job;

	uint8_t *encoded;
	uint8_t k[32] ALIGNED(16);
	uint8_t iv[16] ALIGNED(16);
	uint32_t enc_keys[15 * 4] ALIGNED(16);
	uint32_t dec_keys[15 * 4] ALIGNED(16);
	uint8_t *dst;
	static const char name[] = "des";

	encoded = (uint8_t *)stress_alloc_aligned((size_t)jobs, data_len, 16);
	if (!encoded)
		return;

	stress_rnd_fill(k, sizeof(k));
	stress_rnd_fill(iv, sizeof(iv));
	stress_job_empty(mb_mgr);
	IMB_AES_KEYEXP_256(mb_mgr, k, enc_keys, dec_keys);

	for (dst = encoded, j = 0; j < jobs; j++, dst += data_len) {
		job = stress_job_get_next(mb_mgr);
		job->cipher_direction = ENCRYPT;
		job->chain_order = CIPHER_HASH;
		job->src = data;
		job->dst = dst;
		job->cipher_mode = CBC;
		job->aes_enc_key_expanded = enc_keys;
		job->aes_dec_key_expanded = dec_keys;
		job->aes_key_len_in_bytes = sizeof(k);
		job->iv = iv;
		job->iv_len_in_bytes = sizeof(iv);
		job->cipher_start_src_offset_in_bytes = 0;
		job->msg_len_to_cipher_in_bytes = data_len;
		job->user_data = dst;
		job->user_data2 = (void *)((uint64_t)j);
		job->hash_alg = NULL_HASH;
		job = IMB_SUBMIT_JOB(mb_mgr);
		if (job)
			stress_job_check_status(args, name, job, &jobs_done);
	}

	while ((job = IMB_FLUSH_JOB(mb_mgr)) != NULL)
		stress_job_check_status(args, name, job, &jobs_done);

	stress_jobs_done(args, name, jobs, jobs_done);
	stress_job_empty(mb_mgr);
	free(encoded);
}

static void stress_ipsec_cmac(
	const stress_args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs)
{
	int j, jobs_done = 0;
	struct JOB_AES_HMAC *job;

	uint8_t key[16] ALIGNED(16);
	uint32_t expkey[4 * 15] ALIGNED(16);
	uint32_t dust[4 * 15] ALIGNED(16);
	uint32_t skey1[4], skey2[4];
	uint8_t *output;
	static const char name[] = "cmac";
	uint8_t *dst;

	output = (uint8_t *)stress_alloc_aligned((size_t)jobs, data_len, 16);
	if (!output)
		return;

	stress_rnd_fill(key, sizeof(key));
	IMB_AES_KEYEXP_128(mb_mgr, key, expkey, dust);
	IMB_AES_CMAC_SUBKEY_GEN_128(mb_mgr, expkey, skey1, skey2);
	stress_job_empty(mb_mgr);

	for (dst = output, j = 0; j < jobs; j++, dst += 16) {
		job = stress_job_get_next(mb_mgr);
		job->cipher_direction = ENCRYPT;
		job->chain_order = HASH_CIPHER;
		job->cipher_mode = NULL_CIPHER;
		job->hash_alg = AES_CMAC;
		job->src = data;
		job->hash_start_src_offset_in_bytes = 0;
		job->msg_len_to_hash_in_bytes = data_len;
		job->auth_tag_output = dst;
		job->auth_tag_output_len_in_bytes = 16;
		job->u.CMAC._key_expanded = expkey;
		job->u.CMAC._skey1 = skey1;
		job->u.CMAC._skey2 = skey2;
		job->user_data = dst;
		job = IMB_SUBMIT_JOB(mb_mgr);
		if (job)
			stress_job_check_status(args, name, job, &jobs_done);
	}

	while ((job = IMB_FLUSH_JOB(mb_mgr)) != NULL)
		stress_job_check_status(args, name, job, &jobs_done);

	stress_jobs_done(args, name, jobs, jobs_done);
	stress_job_empty(mb_mgr);
	free(output);
}

static void stress_ipsec_ctr(
	const stress_args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs)
{
	int j, jobs_done = 0;
	struct JOB_AES_HMAC *job;

	uint8_t *encoded;
	uint8_t key[32] ALIGNED(16);
	uint8_t iv[12] ALIGNED(16);		/* 4 byte nonce + 8 byte IV */
	uint32_t expkey[4 * 15] ALIGNED(16);
	uint32_t dust[4 * 15] ALIGNED(16);
	uint8_t *dst;
	static const char name[] = "ctr";

	encoded = (uint8_t *)stress_alloc_aligned((size_t)jobs, data_len, 16);
	if (!encoded)
		return;

	stress_rnd_fill(key, sizeof(key));
	stress_rnd_fill(iv, sizeof(iv));
	IMB_AES_KEYEXP_256(mb_mgr, key, expkey, dust);
	stress_job_empty(mb_mgr);

	for (dst = encoded, j = 0; j < jobs; j++, dst += data_len) {
		job = stress_job_get_next(mb_mgr);
		job->cipher_direction = ENCRYPT;
		job->chain_order = CIPHER_HASH;
		job->cipher_mode = CNTR;
		job->hash_alg = NULL_HASH;
		job->src = data;
		job->dst = dst;
		job->aes_enc_key_expanded = expkey;
		job->aes_dec_key_expanded = expkey;
		job->aes_key_len_in_bytes = sizeof(key);
		job->iv = iv;
		job->iv_len_in_bytes = sizeof(iv);
		job->cipher_start_src_offset_in_bytes = 0;
		job->msg_len_to_cipher_in_bytes = data_len;
		job = IMB_SUBMIT_JOB(mb_mgr);
		if (job)
			stress_job_check_status(args, name, job, &jobs_done);
	}

	while ((job = IMB_FLUSH_JOB(mb_mgr)) != NULL)
		stress_job_check_status(args, name, job, &jobs_done);

	stress_jobs_done(args, name, jobs, jobs_done);
	stress_job_empty(mb_mgr);
	free(encoded);
}

#define HMAC_MD5_DIGEST_SIZE	(16)
#define MMAC_MD5_BLOCK_SIZE	(64)

static void stress_ipsec_hmac_md5(
	const stress_args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs)
{
	int j, jobs_done = 0;
	size_t i;
	struct JOB_AES_HMAC *job;

	uint8_t key[MMAC_MD5_BLOCK_SIZE] ALIGNED(16);
	uint8_t buf[MMAC_MD5_BLOCK_SIZE] ALIGNED(16);
	uint8_t ipad_hash[HMAC_MD5_DIGEST_SIZE] ALIGNED(16);
	uint8_t opad_hash[HMAC_MD5_DIGEST_SIZE] ALIGNED(16);
	uint8_t *output;
	uint8_t *dst;
	static const char name[] = "hmac_md5";

	output = (uint8_t *)stress_alloc_aligned((size_t)jobs, HMAC_MD5_DIGEST_SIZE, 16);
	if (!output)
		return;

	stress_rnd_fill(key, sizeof(key));
	for (i = 0; i < sizeof(key); i++)
		buf[i] = key[i] ^ 0x36;
	IMB_MD5_ONE_BLOCK(mb_mgr, buf, ipad_hash);
	for (i = 0; i < sizeof(key); i++)
		buf[i] = key[i] ^ 0x5c;
	IMB_MD5_ONE_BLOCK(mb_mgr, buf, opad_hash);

	stress_job_empty(mb_mgr);

	for (dst = output, j = 0; j < jobs; j++, dst += HMAC_MD5_DIGEST_SIZE) {
		job = stress_job_get_next(mb_mgr);
		job->aes_enc_key_expanded = NULL;
		job->aes_dec_key_expanded = NULL;
		job->cipher_direction = ENCRYPT;
		job->chain_order = HASH_CIPHER;
		job->dst = NULL;
		job->aes_key_len_in_bytes = 0;
		job->auth_tag_output = dst;
		job->auth_tag_output_len_in_bytes = HMAC_MD5_DIGEST_SIZE;
		job->iv = NULL;
		job->iv_len_in_bytes = 0;
		job->src = data;
		job->cipher_start_src_offset_in_bytes = 0;
		job->msg_len_to_cipher_in_bytes = 0;
		job->hash_start_src_offset_in_bytes = 0;
		job->msg_len_to_hash_in_bytes = data_len;
		job->u.HMAC._hashed_auth_key_xor_ipad = ipad_hash;
		job->u.HMAC._hashed_auth_key_xor_opad = opad_hash;
		job->cipher_mode = NULL_CIPHER;
		job->hash_alg = MD5;
		job->user_data = dst;
		job = IMB_SUBMIT_JOB(mb_mgr);
		if (job)
			stress_job_check_status(args, name, job, &jobs_done);
	}

	while ((job = IMB_FLUSH_JOB(mb_mgr)) != NULL)
		stress_job_check_status(args, name, job, &jobs_done);

	stress_jobs_done(args, name, jobs, jobs_done);
	stress_job_empty(mb_mgr);
	free(output);
}

#define HMAC_SHA1_DIGEST_SIZE	(20)
#define HMAC_SHA1_BLOCK_SIZE	(64)

static void stress_ipsec_hmac_sha1(
	const stress_args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs)
{
	int j, jobs_done = 0;
	size_t i;
	struct JOB_AES_HMAC *job;

	uint8_t key[HMAC_SHA1_BLOCK_SIZE] ALIGNED(16);
	uint8_t buf[HMAC_SHA1_BLOCK_SIZE] ALIGNED(16);
	uint8_t ipad_hash[HMAC_SHA1_DIGEST_SIZE] ALIGNED(16);
	uint8_t opad_hash[HMAC_SHA1_DIGEST_SIZE] ALIGNED(16);
	uint8_t *output;
	uint8_t *dst;
	static const char name[] = "hmac_sha1";

	output = (uint8_t *)stress_alloc_aligned((size_t)jobs, HMAC_SHA1_DIGEST_SIZE, 16);
	if (!output)
		return;

	stress_rnd_fill(key, sizeof(key));
	for (i = 0; i < sizeof(key); i++)
		buf[i] = key[i] ^ 0x36;
	IMB_MD5_ONE_BLOCK(mb_mgr, buf, ipad_hash);
	for (i = 0; i < sizeof(key); i++)
		buf[i] = key[i] ^ 0x5c;
	IMB_MD5_ONE_BLOCK(mb_mgr, buf, opad_hash);

	stress_job_empty(mb_mgr);

	for (dst = output, j = 0; j < jobs; j++, dst += HMAC_SHA1_DIGEST_SIZE) {
		job = stress_job_get_next(mb_mgr);
		job->aes_enc_key_expanded = NULL;
		job->aes_dec_key_expanded = NULL;
		job->cipher_direction = ENCRYPT;
		job->chain_order = HASH_CIPHER;
		job->dst = NULL;
		job->aes_key_len_in_bytes = 0;
		job->auth_tag_output = dst;
		job->auth_tag_output_len_in_bytes = HMAC_SHA1_DIGEST_SIZE;
		job->iv = NULL;
		job->iv_len_in_bytes = 0;
		job->src = data;
		job->cipher_start_src_offset_in_bytes = 0;
		job->msg_len_to_cipher_in_bytes = 0;
		job->hash_start_src_offset_in_bytes = 0;
		job->msg_len_to_hash_in_bytes = data_len;
		job->u.HMAC._hashed_auth_key_xor_ipad = ipad_hash;
		job->u.HMAC._hashed_auth_key_xor_opad = opad_hash;
		job->cipher_mode = NULL_CIPHER;
		job->hash_alg = SHA1;
		job->user_data = dst;
		job = IMB_SUBMIT_JOB(mb_mgr);
		if (job)
			stress_job_check_status(args, name, job, &jobs_done);
	}

	while ((job = IMB_FLUSH_JOB(mb_mgr)) != NULL)
		stress_job_check_status(args, name, job, &jobs_done);

	stress_jobs_done(args, name, jobs, jobs_done);
	stress_job_empty(mb_mgr);
	free(output);
}

static void stress_ipsec_hmac_sha512(
	const stress_args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs)
{
	int j, jobs_done = 0;
	size_t i;
	struct JOB_AES_HMAC *job;

	uint8_t rndkey[SHA_512_BLOCK_SIZE] ALIGNED(16);
	uint8_t key[SHA_512_BLOCK_SIZE] ALIGNED(16);
	uint8_t buf[SHA_512_BLOCK_SIZE] ALIGNED(16);
	uint8_t ipad_hash[SHA512_DIGEST_SIZE_IN_BYTES] ALIGNED(16);
	uint8_t opad_hash[SHA512_DIGEST_SIZE_IN_BYTES] ALIGNED(16);
	uint8_t *output;
	uint8_t *dst;
	static const char name[] = "hmac_sha512";

	output = (uint8_t *)stress_alloc_aligned((size_t)jobs, SHA512_DIGEST_SIZE_IN_BYTES, 16);
	if (!output)
		return;

	stress_rnd_fill(rndkey, sizeof(rndkey));
	(void)shim_memset(key, 0, sizeof(key));

	IMB_SHA512(mb_mgr, rndkey, SHA_512_BLOCK_SIZE, key);

	for (i = 0; i < sizeof(key); i++)
		buf[i] = key[i] ^ 0x36;
	IMB_SHA512_ONE_BLOCK(mb_mgr, buf, ipad_hash);
	for (i = 0; i < sizeof(key); i++)
		buf[i] = key[i] ^ 0x5c;
	IMB_SHA512_ONE_BLOCK(mb_mgr, buf, opad_hash);

	stress_job_empty(mb_mgr);

	for (dst = output, j = 0; j < jobs; j++, dst += SHA512_DIGEST_SIZE_IN_BYTES) {
		job = stress_job_get_next(mb_mgr);
		job->aes_enc_key_expanded = NULL;
		job->aes_dec_key_expanded = NULL;
		job->cipher_direction = ENCRYPT;
		job->chain_order = HASH_CIPHER;
		job->dst = NULL;
		job->aes_key_len_in_bytes = 0;
		job->auth_tag_output = dst;
		job->auth_tag_output_len_in_bytes = SHA512_DIGEST_SIZE_IN_BYTES;
		job->iv = NULL;
		job->iv_len_in_bytes = 0;
		job->src = data;
		job->cipher_start_src_offset_in_bytes = 0;
		job->msg_len_to_cipher_in_bytes = 0;
		job->hash_start_src_offset_in_bytes = 0;
		job->msg_len_to_hash_in_bytes = data_len;
		job->u.HMAC._hashed_auth_key_xor_ipad = ipad_hash;
		job->u.HMAC._hashed_auth_key_xor_opad = opad_hash;
		job->cipher_mode = NULL_CIPHER;
		job->hash_alg = SHA_512;
		job->user_data = dst;
		job = IMB_SUBMIT_JOB(mb_mgr);
		if (job)
			stress_job_check_status(args, name, job, &jobs_done);
	}

	while ((job = IMB_FLUSH_JOB(mb_mgr)) != NULL)
		stress_job_check_status(args, name, job, &jobs_done);

	stress_jobs_done(args, name, jobs, jobs_done);
	stress_job_empty(mb_mgr);
	free(output);
}

static void stress_ipsec_all(
	const stress_args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs);

static stress_ipsec_funcs_t stress_ipsec_funcs[] = {
	{ stress_ipsec_all,		"all",		},
	{ stress_ipsec_cmac,		"cmac",		},
	{ stress_ipsec_ctr,		"ctr",		},
	{ stress_ipsec_des,		"des",		},
	{ stress_ipsec_hmac_md5,	"hmac-md5",	},
	{ stress_ipsec_hmac_sha1,	"hmac-sha1",	},
	{ stress_ipsec_hmac_sha512,	"hmac-sha512",	},
	{ stress_ipsec_sha,		"sha",		},
};

static void stress_ipsec_call_func(
        const stress_args_t *args,
        struct MB_MGR *mb_mgr,
        const uint8_t *data,
        const size_t data_len,
        const int jobs,
	const size_t func_index)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(mb_features); i++) {
		if (mb_features[i].supported) {
			double t;
			uint64_t c;

			c = get_counter(args);
			t = stress_time_now();

			mb_features[i].init_func(mb_mgr);
			stress_ipsec_funcs[func_index].func(args, mb_mgr, data, data_len, jobs);

			mb_features[i].stats.duration += (stress_time_now() - t);
			mb_features[i].stats.ops += (double)(get_counter(args) - c);
		}
	}
}

/*
 *  stress_ipsec_all()
 *	exercise all
 */
static void stress_ipsec_all(
	const stress_args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs)
{
	size_t i;

	for (i = 1; keep_stressing(args) && (i < SIZEOF_ARRAY(stress_ipsec_funcs)); i++)
		stress_ipsec_call_func(args, mb_mgr, data, data_len, jobs, i);
}

static int stress_set_ipsec_mb_method(const char *opt)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_ipsec_funcs); i++) {
		if (!strcmp(opt, stress_ipsec_funcs[i].name))
			return stress_set_setting("ipsec-mb-method", TYPE_ID_SIZE_T, &i);
	}

	(void)fprintf(stderr, "invalid ipsec-mb-method '%s', allowed options are:", opt);
	for (i = 0; i < SIZEOF_ARRAY(stress_ipsec_funcs); i++) {
		(void)fprintf(stderr, " %s", stress_ipsec_funcs[i].name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_ipsec_mb()
 *      stress Intel ipsec_mb instruction
 */
static int stress_ipsec_mb(const stress_args_t *args)
{
	MB_MGR *mb_mgr = NULL;
	uint64_t features;
	uint8_t data[8192] ALIGNED(64);
	size_t i, j;
	bool got_features = false;
	size_t ipsec_mb_feature = 0;
	size_t ipsec_mb_method = 0;
	int ipsec_mb_jobs = 128;

	(void)stress_get_setting("ipsec-mb-jobs", &ipsec_mb_jobs);
	(void)stress_get_setting("ipsec-mb-method", &ipsec_mb_method);

	if (imb_get_version() < IMB_VERSION(0, 51, 0)) {
		if (args->instance == 0)
			pr_inf_skip("%s: version %s of Intel IPSec MB library is too low, skipping\n",
				args->name, imb_get_version_str());
		return EXIT_NOT_IMPLEMENTED;
	}

	mb_mgr = alloc_mb_mgr(0);
	if (!mb_mgr) {
		if (args->instance == 0)
			pr_inf_skip("%s: failed to setup Intel IPSec MB library, skipping\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	features = stress_ipsec_mb_features(args, mb_mgr);

	for (i = 0; i < SIZEOF_ARRAY(mb_features); i++) {
		mb_features[i].supported =
				((mb_features[i].features & features) == mb_features[i].features);
		got_features |= mb_features[i].supported;
		mb_features[i].stats.ops = 0.0;
		mb_features[i].stats.duration = 0.0;
	}
	if (!got_features) {
		if (args->instance == 0)
			pr_inf_skip("%s: not enough CPU features to support Intel IPSec MB library, skipping\n", args->name);
		free_mb_mgr(mb_mgr);
		return EXIT_NOT_IMPLEMENTED;
	}

	if (stress_get_setting("ipsec-mb-feature", &ipsec_mb_feature)) {
		const char *feature_name = mb_features[ipsec_mb_feature].name;

		if (!mb_features[ipsec_mb_feature].supported) {
			if (args->instance == 0) {

				pr_inf_skip("%s: requested ipsec-mb-feature feature '%s' is not supported, skipping\n",
					args->name, feature_name);
			}
			free_mb_mgr(mb_mgr);
			return EXIT_NOT_IMPLEMENTED;
		}
		for (i = 0; i < SIZEOF_ARRAY(mb_features); i++) {
			mb_features[i].supported = (i == ipsec_mb_feature);
		}
		if (args->instance == 0)
			pr_inf("%s: using just feature '%s'\n", args->name, feature_name);
	}

	stress_rnd_fill(data, sizeof(data));
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		for (i = 0; i < SIZEOF_ARRAY(mb_features); i++) {
			if (mb_features[i].supported) {
				mb_features[i].init_func(mb_mgr);
				stress_ipsec_call_func(args, mb_mgr, data, sizeof(data), ipsec_mb_jobs, ipsec_mb_method);
			}
		}
	} while (keep_stressing(args));

	pr_lock();
	for (i = 0, j = 0; i < SIZEOF_ARRAY(mb_features); i++) {
		const ipsec_stats_t *stats = &mb_features[i].stats;

		if (stats->duration > 0.0) {
			char tmp[32];
			const double rate = stats->ops / stats->duration;
			const char *name = mb_features[i].name;

			pr_dbg("%s: %s %.3f bogo ops per sec\n",
				args->name, name, rate);

			(void)snprintf(tmp, sizeof(tmp), "%s bogo ops per sec", name);
			stress_metrics_set(args, j, tmp, rate);
			j++;
		}
	}
	pr_unlock();

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free_mb_mgr(mb_mgr);

	return EXIT_SUCCESS;
}

stressor_info_t stress_ipsec_mb_info = {
	.stressor = stress_ipsec_mb,
	.supported = stress_ipsec_mb_supported,
	.opt_set_funcs = opt_set_funcs,
	.class = CLASS_CPU,
	.help = help
};
#else

static int stress_set_ipsec_mb_method(const char *opt)
{
	(void)opt;

	pr_inf("option --ipsec-mb-method not supported on this system.\n");
	return -1;
}

static int stress_set_ipsec_mb_feature(const char *opt)
{
	(void)opt;

	pr_inf("option --ipsec-mb-feature not supported on this system.\n");
	return -1;
}

static int stress_ipsec_mb_supported(const char *name)
{
	pr_inf_skip("%s: stressor will be skipped, CPU "
		"needs to be an x86-64 and a recent IPSec MB library "
		"is required.\n", name);
	return -1;
}

stressor_info_t stress_ipsec_mb_info = {
	.stressor = stress_unimplemented,
	.supported = stress_ipsec_mb_supported,
	.opt_set_funcs = opt_set_funcs,
	.class = CLASS_CPU,
	.help = help,
	.unimplemented_reason = "built on non-x86-64 without IPSec MB library"
};
#endif
