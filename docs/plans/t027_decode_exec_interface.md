# T027：可扩展的解码与执行接口

状态：design active（2026-10-20）。本任务冻结“取指 → 规范化 → 匹配 → 字段提取 →
操作数绑定 → 执行 → 提交”的接口边界和迁移顺序。目标是让新增 F/D、V、Zfa、原子
和其它扩展时，主要增加指令描述和执行函数，不反复修改整数译码主流程。

## 任务契约

- 范围：`include/cpu/exec.h`、`include/cpu/decode.h`、`src/target/riscv64/decode.c`、
  各扩展的 `instr.def`、`instr/*/exec.c`、`src/target/riscv64/step.c`。
- 目标：统一整数、浮点、向量和后续扩展使用的指令描述、操作数绑定和执行入口。
- 保持：客户机可见行为、`StepResult` 的公开字段、`engine_step` 引擎边界、trace 文件
  格式和 RV64 单 hart 目标。
- 不实现：本任务不实现 H/向量执行语义，不引入 JIT、周期模型或多 ISA 抽象；向量
  只接入布局、描述和绑定接口。

## 结论

“位布局、操作数语义、执行语义”分离的方向满足可扩展目标，但原方案需要四个修正
才能作为接口契约冻结：

1. 签名必须同时说明操作数类型、读写角色和来源字段。仅写
   `OP_FPR_R` 无法区分 `rs2`、`rs3` 和 `rm`。
2. 执行上下文必须同时保留客户机原始编码和 C 展开后的规范编码，并携带指令描述；
   不能只保留一个 `raw_instr`。
3. `Operand` 不应把寄存器类、读写方向和立即数值揉成一个枚举；向量、CSR、AMO
   和读改写指令需要独立表示这些维度。
4. 执行结果必须区分执行状态、PC 更新和实际写回；不能由 `op[0]` 推测一条指令是否
   真的写了寄存器，也不能把“一次访存”冻结为向量通用接口。

修正后的原则是：

```text
取指/展开       保留原始字节，得到 canonical 32-bit encoding
匹配            InstrDesc 的 opcode/funct/key/mask 和 ISA 要求
字段提取        按 layout descriptor 得到纯位置信息 Fields
操作数绑定      按 OperandSpec 把字段映射成寄存器引用、立即数和属性
执行            EHelper(ExecContext*)，不重新解析 raw encoding
提交/观察       ExecResult 记录 trap、PC、写回、访存和其它副作用
```

其中 C 扩展仍然使用独立的 16 位预解码和展开，这是前端规范化，不是两套执行语义。
展开前保留客户机看到的原始 16 位编码，展开后复用基础指令的字段提取和执行函数。

## 1. 当前问题与设计边界

现有 `ExecOperands` 联合体把整数和浮点操作数分成两套结构，但它无法表达混合指令：

- `fcvt.w.s`：GPR 目的、FPR 源和舍入模式；
- `fld`：FPR 目的、GPR 基址和立即数；
- `fmv.x.w`：GPR 目的、FPR 源；
- `vadd.vv`：VR 目的和两个 VR 源；
- CSR、AMO 和向量访存还会带有额外的编码属性。

执行函数如果再次从 `raw_instr` 提取 `rd/rs1/rs2/rm`，就会在译码层之外维护第二份
布局知识。F/D 当前已经通过共享的浮点执行辅助函数复用算术、FMA、转换和比较骨架；
T027 的目标是让公共字段和操作数绑定也只维护一份。

本任务明确区分四类对象：

1. `FetchedInst`：取指事实和原始字节；
2. `DecodedInst`：匹配到的指令描述和布局字段；
3. `ExecContext`：某次执行所需的 hart、PC、规范编码和绑定操作数；
4. `ExecResult`：本次执行的状态和可观察副作用。

`Decode` 可以在迁移期间作为兼容容器，但最终不再同时承担取指、译码和执行副作用。

## 2. 指令描述和布局

### 2.1 指令描述

`instr.def` 仍然是一条指令一行的唯一描述来源，但每条记录需要包含布局、操作数
签名和扩展要求：

```c
typedef uint32_t InstrId;

typedef enum {
  FMT_R,
  FMT_R4,
  FMT_I,
  FMT_S,
  FMT_B,
  FMT_U,
  FMT_J,
  FMT_CSR,
  FMT_V,
} InstrFormat;

typedef struct {
  InstrId id;
  const char* name;
  const char* pattern;
  uint64_t key;
  uint64_t mask;
  uint8_t shift;
  InstrFormat format;
  IsaExtension required_ext;
  const LayoutDesc* layout;
  const OperandSignature* signature;
  EHelper exec;
} InstrDesc;
```

