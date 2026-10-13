// frontend.h - what platform.cpp asks of cinterface.cpp.
// SPDX-License-Identifier: MIT
#ifndef CHIMERA_MELONDS_FRONTEND_H
#define CHIMERA_MELONDS_FRONTEND_H

#include <cstdint>

// the machine stopped itself (a power-off, or a fault it cannot go on from)
void frontend_stopped(int reason);
// the microphone's samples for the machine: fills up to "max" and returns how many
int frontend_mic(int16_t *data, int max);

#endif
