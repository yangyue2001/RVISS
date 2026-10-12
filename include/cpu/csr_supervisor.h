// clang-format off
#define CSRS_Unprivileged_Counter_Timers(f) \
    f(hpmcounter3    , 0xC03) \
    f(hpmcounter4    , 0xC04) f(hpmcounter5    , 0xC05) f(hpmcounter6    , 0xC06) f(hpmcounter7    , 0xC07) \
    f(hpmcounter8    , 0xC08) f(hpmcounter9    , 0xC09) f(hpmcounter10   , 0xC0A) f(hpmcounter11   , 0xC0B) \
    f(hpmcounter12   , 0xC0C) f(hpmcounter13   , 0xC0D) f(hpmcounter14   , 0xC0E) f(hpmcounter15   , 0xC0F) \
    f(hpmcounter16   , 0xC10) f(hpmcounter17   , 0xC11) f(hpmcounter18   , 0xC12) f(hpmcounter19   , 0xC13) \
    f(hpmcounter20   , 0xC14) f(hpmcounter21   , 0xC15) f(hpmcounter22   , 0xC16) f(hpmcounter23   , 0xC17) \
    f(hpmcounter24   , 0xC18) f(hpmcounter25   , 0xC19) f(hpmcounter26   , 0xC1A) f(hpmcounter27   , 0xC1B) \
    f(hpmcounter28   , 0xC1C) f(hpmcounter29   , 0xC1D) f(hpmcounter30   , 0xC1E) f(hpmcounter31   , 0xC1F)
// clang-format on

#define SSTATUS_SIE (1ull << 1)
#define SSTATUS_SPIE (1ull << 5)
#define SSTATUS_UBE (1ull << 6)
#define SSTATUS_SPP (1ull << 8)
#define SSTATUS_VS (3ull << 9)
#define SSTATUS_VS_OFF (0ull << 9)
#define SSTATUS_VS_INITIAL (1ull << 9)
#define SSTATUS_VS_CLEAN (2ull << 9)
#define SSTATUS_VS_DIRTY (3ull << 9)
#define SSTATUS_FS (3ull << 13)
#define SSTATUS_FS_OFF (0ull << 13)
#define SSTATUS_FS_INITIAL (1ull << 13)
#define SSTATUS_FS_CLEAN (2ull << 13)
#define SSTATUS_FS_DIRTY (3ull << 13)
#define SSTATUS_XS (3ull << 15)
#define SSTATUS_XS_OFF (0ull << 15)
#define SSTATUS_XS_INITIAL (1ull << 15)
#define SSTATUS_XS_CLEAN (2ull << 15)
#define SSTATUS_XS_DIRTY (3ull << 15)
#define SSTATUS_SUM (1ull << 18)
#define SSTATUS_MXR (1ull << 19)
#define SSTATUS_SPELP (1ull << 23) /* Zicfilp */
#define SSTATUS_SPELP_NO_LP (0ull << 23)
#define SSTATUS_SPELP_LP (1ull << 23)
#define SSTATUS_SDT (1ull << 24) /* Ssdbltrp */
#define SSTATUS_UXL (3ull << 32) /* SXLEN=64 */
#define SSTATUS_UXL_32 (1ull << 32)
#define SSTATUS_UXL_64 (2ull << 32)
#define SSTATUS_SD (1ull << 63) /* SXLEN=32 时为 bit 31 */

/* ---- stvec (0x105) ---- */
#define STVEC_MODE (3ull << 0)
#define STVEC_MODE_DIRECT (0ull << 0)
#define STVEC_MODE_VECTORED (1ull << 0)
#define STVEC_BASE (~0x3ull)

/* ---- sie (0x104) / sip (0x144) ---- */
#define SIE_SSIE (1ull << 1)
#define SIE_STIE (1ull << 5)
#define SIE_SEIE (1ull << 9)
#define SIE_LCOFIE (1ull << 13) /* Sscofpmf */
#define SIP_SSIP (1ull << 1)
#define SIP_STIP (1ull << 5) /* Sstc 且实现 stimecmp 时只读 */
#define SIP_SEIP (1ull << 9)
#define SIP_LCOFIP (1ull << 13)

