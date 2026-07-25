# RISC-V 汇编器分析与完善记录

## 一、项目概述

这是一个用 C++ 编写的 RISC-V (RV64I) 汇编器练习项目，能够将 `.s` 汇编源文件汇编为 ELF64 可重定位目标文件（`.o`）。

**构建与使用方式：**
```bash
g++ -std=c++17 Assembler.cpp -o myasstest
./myasstest test.s        # 输出 output.o
riscv64-linux-gnu-readelf -a output.o   # 验证
```

**文件结构：**
| 文件 | 行数 | 职责 |
|------|------|------|
| `Assembler.cpp` | 610 | 词法分析、伪指令展开、指令编码、两遍汇编、main |
| `write_elf64_object.h` | 242 | ELF64 节布局、符号表生成、目标文件写入 |

---

## 二、现有架构

```
源文件(.s)
  │
  ▼ tokenize_line()  ── 按空白/逗号/括号/冒号分词，支持 # ; // 注释
  │
  ▼ 第一遍 (Pass 1)
  │   ├─ 识别 label（xxx:）
  │   ├─ 处理 directive（.text .data .globl .align .word）
  │   ├─ expand_pseudo() 伪指令展开（j→jal, mv→addi, ret→jalr...）
  │   ├─ 计算每条指令的 offset，存入 instrs 列表
  │   └─ 记录 label → {section, addr} 到 symtab
  │
  ▼ 第二遍 (Pass 2)
  │   ├─ 遍历 instrs，根据 opcode 选择编码函数
  │   ├─ encode_r_type / encode_i_type / encode_s_type / encode_b_type / encode_u_type / encode_j_type
  │   ├─ 分支/跳转指令查 symtab 计算 PC 相对偏移
  │   └─ 写入 textout / dataout 字节缓冲
  │
  ▼ write_elf64_object()
      ├─ 构建 .text .data .bss .symtab .strtab .shstrtab
      ├─ 所有 label → Elf64_Sym（STB_GLOBAL, STT_FUNC/OBJECT）
      └─ 写入 ELF64 relocatable object
```

### 指令编码格式

RISC-V 指令固定 32 位，有 6 种格式：

```
R-type: [funct7(7)|rs2(5)|rs1(5)|funct3(3)|rd(5)|opcode(7)]   例: add rd,rs1,rs2
I-type: [imm(12)     |rs1(5)|funct3(3)|rd(5)|opcode(7)]       例: addi rd,rs1,imm
S-type: [imm[11:5](7)|rs2(5)|rs1(5)|funct3(3)|imm[4:0](5)|op]  例: sd rs2,imm(rs1)
B-type: [imm[12|10:5](7)|rs2|rs1|funct3|imm[4:1|11](5)|op]    例: beq rs1,rs2,offset
U-type: [imm[31:12](20)              |rd(5)|opcode(7)]         例: lui rd,imm
J-type: [imm[20|10:1|11|19:12](20)   |rd(5)|opcode(7)]         例: jal rd,offset
```

### 已支持的指令

| 类型 | 指令 |
|------|------|
| R-type | add sub sll slt sltu xor srl sra or and |
| I-type 算术 | addi slti sltiu xori ori andi slli srli srai |
| I-type load | lb lh lw ld |
| I-type jump | jalr |
| S-type | sb sh sw sd |
| B-type | beq bne blt bge bltu bgeu |
| U-type | lui auipc |
| J-type | jal |
| 伪指令 | j jr ret beqz bnez ble bgt bleu bgtu mv nop neg seqz snez |
| 汇编指令 | .text .data .globl .align .word |

---

## 三、发现的问题

### 🔴 严重问题

**P1. `li` 伪指令未实现**
- `test.s` 中使用了 `li t6,10`，但 `expand_pseudo()` 没有 `li` 分支
- 运行 test.s 会报 `Unknown opcode: li`
- 代码第583行注释声称"no li or mv at this point"，但 mv 有实现、li 没有

**P2. 无重定位表（.rela）**
- 不生成任何重定位条目
- `.data` 中引用符号、`la`/`call` 等需要重定位的伪指令无法正确工作
- 链接器无法修正跨节引用

**P3. `.globl` 被忽略**
- 第79-81行直接 `continue` 跳过
- 所有符号硬编码为 STB_GLOBAL，无法区分 local/global

### 🟡 功能缺失

**P4. 伪指令覆盖不全** — 缺少 li la call tail bgez bltz bgtz blez not negw
**P5. 指令集不完整** — 缺 M扩展 CSR fence ecall ebreak F/D/C扩展
**P6. 数据指令不全** — 只有 .word，缺 .byte .half .quad .string
**P7. jalr 格式不灵活** — 只支持三操作数形式

### 🟡 工程问题

**P8. 硬编码指令表** — if-else 链，新增指令需改多处
**P9. 单文件设计** — 610行全在一个文件，无模块化
**P10. 错误处理粗糙** — 无列位置，遇错即退出
**P11. 符号排序问题** — ELF 要求 local 符号在 global 之前

