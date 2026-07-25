    .text
    .globl _start
    .type  _start, @function
_start:
    # la 本地符号 -> 直接计算，不产生重定位
    la   a0, local_msg
    la   a1, local_val
    # la 外部符号 -> 产生 R_RISCV_HI20 + R_RISCV_LO12_I
    la   a2, external_func
    la   a3, external_var
    # 基本指令
    addi a0, a0, 1
    ret

    .data
    .align 3
local_val:
    .quad 100
    # .word 外部符号 -> 产生 R_RISCV_32
    .word external_var
    # .word 本地符号 -> 产生 R_RISCV_32
    .word local_msg
    # .word 数字 -> 不产生重定位
    .word 0x12345678
local_msg:
    .string "local"
