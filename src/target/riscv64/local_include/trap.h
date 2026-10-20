#ifndef RVISS_RISCV64_INTR_H
#define RVISS_RISCV64_INTR_H

#include <common.h>
#include <cpu/interrupt.h> /* 异常/中断编号是公共约定，见该头 */

/* target 私有的陷阱接口 */

typedef enum {
  EXCP_INST_ADDR_MIS = 0x00,
  EXCP_INST_ACCESS_FAULT = 0x01,
  EXCP_ILLEGAL_INST = 0x02,
  EXCP_BREAKPOINT = 0x03,
  EXCP_LOAD_ADDR_MIS = 0x04,
  EXCP_LOAD_ACCESS_FAULT = 0x05,
  EXCP_STORE_AMO_ADDR_MIS = 0x06,
  EXCP_STORE_AMO_ACCESS_FAULT = 0x07,
  EXCP_U_ECALL = 0x08,
  EXCP_S_ECALL = 0x09,
  EXCP_VS_ECALL = 0x0a, /* H */
  EXCP_M_ECALL = 0x0b,
  EXCP_INST_PAGE_FAULT = 0x0c,
  EXCP_LOAD_PAGE_FAULT = 0x0d,
  EXCP_STORE_AMO_PAGE_FAULT = 0x0f,
  EXCP_DOUBLE_TRAP = 0x10, /* Ssdbltrp */
  EXCP_SW_CHECK = 0x12,    /* Zicfilp/Zicfiss */
  EXCP_HW_ERR = 0x13,
  EXCP_INST_GUEST_PAGE_FAULT = 0x14,            /* H */
  EXCP_LOAD_GUEST_PAGE_FAULT = 0x15,            /* H */
  EXCP_VIRT_INSTRUCTION_FAULT = 0x16,           /* H */
  EXCP_STORE_STORE_AMO_GUEST_PAGE_FAULT = 0x17, /* H */
} Exception;

typedef enum {
  INT_S_SOFTWARE = 0x01,
  INT_VS_SOFTWARE = 0x02, /* H */
  INT_M_SOFTWARE = 0x03,
  INT_S_TIMER = 0x05,
  INT_VS_TIMER = 0x06, /* H */
  INT_M_TIMER = 0x07,
  INT_RESERVED_8 = 0x08,
  INT_S_EXTERNAL = 0x09,
  INT_VS_EXTERNAL = 0x0a, /* H */
  INT_M_EXTERNAL = 0x0b,
  INT_S_GUEST_EXTERNAL = 0x0c, /* H */
  INT_COUNTER_OVERFLOW = 0x0d, /* Sscofpmf; AIA local */
  INT_RESERVED_14 = 0x0e,
  INT_RESERVED_15 = 0x0f,

  /* AIA standard-local allocation; individual future extensions define names.
   */
  INT_STANDARD_LOCAL_16_FIRST = 0x10,
  INT_STANDARD_LOCAL_23_LAST = 0x17,
  INT_CUSTOM_24_FIRST = 0x18,
  INT_CUSTOM_31_LAST = 0x1f,
  INT_STANDARD_LOCAL_32_FIRST = 0x20,
  INT_AIA_LOW_PRIORITY_RAS = 0x23,  /* 35 */
  INT_AIA_HIGH_PRIORITY_RAS = 0x2b, /* 43 */
  INT_STANDARD_LOCAL_47_LAST = 0x2f,
  INT_CUSTOM_48_FIRST = 0x30,

  /* Short spellings matching the SSI/MSI/STI/SEI terminology in the specs. */
  INT_SSI = 0x01,   // Supervisor software interrupt
  INT_VSSI = 0x02,  // Virtual supervisor software interrupt(H Extension)
  INT_MSI = 0x03,   // Machine software interrupt
  INT_STI = 0x05,   // Supervisor timer interrupt
  INT_VSTI = 0x06,  // Virtual supervisor timer interrupt(H Extension)
  INT_MTI = 0x07,   // Machine timer interrupt
  INT_SEI = 0x09,   // Supervisor external interrupt
  INT_VSEI = 0x0a,  // Virtual supervisor external interrupt(H Extension)
  INT_MEI = 0x0b,   // Machine external interrupt
  INT_SGEI = 0x0c,  // Supervisor guest external interrupt
  INT_COFI = 0x0d,  //
} Interrupt;

/* trap/trap.c：统一陷阱入口，返回陷阱处理例程地址 */
vaddr_t riscv_raise_trap(word_t cause, vaddr_t epc, vaddr_t tval);

/* trap/intr.c：查询当前可响应的最高优先级中断；无则返回 0 */
word_t riscv_query_intr(void);

/* trap/intr.c：清空设备中断线的电平快照（cpu_reset 时调用） */
void riscv_intr_reset(void);

/* 特权返回指令 */
vaddr_t riscv_mret(void);
vaddr_t riscv_sret(void);

#endif /* RVISS_RISCV64_INTR_H */
