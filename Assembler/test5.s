# ============================================================
#  test5.s - RISC-V 汇编器全覆盖测试
#  覆盖: 全指令格式 + M扩展 + CSR + 系统 + 伪指令 + 数据 + 重定位
# ============================================================
    .text
    .globl  _start
    .globl  helper
    .globl  table
    .type   _start, @function

# ============================================================
#  _start : 入口函数
# ============================================================
_start:
    # ------ li: 立即数加载 (小/大/负) ------
    li    a0, 0              # 小立即数 -> addi a0,x0,0
    li    a1, 42             # 小立即数
    li    a2, -1             # 负小立即数
    li    a3, 0x12345678     # 大立即数 -> lui + addi
    li    a4, 0x7fffffff     # 大立即数边界

    # ------ la: 地址加载 ------
    la    a5, local_msg      # 本地符号 -> 直接计算,无重定位
    la    a6, local_val      # 本地符号
    la    a7, external_func  # 外部符号 -> R_RISCV_HI20 + R_RISCV_LO12_I
    la    t0, external_var   # 外部符号 -> R_RISCV_HI20 + R_RISCV_LO12_I

    # ------ 一元伪指令 ------
    mv    t1, a0             # addi t1,a0,0
    nop                     # addi x0,x0,0
    not   t2, a1             # xori t2,a1,-1
    neg   t3, a2             # sub  t3,x0,a2
    negw  t4, a3             # subw t4,x0,a3
    seqz  t5, a0             # sltu t5,x0,a0
    snez  t6, a1             # sltu t6,a1,x0

    # ------ R-type 基础算术 ------
    add   s0, a0, a1
    sub   s1, a3, a2
    sll   s2, a0, a1
    slt   s3, a0, a1
    sltu  s4, a0, a1
    xor   s5, a0, a2
    srl   s6, a3, a1
    sra   s7, a2, a1
    or    s8, a0, a1
    and   s9, a3, a2

    # ------ R-type W 变体 ------
    addw  s10, a0, a1
    subw  s11, a3, a2
    sllw  t0, a0, a1
    srlw  t1, a3, a1
    sraw  t2, a2, a1

    # ------ I-type 算术 ------
    addi  t3, a0, 16
    slti  t4, a0, 100
    sltiu t5, a0, 100
    xori  t6, a2, -1
    ori   s0, a0, 0xff
    andi  s1, a3, 0x0f

    # ------ I-type 移位 ------
    slli  s2, a0, 4
    srli  s3, a3, 4
    srai  s4, a2, 4

    # ------ I-type 移位 W 变体 + addiw ------
    addiw s5, a0, 8
    slliw s6, a0, 4
    srliw s7, a3, 4
    sraiw s8, a2, 4

    # ------ load/store (I-load + S-type) ------
    la    t0, local_val      # 取数据基址
    lb    t1, 0(t0)
    lh    t2, 0(t0)
    lw    t3, 0(t0)
    ld    t4, 0(t0)
    sd    a0, 0(t0)
    sw    a1, 0(t0)
    sh    a2, 0(t0)
    sb    a3, 0(t0)

    # ------ B-type 条件分支 (目标须为本地标签) ------
    beq   a0, a1, .L1
    bne   a0, a1, .L1
    blt   a0, a1, .L1
    bge   a0, a1, .L1
    bltu  a0, a1, .L1
    bgeu  a0, a1, .L1
.L1:
    # ------ 分支伪指令 ------
    beqz  a0, .L2            # beq a0,x0,.L2
    bnez  a1, .L2            # bne a1,x0,.L2
    bgez  a2, .L2            # bge a2,x0,.L2
    bltz  a3, .L2            # blt x0,a3,.L2  -> 实为 blt a3,x0? 见源码
    bgtz  a0, .L2            # blt x0,a0,.L2
    blez  a1, .L2            # bge x0,a1,.L2
.L2:

    # ------ U-type ------
    lui   t3, 0x10000000     # 高20位立即数
    auipc t4, 0              # PC相对,偏移0

    # ------ J-type + 跳转伪指令 (目标须为本地标签) ------
    jal   ra, helper         # 调用 helper
    call  helper             # jal ra,helper
    tail  helper             # jal x0,helper
    j     .Lend              # jal x0,.Lend
.Lend:
    jr    ra                 # jalr x0,ra,0
    ret                      # jalr x0,ra,0

# ============================================================
#  helper : 被调用函数, 覆盖 M扩展 / CSR / 系统指令
# ============================================================
    .globl  helper
helper:
    # ------ M 扩展 ------
    mul    a0, a1, a2
    mulh   a3, a4, a5
    mulhsu a6, a4, a5
    mulhu  a7, a4, a5
    div    t0, a1, a2
    divu   t1, a1, a2
    rem    t2, a1, a2
    remu   t3, a1, a2
    # ------ M 扩展 W 变体 ------
    mulw   t4, a1, a2
    divw   t5, a1, a2
    divuw  t6, a1, a2
    remw   s0, a1, a2
    remuw  s1, a1, a2

    # ------ CSR 指令 ------
    csrrw  s2, mstatus, s3
    csrrs  s4, mie, s5
    csrrc  s6, mip, s7
    csrrwi s8, mstatus, 5
    csrrsi s9, mie, 3
    csrrci s10, mip, 1
    csrrw  s11, 0x340, t0    # 用数字指定CSR (mscratch)

    # ------ 系统指令 ------
    ecall
    ebreak
    fence                     # 默认 rw,rw
    fence  rw, rw
    fence.i
    fence  iorw, iorw
    ret

# ============================================================
#  .data : 数据定义 + R_RISCV_32 重定位
# ============================================================
    .data
    .align 3
local_val:
    .quad  0x0123456789abcdef # 8字节 (须 <= 0x7fffffffffffffff)
table:
    .word  0x11111111          # 4字节立即数
    .word  0x22222222
    .half  0x3333              # 2字节
    .half  0x4444
    .byte  0x55                # 1字节
    .byte  0x66
    .align 3                   # 对齐到8字节
    # --- .word 符号引用 -> 产生 R_RISCV_32 ---
    .word  external_var        # 外部符号
    .word  external_func       # 外部符号
    .word  local_msg           # 本地符号 (同样产生 R_RISCV_32)
    .word  helper              # 本地 .text 符号
local_msg:
    .string "Hello, RISC-V!"   # 含逗号的字符串,测试引号处理
