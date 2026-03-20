.text
.globl mycoroutine_context_swap
.type mycoroutine_context_swap, %function
mycoroutine_context_swap:
    mov x9, sp
    str x9, [x0, #0]
    adr x9, .Lresume
    str x9, [x0, #8]
    stp x19, x20, [x0, #16]
    stp x21, x22, [x0, #32]
    stp x23, x24, [x0, #48]
    stp x25, x26, [x0, #64]
    stp x27, x28, [x0, #80]
    stp x29, x30, [x0, #96]

    ldr x9, [x1, #0]
    mov sp, x9
    ldr x2, [x1, #8]
    ldp x19, x20, [x1, #16]
    ldp x21, x22, [x1, #32]
    ldp x23, x24, [x1, #48]
    ldp x25, x26, [x1, #64]
    ldp x27, x28, [x1, #80]
    ldp x29, x30, [x1, #96]
    br x2

.Lresume:
    ret

.size mycoroutine_context_swap, .-mycoroutine_context_swap

.section .note.GNU-stack,"",%progbits
