// 独立 Spike 进程；只能在初始化读共同输入，无 RVISS 核心依赖。
#include "processor.h"
#include "mmu.h"
#include "isa_parser.h"
#include "simif.h"
#include "case.h"
#include <iostream>
#include <map>

class RamBus : public simif_t {
 public:
  DiffCase &input;
  cfg_t config;
  std::map<size_t, processor_t*> harts;
  uint8_t partial[8] = {};
  unsigned partial_bytes=0;
  uint64_t fetched_raw=0;
  unsigned fetched_length=0;
  bool fetch_complete=false;
  void begin_event() {
    partial_bytes=0; fetched_raw=0; fetched_length=0; fetch_complete=false;
  }
  explicit RamBus(DiffCase &c) : input(c) {
    config.isa = c.isa; config.priv = "MSU"; config.pmpregions = 0;
    config.misaligned = false; config.trigger_count = 0;
    config.mem_layout.clear(); // 实际地址范围由下面 checked RAM callback 定义。
  }
  bool range(reg_t address, size_t length) {
    return length && address >= input.base && address - input.base < input.size &&
           length <= input.size - (address - input.base);
  }
  // 测试支持非整页的 RAM 末尾；不用 Spike 的整页直接映射假设。
  char *addr_to_mem(reg_t) override { return nullptr; }
  bool reservable(reg_t address) override { return range(address, 1); }
  bool mmio_load(reg_t address, size_t length, uint8_t *bytes) override {
    if (!range(address, length)) return false;
    memcpy(bytes, input.ram + address - input.base, length); return true;
  }
  bool mmio_fetch(reg_t address, size_t length, uint8_t *bytes) override {
    if (!mmio_load(address,length,bytes)) return false;
    if (length>sizeof(partial)-partial_bytes) case_error("fetch byte count");
    memcpy(partial+partial_bytes,bytes,length);
    partial_bytes+=length;
    return true;
  }
  void complete_fetch(size_t length) {
    if(length!=partial_bytes || length>8) case_error("fetch trace byte count");
    fetched_raw=0;
    for(unsigned i=0;i<length;i++) fetched_raw|=uint64_t(partial[i])<<(8*i);
    fetched_length=length; fetch_complete=true; partial_bytes=0;
  }
  void observe_fetch(DiffSnapshot &r) {
    if (fetch_complete) {
      r.raw=fetched_raw; r.length=fetched_length;
      r.raw_bytes=fetched_length; r.valid=true;
    } else if (partial_bytes) {
      for(unsigned i=0;i<partial_bytes;i++) r.raw|=uint64_t(partial[i])<<(8*i);
      r.raw_bytes=partial_bytes; r.length=insn_t(r.raw).length();
    }
  }
  bool mmio_store(reg_t address, size_t length, const uint8_t *bytes) override {
    if (!range(address, length)) return false;
    memcpy(input.ram + address - input.base, bytes, length); return true;
  }
  void proc_reset(unsigned) override {}
  const cfg_t &get_cfg() const override { return config; }
  const std::map<size_t, processor_t*> &get_harts() const override { return harts; }
  const char *get_symbol(uint64_t) override { return nullptr; }
};

// 只订阅参考实际完成的取指，不调用翻译/取指来获取观测字段。
class FetchObserver : public memtracer_t {
 public:
  RamBus &bus;
  explicit FetchObserver(RamBus &b):bus(b) {}
  bool interested_in_range(uint64_t,uint64_t,access_type type) override { return type==FETCH; }
  void trace(uint64_t,size_t length,access_type type) override {
    if(type==FETCH) bus.complete_fetch(length);
  }
  void clean_invalidate(uint64_t,size_t,bool,bool) override {}
};

