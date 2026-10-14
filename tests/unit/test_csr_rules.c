#include <common.h>

#include "csr_rules.h"
#include "test.h"

static bool legal_two_values(word_t value) { return value == 0 || value == 4; }

static word_t warl_keep_old(word_t requested, word_t old_value) {
  (void)requested;
  return old_value;
}

static const CsrFieldRule rules[] = {
    {UINT64_C(1) << 0, CSR_FIELD_RW, NULL, NULL},
    {UINT64_C(1) << 1, CSR_FIELD_WPRI, NULL, NULL},
    {UINT64_C(3) << 2, CSR_FIELD_WARL, legal_two_values, warl_keep_old},
    {UINT64_C(3) << 4, CSR_FIELD_WLRL, legal_two_values, NULL},
};

int main(void) {
  word_t old = (UINT64_C(1) << 1) | 4 | (UINT64_C(1) << 4);
  CHECK_EQ(csr_rules_read(rules, ARRLEN(rules), old | UINT64_C(0xfc)),
           UINT64_C(0x3c));

  word_t requested = UINT64_C(0xff);
  word_t value = csr_rules_write(rules, ARRLEN(rules), old, requested);
  /* RW changes, WPRI preserves, invalid WARL/WLRL values preserve old. */
  CHECK_EQ(value, old | 1);

  requested = 1 | 4 | (UINT64_C(1) << 4);
  value = csr_rules_write(rules, ARRLEN(rules), old, requested);
  CHECK_EQ(value, UINT64_C(0x17));
  TEST_MAIN_END();
}
