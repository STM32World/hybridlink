/*
 * ST-Link bootloader update protocol (interface 0), used by ST's
 * ST-LinkUpgrade / STM32CubeIDE to install ST firmware.
 * SPDX-License-Identifier: MIT
 */
#ifndef ST_PROTO_H
#define ST_PROTO_H
#include <stdbool.h>
void st_proto_task(void);
bool st_proto_exit_requested(void);
#endif
