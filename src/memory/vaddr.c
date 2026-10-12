#include <common.h>
#include <memory/vaddr.h>

/*
 * 客户机访问统一交给 RV64 访问路径：有效特权、对齐、翻译和物理访问。
 * 此层不修改 CPU；失败由执行边界统一进入 trap。
 */

MemAccess vaddr_ifetch(vaddr_t addr, int len) {
  return isa_mem_access(addr, len, MEM_TYPE_IFETCH, 0);
}

MemAccess vaddr_read(vaddr_t addr, int len) {
  return isa_mem_access(addr, len, MEM_TYPE_READ, 0);
}

MemAccess vaddr_write(vaddr_t addr, int len, word_t data) {
  return isa_mem_access(addr, len, MEM_TYPE_WRITE, data);
}
