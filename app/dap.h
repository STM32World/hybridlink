/*
 * CMSIS-DAP command processor (transport independent), SWD only.
 * Written from the published CMSIS-DAP protocol description.
 * SPDX-License-Identifier: MIT
 */
#ifndef DAP_H
#define DAP_H

#include <stdint.h>

#define DAP_PACKET_SIZE     64u
#define DAP_PACKET_COUNT    4u

void     dap_init(void);
/* executes one request packet, returns the response length */
uint32_t dap_execute(const uint8_t *req, uint32_t req_len, uint8_t *resp);

#endif
