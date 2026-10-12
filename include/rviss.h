#ifndef RVISS_H
#define RVISS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RVISS_RAM_BASE_DEFAULT UINT64_C(0x80000000)
#define RVISS_RAM_SIZE_DEFAULT (128u * 1024u * 1024u)
#define RVISS_RESET_PC_DEFAULT RVISS_RAM_BASE_DEFAULT

typedef enum {
  RVISS_PRIV_U = 0,
  RVISS_PRIV_S = 1,
  RVISS_PRIV_M = 3,
} rviss_priv_t;

typedef enum {
  RVISS_STOP_NONE = 0,
  RVISS_STOP_HALT,
  RVISS_STOP_LIMIT,
  RVISS_STOP_HOST_ERROR,
} rviss_stop_reason_t;

typedef struct {
  uint64_t ram_base;
  size_t ram_size;
  uint64_t reset_pc;
  uint64_t hartid;
  bool deterministic_time;
  bool uart_output;
} rviss_config_t;

typedef struct rviss rviss_t;

rviss_config_t rviss_default_config(void);
rviss_t *rviss_create(const rviss_config_t *config);
void rviss_destroy(rviss_t *sim);
void rviss_reset(rviss_t *sim);

bool rviss_load_image(rviss_t *sim, const char *path, uint64_t load_addr,
                      bool try_elf);
bool rviss_memcpy(rviss_t *sim, uint64_t paddr, void *buffer, size_t size,
                  bool to_sim);

bool rviss_step(rviss_t *sim);
uint64_t rviss_run(rviss_t *sim, uint64_t instructions);
void rviss_raise_interrupt(rviss_t *sim, uint64_t cause);

bool rviss_checkpoint_save(rviss_t *sim, const char *path);
bool rviss_checkpoint_load(rviss_t *sim, const char *path);

uint64_t rviss_get_gpr(const rviss_t *sim, unsigned index);
void rviss_set_gpr(rviss_t *sim, unsigned index, uint64_t value);
uint64_t rviss_get_pc(const rviss_t *sim);
void rviss_set_pc(rviss_t *sim, uint64_t value);
rviss_priv_t rviss_get_priv(const rviss_t *sim);
uint64_t rviss_get_csr(const rviss_t *sim, unsigned csr);
void rviss_set_csr(rviss_t *sim, unsigned csr, uint64_t value);
uint64_t rviss_get_instret(const rviss_t *sim);
rviss_stop_reason_t rviss_get_stop_reason(const rviss_t *sim);
int rviss_get_halt_code(const rviss_t *sim);
const char *rviss_get_error(const rviss_t *sim);

/* NEMU RV64 full-diff register layout (with FPR slots for ABI compatibility). */
typedef struct {
  uint64_t gpr[32];
  uint64_t fpr[32];
  uint64_t mode;
  uint64_t mstatus, sstatus;
  uint64_t mepc, sepc;
  uint64_t mtval, stval;
  uint64_t mtvec, stvec;
  uint64_t mcause, scause;
  uint64_t satp;
  uint64_t mip, mie;
  uint64_t mscratch, sscratch;
  uint64_t mideleg, medeleg;
  uint64_t pc;
  uint64_t fcsr;
} rviss_difftest_regs_t;

enum { RVISS_DIFFTEST_TO_DUT = 0, RVISS_DIFFTEST_TO_REF = 1 };

/* Symbols exported by librviss_ref.so. */
void difftest_init(void);
void difftest_set_ramsize(size_t ram_size);
void difftest_set_mhartid(int hartid);
void difftest_memcpy(uint64_t addr, void *buf, size_t n, bool direction);
void difftest_memcpy_init(uint64_t addr, void *buf, size_t n, bool direction);
void difftest_regcpy(void *regs, bool direction, ...);
void difftest_csrcpy(void *csrs, bool direction);
void difftest_exec(uint64_t n); /* 指令尝试预算，含同步异常，不含独立中断 */
void difftest_raise_intr(uint64_t cause);
void difftest_uarchstatus_sync(void *state);
void difftest_uarchstatus_cpy(void *state, bool direction);
void difftest_display(void);
void isa_reg_display(void);
void update_dynamic_config(void *config);
int difftest_store_commit(uint64_t *addr, uint64_t *data, uint8_t *mask);
void difftest_guided_exec(void *guide);
int difftest_status(void);

#ifdef __cplusplus
}
#endif

#endif
