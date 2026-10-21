#ifndef RVISS_CPU_EXEC_H
#define RVISS_CPU_EXEC_H

#include <common.h>
#include <cpu/cpu.h>
#include <trap.h>

typedef enum {
  kRne = 0,
  kRtz = 1,
  kRdn = 2,
  kRup = 3,
  kRmm = 4,
  kReserved5 = 5,
  kReserved6 = 6,
  kDyn = 7
} FpRoundingMode;

typedef struct {
  uint8_t rd;
  uint8_t rs1;
  uint8_t rs2;
  word_t imm;
} IntOperands;

typedef struct {
  uint8_t rd;
  uint8_t rs1;
  uint8_t rs2;
  uint8_t rs3;
  word_t imm;
  FpRoundingMode rm;
} FpOperands;

typedef union {
  IntOperands integer;
  FpOperands floating;
} ExecOperands;

typedef struct {
  CpuState* hart;
  vaddr_t npc;
  uint32_t raw_instr;
  ExecOperands operands;
} ExecContext;

typedef struct {
  bool exception;
  word_t tval;
  Exception cause;
} ExecResult;

typedef ExecResult (*EHelper)(ExecContext*);

static inline void gpr_write(CpuState* hart, uint8_t rd, word_t value) {
  if (rd != 0) hart->gpr[rd] = value;
}

static inline word_t gpr_read(CpuState* hart, uint8_t rs) {
  return (rs == 0) ? 0 : hart->gpr[rs];
}

#define INT_RD(ctx) ctx->operands.integer.rd
#define INT_RS1(ctx) ctx->operands.integer.rs1
#define INT_RS2(ctx) ctx->operands.integer.rs2

#define INT_SRC1(ctx) gpr_read(ctx->hart, INT_RS1(ctx))
#define INT_SRC2(ctx) gpr_read(ctx->hart, INT_RS2(ctx))
#define INT_IMM(ctx) ctx->operands.integer.imm

#define PC(ctx) ctx->hart->pc
#define FLOAT_RD(ctx) ctx->operands.floating.rd
#define FLOAT_RS1(ctx) ctx->operands.floating.rs1
#define FLOAT_RS2(ctx) ctx->operands.floating.rs2
#define FLOAT_RS3(ctx) ctx->operands.floating.rs3

#define FLOAT_SRC1(ctx, read_fn) read_fn(ctx->hart, FLOAT_RS1(ctx))
#define FLOAT_SRC2(ctx, read_fn) read_fn(ctx->hart, FLOAT_RS2(ctx))
#define FLOAT_SRC3(ctx, read_fn) read_fn(ctx->hart, FLOAT_RS3(ctx))
#define FLOAT_IMM(ctx) ctx->operands.floating.imm

static inline ExecResult exec_ok(void) {
  return (ExecResult){
      .exception = false,
  };
}

static inline ExecResult exec_trap(Exception cause, word_t tval) {
  return (ExecResult){
      .exception = true,
      .tval = tval,
      .cause = cause,
  };
}

// 返回更新后的 status：xIE <- xPIE, xPIE <- 1
static inline uint64_t xret_restore_xie(uint64_t status, uint64_t xie_bit,
                                        uint64_t xpie_bit) {
  const bool pie = (status & xpie_bit) != 0;
  status = pie ? (status | xie_bit) : (status & ~xie_bit);
  return status | xpie_bit;
}

static inline void reservation_clear(CpuState* hart) {
  hart->reservation_valid = false;
}

#endif /* RVISS_CPU_EXEC_H */
