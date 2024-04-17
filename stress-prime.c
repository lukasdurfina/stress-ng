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

#if defined(HAVE_GMP_H)
#include <gmp.h>
#endif

#define STRESS_PRIME_METHOD_FACTORIAL	(0)
#define STRESS_PRIME_METHOD_INC		(1)
#define STRESS_PRIME_METHOD_PWR2	(2)
#define STRESS_PRIME_METHOD_PWR10	(3)

static sigjmp_buf jmpbuf;
static bool jumped;

static const stress_help_t help[] = {
	{ NULL,	"prime N",		"start N workers that find prime numbers" },
	{ NULL,	"prime-ops N",		"stop after N prime operations" },
	{ NULL, "prime-method M",	"method of searching for next prime [ factorial | inc | pwr2 | pwr10 ]" },
	{ NULL,	NULL,		 	NULL }
};

typedef struct {
	const char *name;
	const int prime_method;
} stress_prime_method_t;

static const stress_prime_method_t stress_prime_methods[] = {
	{ "factorial",	STRESS_PRIME_METHOD_FACTORIAL },
	{ "inc",	STRESS_PRIME_METHOD_INC },
	{ "pwr2",	STRESS_PRIME_METHOD_PWR2 },
	{ "pwr10",	STRESS_PRIME_METHOD_PWR10 },
};

static void MLOCKED_TEXT stress_prime_alarm_handler(int signum)
{
	static int count = 0;
	(void)signum;

	stress_continue_set_flag(false);
	count++;
	if (count > 1)
		siglongjmp(jmpbuf, 1);
}

/*
 *  stress_set_prime_method()
 *	set the default prime method
 */
static int stress_set_prime_method(const char *name)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_prime_methods); i++) {
		if (!strcmp(stress_prime_methods[i].name, name)) {
			stress_set_setting("prime-method", TYPE_ID_SIZE_T, &stress_prime_methods[i].prime_method);
			return 0;
		}
	}

	(void)fprintf(stderr, "prime-method must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(stress_prime_methods); i++) {
		(void)fprintf(stderr, " %s", stress_prime_methods[i].name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_prime_method,	stress_set_prime_method },
	{ 0,			NULL },
};

#if defined(HAVE_GMP_H) &&	\
    defined(HAVE_LIB_GMP)

static int OPTIMIZE3 stress_prime(stress_args_t *args)
{
	double t, rate;
	NOCLOBBER double duration = 0.0;
	NOCLOBBER size_t digits = 1;
	uint64_t ops;
	mpz_t start, value, factorial;
	int prime_method = STRESS_PRIME_METHOD_INC;

	(void)stress_get_setting("prime-method", &prime_method);

	mpz_inits(start, value, factorial, NULL);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	mpz_set_ui(start, 1);
	mpz_set_ui(factorial, 2);

	jumped = false;
	if (sigsetjmp(jmpbuf, 1) != 0) {
		jumped = true;
		goto finish;
	}

	if (stress_sighandler(args->name, SIGALRM, stress_prime_alarm_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	do {
		t = stress_time_now();
		mpz_nextprime(value, start);
		duration += stress_time_now() - t;

		switch (prime_method) {
		default:
		case STRESS_PRIME_METHOD_FACTORIAL:
			mpz_mul(start, start, factorial);
			mpz_add_ui(factorial, factorial, 1);
			break;
		case STRESS_PRIME_METHOD_INC:
			mpz_add_ui(start, value, 2);
			break;
		case STRESS_PRIME_METHOD_PWR2:
			mpz_mul_ui(start, start, 2);
			break;
		case STRESS_PRIME_METHOD_PWR10:
			mpz_mul_ui(start, start, 10);
			break;
		}
		stress_bogo_inc(args);
		digits = mpz_sizeinbase(value, 10);
	} while (stress_continue(args));

finish:
	if (!jumped) {
		/*
		 *  Only garbage collect if we didn't siglongjmp
		 *  here to avoid any heap corruption
		 */
		mpz_clears(start, value, factorial, NULL);
	}

	ops = stress_bogo_get(args);

	pr_inf("%s: %" PRIu64 " primes found, largest prime: %zd digits long\n",
		args->name, ops, digits);

	rate = (duration > 0.0) ? (double)ops / duration : 0.0;
	stress_metrics_set(args, 0, "primes per second", rate, STRESS_HARMONIC_MEAN);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_prime_info = {
	.stressor = stress_prime,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_NONE,
	.help = help
};

#else

stressor_info_t stress_prime_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_NONE,
	.help = help,
	.unimplemented_reason = "built without gmp.h, or libgmp"
};

#endif
