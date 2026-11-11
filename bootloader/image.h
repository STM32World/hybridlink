/*
 * Application image writer shared by the DFU and ST update protocols.
 * The first application page is kept in RAM after it was erased and only
 * written by image_commit(), so an interrupted update leaves no image that
 * looks startable.
 * SPDX-License-Identifier: MIT
 */
#ifndef IMAGE_H
#define IMAGE_H
#include <stdbool.h>
#include <stdint.h>

bool image_erase(uint32_t page_addr);
bool image_write(uint32_t addr, const uint8_t *data, uint32_t len);
bool image_commit(void);
bool image_valid(void);
uint32_t image_flash_end(void);
#endif
