.syntax unified
.cpu cortex-m0plus
.thumb

/* GCC emits direct calls to ARM EABI helpers even for PIC code. Resolving an
 * EABI name itself to an import sentinel lets the linker put that sentinel in
 * a read-only Thumb veneer, which the package loader cannot patch in XIP.
 *
 * Keep each standard EABI symbol local to the cartridge and tail-call a
 * separately named import through r9's writable GOT instead. Use the smallest
 * trampoline that preserves the registers occupied by that helper's input.
 * The tail calls preserve comparison flags and multi-register returns. */
.macro prism_compiler_trampoline_fast name, imported
  .section .text.\name, "ax", %progbits
  .align 1
  .global \name
  .type \name, %function
  .thumb_func
\name:
  mov r2, r9
  ldr r3, 1f
  ldr r3, [r2, r3]
  bx r3
  .align 2
1:
  .word \imported(GOT)
  .size \name, . - \name
.endm

.macro prism_compiler_trampoline_r2 name, imported
  .section .text.\name, "ax", %progbits
  .align 1
  .global \name
  .type \name, %function
  .thumb_func
\name:
  push {r2}
  mov r2, r9
  ldr r3, 1f
  ldr r3, [r2, r3]
  mov ip, r3
  pop {r2}
  bx ip
  .align 2
1:
  .word \imported(GOT)
  .size \name, . - \name
.endm

.macro prism_compiler_trampoline_r2_r3 name, imported
  .section .text.\name, "ax", %progbits
  .align 1
  .global \name
  .type \name, %function
  .thumb_func
\name:
  push {r2, r3}
  mov r2, r9
  ldr r3, 1f
  ldr r3, [r2, r3]
  mov ip, r3
  pop {r2, r3}
  bx ip
  .align 2
1:
  .word \imported(GOT)
  .size \name, . - \name
.endm

prism_compiler_trampoline_fast __aeabi_idiv, prism_import_aeabi_idiv
prism_compiler_trampoline_fast __aeabi_idivmod, prism_import_aeabi_idivmod
prism_compiler_trampoline_fast __aeabi_uidiv, prism_import_aeabi_uidiv
prism_compiler_trampoline_fast __aeabi_uidivmod, prism_import_aeabi_uidivmod
prism_compiler_trampoline_r2_r3 __aeabi_ldivmod, prism_import_aeabi_ldivmod
prism_compiler_trampoline_r2_r3 __aeabi_uldivmod, prism_import_aeabi_uldivmod
prism_compiler_trampoline_r2_r3 __aeabi_lmul, prism_import_aeabi_lmul

prism_compiler_trampoline_fast __clzsi2, prism_import_clzsi2
prism_compiler_trampoline_fast __clzdi2, prism_import_clzdi2
prism_compiler_trampoline_fast __ctzsi2, prism_import_ctzsi2
prism_compiler_trampoline_fast __ctzdi2, prism_import_ctzdi2
prism_compiler_trampoline_fast __popcountsi2, prism_import_popcountsi2
prism_compiler_trampoline_fast __popcountdi2, prism_import_popcountdi2

prism_compiler_trampoline_r2 __aeabi_memcpy, prism_import_aeabi_memcpy
prism_compiler_trampoline_r2 __aeabi_memcpy4, prism_import_aeabi_memcpy4
prism_compiler_trampoline_r2 __aeabi_memcpy8, prism_import_aeabi_memcpy8
prism_compiler_trampoline_r2 __aeabi_memset, prism_import_aeabi_memset
prism_compiler_trampoline_r2 __aeabi_memset4, prism_import_aeabi_memset4
prism_compiler_trampoline_r2 __aeabi_memset8, prism_import_aeabi_memset8