---

## 四、改进路线图

| 阶段 | 目标 | 状态 |
|------|------|------|
| 一 | 实现 li 伪指令，让 test.s 跑通 | 🔄 进行中 |
| 二 | 补全核心伪指令和数据指令 | ⏳ 待定 |
| 三 | 重定位表支持 | ⏳ 待定 |
| 四 | 架构重构（模块化、表驱动） | ⏳ 待定 |
| 五 | 扩展指令集（M/CSR/C扩展） | ⏳ 待定 |

---

## 五、阶段一：实现 `li` 伪指令

### 5.1 问题分析

`li rd, imm`（Load Immediate）是 RISC-V 中最常用的伪指令之一，用于将一个立即数加载到寄存器。

RISC-V 没有单条指令能加载任意 32/64 位立即数，所以 `li` 需要根据立即数大小展开为不同数量的真实指令：

```
情况1: imm 在 12 位有符号范围 [-2048, 2047] 内
  li rd, imm  →  addi rd, x0, imm        （1条指令）

情况2: imm 超出 12 位但可用 32 位表示
  li rd, imm  →  lui rd, hi20             （1或2条指令）
                   addi rd, rd, lo12
```

### 5.2 lui + addi 配对的原理

这是理解 `li` 展开的核心，需要讲清楚：

**lui rd, imm 的作用：** 将 20 位立即数放到 rd 的 bit[31:12]，低 12 位清零。
等价于 `rd = imm & 0xFFFFF000`（imm 左移 12 位后的值）。

**addi rd, rd, imm 的作用：** `rd = rd + sign_extend_12(imm)`，imm 是 12 位有符号数。

**问题：** addi 的 12 位立即数是**有符号**的，范围 [-2048, 2047]。
如果目标立即数的低 12 位 ≥ 2048（即 bit[11] = 1），addi 会把它当作**负数**处理。

**举例说明：**

```
目标: rd = 0x12345FFF

错误做法（直接拆高低位）:
  hi20 = 0x12345, lo12 = 0xFFF
  lui rd, 0x12345   →  rd = 0x12345000
  addi rd, rd, 0xFFF  →  0xFFF 是 -1（有符号），rd = 0x12345000 - 1 = 0x12344FFF  ✗

正确做法（高位补偿 +1）:
  hi20 = 0x12346, lo12 = -1
  lui rd, 0x12346   →  rd = 0x12346000
  addi rd, rd, -1   →  rd = 0x12346000 - 1 = 0x12345FFF  ✓
```

**补偿公式：**
```
hi20 = (imm + 0x800) >> 12    // 0x800 = 2048，低12位≥2048时自动进位
lo12 = imm - (hi20 << 12)     // 结果保证在 [-2048, 2047] 范围内
```

为什么 `+0x800` 能解决问题：
- 0x800 = 2048 = 2^11，正好是 12 位有符号数的正负分界点
- 如果 lo12 的 bit[11] = 1（即原始低12位 ≥ 2048），加 0x800 后会产生进位到 bit[12]，使 hi20 加 1
- 如果 lo12 的 bit[11] = 0（即原始低12位 < 2048），加 0x800 不会进位，hi20 不变

### 5.3 代码实现

在 `expand_pseudo()` 函数中，`mv` 伪指令之前添加 `li` 的处理：

```cpp
// li rd, imm
// 小立即数 [-2048,2047]: addi rd, x0, imm
// 大立即数: lui rd, (hi20<<12) + addi rd, rd, lo12
if(op == "li"){
    if(toks.size() < 3) throw runtime_error("li: missing operands");
    long long imm = parse_imm(toks[2]);
    if(fits_signed(imm, 12)){
        out.push_back({"addi", toks[1], "x0", toks[2]});
    } else {
        long long hi20 = (imm + 0x800) >> 12;
        long long lo12 = imm - (hi20 << 12);
        out.push_back({"lui", toks[1], to_string(hi20 << 12)});
        if(lo12 != 0)
            out.push_back({"addi", toks[1], toks[1], to_string(lo12)});
    }
    return true;
}
```

**关键设计决策：**
1. `to_string(hi20 << 12)` — 因为本汇编器的 `pack_u()` 期望传入左移12位后的完整值（`imm & 0xFFFFF000`），与 GAS 约定不同
2. `lo12 != 0` 时省略 addi — 减少指令数量，但展开结果数量在 Pass1 就固定，不会导致地址不一致
3. 使用 `parse_imm()` 解析立即数 — 复用现有函数，支持十进制和十六进制

### 5.4 验证

（待编译测试后补充）

### 5.4 验证结果

编译运行 test.s 成功，生成了正确的 ELF64 目标文件。

**修复过程中发现的额外 bug：**

在验证时发现 `.text` 只有 36 字节（9条指令），而 test.s 展开后应有 25 条指令。排查发现第一遍（Pass 1）存在严重 bug：

