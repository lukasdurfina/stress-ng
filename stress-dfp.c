/*
 * Copyright (C) 2024      Colin Ian King
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
#include "core-attribute.h"
#include "core-madvise.h"
#include "core-put.h"
#include "core-target-clones.h"

#define LOOPS_PER_CALL	(65536)
#define DFP_ELEMENTS	(8)

/* Currently division with float 80 on ICC trips SIGFPEs, so disable */
#if defined(__ICC)
#undef HAVE__float80
#endif
#if defined(__OpenBSD__)
#undef HAVE__float128
#endif

#define STRESS_DFP_TYPE_DECIMAL32	(0)
#define STRESS_DFP_TYPE_DECIMAL64	(1)
#define STRESS_DFP_TYPE_DECIMAL128	(2)
#define STRESS_DFP_TYPE_ALL		(3)

static const stress_help_t help[] = {
	{ NULL,	"dfp N", 	"start N workers performing decimal floating point math ops" },
	{ NULL,	"dfp-method M",	"select the decimal floating point method to operate with" },
	{ NULL,	"dfp-ops N",	"stop after N decimal floating point math bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_Decimal32) ||	\
    defined(HAVE_Decimal64) ||	\
    defined(HAVE_Decimal128)

typedef struct {
#if defined(HAVE_Decimal32)
	struct {
		_Decimal32 r_init;	/* initialization value for r */
		_Decimal32 r[2];	/* result of computation */
		_Decimal32 add;	/* value to add */
		_Decimal32 add_rev;	/* value to add to revert back */
		_Decimal32 mul;	/* value to multiply */
		_Decimal32 mul_rev;	/* value to multiply to revert back */
	} d32;
#endif
#if defined(HAVE_Decimal64)
	struct {
		_Decimal64 r_init;	/* initialization value for r */
		_Decimal64 r[2];	/* result of computation */
		_Decimal64 add;	/* value to add */
		_Decimal64 add_rev;	/* value to add to revert back */
		_Decimal64 mul;	/* value to multiply */
		_Decimal64 mul_rev;	/* value to multiply to revert back */
	} d64;
#endif
#if defined(HAVE_Decimal128)
	struct {
		_Decimal128 r_init;	/* initialization value for r */
		_Decimal128 r[2];	/* result of computation */
		_Decimal128 add;	/* value to add */
		_Decimal128 add_rev;	/* value to add to revert back */
		_Decimal128 mul;	/* value to multiply */
		_Decimal128 mul_rev;	/* value to multiply to revert back */
	} d128;
#endif
} dfp_data_t;

typedef double (*stress_dfp_func_t)(
	stress_args_t *args,
	dfp_data_t *dfp_data,
	const int idx);

static double stress_dfp_all(
	stress_args_t *args,
	dfp_data_t *dfp_data,
	const int idx);

#define STRESS_DFP_ADD(field, name, do_bogo_ops)				\
static double TARGET_CLONES OPTIMIZE3 name(				\
	stress_args_t *args,						\
	dfp_data_t *dfp_data,						\
	const int idx)							\
{									\
	register int i;							\
	const int loops = LOOPS_PER_CALL >> 1;				\
	double t1, t2;							\
									\
	for (i = 0; i < DFP_ELEMENTS; i++) {				\
		dfp_data[i].field.r[idx] = dfp_data[i].field.r_init;	\
	}								\
									\
	t1 = stress_time_now();						\
	for (i = 0; i < loops ; i++) {					\
		dfp_data[0].field.r[idx] += dfp_data[0].field.add;	\
		dfp_data[0].field.r[idx] += dfp_data[0].field.add_rev;	\
		dfp_data[1].field.r[idx] += dfp_data[1].field.add;	\
		dfp_data[1].field.r[idx] += dfp_data[1].field.add_rev;	\
		dfp_data[2].field.r[idx] += dfp_data[2].field.add;	\
		dfp_data[2].field.r[idx] += dfp_data[2].field.add_rev;	\
		dfp_data[3].field.r[idx] += dfp_data[3].field.add;	\
		dfp_data[3].field.r[idx] += dfp_data[3].field.add_rev;	\
		dfp_data[4].field.r[idx] += dfp_data[4].field.add;	\
		dfp_data[4].field.r[idx] += dfp_data[4].field.add_rev;	\
		dfp_data[5].field.r[idx] += dfp_data[5].field.add;	\
		dfp_data[5].field.r[idx] += dfp_data[5].field.add_rev;	\
		dfp_data[6].field.r[idx] += dfp_data[6].field.add;	\
		dfp_data[6].field.r[idx] += dfp_data[6].field.add_rev;	\
		dfp_data[7].field.r[idx] += dfp_data[7].field.add;	\
		dfp_data[7].field.r[idx] += dfp_data[7].field.add_rev;	\
	}								\
	t2 = stress_time_now();						\
									\
	if (do_bogo_ops)						\
		stress_bogo_inc(args);					\
	return t2 - t1;							\
}

#define STRESS_DFP_MUL(field, name, do_bogo_ops)				\
static double TARGET_CLONES OPTIMIZE3 name(				\
	stress_args_t *args,						\
	dfp_data_t *dfp_data,						\
	const int idx)							\
{									\
	register int i;							\
	const int loops = LOOPS_PER_CALL >> 1;				\
	double t1, t2;							\
									\
	for (i = 0; i < DFP_ELEMENTS; i++) {				\
		dfp_data[i].field.r[idx] = dfp_data[i].field.r_init;	\
	}								\
									\
	t1 = stress_time_now();						\
	for (i = 0; i < loops ; i++) {					\
		dfp_data[0].field.r[idx] *= dfp_data[0].field.mul;	\
		dfp_data[0].field.r[idx] *= dfp_data[0].field.mul_rev;	\
		dfp_data[1].field.r[idx] *= dfp_data[1].field.mul;	\
		dfp_data[1].field.r[idx] *= dfp_data[1].field.mul_rev;	\
		dfp_data[2].field.r[idx] *= dfp_data[2].field.mul;	\
		dfp_data[2].field.r[idx] *= dfp_data[2].field.mul_rev;	\
		dfp_data[3].field.r[idx] *= dfp_data[3].field.mul;	\
		dfp_data[3].field.r[idx] *= dfp_data[3].field.mul_rev;	\
		dfp_data[4].field.r[idx] *= dfp_data[4].field.mul;	\
		dfp_data[4].field.r[idx] *= dfp_data[4].field.mul_rev;	\
		dfp_data[5].field.r[idx] *= dfp_data[5].field.mul;	\
		dfp_data[5].field.r[idx] *= dfp_data[5].field.mul_rev;	\
		dfp_data[6].field.r[idx] *= dfp_data[6].field.mul;	\
		dfp_data[6].field.r[idx] *= dfp_data[6].field.mul_rev;	\
		dfp_data[7].field.r[idx] *= dfp_data[7].field.mul;	\
		dfp_data[7].field.r[idx] *= dfp_data[7].field.mul_rev;	\
	}								\
	t2 = stress_time_now();						\
									\
	if (do_bogo_ops)						\
		stress_bogo_inc(args);					\
	return t2 - t1;							\
}

#define STRESS_DFP_DIV(field, name, do_bogo_ops)				\
static double TARGET_CLONES OPTIMIZE3 name(				\
	stress_args_t *args,						\
	dfp_data_t *dfp_data,						\
	const int idx)							\
{									\
	register int i;							\
	const int loops = LOOPS_PER_CALL >> 1;				\
	double t1, t2;							\
									\
	for (i = 0; i < DFP_ELEMENTS; i++) {				\
		dfp_data[i].field.r[idx] = dfp_data[i].field.r_init;	\
	}								\
									\
	t1 = stress_time_now();						\
	for (i = 0; stress_continue_flag() && (i < loops); i++) {	\
		dfp_data[0].field.r[idx] /= dfp_data[0].field.mul;	\
		dfp_data[0].field.r[idx] /= dfp_data[0].field.mul_rev;	\
		dfp_data[1].field.r[idx] /= dfp_data[1].field.mul;	\
		dfp_data[1].field.r[idx] /= dfp_data[1].field.mul_rev;	\
		dfp_data[2].field.r[idx] /= dfp_data[2].field.mul;	\
		dfp_data[2].field.r[idx] /= dfp_data[2].field.mul_rev;	\
		dfp_data[3].field.r[idx] /= dfp_data[3].field.mul;	\
		dfp_data[3].field.r[idx] /= dfp_data[3].field.mul_rev;	\
		dfp_data[4].field.r[idx] /= dfp_data[4].field.mul;	\
		dfp_data[4].field.r[idx] /= dfp_data[4].field.mul_rev;	\
		dfp_data[5].field.r[idx] /= dfp_data[5].field.mul;	\
		dfp_data[5].field.r[idx] /= dfp_data[5].field.mul_rev;	\
		dfp_data[6].field.r[idx] /= dfp_data[6].field.mul;	\
		dfp_data[6].field.r[idx] /= dfp_data[6].field.mul_rev;	\
		dfp_data[7].field.r[idx] /= dfp_data[7].field.mul;	\
		dfp_data[7].field.r[idx] /= dfp_data[7].field.mul_rev;	\
	}								\
	t2 = stress_time_now();						\
									\
	if (do_bogo_ops)						\
		stress_bogo_inc(args);					\
	return t2 - t1;							\
}

#if defined(HAVE_Decimal32)
STRESS_DFP_ADD(d32, stress_dfp_d32_add, true)
STRESS_DFP_MUL(d32, stress_dfp_d32_mul, true)
STRESS_DFP_DIV(d32, stress_dfp_d32_div, true)
#endif

#if defined(HAVE_Decimal64)
STRESS_DFP_ADD(d64, stress_dfp_d64_add, true)
STRESS_DFP_MUL(d64, stress_dfp_d64_mul, true)
STRESS_DFP_DIV(d64, stress_dfp_d64_div, true)
#endif

#if defined(HAVE_Decimal128)
STRESS_DFP_ADD(d128, stress_dfp_d128_add, true)
STRESS_DFP_MUL(d128, stress_dfp_d128_mul, true)
STRESS_DFP_DIV(d128, stress_dfp_d128_div, true)
#endif

typedef struct {
	const char *name;
	const char *description;
	const stress_dfp_func_t	dfp_func;
	const int dfp_type;
	double duration;
	double ops;
} stress_dfp_funcs_t;

static stress_dfp_funcs_t stress_dfp_funcs[] = {
	{ "all",		"all fp methods",	stress_dfp_all,		STRESS_DFP_TYPE_ALL,		0.0, 0.0 },
#if defined(HAVE_Decimal32)
	{ "df32add",	"_Decimal32 add",		stress_dfp_d32_add,	STRESS_DFP_TYPE_DECIMAL32,	0.0, 0.0 },
#endif
#if defined(HAVE_Decimal64)
	{ "df64add",	"_Decimal64 add",		stress_dfp_d64_add,	STRESS_DFP_TYPE_DECIMAL64,	0.0, 0.0 },
#endif
#if defined(HAVE_Decimal128)
	{ "df128add",	"_Decimal128 add",		stress_dfp_d128_add,	STRESS_DFP_TYPE_DECIMAL128,	0.0, 0.0 },
#endif
#if defined(HAVE_Decimal32)
	{ "df32mul",	"_Decimal32 mul",		stress_dfp_d32_mul,	STRESS_DFP_TYPE_DECIMAL32,	0.0, 0.0 },
#endif
#if defined(HAVE_Decimal64)
	{ "df64mul",	"_Decimal64 mul",		stress_dfp_d64_mul,	STRESS_DFP_TYPE_DECIMAL64,	0.0, 0.0 },
#endif
#if defined(HAVE_Decimal128)
	{ "df128mul",	"_Decimal128 mul",		stress_dfp_d128_mul,	STRESS_DFP_TYPE_DECIMAL128,	0.0, 0.0 },
#endif
#if defined(HAVE_Decimal32)
	{ "df32div",	"_Decimal32 div",		stress_dfp_d32_div,	STRESS_DFP_TYPE_DECIMAL32,	0.0, 0.0 },
#endif
#if defined(HAVE_Decimal64)
	{ "df64div",	"_Decimal64 div",		stress_dfp_d64_div,	STRESS_DFP_TYPE_DECIMAL64,	0.0, 0.0 },
#endif
#if defined(HAVE_Decimal128)
	{ "df128div",	"_Decimal128 div",		stress_dfp_d128_div,	STRESS_DFP_TYPE_DECIMAL128,	0.0, 0.0 },
#endif
};

#define STRESS_NUM_DFP_FUNCS	(SIZEOF_ARRAY(stress_dfp_funcs))

typedef struct {
	const int dfp_type;
	const char *dfp_description;
} dfp_type_map_t;

static const dfp_type_map_t dfp_type_map[] = {
	{ STRESS_DFP_TYPE_DECIMAL32,	"_Decimal32" },
	{ STRESS_DFP_TYPE_DECIMAL64,	"_Decimal64" },
	{ STRESS_DFP_TYPE_DECIMAL128,	"_Decimal128" },
	{ STRESS_DFP_TYPE_ALL,		"all" },
};

static const char * PURE stress_dfp_type(const int dfp_type)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(dfp_type_map); i++) {
		if (dfp_type == dfp_type_map[i].dfp_type)
			return dfp_type_map[i].dfp_description;
	}
	return "unknown";
}

