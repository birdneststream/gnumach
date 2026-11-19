/*
 * Tickless idle entry and exit, the design notes patch 4.  With the
 * tick stopped the CPU hlt's until the earliest visible wheel
 * deadline, which a mode 0 one-shot on PIT channel 0 raises as a
 * plain IRQ0 on the master CPU.  hardclock sees the stopped tick
 * on that interrupt and charges the measured clock gap through
 * clock_catchup instead of the per-tick path, and the idle loop
 * either chains the one-shot or exits through tickless_exit when
 * a thread shows up.  An application processor, which ticks from
 * its local APIC timer and owns no part of the wall clock, masks
 * that timer in idle and needs no one-shot, its wakes are
 * scheduling and device interrupts.
 *
 * Built with --enable-tickless, x86 only, and only with a working
 * high precision clock: without one there is no way to measure
 * the gap.  Everything here runs from the idle thread of one CPU,
 * so the state needs no locks, only splsched around each
 * decision.
 */

#include <stdint.h>

#include <kern/mach_clock.h>
#include <kern/printf.h>

#include <i386/pit.h>
#include <i386/spl.h>
#include <i386/tickless.h>

#if	MACH_TICKLESS

#if	NCPUS > 1 && defined(APIC)
#include <i386/apic.h>
#include <kern/cpu_number.h>

/* Per CPU stop state on the application processors.  Only the
 * local CPU touches its own slot, under splsched, so no lock is
 * needed.  The master's stop state is tickless_active, which is
 * the wall clock relevant part.  */
static int	tk_ap_stopped[NCPUS];
#endif	/* NCPUS > 1 && APIC */

#ifdef LINUX_DEV
static int	tk_warned_linux_dev;
#endif

/* The gate is evaluated once, a machine without a high
 * precision clock never gains tickless idle.  */
static int	tk_gates_checked;
static int	tk_gates_ok;

/*
 * Say whether tickless idle may engage on this machine.
 */
int
tickless_idle_ready(void)
{
	if (tk_gates_checked)
		return tk_gates_ok;

	tk_gates_checked = 1;
	tk_gates_ok = 0;

	/* No high precision clock, no measured gap, no tickless.  */
	if (hpclock_read_ns == NULL)
		return 0;

	tk_gates_ok = 1;
	return 1;
}

/*
 * Arm the one-shot at the earliest wheel deadline in sight.  The
 * deadline is phase corrected against the counter, so the interrupt
 * lands on the deadline and not up to one tick after it.  A deadline
 * past the 55 ms counter range clamps to MAX_PIT_USEC and chains
 * through wakes, no deadline in sight sleeps the maximum and
 * rechecks on the next wake.  Recomputing from the wheel every time
 * lands on the same instant when nothing changed, so there is one
 * re-arm point and it can never push a deadline later.  Runs under
 * splsched.
 */
static void
tk_arm(void)
{
	uint64_t	now_ns, since_ns;
	uint32_t	since_us, spoke_usec;
	int		nearest;
	unsigned int	usec;

	now_ns = hpclock_read_ns();
	nearest = timeout_next_tick();
	/* Time since the last tick or catch up is the phase inside
	 * the current tick.  */
	since_ns = now_ns - last_hpc_read;
	since_us = (uint32_t)(since_ns / 1000);
	if (nearest < 0)
		usec = MAX_PIT_USEC;
	else {
		spoke_usec = (uint32_t)(nearest + 1) * tick;
		usec = spoke_usec - since_us;
		/* The phase can already sit past the deadline, or the
		 * subtraction can wrap, either way fire now.  */
		if (usec == 0 || usec > spoke_usec)
			usec = 1;
		if (usec > MAX_PIT_USEC)
			usec = MAX_PIT_USEC;
	}

	/* Writing the mode 0 control word is what stops the periodic
	 * square wave, so arming is also the stop.  */
	pit_oneshot_arm(usec);
}

/*
 * Idle entry from machine_idle, at spl0 with nothing runnable.
 * Returns TRUE when the tick is managed here and the caller must not
 * hlt on its own.  The order is the one the design fixes: decide
 * under splsched, stop the tick by arming, drop to spl0, hlt.  Any
 * interrupt wakes the hlt and the idle loop rechecks at its top.
 */
int
tickless_machine_idle(void)
{
	spl_t	s;

	s = splsched();

#if	NCPUS > 1 && defined(APIC)
	if (cpu_number() != master_cpu) {
		/* No clock duty here, stopping is just masking the
		 * local timer, nothing to arm.  A tick already in
		 * flight is swallowed in tickless_clock_intr.  */
		if (!tk_ap_stopped[cpu_number()]) {
			if (!tickless_idle_ready()) {
				splx(s);
				return 0;
			}

#ifdef LINUX_DEV
			if (!tk_warned_linux_dev) {
				tk_warned_linux_dev = 1;
				printf("tickless idle on a LINUX_DEV build, "
				       "linux glue timers stall in idle\n");
			}
#endif

			tk_ap_stopped[cpu_number()] = 1;
			lapic->lvt_timer.r |= LAPIC_DISABLE;
		}
		splx(s);

		asm volatile("hlt" : : : "memory");
		return 1;
	}
#endif	/* NCPUS > 1 && APIC */

	if (!tickless_active) {
		if (!tickless_idle_ready()) {
			splx(s);
			return 0;
		}

#ifdef LINUX_DEV
		if (!tk_warned_linux_dev) {
			tk_warned_linux_dev = 1;
			printf("tickless idle on a LINUX_DEV build, "
			       "linux glue timers stall in idle\n");
		}
#endif

		tickless_active = 1;	/* publish before the arming */
		tk_arm();
	}
	else
		/* Awake and still nothing runnable.  Either an
		 * unrelated interrupt woke us, and may have armed an
		 * earlier timeout, or the one-shot fired and the
		 * channel needs a fresh count.  */
		tk_arm();

	splx(s);

	asm volatile("hlt" : : : "memory");
	return 1;
}

/*
 * Idle exit, from the idle thread loop once something is runnable.
 * On the master the gap is charged through clock_catchup and the
 * periodic PIT tick put back, on an application processor the
 * periodic local APIC timer is rewritten, with no gap to charge.
 */
void
tickless_exit(void)
{
	spl_t	s;

	s = splsched();

#if	NCPUS > 1 && defined(APIC)
	if (cpu_number() != master_cpu) {
		if (tk_ap_stopped[cpu_number()]) {
			tk_ap_stopped[cpu_number()] = 0;
			lapic_timer_resume();
		}
		splx(s);
		return;
	}
#endif	/* NCPUS > 1 && APIC */

	if (!tickless_active) {
		splx(s);
		return;
	}

	clock_catchup();
	pit_periodic_restart();
	tickless_active = 0;

	splx(s);
}

/*
 * hardclock entry with the local tick stopped.  On the master the
 * PIT one-shot fired and the measured gap is charged through
 * clock_catchup, the idle loop re-arms or exits on its wake.  On
 * an application processor this can only be a tick that raced the
 * masking, nothing is charged, the timeout wheel is the master's
 * business.  Returns TRUE when hardclock must not run the per
 * tick path.
 */
int
tickless_clock_intr(void)
{
#if	NCPUS > 1 && defined(APIC)
	if (cpu_number() != master_cpu)
		return tk_ap_stopped[cpu_number()];
#endif	/* NCPUS > 1 && APIC */

	if (!tickless_active)
		return 0;

	clock_catchup();
#ifdef LINUX_DEV
	linux_timer_intr();
#endif	/* LINUX_DEV */
	return 1;
}

#endif	/* MACH_TICKLESS */
