#ifndef RVISS_CPU_CSR_MACHINE_H
#define RVISS_CPU_CSR_MACHINE_H

/* ---- misa (0x301) ---- */
#define MISA_MXL (3ull << 62)        /* XLEN encoding field [63:62] */
#define MISA_MXL_32 (1ull << 62)     /* XLEN = 32 */
#define MISA_MXL_64 (2ull << 62)     /* XLEN = 64 */
#define MISA_EXT ((1ull << 26) - 1)  /* single-letter extension bits [25:0] */
#define MISA_A (1ull << 0)           /* atomic */
#define MISA_B (1ull << 1)           /* bit manipulation */
#define MISA_C (1ull << 2)           /* compressed */
#define MISA_D (1ull << 3)           /* double-precision FP */
#define MISA_E (1ull << 4)           /* RV32E embedded base */
#define MISA_F (1ull << 5)           /* single-precision FP */
#define MISA_G (1ull << 6)           /* IMAFD + Zicsr + Zifencei */
#define MISA_H (1ull << 7)           /* hypervisor */
#define MISA_I (1ull << 8)           /* base integer */
#define MISA_J (1ull << 9)           /* reserved */
#define MISA_K (1ull << 10)          /* reserved */
#define MISA_L (1ull << 11)          /* reserved */
#define MISA_M (1ull << 12)          /* integer multiply/divide */
#define MISA_N (1ull << 13)          /* reserved */
#define MISA_O (1ull << 14)          /* reserved */
#define MISA_P (1ull << 15)          /* packed SIMD */
#define MISA_Q (1ull << 16)          /* quad-precision FP */
#define MISA_R (1ull << 17)          /* reserved */
#define MISA_S (1ull << 18)          /* supervisor */
#define MISA_T (1ull << 19)          /* reserved */
#define MISA_U (1ull << 20)          /* user */
#define MISA_V (1ull << 21)          /* vector */
#define MISA_W (1ull << 22)          /* reserved */
#define MISA_X (1ull << 23)          /* non-standard extensions */
#define MISA_Y (1ull << 24)          /* reserved */
#define MISA_Z (1ull << 25)          /* reserved */

/* ---- mvendorid (0xF11) : JEDEC manufacturer ID ---- */
#define MVENDORID_OFFSET (0xFFFFFFull << 7)  /* offset within bank [30:7] */
#define MVENDORID_BANK (0x7Full << 0)        /* number of continuation codes [6:0] */