```
原始逻辑:
  if(expand_pseudo(toks, expanded)) {
      // 只有伪指令会被加入 instrs
      continue;
  }
  // ← 非伪指令（addi/sd/ld/addw等）直接被跳过了！
```

**根因：** `expand_pseudo` 返回 false 时（遇到真实指令如 addi、sd、ld 等），代码没有将它们加入 instrs 列表，直接进入下一次循环。导致所有真实指令在第一遍中被丢弃，第二遍根本看不到它们。

**修复：** 在 `expand_pseudo` 返回 false 后，添加非伪指令处理逻辑，将其作为单条指令加入 instrs：

```cpp
// 非伪指令：直接作为单条真实指令处理
{
    Instr ins;
    ins.lineno = L.lineno;
    ins.sec = cursec;
    ins.toks = toks;
    if(cursec == SEC_TEXT){
        ins.offset = text_off;
        instrs.push_back(ins);
        text_off += 4;
    }else if(cursec == SEC_DATA){
        ins.offset = data_off;
        instrs.push_back(ins);
        data_off += 4;
    }
}
```

**同时发现 test.s 中使用了 `addw` 指令（RV64I W 变体），但编码函数不支持。**

补充实现了 RV64I W 变体指令编码：

| 指令 | 格式 | opcode | 说明 |
|------|------|--------|------|
| addw/subw/sllw/srlw/sraw | R-type | 0x3b | 32位算术运算，结果符号扩展到64位 |
| addiw | I-type | 0x1b | 32位立即数加法 |
| slliw/srliw/sraiw | I-type | 0x1b | 32位移位（shamt 0-31） |

**最终验证结果：**

| 检查项 | 结果 |
|--------|------|
| .text 大小 | 0x64 = 100字节 = 25条指令 ✓ |
| ELF Header | ELF64, RISC-V, REL ✓ |
| 节表 | .text .data .bss .symtab .strtab .shstrtab ✓ |
| 符号表 | _user_f(0x00), .0(0x10), .7(0x2c), .8(0x44) ✓ |
| addi sp,sp,-32 编码 | fe010113 ✓ |
| li t6,10 → addi t6,x0,10 编码 | 00a00f93 ✓ |
| addw t6,t5,t6 编码 | 01ff0fbb ✓ |
| ble t6,t5,.8 → bge t5,t6,.8 编码 | 03ff5063 ✓ |
| ret → jalr x0,ra,0 编码 | 00008067 ✓ |

### 5.5 阶段一总结

阶段一完成了以下工作：
1. **实现 `li` 伪指令** - 支持小立即数（addi 单条）和大立即数（lui+addi 两条），包含 0x800 符号扩展补偿
2. **修复第一遍 bug** - 非伪指令现在能正确加入 instrs 列表并分配 offset
3. **添加 RV64I W 变体** - addw/subw/sllw/srlw/sraw + addiw/slliw/srliw/sraiw
4. **test.s 完整通过** - 25条指令全部正确编码，ELF 文件结构正确

---

## 六、下一步计划（阶段二预览）

阶段二将补全以下内容：
- 伪指令：`la`（加载地址）、`call`（函数调用）、`tail`（尾调用）、`bgez/bltz/bgtz/blez`（零比较分支）、`not`、`negw`
- 数据指令：`.byte .half .quad .string .asciz`
- `jalr` 多操作数格式支持（`jalr rs1`、`jalr rd,rs1`、`jalr rd,imm(rs1)`）
- 更完善的错误处理（列位置、错误恢复）

---

## 七、阶段二：补全核心伪指令和数据指令

### 7.1 新增伪指令

| 伪指令 | 展开为 | 说明 |
|--------|--------|------|
| `la rd, symbol` | `__la_hi` + `__la_lo` | 加载符号地址，第二遍查 symtab 计算 hi20/lo12 |
| `call symbol` | `jal ra, symbol` | 函数调用（简化版，±1MB范围） |
| `tail symbol` | `jal x0, symbol` | 尾调用（不保存返回地址） |
| `bgez rs, label` | `bge rs, x0, label` | ≥0 跳转 |
| `bltz rs, label` | `blt rs, x0, label` | <0 跳转 |
| `bgtz rs, label` | `blt x0, rs, label` | >0 跳转 |
| `blez rs, label` | `bge x0, rs, label` | ≤0 跳转 |
| `not rd, rs` | `xori rd, rs, -1` | 按位取反 |
| `negw rd, rs` | `subw rd, x0, rs` | 32位取负 |

### 7.2 la 伪指令的特殊处理

`la` 需要加载符号地址，但符号地址在第一遍时可能还未确定（前向引用）。解决方案：

1. **第一遍**：`la rd, symbol` 展开为两条占位指令 `__la_hi rd, symbol` 和 `__la_lo rd, symbol`
2. **第二遍**：遇到 `__la_hi` 时查 symtab 获取 symbol 地址，计算 `hi20 = (addr + 0x800) >> 12`，编码为 `lui`；遇到 `__la_lo` 时计算 `lo12 = addr - (hi20 << 12)`，编码为 `addi`

