# SPDX-License-Identifier: MIT
PORT_CFLAGS   := -mcpu=cortex-m3 -mthumb -DCFG_TUSB_MCU=OPT_MCU_STM32F1 -DSTM32F103xB
PORT_SRC      := port/stm32f1/startup.c port/stm32f1/hal.c
PORT_TUSB_SRC := $(TUSB)/portable/st/stm32_fsdev/dcd_stm32_fsdev.c \
                 $(TUSB)/portable/st/stm32_fsdev/fsdev_common.c
PORT_BOOT_LD  := bootloader.ld
PORT_APP_LD   := app.ld
PORT_LIBDIR   := /usr/arm-none-eabi/lib/thumb/v7-m/nofp
# bootloader installer, uploaded through the probe's old bootloader
PORT_INSTALLER_SRC := port/stm32f1/installer.c port/stm32f1/installer_image.S