static int stress_dfp_call_method(
	stress_args_t *args,
	dfp_data_t *dfp_data,
	const size_t method,
	const bool verify)
{
	double dt;
	stress_dfp_funcs_t *func = &stress_dfp_funcs[method];

	dt = func->dfp_func(args, dfp_data, 0);
	func->duration += dt;
	func->ops += (DFP_ELEMENTS * LOOPS_PER_CALL);

	if ((method > 0) && (method < STRESS_NUM_DFP_FUNCS && verify)) {
		register size_t i;
		const int dfp_type = stress_dfp_funcs[method].dfp_type;
		const char *method_name = stress_dfp_funcs[method].name;
		const char *dfp_description = stress_dfp_type(dfp_type);

		dt = func->dfp_func(args, dfp_data, 1);
		if (dt < 0.0)
			return EXIT_FAILURE;
		func->duration += dt;
		func->ops += (DFP_ELEMENTS * LOOPS_PER_CALL);

		/*
		 *  a SIGALRM during 2nd computation pre-verification can
		 *  cause long doubles on some arches to abort early, so
		 *  don't verify these results
		 */
		if (!stress_continue_flag())
			return EXIT_SUCCESS;

		for (i = 0; i < DFP_ELEMENTS; i++) {
			long double r0, r1;
			int ret;

			switch (dfp_type) {
#if defined(HAVE_Decimal32)
			case STRESS_DFP_TYPE_DECIMAL32:
				ret = memcmp(&dfp_data[i].d32.r[0], &dfp_data[i].d32.r[1], sizeof(dfp_data[i].d32.r[0]));
				r0 = (long double)dfp_data[i].d32.r[0];
				r1 = (long double)dfp_data[i].d32.r[1];
				break;
#endif
			default:
				/* Should never happen! */
				return EXIT_SUCCESS;
			}
			if (ret) {
				pr_fail("%s %s %s verification failure on element %zd, got %Lf, expected %Lf\n",
					args->name, dfp_description, method_name, i, r0, r1);
				return EXIT_FAILURE;
			}
		}
	}
	return EXIT_SUCCESS;
}