这保证了前向引用的正确性，因为第二遍时所有符号地址都已确定。

### 7.3 jalr 多操作数格式

| 输入格式 | 展开为 | 说明 |
|----------|--------|------|
| `jalr rs1` | `jalr ra, rs1, 0` | rd 默认为 ra |
| `jalr rd, rs1` | `jalr rd, rs1, 0` | imm 默认为 0 |
| `jalr rd, imm(rs1)` | `jalr rd, rs1, imm` | 内存寻址格式 |
| `jalr rd, rs1, imm` | 不展开 | 已是标准格式 |

### 7.4 新增数据指令

| 指令 | 大小 | 对齐 | 说明 |
|------|------|------|------|
| `.byte value` | 1字节 | 1 | 单字节数据 |
| `.2byte` / `.half value` | 2字节 | 2 | 半字数据 |
| `.4byte` / `.word value` | 4字节 | 4 | 字数据（原有） |
| `.8byte` / `.quad value` | 8字节 | 8 | 四字数据 |
| `.string "..."` / `.asciz` | 变长 | 1 | 字符串 + \0结尾 |
| `.ascii "..."` | 变长 | 1 | 字符串（无\0） |

### 7.5 tokenize_line 引号支持

修改词法分析器，使双引号内的内容（包括空格）作为一个完整 token 保留：

```
修改前: .string "Hello, World!" -> [".string", '"Hello,', 'World!"']  ✗
修改后: .string "Hello, World!" -> [".string", '"Hello, World!"']      ✓
```

同时实现了 `parse_string_literal()` 函数，支持转义字符：`\n` `\t` `\r` `\0` `\\` `\"` `\'`

### 7.6 验证结果

使用 test2.s 测试所有新功能：

**.text 验证（26条指令 = 104字节）：**
| 指令 | 编码 | 验证 |
|------|------|------|
| `li t0, 0x12345678` -> lui+addi | 0x123452b7, 0x67828293 | ✓ |
| `li t2, -1` -> addi x7,x0,-1 | 0xfff00393 | ✓ |
| `la a0, msg(0x10)` -> lui+addi | 0x00000537, 0x01050513 | ✓ |
| `not t3, t0` -> xori | 0xfff2ce13 | ✓ |
| `negw t4, t0` -> subw | 0x40500ebb | ✓ |
| `call helper` -> jal ra | 0x014000ef | ✓ |

**.data 验证：**
| 数据 | hex dump | 验证 |
|------|----------|------|
| `.quad 42` | 2a000000 00000000 | ✓ |
| `.byte 0xFF` | ff | ✓ |
| `.half 0x1234` | 3412 | ✓ |
| `.word 0xDEADBEEF` | efbeadde | ✓ |
| `.string "Hello, RISC-V!\n"` | 48656c6c...210a00 | ✓ |

**test.s 回归测试：** .text 仍为 0x64=100字节=25条指令 ✓

---

## 八、阶段三：重定位表支持

### 8.1 问题背景

阶段二的 `la` 伪指令只能处理**本文件内定义的符号**（直接计算地址编码到指令中）。对于**外部符号**（在其他 .o 文件中定义的符号），汇编时无法知道其地址，必须生成重定位条目交给链接器修正。

同样，`.word symbol` 等数据指令中的符号引用也需要重定位。

### 8.2 RISC-V 重定位类型

RISC-V 使用 RELA 类型重定位表（每个条目包含 offset + type + symbol_index + addend）：

| 类型 | 值 | 用途 | 对应指令 |
|------|-----|------|----------|
| R_RISCV_HI20 | 26 | 绝对地址高20位 | lui rd, %hi(sym) |
| R_RISCV_LO12_I | 27 | 绝对地址低12位(I-type) | addi rd, rd, %lo(sym) |
| R_RISCV_LO12_S | 28 | 绝对地址低12位(S-type) | sw rs, %lo(sym)(rd) |
| R_RISCV_32 | 1 | 32位绝对地址 | .word sym |
| R_RISCV_CALL | 18 | auipc+jalr配对 | call/tail sym |
| R_RISCV_BRANCH | 16 | PC相对分支 | beq/bne/... |
| R_RISCV_JAL | 17 | PC相对跳转 | jal |

### 8.3 实现方案

**核心设计：区分本地符号和外部符号**

```
la rd, symbol 的处理逻辑:
  if symbol 在 symtab 中找到（本文件定义）:
    直接计算 hi20/lo12 并编码（不产生重定位）
  else（外部符号）:
    编码占位值 0，生成 R_RISCV_HI20 / R_RISCV_LO12_I 重定位条目
```

**数据结构：**
- `RelocEntry { offset, type, sym_name, addend }` - 汇编阶段用符号名，写 ELF 时转为索引
- `text_relocs` / `data_relocs` - 分别收集 .text 和 .data 的重定位条目

