#include "../atomic.h"

ExecResult exec_lr_w(ExecContext* ctx) { return amo_lr(ctx, 4); }
ExecResult exec_lr_d(ExecContext* ctx) { return amo_lr(ctx, 8); }
ExecResult exec_sc_w(ExecContext* ctx) { return amo_sc(ctx, 4); }
ExecResult exec_sc_d(ExecContext* ctx) { return amo_sc(ctx, 8); }