/* ---- scounteren (0x106) : 控制 U 态可访问的计数器 ---- */
#define SCOUNTEREN_CY (1ull << 0)
#define SCOUNTEREN_TM (1ull << 1)
#define SCOUNTEREN_IR (1ull << 2)
#define SCOUNTEREN_HPM3 (1ull << 3)
#define SCOUNTEREN_HPM4 (1ull << 4)
#define SCOUNTEREN_HPM5 (1ull << 5)
#define SCOUNTEREN_HPM6 (1ull << 6)
#define SCOUNTEREN_HPM7 (1ull << 7)
#define SCOUNTEREN_HPM8 (1ull << 8)
#define SCOUNTEREN_HPM9 (1ull << 9)
#define SCOUNTEREN_HPM10 (1ull << 10)
#define SCOUNTEREN_HPM11 (1ull << 11)
#define SCOUNTEREN_HPM12 (1ull << 12)
#define SCOUNTEREN_HPM13 (1ull << 13)
#define SCOUNTEREN_HPM14 (1ull << 14)
#define SCOUNTEREN_HPM15 (1ull << 15)
#define SCOUNTEREN_HPM16 (1ull << 16)
#define SCOUNTEREN_HPM17 (1ull << 17)
#define SCOUNTEREN_HPM18 (1ull << 18)
#define SCOUNTEREN_HPM19 (1ull << 19)
#define SCOUNTEREN_HPM20 (1ull << 20)
#define SCOUNTEREN_HPM21 (1ull << 21)
#define SCOUNTEREN_HPM22 (1ull << 22)
#define SCOUNTEREN_HPM23 (1ull << 23)
#define SCOUNTEREN_HPM24 (1ull << 24)
#define SCOUNTEREN_HPM25 (1ull << 25)
#define SCOUNTEREN_HPM26 (1ull << 26)
#define SCOUNTEREN_HPM27 (1ull << 27)
#define SCOUNTEREN_HPM28 (1ull << 28)
#define SCOUNTEREN_HPM29 (1ull << 29)
#define SCOUNTEREN_HPM30 (1ull << 30)
#define SCOUNTEREN_HPM31 (1ull << 31)

/* ---- senvcfg (0x10A) : SXLEN=64 ---- */
#define SENVCFG_FIOM (1ull << 0)
#define SENVCFG_LPE (1ull << 2)   /* Zicfilp */
#define SENVCFG_SSE (1ull << 3)   /* Zicfiss */
#define SENVCFG_CBIE (3ull << 4)  /* Zicbom */
#define SENVCFG_CBCFE (1ull << 6) /* Zicbom */
#define SENVCFG_CBZE (1ull << 7)  /* Zicboz */
#define SENVCFG_PMM (3ull << 32)  /* Ssnpm; RV32 无此字段 */
#define SENVCFG_PMM_DISABLED (0ull << 32)
#define SENVCFG_PMM_RESERVED (1ull << 32)
#define SENVCFG_PMM_PMLEN7 (2ull << 32)
#define SENVCFG_PMM_PMLEN16 (3ull << 32)

/* ---- satp (0x180) ---- */
/* SXLEN=64 (Bare / Sv39 / Sv48 / Sv57) */
#define SATP64_MODE (0xFull << 60)
#define SATP64_MODE_BARE (0ull << 60)
#define SATP64_MODE_SV39 (8ull << 60)
#define SATP64_MODE_SV48 (9ull << 60)
#define SATP64_MODE_SV57 (10ull << 60)
#define SATP64_ASID (0xFFFFull << 44)
#define SATP64_PPN ((1ull << 44) - 1)
/* SXLEN=32 */
#define SATP32_MODE (1ull << 31)
#define SATP32_MODE_BARE (0ull << 31)
#define SATP32_MODE_SV32 (1ull << 31)
#define SATP32_ASID (0x1FFull << 22)
#define SATP32_PPN ((1ull << 22) - 1)