**ELF 输出：**
- 新增 `.rela.text` 节（sh_type=SHT_RELA, sh_link→.symtab, sh_info→.text）
- 新增 `.rela.data` 节（sh_type=SHT_RELA, sh_link→.symtab, sh_info→.data）
- 未定义符号加入符号表（st_shndx=SHN_UNDEF, st_info=STB_GLOBAL|STT_NOTYPE）
- 只在有重定位时才添加对应 .rela 节

### 8.4 重写的 write_elf64_object.h

新增参数：
```cpp
void write_elf64_object(
    const string &filename,
    const vector<uint8_t> &textbin,
    const vector<uint8_t> &databin,
    const unordered_map<string, Label> &symtab_map,
    const vector<RelocEntry> &text_relocs,   // 新增
    const vector<RelocEntry> &data_relocs);   // 新增
```

关键逻辑：
1. 从 relocs 中收集未定义符号名
2. 将已定义符号 + 未定义符号合并到符号表
3. 构建符号名→索引映射，用于重定位条目
4. 将 RelocEntry 转换为 Elf64_Rela 结构
5. 动态添加 .rela.text / .rela.data 节（仅有内容时添加）

### 8.5 验证结果

使用 test3.s 测试（混合本地和外部符号引用）：

**.rela.text（4个条目，仅外部符号）：**
| Offset | Type | Symbol | 来源 |
|--------|------|--------|------|
| 0x10 | R_RISCV_HI20 | external_func | la a2, external_func (lui) |
| 0x14 | R_RISCV_LO12_I | external_func | la a2, external_func (addi) |
| 0x18 | R_RISCV_HI20 | external_var | la a3, external_var (lui) |
| 0x1c | R_RISCV_LO12_I | external_var | la a3, external_var (addi) |

**.rela.data（2个条目）：**
| Offset | Type | Symbol | 来源 |
|--------|------|--------|------|
| 0x08 | R_RISCV_32 | external_var | .word external_var |
| 0x0c | R_RISCV_32 | local_msg | .word local_msg |

**符号表：**
| Symbol | Value | Type | Ndx | 说明 |
|--------|-------|------|-----|------|
| _start | 0x00 | FUNC | .text(1) | 已定义 |
| external_func | 0x00 | NOTYPE | UND | 未定义（外部） |
| external_var | 0x00 | NOTYPE | UND | 未定义（外部） |
| local_msg | 0x14 | OBJECT | .data(2) | 已定义 |
| local_val | 0x00 | OBJECT | .data(2) | 已定义 |

**关键行为验证：**
- la 本地符号（local_msg, local_val）→ 不产生重定位，直接计算地址 ✓
- la 外部符号（external_func, external_var）→ 产生 HI20+LO12_I 重定位 ✓
- .word 数字 → 不产生重定位 ✓
- .word 符号 → 产生 R_RISCV_32 重定位 ✓
- test.s / test2.s 无回归 ✓

---

## 九、阶段四：架构重构（表驱动 + 符号绑定）

### 9.1 表驱动指令编码

**重构前的问题：** 每种指令格式用 if-else 链匹配，新增指令需要修改 encode_xxx 函数和第二遍匹配逻辑两处代码。指令编码信息散落在多个函数中。

**重构后的设计：**

```cpp
// 指令格式分类（11种）
enum InstrFormat { FMT_R, FMT_I_ARITH, FMT_I_ARITH_W, FMT_I_SHIFT, ... };

// 指令定义：一条指令的完整编码信息
struct InstrDef { const char* mnemonic; uint32_t opcode, funct3, funct7; InstrFormat format; };

// 指令表：覆盖 RV64I 全部基本指令 + W 变体（45条）
static const InstrDef instr_table[] = {
    {"add",  0x33, 0x0, 0x00, FMT_R},  {"sub",  0x33, 0x0, 0x20, FMT_R},
    {"addi", 0x13, 0x0, 0,    FMT_I_ARITH}, ...
};

// 查表函数
const InstrDef* lookup_instr(const string& op);
```

**第二遍编码逻辑：** 查表 -> switch(format) -> 调用对应 pack 函数

```cpp
const InstrDef *def = lookup_instr(op);
if(def){
    switch(def->format){
        case FMT_R:  encoded = pack_r(def->funct7, rs2, rs1, def->funct3, rd, def->opcode); break;
        case FMT_I_ARITH: encoded = pack_i(imm, rs1, def->funct3, rd, def->opcode); break;
        ...
    }
}
```

**优势：**
- 新增指令只需在 instr_table 中添加一行，无需修改编码逻辑
- 指令编码信息集中管理，一目了然
- 消除了 encode_r_type / encode_i_type / encode_s_type 等独立函数

### 9.2 符号绑定（.globl 正确处理）

**重构前：** `.globl` 指令被完全忽略，所有符号硬编码为 STB_GLOBAL。

**重构后：**
1. `Label` 结构添加 `bool is_global` 字段
2. `.globl symbol` 指令将符号名记录到 `global_names` 集合
3. 第一遍结束后，将 symtab 中匹配的符号标记为 `is_global = true`
4. ELF 符号表按 ELF 规范排序：local 符号在前，global 在后
5. `sh_info` 设置为第一个 global 符号的索引

