// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 SUSE LLC <mdoucha@suse.cz>
 *
 * CVE-2017-10661
 *
 * Test for race condition vulnerability in timerfd_settime(). Multiple
 * concurrent calls of timerfd_settime() clearing the CANCEL_ON_SET flag may
 * cause memory corruption. Fixed in:
 *
 *  commit 1e38da300e1e395a15048b0af1e5305bd91402f6
 *  Author: Thomas Gleixner <tglx@linutronix.de>
 *  Date:   Tue Jan 31 15:24:03 2017 +0100
 *
 *  timerfd: Protect the might cancel mechanism proper
 */
#include <unistd.h>
#include "tst_timer.h"
#include "tst_safe_timerfd.h"
#include "tst_fuzzy_sync.h"
#include "tst_taint.h"

#define TIMERFD_FLAGS "timerfd_settime(TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET)"

#ifndef TFD_TIMER_CANCEL_ON_SET
#define TFD_TIMER_CANCEL_ON_SET (1<<1)
#endif

static int fd = -1;
static struct tst_its its;
static struct tst_fzsync_pair fzsync_pair;

static struct test_variants {
	int (*tfd_settime)(int fd, int flags, void *new_value, void *old_value);
	enum tst_ts_type type;
	char *desc;
} variants[] = {
#if (__NR_timerfd_settime != __LTP__NR_INVALID_SYSCALL)
	{ .tfd_settime = sys_timerfd_settime, .type = TST_KERN_OLD_TIMESPEC, .desc = "syscall with old kernel spec"},
#endif

#if (__NR_timerfd_settime64 != __LTP__NR_INVALID_SYSCALL)
	{ .tfd_settime = sys_timerfd_settime64, .type = TST_KERN_TIMESPEC, .desc = "syscall time64 with kernel spec"},
#endif
};

static void setup(void)
{
	struct test_variants *tv = &variants[tst_variant];

	tst_res(TINFO, "Testing variant: %s", tv->desc);
	its.type = tv->type;

	tst_taint_init(TST_TAINT_W | TST_TAINT_D);
	fd = SAFE_TIMERFD_CREATE(CLOCK_REALTIME, 0);

	fzsync_pair.exec_loops = 1000000;
	tst_fzsync_pair_init(&fzsync_pair);
}

static void cleanup(void)
{
	if (fd >= 0)
		SAFE_CLOSE(fd);
	tst_fzsync_pair_cleanup(&fzsync_pair);
}

static int punch_clock(int flags)
{
	return variants[tst_variant].tfd_settime(fd, flags, tst_its_get(&its),
						 NULL);

}

static void *thread_run(void *arg)
{
	while (tst_fzsync_run_b(&fzsync_pair)) {
		tst_fzsync_start_race_b(&fzsync_pair);
		punch_clock(0);
		tst_fzsync_end_race_b(&fzsync_pair);
	}

	return arg;
}

static void run(void)
{
	tst_fzsync_pair_reset(&fzsync_pair, thread_run);

	while (tst_fzsync_run_a(&fzsync_pair)) {
		TEST(punch_clock(TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET));

		if (TST_RET == -1)
			tst_brk(TBROK | TTERRNO, TIMERFD_FLAGS " failed");

		if (TST_RET != 0)
			tst_brk(TBROK | TTERRNO, "Invalid " TIMERFD_FLAGS
				" return value");

		tst_fzsync_start_race_a(&fzsync_pair);
		punch_clock(0);
		tst_fzsync_end_race_a(&fzsync_pair);

		if (tst_taint_check()) {
			tst_res(TFAIL, "Kernel is vulnerable");
			return;
		}
	}

	tst_res(TPASS, "Nothing bad happened, probably");
}

static struct tst_test test = {
	.test_all = run,
	.test_variants = ARRAY_SIZE(variants),
	.setup = setup,
	.cleanup = cleanup,
	.min_kver = "2.6.25",
	.tags = (const struct tst_tag[]) {
		{"linux-git", "1e38da300e1e"},
		{"CVE", "2017-10661"},
		{}
	}
};
