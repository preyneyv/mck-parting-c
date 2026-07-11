.syntax unified
.cpu cortex-m0plus
.thumb

/* void prism_call_with_got(function, got_base, context)
 *
 * Native cartridges are compiled with r9 as their single PIC base. Preserve
 * the firmware's r9, install the cartridge's private RAM GOT for the duration
 * of the lifecycle call, then restore the firmware register value.
 */
.global prism_call_with_got
.type prism_call_with_got, %function
.thumb_func
prism_call_with_got:
    push {r4, lr}
    mov  r4, r9
    mov  r9, r1
    mov  r3, r0
    mov  r0, r2
    blx  r3
    mov  r9, r4
    pop  {r4, pc}
.size prism_call_with_got, .-prism_call_with_got
