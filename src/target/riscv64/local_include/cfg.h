#ifndef RVISS_RISCV64_CFG_H
#define RVISS_RISCV64_CFG_H

#include <common.h>
/*
 * RVISS 的 ISA 能力表，按《The RISC-V Instruction Set Manual》Version 20260120
 * 的 章节顺序排列：非特权卷 isa/riscv-unprivileged.pdf，特权卷
 * isa/riscv-privileged.pdf。
 *
 * 约定：
 *   - 字段名 ext_<小写扩展名>，与 QEMU cpu_cfg_fields 命名一致，便于逐项对照。
 *   - 注释里的 §n 是 **20260120 版**章节号，不是 20250508
 * 版；两版章节号不可互推 （卷 I 因 Zalasr 插为第 17 章，原 §17–§35 全部 +1；卷
 * II 是重组，26 章减为 20 章）。
 *   - 基础 ISA（RV64I）不设字段，由 xlen/MXL 表示；卷 I §36 指令列表与 §37
 * 命名规则 不定义扩展，无字段。
 *   - 组合扩展（B、Zk、Zkn、Zks、Zce、Zvkn 等）与子扩展分别设字段：名字本身也是
 *     规范的 ISA 字符串，不能只保留叶节点。
 *   - 有字段不等于已实现。实际能力仍由 Kconfig + RiscvIsaProfile 决定，见
 *     docs/plans/t023_cpu_state_isa_profile.md 与
 * t026_qemu_rv64_isa_layout.md。
 */
