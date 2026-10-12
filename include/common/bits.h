#ifndef RVISS_COMMON_BITS_H
#define RVISS_COMMON_BITS_H

#include <common/types.h>

/* 位域提取：BITS(x, 31, 12) 取 x[31:12]（含两端） */
#define BITS(x, hi, lo) (((x) >> (lo)) & ((~0ull) >> (63 - ((hi) - (lo)))))
#define BIT(x, n) (((x) >> (n)) & 1ull)

/* 符号扩展：把 x 的低 len 位当有符号数扩展满 64 位 */
#define SEXT(x, len)                                       \
  ({                                                       \
    sword_t _v = (sword_t)((uint64_t)(x) << (64 - (len))); \
    _v >> (64 - (len));                                    \
  })

#endif /* RVISS_COMMON_BITS_H */