`fmt` 不再使用容易产生歧义的字符。`R4`、`V`、CSR 和向量访存都应是明确的格式或
布局描述。`required_ext` 用于运行时 ISA profile 检查；编译期是否编译某个执行文件
不能替代客户机当前 hart 的能力检查。

示例形式如下：

```c
INSTR(add, "0000000 ????? ????? 000 ????? 0110011", FMT_R,
      EXT_I, LAYOUT_R, SIG(
        SPEC(GPR, WRITE, FIELD_RD),
        SPEC(GPR, READ,  FIELD_RS1),
        SPEC(GPR, READ,  FIELD_RS2)))

INSTR(fadd_s, "0000000 ????? ????? ??? ????? 1010011", FMT_R,
      EXT_F, LAYOUT_R, SIG(
        SPEC(FPR, WRITE, FIELD_RD),
        SPEC(FPR, READ,  FIELD_RS1),
        SPEC(FPR, READ,  FIELD_RS2),
        SPEC(RM,  READ,  FIELD_RM)))

INSTR(fmadd_s, "?????00 ????? ????? ??? ????? 1000011", FMT_R4,
      EXT_F, LAYOUT_R4, SIG(
        SPEC(FPR, WRITE, FIELD_RD),
        SPEC(FPR, READ,  FIELD_RS1),
        SPEC(FPR, READ,  FIELD_RS2),
        SPEC(FPR, READ,  FIELD_RS3)))

INSTR(fcvt_w_s, "1100000 00000 ????? ??? ????? 1010011", FMT_R,
      EXT_F, LAYOUT_R, SIG(
        SPEC(GPR, WRITE, FIELD_RD),
        SPEC(FPR, READ,  FIELD_RS1),
        SPEC(RM,  READ,  FIELD_RM)))

INSTR(fld, "??????? ????? ????? 011 ????? 0000111", FMT_I,
      EXT_D, LAYOUT_I, SIG(
        SPEC(FPR, WRITE, FIELD_RD),
        SPEC(GPR, READ,  FIELD_RS1),
        SPEC(IMM, READ,  FIELD_IMM_I)))
```

签名必须显式写出字段来源。不能只根据槽位位置猜测 `rs3`、`rm` 或 `zimm`，否则
新增 R4、CSR、向量指令时仍会把布局知识散落到执行函数中。

### 2.2 布局字段

布局层只解释编码位，不出现 GPR/FPR/VR 语义：

```c
typedef struct {
  uint32_t encoding;
  uint8_t rd;
  uint8_t rs1;
  uint8_t rs2;
  uint8_t rs3;
  uint8_t funct3;
  uint8_t funct5;
  uint8_t funct7;
  uint8_t rm;
  uint8_t vm;
  uint8_t aq;
  uint8_t rl;
  uint16_t csr;
  uint32_t zimm;
  word_t imm;
} Fields;
```

`Fields` 是常用字段的规范化结果。扩展特有字段通过 `LayoutDesc` 描述的字段切片或
布局回调提取，不在执行函数中重新移位。B/J/S/U 的非连续立即数在布局层还原并符号
扩展；执行层只看到已经确定的立即数。

布局描述需要支持两种情况：

- 复用现有布局：只引用 `LAYOUT_R/I/S/...`；
- 新增布局：增加一个 `LayoutDesc` 和对应字段提取规则，不修改整数、浮点或向量
  的公共绑定逻辑。

因此，新增扩展通常不会向 `decode_fields()` 增加一个不断增长的 `switch`。只有新增
字段提取机制时才修改布局层。

## 3. 操作数类型、角色和绑定

### 3.1 类型和角色分离

```c
typedef enum {
  OPERAND_NONE,
  OPERAND_GPR,
  OPERAND_FPR,
  OPERAND_VR,
  OPERAND_CSR,
  OPERAND_IMM,
  OPERAND_RM,
  OPERAND_MEM,
} OperandClass;

typedef enum {
  ROLE_NONE,
  ROLE_READ,
  ROLE_WRITE,
  ROLE_READWRITE,
} OperandRole;

typedef enum {
  FIELD_NONE,
  FIELD_RD,
  FIELD_RS1,
  FIELD_RS2,
  FIELD_RS3,
  FIELD_RM,
  FIELD_IMM_I,
  FIELD_IMM_S,
  FIELD_IMM_B,
  FIELD_IMM_U,
  FIELD_IMM_J,
  FIELD_CSR,
  FIELD_ZIMM,
} FieldRef;

typedef struct {
  OperandClass class;
  OperandRole role;
  FieldRef field;
} OperandSpec;

typedef struct {
  uint8_t count;
  OperandSpec slot[EXEC_MAX_OPS];
} OperandSignature;
```