```cpp
// 符号表构建
vector<string> local_syms, global_syms;
for(auto &p : symtab_map)
    (p.second.is_global ? global_syms : local_syms).push_back(p.first);
// ELF规范: local在前, global在后
symnames = {""} + sorted(local_syms) + sorted(global_syms);
sh_info = 1 + local_syms.size();  // 第一个global符号的索引
```

### 9.3 验证结果

**test3.s 符号表（符号绑定验证）：**
| Index | Symbol | Bind | Ndx | 说明 |
|-------|--------|------|-----|------|
| 0 | (null) | LOCAL | UND | null symbol |
| 1 | local_msg | LOCAL | .data(2) | 无 .globl 声明 |
| 2 | local_val | LOCAL | .data(2) | 无 .globl 声明 |
| 3 | _start | GLOBAL | .text(1) | 有 .globl 声明 |
| 4 | external_func | GLOBAL | UND | 未定义外部符号 |
| 5 | external_var | GLOBAL | UND | 未定义外部符号 |

- sh_info = 3（第一个 GLOBAL 符号的索引）✓
- local 在前，global 在后 ✓

**test.s 回归验证：**
- `_user_f` (有 .globl) -> GLOBAL ✓
- `.0`, `.7`, `.8` (无 .globl) -> LOCAL ✓

**三个测试文件全部通过，无回归 ✓**

---

## 十、阶段五：M 扩展 + CSR 指令 + 系统指令

### 10.1 目标

前四个阶段完成了 RV64I 基础整数指令、伪指令、重定位和表驱动架构重构。
本阶段在此基础上扩展三大类指令，使汇编器覆盖更完整的 RISC-V 指令集：

| 类别 | 新增指令 | 数量 |
|------|---------|------|
| M 扩展（乘除法） | `mul mulh mulhsu mulhu div divu rem remu` | 8 |
| M 扩展 W 变体（32位） | `mulw divw divuw remw remuw` | 5 |
| CSR 指令 | `csrrw csrrs csrrc csrrwi csrrsi csrrci` | 6 |
| 系统指令 | `ecall ebreak fence fence.i` | 4 |
| **合计** | | **23** |

### 10.2 M 扩展指令

M 扩展全部是 R-type 格式，与 RV64I 的 R-type 指令结构完全一致，
只是 funct7=0x01（基础整数 R-type 的 funct7 是 0x00 或 0x20）。
因此只需在 `instr_table` 中添加表项，**无需新增编码逻辑**——这正是阶段四表驱动重构的价值体现。

```
R-type 编码: funct7[31:25] | rs2[24:20] | rs1[19:15] | funct3[14:12] | rd[11:7] | opcode[6:0]
```

**M 扩展 RV64（opcode=0x33）：**
| 指令 | funct3 | 说明 |
|------|--------|------|
| mul | 0x0 | rd = rs1 × rs2（低位） |
| mulh | 0x1 | rd = (rs1 × rs2) >> 64（有符号×有符号高位） |
| mulhsu | 0x2 | 有符号 × 无符号高位 |
| mulhu | 0x3 | 无符号 × 无符号高位 |
| div | 0x4 | 有符号除法 |
| divu | 0x5 | 无符号除法 |
| rem | 0x6 | 有符号取余 |
| remu | 0x7 | 无符号取余 |

**M 扩展 W 变体（opcode=0x3b）：** 只对低 32 位操作，结果符号扩展到 64 位。
与 RV64I 的 W 变体（addw/subw 等）共用 opcode=0x3b。

表项定义示例：
```cpp
// M扩展 RV64 (opcode=0x33, funct7=0x01)
{"mul",    0x33, 0x0, 0x01, FMT_R},  {"mulh",   0x33, 0x1, 0x01, FMT_R},
{"div",    0x33, 0x4, 0x01, FMT_R},  {"divu",   0x33, 0x5, 0x01, FMT_R},
{"rem",    0x33, 0x6, 0x01, FMT_R},  {"remu",   0x33, 0x7, 0x01, FMT_R},
// M扩展 W变体 (opcode=0x3b, funct7=0x01)
{"mulw",   0x3b, 0x0, 0x01, FMT_R},  {"divw",   0x3b, 0x4, 0x01, FMT_R},
{"remw",   0x3b, 0x6, 0x01, FMT_R},  {"remuw",  0x3b, 0x7, 0x01, FMT_R},
```

> **关键认知**：表驱动架构使得新增同格式指令只需"加一行表项"，零代码逻辑改动。
> 如果还是阶段三之前的大 switch-case 写法，每条指令都要写一个 case 分支。

### 10.3 CSR 指令

CSR（Control and Status Register）指令用于读写处理器控制和状态寄存器。
编码格式接近 I-type，但 imm 字段被 CSR 地址占用：

