// clang-format off
#define CSRS_Unprivileged_FloatingPoint(f) \
    f(fflags          , 0x001) \
    f(frm             , 0x002) \
    f(fcsr            , 0x003)

/* 非特权向量 CSR */
#define CSRS_Unprivileged_Vector(f) \
    f(vstart          , 0x008) \
    f(vxsat           , 0x009) \
    f(vxrm            , 0x00A) \
    f(vcsr            , 0x00F) \
    f(vl              , 0xC20) \
    f(vtype           , 0xC21) \
    f(vlenb           , 0xC22)

/* 非特权 Zicfiss 扩展 CSR */
#define CSRS_Unprivileged_Zicfiss(f) \
    f(ssp             , 0x011)

/* 非特权熵源扩展 (Zkr) CSR */
#define CSRS_Unprivileged_Zkr(f) \
    f(seed            , 0x015)

/* 非特权 Zcmt 扩展 CSR */
#define CSRS_Unprivileged_Zcmt(f) \
    f(jvt             , 0x017)

/* 非特权计数器/定时器 (带 h 后缀的 0xC8x-0xC9x 为 RV32 only) */
#define CSRS_Unprivileged_Counter_Timers(f) \
    f(cycle           , 0xC00) \
    f(time            , 0xC01) \
    f(instret         , 0xC02) \
    f(hpmcounter3     , 0xC03) \
    f(hpmcounter4     , 0xC04) f(hpmcounter5     , 0xC05) f(hpmcounter6     , 0xC06) f(hpmcounter7     , 0xC07) \
    f(hpmcounter8     , 0xC08) f(hpmcounter9     , 0xC09) f(hpmcounter10    , 0xC0A) f(hpmcounter11    , 0xC0B) \
    f(hpmcounter12    , 0xC0C) f(hpmcounter13    , 0xC0D) f(hpmcounter14    , 0xC0E) f(hpmcounter15    , 0xC0F) \
    f(hpmcounter16    , 0xC10) f(hpmcounter17    , 0xC11) f(hpmcounter18    , 0xC12) f(hpmcounter19    , 0xC13) \
    f(hpmcounter20    , 0xC14) f(hpmcounter21    , 0xC15) f(hpmcounter22    , 0xC16) f(hpmcounter23    , 0xC17) \
    f(hpmcounter24    , 0xC18) f(hpmcounter25    , 0xC19) f(hpmcounter26    , 0xC1A) f(hpmcounter27    , 0xC1B) \
    f(hpmcounter28    , 0xC1C) f(hpmcounter29    , 0xC1D) f(hpmcounter30    , 0xC1E) f(hpmcounter31    , 0xC1F)

/* U 态合计 */
#define CSRS_Unprivileged(f) \
    CSRS_Unprivileged_FloatingPoint(f) \
    CSRS_Unprivileged_Vector(f) \
    CSRS_Unprivileged_Zicfiss(f) \
    CSRS_Unprivileged_Zkr(f) \
    CSRS_Unprivileged_Zcmt(f) \
    CSRS_Unprivileged_Counter_Timers(f)
// clang-format on

/* ============================================================================
 *                        U 态 (非特权 Unprivileged)
 * ============================================================================
 */

/* ---- fflags (0x001) ---- */
#define FFLAGS_NX (1ull << 0)
#define FFLAGS_UF (1ull << 1)
#define FFLAGS_OF (1ull << 2)
#define FFLAGS_DZ (1ull << 3)
#define FFLAGS_NV (1ull << 4)

/* ---- frm (0x002) ---- */
#define FRM_RNE (0ull << 0)
#define FRM_RTZ (1ull << 0)
#define FRM_RDN (2ull << 0)
#define FRM_RUP (3ull << 0)
#define FRM_RMM (4ull << 0)
#define FRM_DYN (7ull << 0) /* 仅指令 rm 字段用, 寄存器中保留 */

/* ---- fcsr (0x003) = frm << 5 | fflags ---- */
#define FCSR_NX (1ull << 0)
#define FCSR_UF (1ull << 1)
#define FCSR_OF (1ull << 2)
#define FCSR_DZ (1ull << 3)
#define FCSR_NV (1ull << 4)
#define FCSR_FFLAGS (0x1Full << 0)
#define FCSR_FRM (7ull << 5)

/* ---- vstart (0x008) : 无字段(元素索引, 宽度随 VLEN) ---- */

/* ---- vxsat (0x009) ---- */
#define VXSAT_VXSAT (1ull << 0)

/* ---- vxrm (0x00A) ---- */
#define VXRM_VXRM (3ull << 0)

/* ---- vcsr (0x00F) ---- */
#define VCSR_VXSAT (1ull << 0)
#define VCSR_VXRM (3ull << 1)

/* ---- vl (0xC20) : 无字段(当前向量长度) ---- */

/* ---- vtype (0xC21) : 只读, 由 vset{i}vl{i} 设置 ----
 * vill 位于 bit XLEN-1 (RV32 为 bit 31) */
#define VTYPE_VLMUL (7ull << 0)
#define VTYPE_VLMUL_1 (0ull << 0)   /* LMUL=1   */
#define VTYPE_VLMUL_2 (1ull << 0)   /* LMUL=2   */
#define VTYPE_VLMUL_4 (2ull << 0)   /* LMUL=4   */
#define VTYPE_VLMUL_8 (3ull << 0)   /* LMUL=8   */
#define VTYPE_VLMUL_8TH (5ull << 0) /* LMUL=1/8 */
#define VTYPE_VLMUL_4TH (6ull << 0) /* LMUL=1/4 */
#define VTYPE_VLMUL_2TH (7ull << 0) /* LMUL=1/2 */
#define VTYPE_VSEW (7ull << 3)
#define VTYPE_VSEW_8 (0ull << 3)
#define VTYPE_VSEW_16 (1ull << 3)
#define VTYPE_VSEW_32 (2ull << 3)
#define VTYPE_VSEW_64 (3ull << 3)
#define VTYPE_VSEW_128 (4ull << 3)
#define VTYPE_VSEW_256 (5ull << 3)
#define VTYPE_VSEW_512 (6ull << 3)
#define VTYPE_VSEW_1024 (7ull << 3)
#define VTYPE_VTA (1ull << 6)
#define VTYPE_VMA (1ull << 7)
#define VTYPE_VILL (1ull << 63) /* RV32: (1ul << 31) */

/* ---- vlenb (0xC22) : 无字段(VLEN/8, 设计常量) ---- */

/* ---- ssp (0x011) : Shadow Stack Pointer, 无字段(Zicfiss, 由 Smstateen/sspctl
 * 控制访问) ---- */

/* ---- seed (0x015) : Zkr, 访问需 csrrw 方式 ---- */
#define SEED_ENTROPY (0xFFFFull << 0)
#define SEED_CUSTOM (0xFFull << 16) /* 自定义/实验用途 */
#define SEED_OPST (3ull << 30)
#define SEED_OPST_BIST (0ull << 30)
#define SEED_OPST_WAIT (1ull << 30)
#define SEED_OPST_ES16 (2ull << 30)
#define SEED_OPST_DEAD (3ull << 30)

/* ---- jvt (0x017) : Zcmt ----
 * base 位于 bit XLEN-1:6 (64 字节对齐) */
#define JVT_MODE (0x3Full << 0)
#define JVT_MODE_JUMP_TABLE (0ull << 0) /* 仅定义 0; 其余保留 */
#define JVT_BASE (~0x3Full)
