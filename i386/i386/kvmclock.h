/*
 * KVM paravirtualized clock, the kvmclock protocol.
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

#ifndef _I386_KVMCLOCK_H_
#define _I386_KVMCLOCK_H_

#include <stdint.h>

#include <mach/boolean.h>

/* Hypervisor CPUID leaves, 0x40000000 returns the KVMKVMKVM
 * signature and 0x40000001 the feature bitmap in EAX.  */
#define KVM_CPUID_SIGNATURE	0x40000000
#define KVM_CPUID_FEATURES	0x40000001

/* "KVMK", the first four bytes of the KVMKVMKVM signature, in the
 * byte order CPUID delivers EBX.  */
#define KVM_CPUID_SIGNATURE_EBX	0x4b4d564b

/* The new kvmclock MSR pair, selected by this feature bit.  */
#define KVM_FEATURE_CLOCKSOURCE2	(1 << 3)

/* The time info struct is the ABI between host and guest, the host
 * writes it and bumps version odd before and even after, the guest
 * reads it under that version.  One per virtual CPU, ours lives in
 * the kernel image and the host address is registered through the
 * MSR, low bit set enables the clock.  */
struct pvclock_vcpu_time_info {
	uint32_t	version;
	uint32_t	pad0;
	uint64_t	tsc_timestamp;
	uint64_t	system_time;
	uint32_t	tsc_to_system_mul;
	int8_t		tsc_shift;
	uint8_t		flags;
	uint8_t		pad[2];
} __attribute__((__packed__));

boolean_t kvmclock_init(void);
uint64_t kvmclock_read_ns(void);

#endif /* _I386_KVMCLOCK_H_ */