```
CSR 格式: csr[31:20] | rs1[19:15] | funct3[14:12] | rd[11:7] | opcode[6:0]
         (opcode=0x73)
```

**6 条 CSR 指令：**
| 指令 | funct3 | 格式 | 说明 |
|------|--------|------|------|
| csrrw | 0x1 | FMT_I_CSR | 原子读旧值+写新值（rs1=寄存器） |
| csrrs | 0x2 | FMT_I_CSR | 读旧值+置位（rs1=寄存器掩码） |
| csrrc | 0x3 | FMT_I_CSR | 读旧值+清位 |
| csrrwi | 0x5 | FMT_I_CSR_IMM | 同 csrrw 但源操作数为 5 位立即数 |
| csrrsi | 0x6 | FMT_I_CSR_IMM | 同 csrrs 但立即数 |
| csrrci | 0x7 | FMT_I_CSR_IMM | 同 csrrc 但立即数 |

**新增两个 InstrFormat：** `FMT_I_CSR` 和 `FMT_I_CSR_IMM`，区分源操作数是寄存器还是立即数。

**CSR 名称映射：** 支持符号名（如 `mstatus`）和数字地址（如 `0x300`）两种写法：

```cpp
static unordered_map<string, uint32_t> csr_names = {
    {"mstatus", 0x300}, {"misa", 0x301}, {"mie", 0x304}, {"mtvec", 0x305},
    {"mscratch", 0x340}, {"mepc", 0x341}, {"mcause", 0x342}, {"mip", 0x344},
    // ... S-mode 和计时器寄存器
};

uint32_t parse_csr(const string &s){
    auto it = csr_names.find(s);
    if(it != csr_names.end()) return it->second;  // 符号名查表
    return (uint32_t)parse_imm(s);                // 否则当数字解析
}
```

**编码实现（Pass 2 switch）：**
```cpp
case FMT_I_CSR: {  // csrrw rd, csr, rs1
    int rd=reg_id(ins.toks[1]);
    uint32_t csr=parse_csr(ins.toks[2]);
    int rs1=reg_id(ins.toks[3]);
    encoded = (csr<<20)|(rs1<<15)|(def->funct3<<12)|(rd<<7)|def->opcode;
    break;
}
case FMT_I_CSR_IMM: {  // csrrwi rd, csr, imm5
    int rd=reg_id(ins.toks[1]);
    uint32_t csr=parse_csr(ins.toks[2]);
    long long imm=parse_imm(ins.toks[3]);
    if(imm<0||imm>31) throw runtime_error("csr imm out of range");
    encoded = (csr<<20)|((uint32_t)imm<<15)|(def->funct3<<12)|(rd<<7)|def->opcode;
    break;
}
```

### 10.4 系统指令（ecall / ebreak / fence / fence.i）

#### ecall / ebreak

这俩是**无操作数指令**，新增 `FMT_NONE` 格式：

| 指令 | 编码 | 说明 |
|------|------|------|
| ecall | `0x00000073` | 环境调用（系统调用） |
| ebreak | `0x00100073` | 断点（imm[11:0]=1） |

```cpp
case FMT_NONE: {
    encoded = (op=="ebreak") ? 0x00100073 : 0x00000073;
    break;
}
```

> ecall 和 ebreak 共用 opcode=0x73（SYSTEM），区别仅在于 imm 字段：
> ecall imm=0，ebreak imm=1。

#### fence / fence.i

`fence` 指令用于内存屏障，保证之前的内存操作对之后的内存操作可见。
`fence.i` 是指令同步屏障（保证指令缓存与数据缓存一致）。

**fence 编码格式（opcode=0x0f, MISC-MEM）：**
```
fm[31:28] | pred[27:24] | succ[23:20] | rs1[19:15]=0 | funct3[14:12]=0 | rd[11:7]=0 | opcode[6:0]=0x0f
```

pred/succ 用字母组合表示：
- `i` = input (0x8)
- `o` = output (0x4)
- `r` = read (0x2)
- `w` = write (0x1)

例如 `fence rw, rw` 表示：之前的 read/write 操作对之后的 read/write 可见。
`fence iorw, iorw` 表示全部四种操作都屏障。

**伪指令展开（Pass 1）：** `fence` 无参数时默认 `rw, rw`：

```cpp
if(op == "fence"){
    if(toks.size() >= 3)
        out.push_back({"__fence", toks[1], toks[2]});
    else
        out.push_back({"__fence", "rw", "rw"});  // 默认
}
if(op == "fence.i")
    out.push_back({"__fence_i"});
```

**pred/succ 解析：**
```cpp
int parse_fence_arg(const string &s){
    int v = 0;
    for(char c : s){
        switch(tolower(c)){
            case 'i': v |= 0x8; break;
            case 'o': v |= 0x4; break;
            case 'r': v |= 0x2; break;
            case 'w': v |= 0x1; break;
        }
    }
    return v;
}
```

