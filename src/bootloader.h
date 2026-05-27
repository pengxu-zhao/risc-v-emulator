#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include "common.h"

int load_bin(const char *filename, uint64_t load_addr);

#endif