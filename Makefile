# hybridlink - ST-Link/V2-1 compatible firmware (bootloader + application)
# SPDX-License-Identifier: MIT

BOARD   ?= jolt_connect
PORT    ?= stm32f1
CROSS   ?= arm-none-eabi-
LIBC_SPECS ?= /usr/arm-none-eabi/lib/picolibc.specs
LIBC_INC   ?= /usr/arm-none-eabi/include
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size
BUILD   ?= build

TUSB    := lib/tinyusb/src
PORTDIR := port/$(PORT)

include $(PORTDIR)/port.mk

CFLAGS  += --specs=$(LIBC_SPECS) -isystem $(LIBC_INC) $(PORT_CFLAGS) -Os -g3 -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
           -ffunction-sections -fdata-sections -fno-common \
           -Icommon -Iboards/$(BOARD) -I$(PORTDIR) -I$(PORTDIR)/include -I$(TUSB)
LDFLAGS += $(PORT_CFLAGS) -nostartfiles --specs=$(LIBC_SPECS) \
           -Wl,--gc-sections -L$(PORTDIR) $(if $(PORT_LIBDIR),-L$(PORT_LIBDIR))

TUSB_SRC := $(TUSB)/tusb.c $(TUSB)/common/tusb_fifo.c $(TUSB)/device/usbd.c \
            $(PORT_TUSB_SRC)
COMMON_SRC := common/usb_serial.c $(PORT_SRC)

ST_COMPAT ?= 1
# 1: DFU upload reads back any application, also ST's firmware installed
# through the ST protocol (for your own builds; the default only reads back
# hybridlink images)
DFU_UPLOAD_ANY ?= 0
BOOT_SRC := $(wildcard bootloader/*.c) $(COMMON_SRC) $(TUSB_SRC) $(TUSB)/class/dfu/dfu_device.c
APP_SRC  := $(wildcard app/*.c) $(COMMON_SRC) $(TUSB_SRC) $(TUSB)/class/cdc/cdc_device.c

BOOT_OBJ := $(patsubst %.c,$(BUILD)/bootloader/%.o,$(BOOT_SRC))
APP_OBJ  := $(patsubst %.c,$(BUILD)/app/%.o,$(APP_SRC))

all: $(BUILD)/bootloader.bin $(BUILD)/app.bin $(BUILD)/hybridlink-full.bin \
     $(BUILD)/hybridlink-daplink.bin \
     $(if $(PORT_INSTALLER_SRC),$(BUILD)/installer.bin)

$(BUILD)/bootloader/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Ibootloader -DST_COMPAT=$(ST_COMPAT) -DDFU_UPLOAD_ANY=$(DFU_UPLOAD_ANY) -MMD -c $< -o $@

$(BUILD)/app/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iapp -MMD -c $< -o $@

# bootloader installer: an application that embeds bootloader.bin
INST_OBJ := $(patsubst %,$(BUILD)/installer/%.o,$(basename $(PORT_INSTALLER_SRC)))

$(BUILD)/installer/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BUILD)/installer/%.o: %.S $(BUILD)/bootloader.bin
	@mkdir -p $(dir $@)
	$(CC) $(PORT_CFLAGS) -DBOOT_IMAGE='"$(BUILD)/bootloader.bin"' -c $< -o $@

$(BUILD)/installer.elf: $(INST_OBJ)
	$(CC) $(LDFLAGS) -T$(PORT_APP_LD) -Wl,-Map=$(BUILD)/installer.map $^ -o $@
	$(SIZE) $@

$(BUILD)/bootloader.elf: $(BOOT_OBJ)
	$(CC) $(LDFLAGS) -T$(PORT_BOOT_LD) -Wl,-Map=$(BUILD)/bootloader.map $^ -o $@
	$(SIZE) $@

$(BUILD)/app.elf: $(APP_OBJ)
	$(CC) $(LDFLAGS) -T$(PORT_APP_LD) -Wl,-Map=$(BUILD)/app.map $^ -o $@
	$(SIZE) $@

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

# bootloader padded to 16 KiB followed by the application, for SWD flashing
$(BUILD)/hybridlink-full.bin: $(BUILD)/bootloader.bin $(BUILD)/app.bin
	python3 tools/mkimage.py $^ $@

# the same, as a bootloader update for probes running DAPLink (drag and drop)
$(BUILD)/hybridlink-daplink.bin: $(BUILD)/hybridlink-full.bin
	python3 tools/mkdaplink.py $< $@

clean:
	rm -rf $(BUILD)

# the host tool and the images it installs: make install [PREFIX=~/.local]
PREFIX  ?= /usr/local
IMAGES  := app.bin bootloader.bin hybridlink-full.bin hybridlink-daplink.bin \
           $(if $(PORT_INSTALLER_SRC),installer.bin)

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/hybridlink
	install -m 755 tools/hybridlink $(DESTDIR)$(PREFIX)/bin/hybridlink
	install -m 644 $(addprefix $(BUILD)/,$(IMAGES)) $(DESTDIR)$(PREFIX)/share/hybridlink

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/hybridlink
	rm -rf $(DESTDIR)$(PREFIX)/share/hybridlink

-include $(BOOT_OBJ:.o=.d) $(APP_OBJ:.o=.d) $(INST_OBJ:.o=.d)

.PHONY: all clean install uninstall