`GPR/FPR/VR` 描述寄存器类，`READ/WRITE/READWRITE` 描述方向，`FieldRef` 描述它来自
哪个编码字段。三者不能合并成一个枚举，否则每增加一种寄存器类或访问方向都要扩展
一组 `OP_*_R/W` 名称。

### 3.2 绑定结果

绑定阶段把 `OperandSpec + Fields` 变成寄存器引用或立即数，不默认读取寄存器值：

```c
typedef struct {
  OperandClass class;
  OperandRole role;
  uint8_t idx;
  word_t imm;
} OperandRef;
```

访问值由执行层的访问器完成：

```c
word_t gpr_read_operand(ExecContext*, const OperandRef*);
word_t fpr_read_raw(ExecContext*, const OperandRef*);
void   gpr_write_operand(ExecContext*, const OperandRef*, word_t);
void   fpr_write_raw(ExecContext*, const OperandRef*, word_t);
```

FPR 绑定只提供原始 64 位位模式；F/D 适配层决定 NaN-box 检查和写回格式。VR 绑定只
提供寄存器索引，向量状态通过专用访问器读取，不把整个向量值塞进 `word_t`。

舍入模式建议作为绑定后的特殊属性而不是普通寄存器槽：

```c
typedef enum {
  RM_NONE,
  RM_STATIC,
  RM_DYNAMIC,
} RoundingModeKind;
```

译码阶段提取原始 `rm`；浮点执行开始时处理 `rm=111` 和 `fcsr.frm`，并在执行前检查
5/6/7。这样 `fsgnj/fmin/feq/fclass` 的 `funct3` 不会被误认为舍入模式。

## 4. 取指、C 展开和执行上下文

### 4.1 原始指令和规范编码

```c
typedef struct {
  vaddr_t pc;
  uint32_t raw_bits;
  uint8_t raw_len;
  uint32_t encoding;
  uint8_t ilen;
  bool instruction_valid;
} FetchedInst;
```

- `raw_bits/raw_len` 保存客户机实际看到的 16 或 32 位字节；
- `encoding` 是执行用的规范 32 位编码；普通指令中它等于 `raw_bits`；
- C 指令先由压缩表展开成 `encoding`，再进入主指令表；
- 非法指令和 trace 使用 `raw_bits`，执行字段使用 `encoding`；
- 第二个 halfword 取指失败时，`ilen` 可以是 4 而 `raw_len` 只有 2。

### 4.2 执行上下文

```c
typedef struct {
  CpuState* hart;
  vaddr_t pc;          /* 当前指令地址 */
  vaddr_t seq_pc;      /* pc + ilen；jal/jalr 的链接值 */
  uint32_t raw_bits;   /* trace、difftest、非法指令 tval */
  uint32_t encoding;   /* C 展开后的规范编码 */
  uint8_t raw_len;
  uint8_t ilen;
  bool vm;

  const InstrDesc* desc;
  Fields fields;
  OperandRef op[EXEC_MAX_OPS];
  uint8_t op_count;
} ExecContext;
```

`ExecContext` 不重新解析编码；执行函数只使用 `fields` 和 `op[]`。执行函数通过
`desc` 访问指令的宽度、扩展要求和向量/原子属性。

`seq_pc` 和 `next_pc` 必须区分：前者是顺序地址和链接值，后者是执行结果提交后的
控制流地址。`npc` 不再承担两个含义。

## 5. 指令描述、匹配和扩展接入

匹配流程固定为：

```text
① 取指并检查 IALIGN
② C 预解码/展开，保留 raw_bits 和 encoding
③ opcode/funct3 桶筛选
④ 完整 key/mask 匹配
⑤ 检查 InstrDesc.required_ext 和当前 ISA profile
⑥ decode_fields(encoding, layout)
⑦ bind_operands(fields, signature)
⑧ 调用 desc->exec(&ctx)
```

opcode/funct3 桶仍然保留“一条指令一行”的可读性和运行时效率；桶内继续做完整
key/mask 匹配，不把哈希或桶索引当成最终正确性判断。扩展增加指令时只增加描述项，
不修改主匹配循环。

绑定失败、扩展未启用、静态 rm 非法等情况都必须形成统一的非法指令结果；不能因为
某个扩展没有编译进来而让同一编码落入另一条指令。

