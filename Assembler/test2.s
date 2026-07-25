    .text
    .align 1
    .globl _start
    .type  _start, @function
_start:
    # li 大立即数测试 (需要 lui+addi)
    li   t0, 0x12345678
    li   t1, 0x7FFFFFFF
    li   t2, -1
    # la 加载地址测试
    la   a0, msg
    la   a1, counter
    # 零比较分支
    bgez t0, .L1
    bltz t0, .L2
    bgtz t0, .L3
    blez t0, .L4
.L1:
.L2:
.L3:
.L4:
    # not / negw
    not  t3, t0
    negw t4, t0
    # jalr 多格式
    jalr t5
    jalr t6, t5
    jalr a2, 8(t5)
    # call / tail
    call helper
    tail cleanup
    # 基本指令
    addw t0, t1, t2
    mv   a3, t0
    ret

helper:
    addi a0, a0, 1
    ret

cleanup:
    ret

    .data
    .align 3
counter:
    .quad 42
    .byte 0xFF
    .half 0x1234
    .word 0xDEADBEEF
msg:
    .string "Hello, RISC-V!\n"
