/*
 * KVM paravirtualized clock source.
 *
 * Copyright (C) 2026 Free Software Foundation
 *
 * This program is free software ; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY ; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the program ; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdint.h>

#include <mach/boolean.h>

#include <i386/kvmclock.h>
#include <i386/locore.h>
#include <i386/msr.h>
#include <i386/proc_reg.h>
#include <i386/smp.h>
#include <i386/spl.h>
#include <i386/vm_param.h>

#define KVM_MSR_ENABLED	1

/* Cache line aligned so the host never shares the line with anything
 * else the kernel writes.  Volatile because the host writes it
 * asynchronously to guest execution.  */
static volatile struct pvclock_vcpu_time_info kvmclock_pvti
	__attribute__((__aligned__(64)));

/*
 * Scale a TSC delta by the host supplied fraction, the result is
 * nanoseconds.  The 64 by 32 multiply keeps the full product before
 * the shift, gcc expands it inline on i386.
 */
uint64_t
kvmclock_scale(uint64_t delta, uint32_t mul, int8_t shift)
{
	if (shift < 0)
		delta >>= -shift;
	else
		delta <<= shift;
	return (delta * (uint64_t) mul) >> 32;
}

/*
 * Read the nanosecond clock.  The version protocol makes a tear free
 * read of the host fields possible, the barriers keep the compiler
 * from moving the field reads across the version reads.
 */
uint64_t
kvmclock_read_ns(void)
{
	volatile struct pvclock_vcpu_time_info *src = &kvmclock_pvti;
	uint32_t	version;
	uint64_t	delta, ns;

	do {
		version = src->version & ~1;
		__sync_synchronize();
		delta = read_tsc() - src->tsc_timestamp;
		ns = src->system_time
		    + kvmclock_scale(delta, src->tsc_to_system_mul,
				     src->tsc_shift);
		__sync_synchronize();
	} while (version != src->version);

	return ns;
}

/*
 * Detect the KVM hypervisor and its kvmclock feature, register the
 * time info struct with the host and take over the high precision
 * clock read.  Returns TRUE when the kvmclock was taken, FALSE
 * when anything was missing, KVM, the feature, a responding host,
 * and the caller keeps the previous clock source.
 */
boolean_t
kvmclock_init(void)
{
	uint32_t	eax, ebx, ecx, edx;
	spl_t		s;
	int		i;

	/* kvmclock reads the TSC, no TSC means no CPUID and no
	 * kvmclock either.  */
	if (!CPU_HAS_FEATURE(CPU_FEATURE_TSC))
		return FALSE;

	eax = KVM_CPUID_SIGNATURE;
	ecx = 0;
	eax = KVM_CPUID_SIGNATURE;
	ecx = 0;
	cpuid(eax, ebx, ecx, edx);
	/* EBX carries the first four bytes of the KVMKVMKVM
	 * signature, enough to know the hypervisor.  */
	if (ebx != KVM_CPUID_SIGNATURE_EBX)
		return FALSE;

	eax = KVM_CPUID_FEATURES;
	ecx = 0;
	eax = KVM_CPUID_FEATURES;
	ecx = 0;
	cpuid(eax, ebx, ecx, edx);
	if (!(eax & KVM_FEATURE_CLOCKSOURCE2))
		return FALSE;

	wrmsr(MSR_KVM_SYSTEM_TIME_NEW,
	      (uint64_t) _kvtophys((vm_offset_t) &kvmclock_pvti)
	      | KVM_MSR_ENABLED);

	/* The host fills the struct when it next enters the guest,
	 * so make each poll an MSR read, the exit it causes hands the
	 * host the entry it needs.  The host fills on the first exit
	 * after the write above, so the loop is typically done after
	 * one iteration, the bound only guards a host that never
	 * fills, which falls back to the HPET read.  */
	for (i = 0; i < 100000 && kvmclock_pvti.version == 0; i++)
		(void) rdmsr(MSR_KVM_SYSTEM_TIME_NEW);
	if (kvmclock_pvti.version == 0)
		return FALSE;

	/* hpc_init publishes the read and rebases the per tick
	 * reference, one place for every clock that can win.  */
	return TRUE;
}