/* ---- mstatus (0x300) ---- */
#define MSTATUS_SIE (1ull << 1)           /* S-mode interrupt enable */
#define MSTATUS_MIE (1ull << 3)           /* M-mode interrupt enable */
#define MSTATUS_SPIE (1ull << 5)          /* S-mode previous interrupt enable */
#define MSTATUS_UBE (1ull << 6)           /* U-mode big-endian */
#define MSTATUS_MPIE (1ull << 7)          /* M-mode previous interrupt enable */
#define MSTATUS_SPP (1ull << 8)           /* privilege before S-mode trap: 0=U, 1=S */
#define MSTATUS_VS (3ull << 9)            /* vector unit state */
#define MSTATUS_VS_OFF (0ull << 9)        /* off */
#define MSTATUS_VS_INITIAL (1ull << 9)    /* initial */
#define MSTATUS_VS_CLEAN (2ull << 9)      /* clean */
#define MSTATUS_VS_DIRTY (3ull << 9)      /* dirty */
#define MSTATUS_MPP (3ull << 11)          /* privilege before M-mode trap */
#define MSTATUS_MPP_U (0ull << 11)        /* U-mode */
#define MSTATUS_MPP_S (1ull << 11)        /* S-mode */
#define MSTATUS_MPP_M (3ull << 11)        /* M-mode */
#define MSTATUS_FS (3ull << 13)           /* FP unit state */
#define MSTATUS_FS_OFF (0ull << 13)       /* off */
#define MSTATUS_FS_INITIAL (1ull << 13)   /* initial */
#define MSTATUS_FS_CLEAN (2ull << 13)     /* clean */
#define MSTATUS_FS_DIRTY (3ull << 13)     /* dirty */
#define MSTATUS_XS (3ull << 15)           /* additional extension state, read-only */
#define MSTATUS_XS_OFF (0ull << 15)       /* off */
#define MSTATUS_XS_INITIAL (1ull << 15)   /* initial */
#define MSTATUS_XS_CLEAN (2ull << 15)     /* clean */
#define MSTATUS_XS_DIRTY (3ull << 15)     /* dirty */
#define MSTATUS_MPRV (1ull << 17)         /* use MPP for load/store privilege */
#define MSTATUS_SUM (1ull << 18)          /* allow S-mode access to U-mode pages */
#define MSTATUS_MXR (1ull << 19)          /* make executable pages readable */
#define MSTATUS_TVM (1ull << 20)          /* trap S-mode virtual memory ops */
#define MSTATUS_TW (1ull << 21)           /* trap S-mode WFI */
#define MSTATUS_TSR (1ull << 22)          /* trap S-mode SRET */
#define MSTATUS_SPELP (1ull << 23)        /* Zicfilp: expected landing pad (S) */
#define MSTATUS_SPELP_NO_LP (0ull << 23)  /* no landing pad expected */
#define MSTATUS_SPELP_LP (1ull << 23)     /* landing pad expected */
#define MSTATUS_SDT (1ull << 24)          /* Ssdbltrp: S-mode double trap enable */
#define MSTATUS_UXL (3ull << 32)          /* U-mode XLEN encoding */
#define MSTATUS_UXL_32 (1ull << 32)       /* XLEN = 32 */
#define MSTATUS_UXL_64 (2ull << 32)       /* XLEN = 64 */
#define MSTATUS_SXL (3ull << 34)          /* S-mode XLEN encoding */
#define MSTATUS_SXL_32 (1ull << 34)       /* XLEN = 32 */
#define MSTATUS_SXL_64 (2ull << 34)       /* XLEN = 64 */
#define MSTATUS_SBE (1ull << 36)          /* S-mode big-endian */
#define MSTATUS_MBE (1ull << 37)          /* M-mode big-endian */
#define MSTATUS_GVA (1ull << 38)          /* H: mtval holds guest virtual address */
#define MSTATUS_MPV (1ull << 39)          /* H: previous mode was virtualized */
#define MSTATUS_MPELP (1ull << 41)        /* Zicfilp: expected landing pad (M) */
#define MSTATUS_MPELP_NO_LP (0ull << 41)  /* no landing pad expected */
#define MSTATUS_MPELP_LP (1ull << 41)     /* landing pad expected */
#define MSTATUS_MDT (1ull << 42)          /* Smdbltrp: double trap status, reset 1 */
#define MSTATUS_SD (1ull << 63)           /* FS/VS/XS dirty, read-only; RV32: bit 31 */

/* ---- mtvec (0x305) ---- */
#define MTVEC_MODE (3ull << 0)           /* MODE field [1:0]: 0=direct, 1=vectored */
#define MTVEC_MODE_DIRECT (0ull << 0)    /* all traps jump to BASE */
#define MTVEC_MODE_VECTORED (1ull << 0)  /* interrupts jump to BASE + 4 x cause */
#define MTVEC_BASE (~0x3ull)             /* base address [63:2], 4-byte aligned */

/* ---- medeleg (0x302) ---- */
#define MEDELEG_INST_ADDR_MISALIGNED (1ull << 0)   /* misaligned instruction fetch */
#define MEDELEG_INST_ACCESS_FAULT (1ull << 1)      /* instruction access fault */
#define MEDELEG_ILLEGAL_INST (1ull << 2)           /* illegal instruction */
#define MEDELEG_BREAKPOINT (1ull << 3)             /* breakpoint */
#define MEDELEG_LOAD_ADDR_MISALIGNED (1ull << 4)   /* misaligned load */
#define MEDELEG_LOAD_ACCESS_FAULT (1ull << 5)      /* load access fault */
#define MEDELEG_STORE_ADDR_MISALIGNED (1ull << 6)  /* misaligned store */
#define MEDELEG_STORE_ACCESS_FAULT (1ull << 7)     /* store access fault */
#define MEDELEG_ECALL_U (1ull << 8)                /* ecall from U-mode */
#define MEDELEG_ECALL_S (1ull << 9)                /* ecall from S-mode */
#define MEDELEG_ECALL_M (1ull << 11)               /* read-only 0 */
#define MEDELEG_INST_PAGE_FAULT (1ull << 12)       /* instruction page fault */
#define MEDELEG_LOAD_PAGE_FAULT (1ull << 13)       /* load page fault */
#define MEDELEG_STORE_PAGE_FAULT (1ull << 15)      /* store page fault */
#define MEDELEG_DOUBLE_TRAP (1ull << 16)           /* read-only 0 */
#define MEDELEG_SOFTWARE_CHECK (1ull << 18)        /* software check */
#define MEDELEG_HARDWARE_ERROR (1ull << 19)        /* hardware error */