prism_compiler_trampoline_fast __aeabi_fadd, prism_import_aeabi_fadd
prism_compiler_trampoline_fast __aeabi_fdiv, prism_import_aeabi_fdiv
prism_compiler_trampoline_fast __aeabi_fmul, prism_import_aeabi_fmul
prism_compiler_trampoline_fast __aeabi_frsub, prism_import_aeabi_frsub
prism_compiler_trampoline_fast __aeabi_fsub, prism_import_aeabi_fsub
prism_compiler_trampoline_fast __aeabi_cfcmpeq, prism_import_aeabi_cfcmpeq
prism_compiler_trampoline_fast __aeabi_cfrcmple, prism_import_aeabi_cfrcmple
prism_compiler_trampoline_fast __aeabi_cfcmple, prism_import_aeabi_cfcmple
prism_compiler_trampoline_fast __aeabi_fcmpeq, prism_import_aeabi_fcmpeq
prism_compiler_trampoline_fast __aeabi_fcmplt, prism_import_aeabi_fcmplt
prism_compiler_trampoline_fast __aeabi_fcmple, prism_import_aeabi_fcmple
prism_compiler_trampoline_fast __aeabi_fcmpge, prism_import_aeabi_fcmpge
prism_compiler_trampoline_fast __aeabi_fcmpgt, prism_import_aeabi_fcmpgt
prism_compiler_trampoline_fast __aeabi_fcmpun, prism_import_aeabi_fcmpun
prism_compiler_trampoline_fast __aeabi_i2f, prism_import_aeabi_i2f
prism_compiler_trampoline_fast __aeabi_ui2f, prism_import_aeabi_ui2f
prism_compiler_trampoline_fast __aeabi_f2iz, prism_import_aeabi_f2iz
prism_compiler_trampoline_fast __aeabi_f2uiz, prism_import_aeabi_f2uiz
prism_compiler_trampoline_fast __aeabi_l2f, prism_import_aeabi_l2f
prism_compiler_trampoline_fast __aeabi_ul2f, prism_import_aeabi_ul2f
prism_compiler_trampoline_fast __aeabi_f2lz, prism_import_aeabi_f2lz
prism_compiler_trampoline_fast __aeabi_f2ulz, prism_import_aeabi_f2ulz
prism_compiler_trampoline_fast __aeabi_f2d, prism_import_aeabi_f2d

prism_compiler_trampoline_r2_r3 __aeabi_dadd, prism_import_aeabi_dadd
prism_compiler_trampoline_r2_r3 __aeabi_ddiv, prism_import_aeabi_ddiv
prism_compiler_trampoline_r2_r3 __aeabi_dmul, prism_import_aeabi_dmul
prism_compiler_trampoline_r2_r3 __aeabi_drsub, prism_import_aeabi_drsub
prism_compiler_trampoline_r2_r3 __aeabi_dsub, prism_import_aeabi_dsub
prism_compiler_trampoline_r2_r3 __aeabi_cdcmpeq, prism_import_aeabi_cdcmpeq
prism_compiler_trampoline_r2_r3 __aeabi_cdrcmple, prism_import_aeabi_cdrcmple
prism_compiler_trampoline_r2_r3 __aeabi_cdcmple, prism_import_aeabi_cdcmple
prism_compiler_trampoline_r2_r3 __aeabi_dcmpeq, prism_import_aeabi_dcmpeq
prism_compiler_trampoline_r2_r3 __aeabi_dcmplt, prism_import_aeabi_dcmplt
prism_compiler_trampoline_r2_r3 __aeabi_dcmple, prism_import_aeabi_dcmple
prism_compiler_trampoline_r2_r3 __aeabi_dcmpge, prism_import_aeabi_dcmpge
prism_compiler_trampoline_r2_r3 __aeabi_dcmpgt, prism_import_aeabi_dcmpgt
prism_compiler_trampoline_r2_r3 __aeabi_dcmpun, prism_import_aeabi_dcmpun
prism_compiler_trampoline_fast __aeabi_i2d, prism_import_aeabi_i2d
prism_compiler_trampoline_fast __aeabi_l2d, prism_import_aeabi_l2d
prism_compiler_trampoline_fast __aeabi_ui2d, prism_import_aeabi_ui2d
prism_compiler_trampoline_fast __aeabi_ul2d, prism_import_aeabi_ul2d
prism_compiler_trampoline_fast __aeabi_d2iz, prism_import_aeabi_d2iz
prism_compiler_trampoline_fast __aeabi_d2lz, prism_import_aeabi_d2lz
prism_compiler_trampoline_fast __aeabi_d2uiz, prism_import_aeabi_d2uiz
prism_compiler_trampoline_fast __aeabi_d2ulz, prism_import_aeabi_d2ulz
prism_compiler_trampoline_fast __aeabi_d2f, prism_import_aeabi_d2f

.section .note.GNU-stack, "", %progbits
