#ifndef RVISS_DEVICE_INTC_PLIC_H
#define RVISS_DEVICE_INTC_PLIC_H

#include <device/device.h>

#define PLIC_NR_SOURCES 32 /* ID 0 保留；source 1..31 */
#define PLIC_MMIO_SIZE UINT64_C(0x4000000)
#define PLIC_NR_CONTEXTS 2

typedef struct Plic Plic;

Plic* plic_create(IrqLine* machine_external, IrqLine* supervisor_external);
void plic_release(Plic* plic);
void plic_reset(Plic* plic);
void plic_mmio(Plic* plic, MemObject* mmio);
void plic_update(Plic* plic);
void plic_set_source(Plic* plic, unsigned source, bool level);

#endif /* RVISS_DEVICE_INTC_PLIC_H */
