#ifndef RVISS_CHECKPOINT_CHECKPOINT_H
#define RVISS_CHECKPOINT_CHECKPOINT_H

#include <common.h>

/*
 * Checkpoint v2 serializes CPU state and the complete RAM image. Its checksum
 * covers the fixed header, CPU_state and RAM. Devices, logical time and event
 * sequence are deliberately outside this interface until a state contract is
 * defined.
 */

bool cpt_save(const char *path);
bool cpt_load(const char *path);

#endif /* RVISS_CHECKPOINT_CHECKPOINT_H */