## 6. 执行结果和提交边界

### 6.1 执行状态

```c
typedef enum {
  EXEC_OK,
  EXEC_TRAP,
  EXEC_HOST_ERROR,
} ExecStatus;

typedef struct {
  ExecStatus status;
  word_t cause;
  word_t tval;
  bool next_pc_valid;
  vaddr_t next_pc;
  bool wait_request;
  bool counter_write_cycle;
  bool counter_write_instret;
  MemAccess memory_summary;
} ExecResult;
```

`trap` 布尔值改为状态枚举，避免 host error、同步异常和正常退休混在一起。`cause`
和 `tval` 只在 `EXEC_TRAP` 有效；第一处故障获胜，后续路径不能覆盖它。

### 6.2 写回观察

不能由 `op[0]` 推断实际写回。执行层应通过统一写回辅助函数记录真实发生的写回：

```c
void exec_write_gpr(ExecContext*, uint8_t rd, word_t value);
void exec_write_fpr(ExecContext*, uint8_t rd, word_t value);
```

迁移初期这些函数可以立即更新 hart，并同时填充执行事件的写回记录，以保持当前
解释器性能和语义。后续如果需要严格的集中提交，可以把同一组访问器改为写入
write-set，再由 step 提交，不改变执行函数签名。

普通标量指令的约束是：可能产生同步异常的检查必须在目的寄存器写回之前完成。允许
的内存副作用不回滚；store、MMIO 和 SC 的实际写入情况必须记录。向量部分写入和
故障恢复另行定义，不能用“任何失败都绝不写目的寄存器”覆盖向量语义。

### 6.3 PC 提交

推荐执行函数只通过 `ExecResult.next_pc` 请求控制流变化，step 在确认 `EXEC_OK` 后
统一做对齐检查并提交。若迁移期暂时保留“执行前把 hart PC 推进到 seq_pc”的旧路径，
必须保证异常、链接值和跳转目标都使用 `ctx->pc/ctx->seq_pc`，不能隐式依赖已被提前
修改的 `hart->pc`。

### 6.4 访存记录

标量阶段可以暂时在 `memory_summary` 中保存一条最近的显式访问，以兼容现有
`StepResult`。这个字段不是未来向量的完整内存接口：向量访存、拆分访问和 fault-only-
first 需要由内存访问路径产生多个 `MemoryEvent`，并在最终事件中记录部分进度。

## 7. F/D、向量和其它扩展如何复用

### 7.1 F/D

F/D 共用 `fp_binary/fp_unary/fp_fma/fp_compare` 等执行骨架，差异只保留在：

- f32/f64 SoftFloat 函数族；
- FPR 原始位读写；
- f32 NaN-box 检查和写回；
- `fcvt.s.d`/`fcvt.d.s` 的 boxing 方向；
- 对应扩展的 ISA/FS 检查。

执行函数不再从 `raw_bits` 现场提取 `rd/rs1/rs2/rs3/rm`。

### 7.2 V

V 的第一阶段只接入描述和绑定，不实现执行语义。向量描述除了 VR 操作数，还应携带：

```c
typedef struct {
  uint8_t vd, vs1, vs2, vs3;
  bool vm;
  uint8_t nf;
  uint8_t mop;
  uint8_t lumop;
  uint8_t sumop;
} VectorAttrs;
```

`vl`、`vtype`、`vstart`、`SEW`、`LMUL`、tail/mask policy 等运行时状态由向量执行层从
hart 读取，不复制到普通标量操作数值中。向量 mask 是编码属性和状态访问的一部分，
不能只用一个普通 `OP_IMM` 槽表达。

新增向量算术指令通常需要：

1. 增加一条 `InstrDesc`；
2. 复用 `FMT_V` 或新增一个布局描述；
3. 增加 `OperandSignature` 和 `VectorAttrs`；
4. 增加执行函数。

不需要修改整数、F/D 的字段提取和绑定主循环。只有新增寄存器类或新的副作用类别时，
才扩展公共访问器或提交接口一次。

### 7.3 新扩展成本

| 新增内容 | 主要改动 |
| --- | --- |
| 复用既有布局的标量指令 | `InstrDesc`/`instr.def` 一行 + 执行函数 |
| 新位布局 | 一个 `LayoutDesc` 或布局提取规则 |
| 新寄存器类 | `OperandClass` 和访问器增加一次 |
| 新宽度 | 读写适配器和 SoftFloat/整数转换函数 |
| 新指令属性 | `InstrAttrs` 增加字段并由描述项填充 |
| 新提交副作用 | `ExecResult`/effect 记录增加一次，step 统一应用 |
| 向量执行语义 | V 专用执行层；不修改标量绑定规则 |

