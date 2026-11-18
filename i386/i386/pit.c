/* 
 * Mach Operating System
 * Copyright (c) 1991,1990,1989 Carnegie Mellon University
 * Copyright (c) 1991 IBM Corporation 
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation,
 * and that the name IBM not be used in advertising or publicity 
 * pertaining to distribution of the software without specific, written
 * prior permission.
 * 
 * CARNEGIE MELLON AND IBM ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND IBM DISCLAIM ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
  Copyright 1988, 1989 by Intel Corporation, Santa Clara, California.

		All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appears in all
copies and that both the copyright notice and this permission notice
appear in supporting documentation, and that the name of Intel
not be used in advertising or publicity pertaining to distribution
of the software without specific, written prior permission.

INTEL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS,
IN NO EVENT SHALL INTEL BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <kern/mach_clock.h>
#include <i386/ipl.h>
#include <machine/irq.h>
#include <i386/pit.h>
#include <i386/pio.h>
#include <kern/cpu_number.h>

int pitctl_port  = PITCTL_PORT;		/* For 386/20 Board */
int pitctr0_port = PITCTR0_PORT;	/* For 386/20 Board */
/* We want PIT 0 in square wave mode */

int pit0_mode = PIT_C0|PIT_SQUAREMODE|PIT_READMODE ;


unsigned int clknumb = CLKNUM;		/* interrupt interval for timer 0 */

void
pit_prepare_sleep(int usec)
{
    /* Prepare to sleep for 1/persec seconds */
    uint64_t val = 0;
    uint8_t lsb, msb;

    val = inb(PITAUX_PORT);
    val &= ~PITAUX_OUT2;
    val |= PITAUX_GATE2;
    outb (PITAUX_PORT, val);
    /* Mode 0 starts counting on the divisor load.  Mode 1 would
     * need a gate edge, and the port 0x61 gate writes freeze
     * channel 0 counting on the KVM PIT model.  */
    outb (PITCTL_PORT, PIT_C2 | PIT_LOADMODE | PIT_TERMMODE);
    val = (uint64_t)CLKNUM * usec / 1000000;
    if (val == 0)
        val = 1;
    lsb = val & 0xff;
    msb = val >> 8;
    outb (PITCTR2_PORT, lsb);
    val = inb(POST_PORT); /* ~1us i/o delay */
    outb (PITCTR2_PORT, msb);
}

void
pit_sleep(void)
{
    /* Wait until counter reaches zero */
    while ((inb(PITAUX_PORT) & PITAUX_VAL) == 0);
}

void
pit_udelay(int usec)
{
    while (usec > MAX_PIT_USEC) {
        pit_prepare_sleep(MAX_PIT_USEC);
        pit_sleep();
        usec -= MAX_PIT_USEC;
    }
    pit_prepare_sleep(usec);
    pit_sleep();
}

void
pit_mdelay(int msec)
{
    pit_udelay(1000 * msec);
}

/*
 * Arm PIT channel 0 in mode 0 to raise IRQ0 once after USEC
 * microseconds.  Mode 0 starts counting as soon as the divisor is
 * written, which is what channel 0 needs since its gate is hardwired
 * high and mode 1 would never see a trigger edge.  The 16 bit
 * counter caps a single shot at MAX_PIT_USEC, so USEC is clamped and
 * the duration actually armed is returned for callers that chain
 * longer waits.  Interrupts are disabled around the three outb
 * writes so a clock interrupt cannot observe a half loaded counter.
 */
unsigned int
pit_oneshot_arm(unsigned int usec)
{
	uint64_t	count;
	unsigned char	byte;
	unsigned long	s;

	if (usec > MAX_PIT_USEC)
		usec = MAX_PIT_USEC;

	count = (uint64_t)CLKNUM * usec / 1000000;
	if (count == 0)
		count = 1;
	if (count > 0xffff)
		count = 0xffff;

	s = sploff();

	outb(pitctl_port, PIT_C0|PIT_LOADMODE|PIT_TERMMODE);
	byte = count;
	outb(pitctr0_port, byte);
	byte = count >> 8;
	outb(pitctr0_port, byte);

	splon(s);

	return count * 1000000 / CLKNUM;
}

/*
 * Put channel 0 back into the square wave clkstart left it in, with
 * the hz derived divisor.  Restores the periodic tick after a
 * one-shot armed by pit_oneshot_arm.
 */
void
pit_periodic_restart(void)
{
	unsigned char	byte;
	unsigned long s;

	s = sploff();         /* disable interrupts */

	/* Since we use only timer 0, we program that.
	 * 8254 Manual specifically says you do not need to program
	 * timers you do not use
	 */
	outb(pitctl_port, pit0_mode);
	clknumb = (CLKNUM + hz / 2) / hz;
	byte = clknumb;
	outb(pitctr0_port, byte);
	byte = clknumb>>8;
	outb(pitctr0_port, byte); 
	splon(s);         /* restore interrupt state */
}

void
clkstart(void)
{
	if (cpu_number() != 0)
		/* Only one PIT initialization is needed */
		return;

	pit_periodic_restart();
}
