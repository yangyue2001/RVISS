#include <common.h>
#include <cpu/cpu.h>
#include <cpu/csr_list.h>

#include "csr.h" /* target 私有：csr_read / csr_write */
#include "test.h"

/*
 * CSR 访问语义单测。
 *
 * 重点验证"从地址推导特权级/只读"这条设计（它取代了以前手工填的
 * min_priv / writable 字段）：
 *   - M 模式 CSR 在 U/S 模式必须非法；S 模式 CSR 在 U 模式非法；M 模式通吃
 *   - 只读 CSR（0xf1x 段）写必须非法（由 csr[11:10]==3 自动判定，不再靠字段）
 *   - 视图语义：写 sstatus 不能覆盖 mstatus 的 M 侧位；sie 与 mip 互不影响
 */

int main(void) {
  cpu_reset(0x80000000ull);
  word_t v = 0;

  /* ---- 特权级：由 csr[9:8] 推导 ---- */
  cpu.priv = PRV_U;
  CHECK(!csr_read(CSR_MSTATUS, &v));  /* M CSR：U 模式不可访问 */
  CHECK(!csr_write(CSR_MSTATUS, 0));
  CHECK(!csr_read(CSR_SSTATUS, &v));  /* S CSR：U 模式也不可访问 */

  cpu.priv = PRV_S;
  CHECK(csr_read(CSR_SSTATUS, &v));   /* S 模式可访问 S CSR */
  CHECK(!csr_read(CSR_MSTATUS, &v));  /* 但访问不了 M CSR */

  cpu.priv = PRV_M;
  CHECK(csr_read(CSR_MSTATUS, &v));   /* M 模式通吃 */
  CHECK(csr_read(CSR_SSTATUS, &v));

  /* ---- 只读：0xf1x 段由地址位自动判定 ---- */
  CHECK(csr_read(CSR_MVENDORID, &v));
  CHECK(!csr_write(CSR_MVENDORID, 0x1234));
  CHECK(csr_read(CSR_MHARTID, &v));
  CHECK(!csr_write(CSR_MHARTID, 1));

  /* ---- 新加的 CSR 自动获得派生属性 ----
   * mconfigptr 是在 csr_list.h 里"加两行"就有的：M 模式可读，
   * 且因为落在 0xf1x 段被自动判为只读（无需任何额外声明）。*/
  CHECK(csr_read(CSR_MCONFIGPTR, &v));
  CHECK(!csr_write(CSR_MCONFIGPTR, 0x1000));

  /* ---- 未实现的 CSR → 非法（不静默返回 0） ---- */
  CHECK(!csr_read(0x123, &v));
  CHECK(!csr_write(0x123, 0));

  /* ---- 视图：写 sstatus 只动 S 侧位，M 侧独有位必须保留 ---- */
  cpu.mstatus = MSTATUS_MIE | MSTATUS_MPIE;
  CHECK(csr_write(CSR_SSTATUS, ~0ull)); /* 全 1 写 sstatus */
  CHECK(cpu.mstatus & MSTATUS_MIE);     /* M 侧位被保留 */
  CHECK(cpu.mstatus & MSTATUS_MPIE);
  CHECK(cpu.mstatus & MSTATUS_SIE);     /* S 侧位被写入 */
  CHECK(cpu.mstatus & MSTATUS_SUM);
  CHECK(csr_read(CSR_SSTATUS, &v));     /* 读回只看到 S 侧位 */
  CHECK(!(v & MSTATUS_MIE));

  /* ---- 视图：sie 是 mie 的视图，与 mip 是不同存储 ---- */
  cpu.mie = 0;
  cpu.mip = 1ull << 7; /* 假装 MTIP 挂起 */
  CHECK(csr_write(CSR_SIE, ~0ull));
  CHECK_EQ(cpu.mip, 1ull << 7);         /* 写 sie 不碰 mip */
  CHECK(csr_read(CSR_SIE, &v));
  CHECK(v & (1ull << 1));               /* SSIP 在 SIE 视图内 */
  CHECK(!(v & (1ull << 7)));            /* MTIP 不在 SIE 视图内 */

  /* ---- 写 satp 的副作用钩子仍应生效（M3 前是空实现，只验证不崩） ---- */
  cpu.priv = PRV_M;
  CHECK(csr_write(CSR_SATP, 0));
  CHECK(csr_read(CSR_SATP, &v));

  TEST_MAIN_END();
}
