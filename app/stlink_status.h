/*
 * ST-Link/V2 protocol constants (as used by all public host tools).
 * SPDX-License-Identifier: MIT
 */
#ifndef STLINK_STATUS_H
#define STLINK_STATUS_H

/* status codes */
#define STLINK_OK                   0x80
#define STLINK_FALSE                0x81
#define STLINK_CORE_RUNNING         0x80
#define STLINK_CORE_HALTED          0x81
#define STLINK_NO_DEVICE            0x05
#define STLINK_SWD_AP_WAIT          0x10
#define STLINK_SWD_AP_FAULT         0x11
#define STLINK_SWD_AP_ERROR         0x12
#define STLINK_SWD_AP_PARITY        0x13
#define STLINK_SWD_DP_WAIT          0x14
#define STLINK_SWD_DP_FAULT         0x15
#define STLINK_SWD_DP_ERROR         0x16
#define STLINK_SWD_DP_PARITY        0x17
#define STLINK_SWD_AP_WDATA_ERROR   0x18
#define STLINK_SWD_AP_STICKY_ERROR  0x19
#define STLINK_SWD_AP_STICKYORUN    0x1A
#define STLINK_BAD_AP               0x1D

/* top level commands */
#define STLINK_GET_VERSION          0xF1
#define STLINK_DEBUG_COMMAND        0xF2
#define STLINK_DFU_COMMAND          0xF3
#define STLINK_SWIM_COMMAND         0xF4
#define STLINK_GET_CURRENT_MODE     0xF5
#define STLINK_GET_TARGET_VOLTAGE   0xF7
#define STLINK_BOOTLOADER_COMMAND   0xF9

#define STLINK_DFU_EXIT             0x07

/* modes reported by GET_CURRENT_MODE */
#define STLINK_MODE_DFU             0x00
#define STLINK_MODE_MASS            0x01
#define STLINK_MODE_DEBUG           0x02

/* debug sub commands */
#define DBG_ENTER_JTAG_RESET        0x00
#define DBG_GETSTATUS               0x01
#define DBG_FORCEDEBUG              0x02
#define DBG_APIV1_RESETSYS          0x03
#define DBG_APIV1_READALLREGS       0x04
#define DBG_APIV1_READREG           0x05
#define DBG_APIV1_WRITEREG          0x06
#define DBG_READMEM_32BIT           0x07
#define DBG_WRITEMEM_32BIT          0x08
#define DBG_RUNCORE                 0x09
#define DBG_STEPCORE                0x0A
#define DBG_APIV1_SETFP             0x0B
#define DBG_READMEM_8BIT            0x0C
#define DBG_WRITEMEM_8BIT           0x0D
#define DBG_APIV1_CLEARFP           0x0E
#define DBG_APIV1_WRITEDEBUGREG     0x0F
#define DBG_APIV1_SETWATCHPOINT     0x10
#define DBG_APIV1_ENTER             0x20
#define DBG_EXIT                    0x21
#define DBG_READCOREID              0x22
#define DBG_APIV2_ENTER             0x30
#define DBG_APIV2_READ_IDCODES      0x31
#define DBG_APIV2_RESETSYS          0x32
#define DBG_APIV2_READREG           0x33
#define DBG_APIV2_WRITEREG          0x34
#define DBG_APIV2_WRITEDEBUGREG     0x35
#define DBG_APIV2_READDEBUGREG      0x36
#define DBG_APIV2_SETWATCHPOINT     0x37
#define DBG_APIV2_SETFP             0x38
#define DBG_APIV2_CLEARFP           0x39
#define DBG_APIV2_READALLREGS       0x3A
#define DBG_APIV2_GETLASTRWSTATUS   0x3B
#define DBG_APIV2_DRIVE_NRST        0x3C
#define DBG_APIV2_READALLREGS_FP    0x3D
#define DBG_APIV2_GETLASTRWSTATUS2  0x3E
#define DBG_APIV2_START_TRACE_RX    0x40
#define DBG_APIV2_STOP_TRACE_RX     0x41
#define DBG_APIV2_GET_TRACE_NB      0x42
#define DBG_APIV2_SWD_SET_FREQ      0x43
#define DBG_APIV2_JTAG_SET_FREQ     0x44
#define DBG_APIV2_READ_DAP_REG      0x45
#define DBG_APIV2_WRITE_DAP_REG     0x46
#define DBG_APIV2_READMEM_16BIT     0x47
#define DBG_APIV2_WRITEMEM_16BIT    0x48
#define DBG_APIV2_INIT_AP           0x4B
#define DBG_APIV2_CLOSE_AP_DBG      0x4C
#define DBG_WRITEMEM_32BIT_NOINC    0x50
#define DBG_READMEM_32BIT_NOINC     0x54
#define DBG_APIV2_BOARD_INFO        0x56
#define DBG_APIV2_GET_CAPS          0x57
#define DBG_APIV2_GET_TRACE_CAPS    0x58

#define DBG_ENTER_SWD               0xA3
#define DBG_ENTER_JTAG_NO_RESET     0xA4

#endif