static double stress_dfp_all(
	stress_args_t *args,
	dfp_data_t *dfp_data,
	const int idx)
{
	size_t i;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	(void)idx;

	for (i = 1; i < STRESS_NUM_DFP_FUNCS; i++) {
		if (stress_dfp_call_method(args, dfp_data, i, verify) == EXIT_FAILURE)
			return -1.0;
	}
	return 0.0;
}

static int stress_dfp(stress_args_t *args)
{
	size_t i, mmap_size;
	dfp_data_t *dfp_data;
	size_t fp_method = 0;	/* "all" */
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	int rc = EXIT_SUCCESS;

	stress_catch_sigill();

	mmap_size = DFP_ELEMENTS * sizeof(*dfp_data);
	dfp_data = (dfp_data_t *)stress_mmap_populate(NULL, mmap_size,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (dfp_data == MAP_FAILED) {
		pr_inf_skip("%s: failed to allocate %d decimal floating point elements, skipping stressor\n",
			args->name, DFP_ELEMENTS);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(dfp_data, mmap_size, "dfp-data");
	(void)stress_madvise_mergeable(dfp_data, mmap_size);

	(void)stress_get_setting("fp-method", &fp_method);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < DFP_ELEMENTS; i++) {
		long double ld;
		uint32_t r;

#if defined(HAVE_Decimal32)
		r = stress_mwc32();
		ld = (long double)i + (long double)r / ((long double)(1ULL << 38));
		dfp_data[i].d32.r_init = ld;
		dfp_data[i].d32.r[0] = ld;
		dfp_data[i].d32.r[1] = ld;
#endif

		r = stress_mwc32();
		ld = (long double)r / ((long double)(1ULL << 31));
#if defined(HAVE_Decimal32)
		dfp_data[i].d32.add = ld;
#endif

		ld = -(ld * 0.992);
#if defined(HAVE_Decimal32)
		dfp_data[i].d32.add_rev = ld;
#endif

		r = stress_mwc32();
		ld = (long double)i + (long double)r / ((long double)(1ULL << 36));
#if defined(HAVE_Decimal32)
		dfp_data[i].d32.mul = ld;
#endif

		ld = 0.9995 / ld;
#if defined(HAVE_Decimal32)
		dfp_data[i].d32.mul_rev = ld;
#endif
	}

	do {
		if (stress_dfp_call_method(args, dfp_data, fp_method, verify) == EXIT_FAILURE) {
			rc = EXIT_FAILURE;
			break;
		}
	} while (stress_continue(args));

	for (i = 1; i < STRESS_NUM_DFP_FUNCS; i++) {
		const double ops = stress_dfp_funcs[i].ops;
		const double duration = stress_dfp_funcs[i].duration;
		if ((duration > 0.0) && (ops > 0.0)) {
			char msg[64];
			const double rate = stress_dfp_funcs[i].ops / stress_dfp_funcs[i].duration;

			(void)snprintf(msg, sizeof(msg), "Mdfp-ops per sec, %-20s", stress_dfp_funcs[i].description);
			stress_metrics_set(args, i - 1, msg,
				rate / 1000000.0, STRESS_METRIC_HARMONIC_MEAN);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)dfp_data, mmap_size);

	return rc;
}

static const char *stress_dfp_method(const size_t i)
{
	return (i < STRESS_NUM_DFP_FUNCS) ? stress_dfp_funcs[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_dfp_method, "dfp-method", TYPE_ID_SIZE_T_METHOD, 0, 1, stress_dfp_method },
	END_OPT,
};

const stressor_info_t stress_dfp_info = {
	.stressor = stress_dfp,
	.class = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else

static const char *stress_dfp_method(const size_t i)
{
	(void)i;

	return NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_dfp_method, "dfp-method", TYPE_ID_SIZE_T_METHOD, 0, 1, stress_dfp_method },
	END_OPT,
};

const stressor_info_t stress_dfp_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without decimal _Decimal32, _Decimal64 or _Decimal128 support"
};

#endif
