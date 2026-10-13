// frontend.h - what platform.cpp asks of cinterface.cpp.
// SPDX-License-Identifier: MIT
#ifndef CHIMERA_MELONDS_FRONTEND_H
#define CHIMERA_MELONDS_FRONTEND_H

#include <cstdint>

// the machine stopped itself (a power-off, or a fault it cannot go on from)
void frontend_stopped(int reason);
// the microphone's samples for the machine: fills up to "max" and returns how many
int frontend_mic(int16_t *data, int max);

// A file the machine reads AND writes while it runs (the DSi's NAND) is kept
// in the machine's memory, where a savestate holds it: true, with where it
// is and how long, when "name" is such a file.
bool frontend_memory_file(const char *name, uint8_t **data, uint64_t *size);

#endif