/* ---- mideleg (0x303) : interrupt delegation, same layout as mip ----
 * With H, bits 2/6/10 are read-only 1 (VS interrupts);
 * bit 12 is read-only 1 when GEILEN > 0 */
#define MIDELEG_SSI (1ull << 1)     /* supervisor software */
#define MIDELEG_VSSI (1ull << 2)    /* virtual supervisor software (H) */
#define MIDELEG_MSI (1ull << 3)     /* machine software */
#define MIDELEG_STI (1ull << 5)     /* supervisor timer */
#define MIDELEG_VSTI (1ull << 6)    /* virtual supervisor timer (H) */
#define MIDELEG_MTI (1ull << 7)     /* machine timer */
#define MIDELEG_SEI (1ull << 9)     /* supervisor external */
#define MIDELEG_VSEI (1ull << 10)   /* virtual supervisor external (H) */
#define MIDELEG_MEI (1ull << 11)    /* machine external */
#define MIDELEG_SGEI (1ull << 12)   /* supervisor guest external (H) */
#define MIDELEG_LCOFI (1ull << 13)  /* local counter overflow (Sscofpmf) */

/* ---- mie (0x304) / mip (0x344) : standard interrupt bits [15:0] ----
 * LCOFI: Sscofpmf; VSSIP/VSTIP/VSEIP/SGEIP: H extension
 * (in mip, STIP is read-only when stimecmp is implemented;
 * MEIP/MTIP/MSIP are read-only in mip) */
#define MIE_SSIE (1ull << 1)     /* S-mode software interrupt enable */
#define MIE_VSSIE (1ull << 2)    /* VS software interrupt enable (H) */
#define MIE_MSIE (1ull << 3)     /* M-mode software interrupt enable */
#define MIE_STIE (1ull << 5)     /* S-mode timer interrupt enable */
#define MIE_VSTIE (1ull << 6)    /* VS timer interrupt enable (H) */
#define MIE_MTIE (1ull << 7)     /* M-mode timer interrupt enable */
#define MIE_SEIE (1ull << 9)     /* S-mode external interrupt enable */
#define MIE_VSEIE (1ull << 10)   /* VS external interrupt enable (H) */
#define MIE_MEIE (1ull << 11)    /* M-mode external interrupt enable */
#define MIE_SGEIE (1ull << 12)   /* S guest external interrupt enable (H) */
#define MIE_LCOFIE (1ull << 13)  /* local counter overflow interrupt enable */
#define MIP_SSIP (1ull << 1)     /* S-mode software interrupt pending */
#define MIP_VSSIP (1ull << 2)    /* VS software interrupt pending (H) */
#define MIP_MSIP (1ull << 3)     /* M-mode software interrupt pending */
#define MIP_STIP (1ull << 5)     /* S-mode timer interrupt pending */
#define MIP_VSTIP (1ull << 6)    /* VS timer interrupt pending (H) */
#define MIP_MTIP (1ull << 7)     /* M-mode timer interrupt pending */
#define MIP_SEIP (1ull << 9)     /* S-mode external interrupt pending */
#define MIP_VSEIP (1ull << 10)   /* VS external interrupt pending (H) */
#define MIP_MEIP (1ull << 11)    /* M-mode external interrupt pending */
#define MIP_SGEIP (1ull << 12)   /* S guest external interrupt pending (H) */
#define MIP_LCOFIP (1ull << 13)  /* local counter overflow interrupt pending */
/* bits 16 and above are for platform/custom use */