/* ---- scause (0x142) ----
 * 原因编码见文件末尾附录 (与 mcause 相同) */
#define SCAUSE_INTERRUPT (1ull << 63) /* RV32: (1ul << 31) */
#define SCAUSE_CODE ((1ull << 63) - 1)

/* ---- srmcfg (Ssqosid) : SXLEN=64 ---- */
#define SRMCFG_RCID (0xFFFull << 0)
#define SRMCFG_MCID (0xFFFull << 16)

/* ---- scountovf (0xDA0) : Sscofpmf, 只读 ----
 * bit n 是 mhpmeventn.OF 的影子 (n = 3..31) */
#define SCOUNTOVF_OF3 (1ull << 3)
#define SCOUNTOVF_OF4 (1ull << 4)
#define SCOUNTOVF_OF5 (1ull << 5)
#define SCOUNTOVF_OF6 (1ull << 6)
#define SCOUNTOVF_OF7 (1ull << 7)
#define SCOUNTOVF_OF8 (1ull << 8)
#define SCOUNTOVF_OF9 (1ull << 9)
#define SCOUNTOVF_OF10 (1ull << 10)
#define SCOUNTOVF_OF11 (1ull << 11)
#define SCOUNTOVF_OF12 (1ull << 12)
#define SCOUNTOVF_OF13 (1ull << 13)
#define SCOUNTOVF_OF14 (1ull << 14)
#define SCOUNTOVF_OF15 (1ull << 15)
#define SCOUNTOVF_OF16 (1ull << 16)
#define SCOUNTOVF_OF17 (1ull << 17)
#define SCOUNTOVF_OF18 (1ull << 18)
#define SCOUNTOVF_OF19 (1ull << 19)
#define SCOUNTOVF_OF20 (1ull << 20)
#define SCOUNTOVF_OF21 (1ull << 21)
#define SCOUNTOVF_OF22 (1ull << 22)
#define SCOUNTOVF_OF23 (1ull << 23)
#define SCOUNTOVF_OF24 (1ull << 24)
#define SCOUNTOVF_OF25 (1ull << 25)
#define SCOUNTOVF_OF26 (1ull << 26)
#define SCOUNTOVF_OF27 (1ull << 27)
#define SCOUNTOVF_OF28 (1ull << 28)
#define SCOUNTOVF_OF29 (1ull << 29)
#define SCOUNTOVF_OF30 (1ull << 30)
#define SCOUNTOVF_OF31 (1ull << 31)

/* ---- scountinhibit (0x120) : Smcdeleg/Ssccfg, mcountinhibit 的掩码别名 ----
 * (无 TM 位) */
