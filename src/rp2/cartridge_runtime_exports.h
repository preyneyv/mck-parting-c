#pragma once

/* The Pico SDK implements these linker-wrapped ARM runtime entry points in
 * assembly. Cartridge imports jump to them using their ARM EABI calling
 * conventions, so C only needs opaque function declarations to take their
 * addresses here. */
#define PRISM_RUNTIME_EXPORT(symbol) extern void symbol(void)

PRISM_RUNTIME_EXPORT(__wrap___aeabi_idiv);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_idivmod);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_uidiv);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_uidivmod);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_ldivmod);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_uldivmod);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_lmul);

PRISM_RUNTIME_EXPORT(__wrap___clzsi2);
PRISM_RUNTIME_EXPORT(__wrap___clzdi2);
PRISM_RUNTIME_EXPORT(__wrap___ctzsi2);
PRISM_RUNTIME_EXPORT(__wrap___ctzdi2);
PRISM_RUNTIME_EXPORT(__wrap___popcountsi2);
PRISM_RUNTIME_EXPORT(__wrap___popcountdi2);

PRISM_RUNTIME_EXPORT(__wrap___aeabi_memcpy);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_memcpy4);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_memcpy8);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_memset);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_memset4);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_memset8);

PRISM_RUNTIME_EXPORT(__wrap___aeabi_fadd);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_fdiv);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_fmul);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_frsub);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_fsub);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_cfcmpeq);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_cfrcmple);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_cfcmple);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_fcmpeq);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_fcmplt);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_fcmple);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_fcmpge);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_fcmpgt);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_fcmpun);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_i2f);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_ui2f);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_f2iz);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_f2uiz);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_l2f);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_ul2f);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_f2lz);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_f2ulz);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_f2d);

PRISM_RUNTIME_EXPORT(__wrap___aeabi_dadd);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_ddiv);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_dmul);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_drsub);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_dsub);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_cdcmpeq);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_cdrcmple);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_cdcmple);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_dcmpeq);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_dcmplt);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_dcmple);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_dcmpge);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_dcmpgt);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_dcmpun);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_i2d);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_l2d);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_ui2d);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_ul2d);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_d2iz);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_d2lz);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_d2uiz);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_d2ulz);
PRISM_RUNTIME_EXPORT(__wrap___aeabi_d2f);

PRISM_RUNTIME_EXPORT(__wrap_printf);
PRISM_RUNTIME_EXPORT(__wrap_vprintf);
PRISM_RUNTIME_EXPORT(__wrap_puts);
PRISM_RUNTIME_EXPORT(__wrap_putchar);
PRISM_RUNTIME_EXPORT(__wrap_getchar);
PRISM_RUNTIME_EXPORT(__wrap_sprintf);
PRISM_RUNTIME_EXPORT(__wrap_vsnprintf);

#undef PRISM_RUNTIME_EXPORT