/* ---- mcounteren (0x306) : counter access from lower modes ---- */
#define MCOUNTEREN_CY (1ull << 0)    /* cycle counter read enable */
#define MCOUNTEREN_TM (1ull << 1)    /* time counter read enable */
#define MCOUNTEREN_IR (1ull << 2)    /* instret counter read enable */
#define MCOUNTEREN_HPM3 (1ull << 3)  /* hpmcounter3-31 read enable, one bit each */
#define MCOUNTEREN_HPM4 (1ull << 4)
#define MCOUNTEREN_HPM5 (1ull << 5)
#define MCOUNTEREN_HPM6 (1ull << 6)
#define MCOUNTEREN_HPM7 (1ull << 7)
#define MCOUNTEREN_HPM8 (1ull << 8)
#define MCOUNTEREN_HPM9 (1ull << 9)
#define MCOUNTEREN_HPM10 (1ull << 10)
#define MCOUNTEREN_HPM11 (1ull << 11)
#define MCOUNTEREN_HPM12 (1ull << 12)
#define MCOUNTEREN_HPM13 (1ull << 13)
#define MCOUNTEREN_HPM14 (1ull << 14)
#define MCOUNTEREN_HPM15 (1ull << 15)
#define MCOUNTEREN_HPM16 (1ull << 16)
#define MCOUNTEREN_HPM17 (1ull << 17)
#define MCOUNTEREN_HPM18 (1ull << 18)
#define MCOUNTEREN_HPM19 (1ull << 19)
#define MCOUNTEREN_HPM20 (1ull << 20)
#define MCOUNTEREN_HPM21 (1ull << 21)
#define MCOUNTEREN_HPM22 (1ull << 22)
#define MCOUNTEREN_HPM23 (1ull << 23)
#define MCOUNTEREN_HPM24 (1ull << 24)
#define MCOUNTEREN_HPM25 (1ull << 25)
#define MCOUNTEREN_HPM26 (1ull << 26)
#define MCOUNTEREN_HPM27 (1ull << 27)
#define MCOUNTEREN_HPM28 (1ull << 28)
#define MCOUNTEREN_HPM29 (1ull << 29)
#define MCOUNTEREN_HPM30 (1ull << 30)
#define MCOUNTEREN_HPM31 (1ull << 31)

/* ---- mcountinhibit (0x320) : counter increment inhibit, no TM bit ---- */
#define MCOUNTINHIBIT_CY (1ull << 0)    /* inhibit cycle counter increment */
#define MCOUNTINHIBIT_IR (1ull << 2)    /* inhibit instret counter increment */
#define MCOUNTINHIBIT_HPM3 (1ull << 3)  /* hpmcounter3-31 inhibit, one bit each */
#define MCOUNTINHIBIT_HPM4 (1ull << 4)
#define MCOUNTINHIBIT_HPM5 (1ull << 5)
#define MCOUNTINHIBIT_HPM6 (1ull << 6)
#define MCOUNTINHIBIT_HPM7 (1ull << 7)
#define MCOUNTINHIBIT_HPM8 (1ull << 8)
#define MCOUNTINHIBIT_HPM9 (1ull << 9)
#define MCOUNTINHIBIT_HPM10 (1ull << 10)
#define MCOUNTINHIBIT_HPM11 (1ull << 11)
#define MCOUNTINHIBIT_HPM12 (1ull << 12)
#define MCOUNTINHIBIT_HPM13 (1ull << 13)
#define MCOUNTINHIBIT_HPM14 (1ull << 14)
#define MCOUNTINHIBIT_HPM15 (1ull << 15)
#define MCOUNTINHIBIT_HPM16 (1ull << 16)
#define MCOUNTINHIBIT_HPM17 (1ull << 17)
#define MCOUNTINHIBIT_HPM18 (1ull << 18)
#define MCOUNTINHIBIT_HPM19 (1ull << 19)
#define MCOUNTINHIBIT_HPM20 (1ull << 20)
#define MCOUNTINHIBIT_HPM21 (1ull << 21)
#define MCOUNTINHIBIT_HPM22 (1ull << 22)
#define MCOUNTINHIBIT_HPM23 (1ull << 23)
#define MCOUNTINHIBIT_HPM24 (1ull << 24)
#define MCOUNTINHIBIT_HPM25 (1ull << 25)
#define MCOUNTINHIBIT_HPM26 (1ull << 26)
#define MCOUNTINHIBIT_HPM27 (1ull << 27)
#define MCOUNTINHIBIT_HPM28 (1ull << 28)
#define MCOUNTINHIBIT_HPM29 (1ull << 29)
#define MCOUNTINHIBIT_HPM30 (1ull << 30)
#define MCOUNTINHIBIT_HPM31 (1ull << 31)