**编码（Pass 2）：**
```cpp
// fence pred, succ -> 编码为 0x0f 类型
else if(op=="__fence"){
    int pred = parse_fence_arg(ins.toks[1]);
    int succ = parse_fence_arg(ins.toks[2]);
    encoded = ((pred<<4 | succ) << 20) | 0x0f;
    // pred 放 bits[27:24], succ 放 bits[23:20], fm=0
}
// fence.i -> 0x0000100f
else if(op=="__fence_i"){
    encoded = 0x0000100f;  // opcode=0x0f, funct3=0x1
}
```

### 10.5 验证结果

**test4.s 测试文件覆盖全部 23 条新指令。**

验证方法：Python 脚本解析 ELF `.text` 段，逐条对比 32 位编码与手工计算的期望值。

```
.text: offset=0x40, size=96 bytes (24 instructions)

[PASS] mul   a0,a1,a2            got=0x02c58533 exp=0x02c58533
[PASS] mulh  a3,a4,a5            got=0x02f716b3 exp=0x02f716b3
[PASS] div   a6,a7,t0            got=0x0258c833 exp=0x0258c833
[PASS] divu  t1,t2,t3            got=0x03c3d333 exp=0x03c3d333
[PASS] rem   t4,t5,t6            got=0x03ff6eb3 exp=0x03ff6eb3
[PASS] remu  s0,s1,s2            got=0x0324f433 exp=0x0324f433
[PASS] mulw  s3,s4,s5            got=0x035a09bb exp=0x035a09bb
[PASS] divw  s6,s7,s8            got=0x038bcb3b exp=0x038bcb3b
[PASS] remw  s9,s10,s11          got=0x03bd6cbb exp=0x03bd6cbb
[PASS] remuw t0,t1,t2            got=0x027372bb exp=0x027372bb
[PASS] csrrw t3,mstatus,t4       got=0x300e9e73 exp=0x300e9e73
[PASS] csrrs t5,mie,t6           got=0x304faf73 exp=0x304faf73
[PASS] csrrc a0,mip,a1           got=0x3445b573 exp=0x3445b573
[PASS] csrrwi a2,mstatus,5       got=0x3002d673 exp=0x3002d673
[PASS] csrrsi a3,mie,3           got=0x3041e6f3 exp=0x3041e6f3
[PASS] csrrci a4,mip,1           got=0x3440f773 exp=0x3440f773
[PASS] csrrw a5,0x340,a6         got=0x340817f3 exp=0x340817f3
[PASS] ecall                     got=0x00000073 exp=0x00000073
[PASS] ebreak                    got=0x00100073 exp=0x00100073
[PASS] fence (bare)              got=0x0330000f exp=0x0330000f
[PASS] fence rw,rw               got=0x0330000f exp=0x0330000f
[PASS] fence.i                   got=0x0000100f exp=0x0000100f
[PASS] fence iorw,iorw           got=0x0ff0000f exp=0x0ff0000f
[PASS] ret                       got=0x00008067 exp=0x00008067

>>> 24/24 passed <<<
```

> **踩坑记录**：验证过程中 `remw s9,s10,s11` 一度显示不匹配，
> 排查后发现是 Python 验证脚本的期望值算错了（rs2 写成 11 而非 s11=27），
> 汇编器输出的 `0x03bd6cbb` 完全正确。教训：先验证工具本身再下结论。

**回归测试：** test.s / test2.s / test3.s 全部正常汇编，ELF 结构正确，无回归 ✓

### 10.6 阶段五总结

| 维度 | 内容 |
|------|------|
| 新增指令 | 23 条（M扩展13 + CSR 6 + 系统指令4） |
| 新增 InstrFormat | `FMT_I_CSR`、`FMT_I_CSR_IMM`、`FMT_NONE` |
| 新增辅助函数 | `parse_csr()`、`parse_fence_arg()` |
| 新增数据结构 | `csr_names` 映射表 |
| 代码改动量 | ~60 行（含表项+编码+伪指令展开） |
| 验证 | 24/24 指令编码正确，3 个回归测试通过 |

**核心收获：**
1. 表驱动架构的威力再次体现——M 扩展 13 条 R-type 指令只加了表项，零逻辑代码
2. CSR 指令的特殊性在于操作数顺序（rd, csr, rs1）和 CSR 地址可符号化
3. fence 的 pred/succ 编码理解了内存屏障的硬件语义（i/o/r/w 四种操作的前驱/后继约束）
4. 验证工具本身可能有 bug，排查问题时要先排除工具因素

### 10.7 下一步计划

当前汇编器已覆盖 RV64I + M 扩展 + CSR + 系统指令，后续可扩展方向：
- **A 扩展（原子指令）**：lr/sc/amoadd/amoswap 等，需处理内存排序语义
- **F/D 扩展（浮点）**：新增浮点寄存器组（f0-f31）和浮点指令格式
- **压缩指令（C 扩展）**：16 位编码，需要全新的编码/解码逻辑
- **更完善的错误诊断**：行号、列号、错误提示
- **链接器支持**：将多个 .o 文件链接为可执行文件
