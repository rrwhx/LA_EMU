/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch CPU -- internal functions and types
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_INTERNALS_H
#define LOONGARCH_INTERNALS_H

#define FCMP_LT   0b0001  /* fp0 < fp1 */
#define FCMP_EQ   0b0010  /* fp0 = fp1 */
#define FCMP_UN   0b0100  /* unordered */
#define FCMP_GT   0b1000  /* fp0 > fp1 */

#define TARGET_PHYS_MASK MAKE_64BIT_MASK(0, TARGET_PHYS_ADDR_SPACE_BITS)
#define TARGET_VIRT_MASK MAKE_64BIT_MASK(0, TARGET_VIRT_ADDR_SPACE_BITS)

/* Global bit used for lddir/ldpte */
#define LOONGARCH_PAGE_HUGE_SHIFT   6
/* Global bit for huge page */
#define LOONGARCH_HGLOBAL_SHIFT     12

void loongarch_translate_init(void);


void do_raise_exception(CPULoongArchState *env,
                                   uint32_t exception,
                                   uintptr_t pc);

const char *loongarch_exception_name(int32_t exception);

int ieee_ex_to_loongarch(int xcpt);
void restore_fp_status(CPULoongArchState *env);

uint64_t read_fcc(CPULoongArchState *env);
void write_fcc(CPULoongArchState *env, uint64_t val);


#endif