/* ---- mhpmevent3-31 (0x323-0x33F) : one CSR per counter, same layout ----
 * EVENT is the event selector (bits 55:0); 0 selects no event;
 * the INH/OF bits are defined by Sscofpmf */
#define MHPMEVENT_EVENT ((1ull << 56) - 1)  /* event selector, 0 = no event */
#define MHPMEVENT_VUINH (1ull << 58)        /* inhibit VU-mode counting */
#define MHPMEVENT_VSINH (1ull << 59)        /* inhibit VS-mode counting */
#define MHPMEVENT_UINH (1ull << 60)         /* inhibit U-mode counting */
#define MHPMEVENT_SINH (1ull << 61)         /* inhibit S-mode counting */
#define MHPMEVENT_MINH (1ull << 62)         /* inhibit M-mode counting */
#define MHPMEVENT_OF (1ull << 63)           /* counter overflow */

/* ---- mcyclecfg (0x321) / minstretcfg (0x322) : Smcntrpmf ----
 * OF (bit 63) is hardwired 0: these counters raise no overflow interrupt */
#define MCYCLECFG_VUINH (1ull << 58)    /* inhibit VU-mode counting */
#define MCYCLECFG_VSINH (1ull << 59)    /* inhibit VS-mode counting */
#define MCYCLECFG_UINH (1ull << 60)     /* inhibit U-mode counting */
#define MCYCLECFG_SINH (1ull << 61)     /* inhibit S-mode counting */
#define MCYCLECFG_MINH (1ull << 62)     /* inhibit M-mode counting */
#define MCYCLECFG_OF (1ull << 63)       /* read-only 0 */
#define MINSTRETCFG_VUINH (1ull << 58)  /* inhibit VU-mode counting */
#define MINSTRETCFG_VSINH (1ull << 59)  /* inhibit VS-mode counting */
#define MINSTRETCFG_UINH (1ull << 60)   /* inhibit U-mode counting */
#define MINSTRETCFG_SINH (1ull << 61)   /* inhibit S-mode counting */
#define MINSTRETCFG_MINH (1ull << 62)   /* inhibit M-mode counting */
#define MINSTRETCFG_OF (1ull << 63)     /* read-only 0 */

/* ---- mcause (0x342) ---- */
#define MCAUSE_INTERRUPT (1ull << 63)   /* 1 = interrupt, 0 = exception; RV32: bit 31 */
#define MCAUSE_CODE ((1ull << 63) - 1)  /* exception/interrupt code [62:0] */

/* ---- menvcfg (0x30A) : RV64 ---- */
#define MENVCFG_FIOM (1ull << 0)           /* fence.i also orders device I/O */
#define MENVCFG_LPE (1ull << 2)            /* Zicfilp: landing pad enforcement */
#define MENVCFG_SSE (1ull << 3)            /* Zicfiss: shadow stack enable */
#define MENVCFG_CBIE (3ull << 4)           /* Zicbom: cache block invalidate enable */
#define MENVCFG_CBCFE (1ull << 6)          /* Zicbom: cache block clean/flush enable */
#define MENVCFG_CBZE (1ull << 7)           /* Zicbom: cache block zero enable */
#define MENVCFG_PMM (3ull << 32)           /* Smnpm: pointer masking mode */
#define MENVCFG_PMM_DISABLED (0ull << 32)  /* pointer masking disabled */
#define MENVCFG_PMM_RESERVED (1ull << 32)  /* reserved */
#define MENVCFG_PMM_PMLEN7 (2ull << 32)    /* PMLEN = 7 */
#define MENVCFG_PMM_PMLEN16 (3ull << 32)   /* PMLEN = 16 */
#define MENVCFG_DTE (1ull << 59)           /* Ssdbltrp: double trap enable */
#define MENVCFG_CDE (1ull << 60)           /* Smcdeleg: counter delegation enable */
#define MENVCFG_ADUE (1ull << 61)          /* Svadu: hardware A/D update enable */
#define MENVCFG_PBMTE (1ull << 62)         /* Svpbmt: page-based memory types enable */
#define MENVCFG_STCE (1ull << 63)          /* Sstc: stimecmp enable */

