#ifndef RVISS_DEVICE_INTC_PLIC_H
#define RVISS_DEVICE_INTC_PLIC_H
#include <device/device.h>
#define PLIC_NR_SOURCES 32 /* ID0 reserved; sources1..31 */
#define PLIC_NR_CONTEXTS 2

typedef struct {
  MmioRegion mmio;
  IrqLine *meip_irq, *seip_irq;
  uint32_t priority[PLIC_NR_SOURCES];
  uint32_t enable_bits[PLIC_NR_CONTEXTS], threshold[PLIC_NR_CONTEXTS];
  uint32_t levels, pending, busy;
} PlicState;
void plic_init(PlicState *s);
void plic_reset(PlicState *s);
void plic_set_source(PlicState *s, unsigned source, bool level);
#endif
