// clang-format off
/* ---- Hypervisor ---- */

#define CSRS_Hypervisor_TrapSetup(f) \
    f(hstatus           , 0x600) \
    f(hedeleg           , 0x602) \
    f(hideleg           , 0x603) \
    f(hie               , 0x604) \
    f(hcounteren        , 0x606) \
    f(hgeie             , 0x607)

#define CSRS_Hypervisor_TrapHandling(f) \
    f(htval             , 0x643) \
    f(hip               , 0x644) \
    f(hvip              , 0x645) \
    f(htinst            , 0x64A) \
    f(hgeip             , 0xE12)

#define CSRS_Hypervisor_Configuration(f) \
    f(henvcfg           , 0x60A)

#define CSRS_Hypervisor_StateEnable(f) \
    f(hstateen0         , 0x60C) \
    f(hstateen1         , 0x60D) \
    f(hstateen2         , 0x60E) \
    f(hstateen3         , 0x60F)

#define CSRS_Hypervisor_Protection_Translation(f) \
    f(hgatp             , 0x680)

#define CSRS_Hypervisor_Counter_Timers(f) \
    f(htimedelta        , 0x605)

#define CSRS_Hypervisor_AIA(f) \
    f(hvien             , 0x608) \
    f(hvictl            , 0x609) \
    f(hviprio1          , 0x646) \
    f(hviprio2          , 0x647)

#define CSRS_Hypervisor_DebugTrace(f) \
    f(hcontext          , 0x6A8)

#define CSRS_Hypervisor(f) \
    CSRS_Hypervisor_TrapSetup(f) \
    CSRS_Hypervisor_TrapHandling(f) \
    CSRS_Hypervisor_Configuration(f) \
    CSRS_Hypervisor_Protection_Translation(f) \
    CSRS_Hypervisor_DebugTrace(f) \
    CSRS_Hypervisor_Counter_Timers(f) \
    CSRS_Hypervisor_StateEnable(f)



#define CSRS_VirtualSupervisor_TrapSetup(f) \
    f(vsstatus          , 0x200) \
    f(vsie              , 0x204) \
    f(vstvec            , 0x205)

#define CSRS_VirtualSupervisor_TrapHandling(f) \
    f(vsscratch         , 0x240) \
    f(vsepc             , 0x241) \
    f(vscause           , 0x242) \
    f(vstval            , 0x243) \
    f(vsip              , 0x244)

#define CSRS_VirtualSupervisor_Protection(f) \
    f(vsatp             , 0x280)

#define CSRS_VirtualSupervisor_Counter_Timers(f) \
    f(vstimecmp         , 0x24D)

#define CSRS_VirtualSupervisor_CTR(f) \
    f(vsctrctl          , 0x24E)

#define CSRS_VirtualSupervisor_Indirect(f) \
    f(vsiselect         , 0x250) \
    f(vsireg            , 0x251) \
    f(vsireg2           , 0x252) \
    f(vsireg3           , 0x253) \
    f(vsireg4           , 0x255) \
    f(vsireg5           , 0x256) \
    f(vsireg6           , 0x257)

#define CSRS_VirtualSupervisor_AIA(f) \
    f(vstopei           , 0x25C) \
    f(vstopi            , 0xEB0)

#define CSRS_VirtualSupervisor(f) \
    CSRS_VirtualSupervisor_TrapSetup(f) \
    CSRS_VirtualSupervisor_TrapHandling(f) \
    CSRS_VirtualSupervisor_Protection(f) \
    CSRS_VirtualSupervisor_Counter_Timers(f) \
    CSRS_VirtualSupervisor_CTR(f) \
    CSRS_VirtualSupervisor_Indirect(f) \
    CSRS_VirtualSupervisor_AIA(f)

// clang-format on