#define SCOUNTERINHIBIT_CY (1ull << 0)
#define SCOUNTERINHIBIT_IR (1ull << 2)
#define SCOUNTERINHIBIT_HPM3 (1ull << 3)
#define SCOUNTERINHIBIT_HPM4 (1ull << 4)
#define SCOUNTERINHIBIT_HPM5 (1ull << 5)
#define SCOUNTERINHIBIT_HPM6 (1ull << 6)
#define SCOUNTERINHIBIT_HPM7 (1ull << 7)
#define SCOUNTERINHIBIT_HPM8 (1ull << 8)
#define SCOUNTERINHIBIT_HPM9 (1ull << 9)
#define SCOUNTERINHIBIT_HPM10 (1ull << 10)
#define SCOUNTERINHIBIT_HPM11 (1ull << 11)
#define SCOUNTERINHIBIT_HPM12 (1ull << 12)
#define SCOUNTERINHIBIT_HPM13 (1ull << 13)
#define SCOUNTERINHIBIT_HPM14 (1ull << 14)
#define SCOUNTERINHIBIT_HPM15 (1ull << 15)
#define SCOUNTERINHIBIT_HPM16 (1ull << 16)
#define SCOUNTERINHIBIT_HPM17 (1ull << 17)
#define SCOUNTERINHIBIT_HPM18 (1ull << 18)
#define SCOUNTERINHIBIT_HPM19 (1ull << 19)
#define SCOUNTERINHIBIT_HPM20 (1ull << 20)
#define SCOUNTERINHIBIT_HPM21 (1ull << 21)
#define SCOUNTERINHIBIT_HPM22 (1ull << 22)
#define SCOUNTERINHIBIT_HPM23 (1ull << 23)
#define SCOUNTERINHIBIT_HPM24 (1ull << 24)
#define SCOUNTERINHIBIT_HPM25 (1ull << 25)
#define SCOUNTERINHIBIT_HPM26 (1ull << 26)
#define SCOUNTERINHIBIT_HPM27 (1ull << 27)
#define SCOUNTERINHIBIT_HPM28 (1ull << 28)
#define SCOUNTERINHIBIT_HPM29 (1ull << 29)
#define SCOUNTERINHIBIT_HPM30 (1ull << 30)
#define SCOUNTERINHIBIT_HPM31 (1ull << 31)

/* ---- mctrctl 的 S 态视图 sctrctl (Smctr/Ssctr) ----
 * 与 mctrctl 相同位布局; 其中 bit 2 (M) 与 bit 9 (MTE) 只读 0 */
#define SCTRCTL_U (1ull << 0)
#define SCTRCTL_S (1ull << 1)
#define SCTRCTL_M (1ull << 2) /* 只读 0 */
#define SCTRCTL_RASEMU (1ull << 7)
#define SCTRCTL_STE (1ull << 8)
#define SCTRCTL_MTE (1ull << 9) /* 只读 0 */
#define SCTRCTL_BPFRZ (1ull << 11)
#define SCTRCTL_LCOFIFRZ (1ull << 12)
#define SCTRCTL_EXCINH (1ull << 33)
#define SCTRCTL_INTRINH (1ull << 34)
#define SCTRCTL_TRETINH (1ull << 35)
#define SCTRCTL_NTBREN (1ull << 36)
#define SCTRCTL_TKBRINH (1ull << 37)
#define SCTRCTL_INDCALLINH (1ull << 40)
#define SCTRCTL_DIRCALLINH (1ull << 41)
#define SCTRCTL_INDJMPINH (1ull << 42)
#define SCTRCTL_DIRJMPINH (1ull << 43)
#define SCTRCTL_CORSWAPINH (1ull << 44)
#define SCTRCTL_RETINH (1ull << 45)
#define SCTRCTL_INDLJMPINH (1ull << 46)
#define SCTRCTL_DIRLJMPINH (1ull << 47)
#define SCTRCTL_CUSTOM (0xFull << 60)

/* ---- sctrdepth (Smctr/Ssctr) ---- */
#define SCTRDEPTH_DEPTH (7ull << 0)
#define SCTRDEPTH_DEPTH_16 (0ull << 0)
#define SCTRDEPTH_DEPTH_32 (1ull << 0)
#define SCTRDEPTH_DEPTH_64 (2ull << 0)
#define SCTRDEPTH_DEPTH_128 (3ull << 0)
#define SCTRDEPTH_DEPTH_256 (4ull << 0)

/* ---- sctrstatus (Smctr/Ssctr, 只读状态由硬件更新) ---- */
#define SCTRSTATUS_FROZEN (1ull << 31)
#define SCTRSTATUS_WRPTR (0xFFull << 0)

/* ---- sstateen0 (0x10C) : Smstateen/Ssstateen ----
 * 与 mstateen0 的低位对应; sstateen1-3 本版无定义位(只读 0) */
#define SSTATEEN0_C (1ull << 0)
#define SSTATEEN0_FCSR (1ull << 1)
#define SSTATEEN0_JVT (1ull << 2)