static void snapshot(processor_t &p, DiffSnapshot &r) {
  auto *s = p.get_state();
  r.pc = s->pc; r.privilege = s->prv;
  for (unsigned i = 0; i < 32; i++) r.gpr[i] = s->XPR[i];
  for (unsigned i = 0; i < DIFF_CSR_COUNT; i++) r.csr[i] = p.get_csr(diff_csrs[i], insn_t(0), false, true);
}

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  DiffCase c = load_case(argv[1]);
  RamBus bus(c);
  isa_parser_t isa(c.isa, "MSU");
  FILE *commits = tmpfile();
  if (!commits) case_error("commit observation file");
  processor_t p(&isa, &bus.config, &bus, 0, false, commits, std::cerr);
  // 本阶段固定 Bare、ASIDLEN=0；在 CSR 配置前选择独立参考能力。
  p.set_mmu_capability(IMPL_MMU_SBARE);
  p.set_impl(IMPL_MMU_ASID, false);
  bus.harts[0] = &p;
  auto *s = p.get_state();
  FetchObserver fetch_observer(bus);
  p.get_mmu()->register_memtracer(&fetch_observer);
  s->pc = c.pc;
  for (unsigned i = 0; i < 32; i++) s->XPR.write(i, c.gpr[i]);
  for (unsigned i = 0; i < 4096; i++) if (c.csr_set[i]) {
    p.put_csr(i, c.csr[i]);
    // 该版本写 API 为即将发生的退休预减 1；初始化没有写指令，恢复共同输入。
    if (i==CSR_MINSTRET) s->minstret->bump(1);
    if (i==CSR_MCYCLE) s->mcycle->bump(1);
  }
  s->time->sync(c.time_value);
  p.set_privilege(c.priv, false);
  p.enable_log_commits();
  DiffSnapshot initial = {};
  initial.outcome = "initial"; initial.pc_before = s->pc; initial.privilege_before = s->prv;
  snapshot(p, initial); emit_snapshot(&initial, c.ram, c.size);
  uint64_t retired_total=0;
  for (uint64_t i = 0; i < c.steps; i++) {
    for(unsigned j=0;j<c.clock_count;j++)
      if(c.clock[j].event==i+1) s->time->sync(c.clock[j].value);
    for (unsigned j=0;j<c.irq_count;j++) if(c.irq[j].event==i+1) {
      reg_t mask=reg_t(1)<<c.irq[j].bit;
      s->mip->backdoor_write_with_mask(mask,c.irq[j].level?mask:0);
    }
    DiffSnapshot r = {};
    r.seq = i + 1; r.pc_before = s->pc; r.privilege_before = s->prv;
    bus.begin_event();
    s->log_mem_write.clear(); s->log_reg_write.clear(); s->log_mem_read.clear();
    fflush(commits);
    long before=ftell(commits);
    if (before<0) case_error("commit file position");
    p.step(1); // 该版本 trap 时返回，不继续执行 handler。
    if (fflush(commits)) case_error("commit observation flush");
    long after=ftell(commits);
    if (after<0) case_error("commit file position");
    bus.observe_fetch(r);
    r.retired = after>before;
    retired_total+=r.retired;
    r.retired_total=retired_total;
    r.outcome = r.retired ? "retired" : "synchronous_trap";
    if (!r.retired) {
      reg_t cause=p.get_csr(s->prv==PRV_M?CSR_MCAUSE:CSR_SCAUSE,insn_t(0),false,true);
      if (cause >> 63) {
        r.outcome="interrupt";
        r.raw=0; r.length=0; r.raw_bytes=0; r.valid=false;
      }
    }
    if (!s->log_mem_write.empty()) {
      if (s->log_mem_write.size() != 1) case_error("multiple stores not in contract");
      const auto &write = s->log_mem_write[0];
      r.store = true; r.store_addr = std::get<0>(write);
      r.store_data = std::get<1>(write); r.store_length = std::get<2>(write);
    }
    snapshot(p, r); emit_snapshot(&r, c.ram, c.size);
  }
  fclose(commits);
  free(c.ram);
  return 0;
}