## 8. 当前接口的迁移顺序

每一步保持客户机行为不变，并单独提交和验证：

| 步骤 | 内容 | 验证 |
| --- | --- | --- |
| 0 | 固定异常类型、`ExecStatus`、`InstrId` 和 ISA profile 接口 | 头文件独立编译 |
| 1 | 引入 `FetchedInst/Fields/InstrDesc`，保留旧 `Decode` 适配层 | 现有 decode 单测 |
| 2 | 接入 C 的 `raw_bits + encoding` 双编码模型 | C 全编码、非法指令 tval |
| 3 | 引入 `OperandSpec/OperandSignature/OperandRef`，先迁移 I/M | I/M 单测和 Spike 对拍 |
| 4 | 迁移 CSR、A 和控制流的字段映射、PC 提交和写回观察 | CSR/A/branch/jump 回归 |
| 5 | 迁移 F/D，共享 FP 执行骨架，验证 rm、NaN-box 和转换 | F/D harness、Spike 对拍 |
| 6 | 把标量访存接入统一 memory effect，保留现有 `StepResult` | load/store、MMIO、fault |
| 7 | 接入 `FMT_V`、VR 操作数和向量属性，不实现 V 语义 | 向量译码单测 |
| 8 | 扩展向量内存事件和 fault/restart 语义 | 独立 V 规范测试 |
| 9 | 全量回归 | `make check`、Spike、Linux smoke |

迁移期间不允许同时重写所有 `exec.c` 和 `step.c`。每个切片都应有一个可独立验证的
接口行为，避免把字段布局、提交顺序和指令语义错误混在同一次提交中。

## 9. 必须保持的不变量

1. 每个编码字段只在布局层解释一次；执行函数不重新解析原始编码。
2. 原始编码和 C 展开编码同时保留；trace、difftest、`tval` 使用原始编码，执行字段
   使用规范编码。
3. 操作数签名同时包含类型、读写角色和字段来源。
4. 绑定阶段只建立引用和立即数，不把不同寄存器类强行转换成同一种值快照。
5. 不支持的扩展在匹配后、执行前形成非法指令，不落入其它扩展的编码。
6. trap、host error、正常退休和等待请求具有互斥且明确的结果状态。
7. `StepResult` 观察实际发生的写回和访存，不根据 `op[0]` 或格式字母推测副作用。
8. 标量可能故障的检查先于目的寄存器提交；允许的内存副作用按实际发生情况记录。
9. `seq_pc`、链接值和最终 `next_pc` 不混用。
10. 扩展描述的增加不要求修改公共译码主循环；只有新增布局、寄存器类或副作用类别
    时才修改相应公共接口。

## 10. 现有源码的迁移注意事项

当前工作树处于接口迁移中间态，以下差异需要在代码切片中逐项消除，不能当作已经
完成的能力：

- `include/cpu/exec.h` 仍保留旧的 `ExecOperands` 联合体和旧的 `ExecResult` 字段；
- `decode.h`、`decode.c`、`step.c` 的结构体字段和函数签名尚未完全统一；
- 公共执行头文件不应依赖 target 私有的异常头，异常编号应放在共享的架构头中；
- F/D 执行文件已经有公共浮点执行骨架，但译码表和 step 接线需要独立迁移；
- 当前旧接口的 `Decode` 同时承载取指事实、操作数和执行副作用，适配层删除前必须
  先保留 trace、fault 和 difftest 所需字段。

## 11. 验收标准

- `instr.def` 新增一条复用已有布局的指令时，不修改主译码循环；
- 新增 F/D 指令时，不在执行函数中重新提取 `rd/rs1/rs2/rs3/rm`；
- C 指令的原始编码、展开编码、长度和非法指令 `tval` 均可独立观察；
- I/M/A/F/D 的执行结果、寄存器写回、trap、计数器和 trace 与迁移前一致；
- rm 非法、frm 非法、坏 NaN-box、FCVT 饱和、FMIN/FMAX 的 `±0` 仍有专项测试；
- 向量布局可以被解析并生成 `OperandRef + VectorAttrs`，但本任务不声称 V 语义已经
  支持；
- 运行 `make CC=/usr/bin/gcc check`、相关单测、Spike 逐事件对拍和 Linux smoke，
  并把失败区分为基线问题和本次回归。