/* ---- mseccfg (0x747) : RV64 ----
 * MML is sticky: once set, only reset clears it;
 * RLB becomes read-only once cleared to 0 */
#define MSECCFG_MML (1ull << 0)            /* Smepmp: M-mode subject to PMP */
#define MSECCFG_MMWP (1ull << 1)           /* Smepmp: M-mode whitelist policy */
#define MSECCFG_RLB (1ull << 2)            /* Smepmp: bypass rule lock */
#define MSECCFG_USEED (1ull << 8)          /* Zkr: U-mode access to seed CSR */
#define MSECCFG_SSEED (1ull << 9)          /* Zkr: S/HS-mode access to seed CSR */
#define MSECCFG_MLPE (1ull << 10)          /* Zicfilp: M-mode landing pad enable */
#define MSECCFG_PMM (3ull << 32)           /* Smmpm: pointer masking mode */
#define MSECCFG_PMM_DISABLED (0ull << 32)  /* pointer masking disabled */
#define MSECCFG_PMM_RESERVED (1ull << 32)  /* reserved */
#define MSECCFG_PMM_PMLEN7 (2ull << 32)    /* PMLEN = 7 */
#define MSECCFG_PMM_PMLEN16 (3ull << 32)   /* PMLEN = 16 */

/* ---- pmpcfg0-15 (0x3A0-0x3AF) : one 8-bit entry per byte ----
 * per-entry fields (R=0, W=1 is reserved; Smepmp reinterprets it) */
#define PMPCFG_R (1ull << 0)        /* read */
#define PMPCFG_W (1ull << 1)        /* write */
#define PMPCFG_X (1ull << 2)        /* execute */
#define PMPCFG_A (3ull << 3)        /* address matching mode */
#define PMPCFG_A_OFF (0ull << 3)    /* disabled */
#define PMPCFG_A_TOR (1ull << 3)    /* top of range */
#define PMPCFG_A_NA4 (2ull << 3)    /* naturally aligned 4 bytes */
#define PMPCFG_A_NAPOT (3ull << 3)  /* naturally aligned power-of-two */
#define PMPCFG_L (1ull << 7)        /* locked until reset; enforced in M-mode too */
/* bit offset of entry n within its pmpcfg register */
#define PMPCFG_ENTRY_SHIFT_RV64(n) (((n) % 8) * 8)  /* pmpcfg0/2/.../14 */
#define PMPCFG_ENTRY_SHIFT_RV32(n) (((n) % 4) * 8)  /* pmpcfg0..15 */

/* ---- pmpaddr0-63 (0x3B0-0x3EF) ---- */
#define PMPADDR_ADDR_RV64 ((1ull << 54) - 1)  /* paddr[55:2], bits 53:0 */
#define PMPADDR_ADDR_RV32 (0xFFFFFFFFull)     /* paddr[33:2], bits 31:0 */

/* ---- mstateen0 (0x30C) : Smstateen ----
 * mstateen1-3 define only bit 63 (SE1/SE2/SE3); all other bits reserved */
