#include <common.h>
#include <cpu/cpu.h>
#include <memory/mmu.h>
#include <memory/paddr.h>
#include <memory/vaddr.h>

/*
 * 客户机虚地址入口。
 *
 * 1. 先看自然对齐。取指一律不允许非对齐；load/store 由 Kconfig 决定。
 * 2. 允许的非对齐访问在访问宽度的对齐边界拆成正好两段，每段调用 vaddr_unit。
 * 3. vaddr_unit 决定要不要翻译、检查规范虚拟地址、按 satp.MODE 走页表，
 *    然后用翻译得到的物理地址做 paddr_access。
 *
 * 两段的长度不一定还是 1/2/4/8。例如 8 字节访问落在 +1 时，两段是 7 字节和 1
 * 字节。 对齐边界同时也是这两段之间唯一可能的页边界。
 *
 * RVISS_TEST_MISALIGNED_NATIVE 只给切片测试覆盖另一侧配置，不是客户机开关。
 */

#ifdef RVISS_TEST_MISALIGNED_NATIVE
#undef CONFIG_RV_MISALIGNED_TRAP
#define CONFIG_RV_MISALIGNED_NATIVE 1
#endif

enum { BITS_PER_BYTE = 8 };

static inline bool satp_mode_is_bare(reg_t satp_mode) {
  return satp_mode == SATP64_MODE_BARE;
}

static inline bool satp_mode_is_sv39(reg_t satp_mode) {
  return satp_mode == SATP64_MODE_SV39;
}

static inline bool satp_mode_is_sv48(reg_t satp_mode) {
  return satp_mode == SATP64_MODE_SV48;
}

static inline bool satp_mode_is_sv57(reg_t satp_mode) {
  return satp_mode == SATP64_MODE_SV57;
}

static inline bool need_translate(Priv privilege, reg_t satp_mode) {
  return privilege != PRV_M && !satp_mode_is_bare(satp_mode);
}

static inline bool vaddr_is_canonical(vaddr_t vaddr, reg_t satp_mode) {
  return (satp_mode_is_sv39(satp_mode) &&
          vaddr == (vaddr_t)SEXT(vaddr, RISCV_SV39_VA_BITS)) ||
         (satp_mode_is_sv48(satp_mode) &&
          vaddr == (vaddr_t)SEXT(vaddr, RISCV_SV48_VA_BITS)) ||
         (satp_mode_is_sv57(satp_mode) &&
          vaddr == (vaddr_t)SEXT(vaddr, RISCV_SV57_VA_BITS));
}

static bool load_store_misaligned_native(void) {
#ifdef CONFIG_RV_MISALIGNED_NATIVE
  return true;
#else
  return false;
#endif
}

static bool guest_len_ok(word_t len) {
  return len == 1 || len == 2 || len == 4 || len == 8;
}

static MemResult resolve_paddr(MemRequest* req) {
  reg_t mode = cpu.satp & SATP64_MODE;
  if (!need_translate(req->privilege, mode)) return mem_result_data(req->addr);
  if (!vaddr_is_canonical(req->addr, mode)) {
    return mem_result_fault(mem_page_fault(req->type), req->addr);
  }
  MmuRequest translate = {.vaddr = req->addr,
                          .type = req->type,
                          .privilege = req->privilege,
                          .mxr = (cpu.mstatus & MSTATUS_MXR) != 0,
                          .sum = (cpu.mstatus & MSTATUS_SUM) != 0};
  return riscv_mmu_translate(&translate);
}

/* paddr_access 的 tval 是物理地址。客户机异常仍报告原来的虚拟地址。 */
static MemResult vaddr_unit(MemRequest* req) {
  MemResult resolved = resolve_paddr(req);
  if (resolved.exception) return resolved;
  MemRequest physical = *req;
  physical.addr = resolved.data;
  MemResult result = paddr_access(&physical);
  if (result.exception) result.tval = req->addr;
  return result;
}

static void split_misaligned(vaddr_t addr, word_t len, vaddr_t* low_addr,
                             word_t* low_len, vaddr_t* high_addr,
                             word_t* high_len) {
  word_t mask = len - 1;
  vaddr_t boundary = (addr + mask) & ~mask;
  *low_addr = addr;
  *low_len = boundary - addr;
  *high_addr = boundary;
  *high_len = len - *low_len;
  Assert(*low_len > 0 && *high_len > 0 && *low_len < len, "非对齐切分失败");
}

static MemResult access_misaligned(MemRequest* req) {
  vaddr_t low_addr = 0, high_addr = 0;
  word_t low_len = 0, high_len = 0;
  split_misaligned(req->addr, req->len, &low_addr, &low_len, &high_addr,
                   &high_len);

  MemRequest low = {.addr = low_addr,
                    .len = low_len,
                    .type = req->type,
                    .data = 0,
                    .privilege = req->privilege};
  MemRequest high = {.addr = high_addr,
                     .len = high_len,
                     .type = req->type,
                     .data = 0,
                     .privilege = req->privilege};
  if (mem_access_is_write(req->type)) {
    low.data = req->data;
    high.data = req->data >> (low_len * BITS_PER_BYTE);
  }

  MemResult low_result = vaddr_unit(&low);
  if (low_result.exception) return low_result;
  MemResult high_result = vaddr_unit(&high);
  if (high_result.exception) return high_result;
  if (mem_access_is_write(req->type)) return mem_result_ok();
  return mem_result_data(low_result.data |
                         (high_result.data << (low_len * BITS_PER_BYTE)));
}

MemResult vaddr_access(MemRequest* req) {
  Assert(req != NULL, "vaddr_access 缺少请求");
  Assert(guest_len_ok(req->len), "vaddr_access 长度必须是 1、2、4 或 8");

  bool misaligned = (req->addr & (req->len - 1)) != 0;
  if (misaligned &&
      (req->type == kMemIFetch || !load_store_misaligned_native())) {
    return mem_result_fault(mem_misaligned_fault(req->type), req->addr);
  }
  if (!misaligned) return vaddr_unit(req);
  return access_misaligned(req);
}