typedef struct {
  /* ===================== 非特权卷（Volume I） ===================== */
  /* §7 Zicntr 与 Zihpm */
  bool ext_zicntr;
  bool ext_zihpm;
  /* §8 Zihintntl */
  bool ext_zihintntl;
  /* §9 Zihintpause */
  bool ext_zihintpause;
  /* §10 Zimop，§10.1 Zcmop */
  bool ext_zimop;
  bool ext_zcmop;
  /* §11 Zicond */
  bool ext_zicond;
  /* §14 Zawrs */
  bool ext_zawrs;
  /* §15 Zacas */
  bool ext_zacas;
  /* §16 Zabha */
  bool ext_zabha;
  /* §17 Zalasr：20260120 新增 */
  bool ext_zalasr;
  /* §19 Ztso */
  bool ext_ztso;
  /* §20 CMO：§20.6.1 Zicbom，§20.6.2 Zicboz，§20.6.3 Zicbop */
  bool ext_zicbom;
  bool ext_zicboz;
  bool ext_zicbop;
  /* §23 Q */
  bool ext_q;
  /* §24 Zfh，§24.6 Zfhmin */
  bool ext_zfh;
  bool ext_zfhmin;
  /* §25 BF16：§25.4.1 Zfbfmin，§25.4.2 Zvfbfmin，§25.4.3 Zvfbfwma */
  bool ext_zfbfmin;
  bool ext_zvfbfmin;
  bool ext_zvfbfwma;
  /* §26 Zfa */
  bool ext_zfa;
  /* §27 Zfinx，§27.2 Zdinx，§27.4 Zhinx，§27.5 Zhinxmin */
  bool ext_zfinx;
  bool ext_zdinx;
  bool ext_zhinx;
  bool ext_zhinxmin;
  /* §29 Zc*：§29.3 Zce，§29.8 Zcb，§29.9 Zcmp，§29.10 Zcmt */
  bool ext_zce;
  bool ext_zcb;
  bool ext_zcmp;
  bool ext_zcmt;
  /* §30 位操作：§30.1 B，§30.2 Zba，§30.3 Zbb，§30.4 Zbc，§30.5 Zbs，
     §30.6 Zbkb，§30.7 Zbkc，§30.8 Zbkx */
  bool ext_b;
  bool ext_zba;
  bool ext_zbb;
  bool ext_zbc;
  bool ext_zbs;
  bool ext_zbkb;
  bool ext_zbkc;
  bool ext_zbkx;
  /* §31 V：§31.18.1 Zvl*，§31.18.2 Zve*，§31.18.4 Zvfhmin，§31.18.5 Zvfh。
     手册的 Zvl 表到 1024b，更长的按同一命名规则延续，如需再补。 */
  bool ext_v;
  bool ext_zvl32b;
  bool ext_zvl64b;
  bool ext_zvl128b;
  bool ext_zvl256b;
  bool ext_zvl512b;
  bool ext_zvl1024b;
  bool ext_zve32x;
  bool ext_zve32f;
  bool ext_zve64x;
  bool ext_zve64f;
  bool ext_zve64d;
  bool ext_zvfhmin;
  bool ext_zvfh;
  /* §32 标量密码：§32.2.1–3 的 Zbkb/Zbkc/Zbkx 已在 §30 列出，不重复设字段 */
  /* §32.2.4 Zknd，§32.2.5 Zkne，§32.2.6 Zknh，§32.2.7 Zksed，§32.2.8 Zksh */
  bool ext_zknd;
  bool ext_zkne;
  bool ext_zknh;
  bool ext_zksed;
  bool ext_zksh;
  /* §32.2.9 Zkr（只有 CSR，无指令） */
  bool ext_zkr;
  /* §32.2.10 Zkn，§32.2.11 Zks，§32.2.12 Zk（shorthand） */
  bool ext_zkn;
  bool ext_zks;
  bool ext_zk;
  /* §32.2.13 Zkt；正文列表在 §32.5 */
  bool ext_zkt;
  /* §33 向量密码（§33.2） */
  bool ext_zvbb;   /* §33.2.1 */
  bool ext_zvbc;   /* §33.2.2 */
  bool ext_zvkb;   /* §33.2.3 */
  bool ext_zvkg;   /* §33.2.4 */
  bool ext_zvkned; /* §33.2.5 */
  bool ext_zvknha; /* §33.2.6 Zvknh[ab] */
  bool ext_zvknhb; /* §33.2.6 Zvknh[ab] */
  bool ext_zvksed; /* §33.2.7 */
  bool ext_zvksh;  /* §33.2.8 */
  bool ext_zvkn;   /* §33.2.9  */
  bool ext_zvknc;  /* §33.2.10 */
  bool ext_zvkng;  /* §33.2.11 */
  bool ext_zvks;   /* §33.2.12 */
  bool ext_zvksc;  /* §33.2.13 */
  bool ext_zvksg;  /* §33.2.14 */
  bool ext_zvkt;   /* §33.2.15 */
  /* §34 CFI：§34.1 Zicfilp，§34.2 Zicfiss */
  bool ext_zicfilp;
  bool ext_zicfiss;

  /* ===================== 特权卷（Volume II） ===================== */

  /* §3 机器级 ISA 1.13、§12 监督级 ISA 1.13：用规范版本表示，不设 ext 字段 */
  /* §4 Smstateen/Ssstateen */
  bool ext_smstateen;
  bool ext_ssstateen;
  /* §5 Smcsrind/Sscsrind */
  bool ext_smcsrind;
  bool ext_sscsrind;
  /* §6 Smepmp */
  bool ext_smepmp;
  /* §7 Smcntrpmf */
  bool ext_smcntrpmf;
  /* §8 Smrnmi */
  bool ext_smrnmi;
  /* §9 Smcdeleg/Ssccfg */
  bool ext_smcdeleg;
  bool ext_ssccfg;
  /* §10 Smdbltrp */
  bool ext_smdbltrp;
  /* §11 Smctr，及其监督级部分 Ssctr */
  bool ext_smctr;
  bool ext_ssctr;
  /* §12.3–§12.6 的 Sv32/Sv39/Sv48/Sv57 是 satp.MODE 分页模式，按 T023 用
     RiscvVmMode 表示，不在这里设布尔能力位 */
  /* §12 页表 A/D 行为：Svade */
  bool ext_svade;
  bool ext_svnapot;     /* §12.7  */
  bool ext_svpbmt;      /* §12.8  */
  bool ext_svinval;     /* §12.9  */
  bool ext_svadu;       /* §12.10 */
  bool ext_svvptc;      /* §12.11 */
  bool ext_svrsw60t59b; /* §12.12：20260120 新增 */
  bool ext_ssqosid;     /* §12.13 */
  /* §13 Sstc */
  bool ext_sstc;
  /* §14 Sscofpmf */
  bool ext_sscofpmf;
  /* §15 H，及柜级 Shlcofideleg */
  bool ext_h;
  bool ext_shlcofideleg;
  /* §16 CFI：只定义 CSR 与特权规则，指令在卷 I §34，无新增扩展名 */
  /* §17 Ssdbltrp */
  bool ext_ssdbltrp;
  /* §18 指针掩码（§18.2.7）：只列对硬件有定义的三个；
     Sspm/Supm 只描述执行环境、不影响硬件实现，故不设字段 */
  bool ext_smnpm;
  bool ext_ssnpm;
  bool ext_smmpm;

  uint32_t mvendorid;
  uint32_t marchid;
  uint64_t mimpid;
} ISAConfig;

#endif /* RVISS_RISCV64_CFG_H */