#define MSTATEEN0_C (1ull << 0)         /* custom state access */
#define MSTATEEN0_FCSR (1ull << 1)      /* fcsr access (Zfinx/Zdinx/Zhinx) */
#define MSTATEEN0_JVT (1ull << 2)       /* jvt access (Zcmt) */
#define MSTATEEN0_CTR (1ull << 54)      /* Smctr: ctr CSRs access */
#define MSTATEEN0_SRMCFG (1ull << 55)   /* Ssqosid: srmcfg access */
#define MSTATEEN0_P1P13 (1ull << 56)    /* hedelegh access (priv spec v1.13) */
#define MSTATEEN0_CONTEXT (1ull << 57)  /* scontext access (Sdtrig) */
#define MSTATEEN0_IMSIC (1ull << 58)    /* Ssaia: imsic CSRs access */
#define MSTATEEN0_AIA (1ull << 59)      /* Ssaia: aia CSRs access */
#define MSTATEEN0_CSRIND (1ull << 60)   /* siselect/sireg (Smcsrind/Sscsrind) */
#define MSTATEEN0_ENVCFG (1ull << 62)   /* henvcfg access */
#define MSTATEEN0_SE0 (1ull << 63)      /* gate sstateen0/hstateen0 */
#define MSTATEEN1_SE1 (1ull << 63)      /* gate sstateen1/hstateen1 */
#define MSTATEEN2_SE2 (1ull << 63)      /* gate sstateen2/hstateen2 */
#define MSTATEEN3_SE3 (1ull << 63)      /* gate sstateen3/hstateen3 */

/* ---- mnstatus (0x744) : Smrnmi ---- */
#define MNSTATUS_NMIE (1ull << 3)          /* NMI interrupt enable */
#define MNSTATUS_MNPV (1ull << 7)          /* H: previous mode was virtualized */
#define MNSTATUS_MNPELP (1ull << 9)        /* Zicfilp: expected landing pad */
#define MNSTATUS_MNPELP_NO_LP (0ull << 9)  /* no landing pad expected */
#define MNSTATUS_MNPELP_LP (1ull << 9)     /* landing pad expected */
#define MNSTATUS_MNPP (3ull << 11)         /* privilege before NMI */
#define MNSTATUS_MNPP_U (0ull << 11)       /* U-mode */
#define MNSTATUS_MNPP_S (1ull << 11)       /* S-mode */
#define MNSTATUS_MNPP_M (3ull << 11)       /* M-mode */

/* ---- mctrctl (0x34E) : Smctr, 64-bit ---- */
#define MCTRCTL_U (1ull << 0)            /* record transfers in U-mode */
#define MCTRCTL_S (1ull << 1)            /* record transfers in S/HS-mode */
#define MCTRCTL_M (1ull << 2)            /* record transfers in M-mode */
#define MCTRCTL_RASEMU (1ull << 7)       /* RAS emulation mode */
#define MCTRCTL_STE (1ull << 8)          /* record traps to S-mode while S=0 */
#define MCTRCTL_MTE (1ull << 9)          /* record traps to M-mode */
#define MCTRCTL_BPFRZ (1ull << 11)       /* freeze on breakpoint exception */
#define MCTRCTL_LCOFIFRZ (1ull << 12)    /* freeze on local counter overflow */
#define MCTRCTL_EXCINH (1ull << 33)      /* inhibit recording exceptions */
#define MCTRCTL_INTRINH (1ull << 34)     /* inhibit recording interrupts */
#define MCTRCTL_TRETINH (1ull << 35)     /* inhibit recording trap returns */
#define MCTRCTL_NTBREN (1ull << 36)      /* record not-taken branches (opt-in) */
#define MCTRCTL_TKBRINH (1ull << 37)     /* inhibit recording taken branches */
#define MCTRCTL_INDCALLINH (1ull << 40)  /* inhibit recording indirect calls */
#define MCTRCTL_DIRCALLINH (1ull << 41)  /* inhibit recording direct calls */
#define MCTRCTL_INDJMPINH (1ull << 42)   /* inhibit recording indirect jumps */
#define MCTRCTL_DIRJMPINH (1ull << 43)   /* inhibit recording direct jumps */
#define MCTRCTL_CORSWAPINH (1ull << 44)  /* inhibit recording co-routine swaps */
#define MCTRCTL_RETINH (1ull << 45)      /* inhibit recording returns */
#define MCTRCTL_INDLJMPINH (1ull << 46)  /* inhibit other indirect jumps (linkage) */
#define MCTRCTL_DIRLJMPINH (1ull << 47)  /* inhibit other direct jumps (linkage) */
#define MCTRCTL_CUSTOM (0xFull << 60)    /* custom use [63:60] */
#endif /* RVISS_CPU_CSR_MACHINE_H */
