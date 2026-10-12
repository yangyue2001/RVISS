#include <memory/vaddr.h>

/* No translation cache: every access walks current PTEs. A full SFENCE.VMA
 * is therefore satisfied without stale entries, independent of rs1/rs2. */
void isa_mmu_flush_tlb(void) {}
