    .text
    .globl _start
    .type  _start, @function
_start:
    # M 扩展
    mul   a0, a1, a2
    mulh  a3, a4, a5
    div   a6, a7, t0
    divu  t1, t2, t3
    rem   t4, t5, t6
    remu  s0, s1, s2
    mulw  s3, s4, s5
    divw  s6, s7, s8
    remw  s9, s10, s11
    remuw t0, t1, t2
    # CSR 指令
    csrrw t3, mstatus, t4
    csrrs t5, mie, t6
    csrrc a0, mip, a1
    csrrwi a2, mstatus, 5
    csrrsi a3, mie, 3
    csrrci a4, mip, 1
    # CSR 用数字地址
    csrrw a5, 0x340, a6
    # 系统指令
    ecall
    ebreak
    fence
    fence rw, rw
    fence.i
    fence iorw, iorw
    ret
