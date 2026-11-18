#ifndef _I386_TICKLESS_H_
#define _I386_TICKLESS_H_

/* Tickless idle, i386/i386/tickless.c.  The MACH_TICKLESS gate
   itself is defined in kern/mach_clock.h, which every user of this
   code includes one way or another.  */

#include <kern/mach_clock.h>

#if	MACH_TICKLESS

extern int	tickless_idle_ready(void);
extern int	tickless_machine_idle(void);
extern void	tickless_exit(void);

#endif	/* MACH_TICKLESS */

#endif /* _I386_TICKLESS_H_ */
