#include "../atomic.h"

ExecResult exec_amoadd_w(ExecContext* ctx) { return amo_operate(ctx, 4, AMO_ADD); }
ExecResult exec_amoadd_d(ExecContext* ctx) { return amo_operate(ctx, 8, AMO_ADD); }
ExecResult exec_amoswap_w(ExecContext* ctx) {
  return amo_operate(ctx, 4, AMO_SWAP);
}
ExecResult exec_amoswap_d(ExecContext* ctx) {
  return amo_operate(ctx, 8, AMO_SWAP);
}
ExecResult exec_amoxor_w(ExecContext* ctx) { return amo_operate(ctx, 4, AMO_XOR); }
ExecResult exec_amoxor_d(ExecContext* ctx) { return amo_operate(ctx, 8, AMO_XOR); }
ExecResult exec_amoor_w(ExecContext* ctx) { return amo_operate(ctx, 4, AMO_OR); }
ExecResult exec_amoor_d(ExecContext* ctx) { return amo_operate(ctx, 8, AMO_OR); }
ExecResult exec_amoand_w(ExecContext* ctx) { return amo_operate(ctx, 4, AMO_AND); }
ExecResult exec_amoand_d(ExecContext* ctx) { return amo_operate(ctx, 8, AMO_AND); }
ExecResult exec_amomin_w(ExecContext* ctx) { return amo_operate(ctx, 4, AMO_MIN); }
ExecResult exec_amomin_d(ExecContext* ctx) { return amo_operate(ctx, 8, AMO_MIN); }
ExecResult exec_amomax_w(ExecContext* ctx) { return amo_operate(ctx, 4, AMO_MAX); }
ExecResult exec_amomax_d(ExecContext* ctx) { return amo_operate(ctx, 8, AMO_MAX); }
ExecResult exec_amominu_w(ExecContext* ctx) {
  return amo_operate(ctx, 4, AMO_MINU);
}
ExecResult exec_amominu_d(ExecContext* ctx) {
  return amo_operate(ctx, 8, AMO_MINU);
}
ExecResult exec_amomaxu_w(ExecContext* ctx) {
  return amo_operate(ctx, 4, AMO_MAXU);
}
ExecResult exec_amomaxu_d(ExecContext* ctx) {
  return amo_operate(ctx, 8, AMO_MAXU);
}
