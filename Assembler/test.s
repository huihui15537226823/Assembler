    .file    "/home/hu1/Compiler/test/test.s"
    .option nopic
    .attribute arch,     "rv64i2p1_m2p0_a2p1_f2p2_d2p2_c2p0_zicsr2p0_zifencei2p0"
    .attribute unaligned_access, 0
    .attribute stack_align, 16
    .text
    .align 1
    .globl _user_f
    .type  _user_f, @function
_user_f: 
    addi  sp,sp,-32
    sd  ra,24(sp)
    sd  s0,16(sp)
    addi  s0,sp,32
.0: 
    sw  a0,-24(s0)
    li  t6,10
    sw  t6,-20(s0)
    lw  t6,-20(s0)
    li  t5,5
    ble  t6,t5,.8
    j  .7
.7: 
    lw  t5,-20(s0)
    mv  a0,t5
    ld  ra,24(sp)
    ld  s0,16(sp)
    addi  sp,sp,32
    ret  
.8: 
    lw  t5,-20(s0)
    li  t6,2
    addw  t6,t5,t6
    mv  a0,t6
    ld  ra,24(sp)
    ld  s0,16(sp)
    addi  sp,sp,32
    ret  
    .size	_user_f, .-_user_f