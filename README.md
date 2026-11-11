# hybridlink

Open-source multi-mode debug probe firmware, written from scratch on top of
[TinyUSB](lib/tinyusb). One firmware, several probe personalities selected
at run time:

* **ST-Link mode**: ST-Link/V2-1 compatible, recognised by STM32CubeIDE,
  STM32CubeProgrammer, OpenOCD and the stlink tools
* **CMSIS-DAP mode**: DAPLink compatible CMSIS-DAP v2, for targets ST's
  tools do not cover, such as the RP2350

See [Probe modes](#probe-modes). hybridlink is the default firmware of the
[Jolt Connect](https://stm32world.com/wiki/Jolt_Connect) and the Streamline
Connect, whose hardware matches the ST-Link found on early Nucleo boards
(STM32F103CB, 8 MHz crystal). Everything outside `port/` and `boards/` is
portable, so other probe MCUs can follow.

No ST code is used: no HAL, no CMSIS device headers, no ST libraries.
The STM32F1 port is written at register level from the reference manual.

The firmware has two parts:

| Part | Address | Purpose |
|------|---------|---------|
| bootloader | `0x08000000`, 15 KiB | updater: standard USB DFU and ST's update protocol |
| configuration page | `0x08003C00`, 1 KiB | written by ST's updater, used by ST firmware only |
| application | `0x08004000`, up to end of flash | ST-Link/V2-1 probe: SWD, virtual COM port, SWO |

## What the host sees

* USB `0483:3752` "hybridlink ST-Link compatible" (manufacturer
  "STM32World"; ST tools go by VID/PID, not by these strings), the ST-Link/V2-1
  layout without mass storage:
  * IF0 vendor debug interface: EP1 OUT/IN (64), EP2 IN trace (32)
  * IF1/IF2 CDC ACM virtual COM port (IAD): EP4 notify, EP5 data
* Firmware version `V2J48M35` by default (configurable, see below), serial = the 96-bit UID as 24 hex digits,
  just like a genuine V2-1.
* Recognised as an ST-Link by STM32CubeIDE (ST-LINK_gdbserver),
  STM32CubeProgrammer, OpenOCD and the stlink tools.

The bootloader enumerates like the genuine ST-Link bootloader, `0483:3748`,
with two interfaces:

* interface 0: ST's update protocol, so ST-LinkUpgrade (also used by
  STM32CubeIDE and STM32CubeProgrammer) can install ST firmware
* interface 1: standard USB DFU 1.1, for hybridlink images with dfu-util

## Probe modes

| mode | USB | for |
|------|-----|-----|
| ST-Link (default) | `0483:3752`, ST-Link/V2-1 | STM32CubeIDE, STM32CubeProgrammer, OpenOCD, stlink |
| CMSIS-DAP | `0D28:0204`, "hybridlink CMSIS-DAP" | OpenOCD, pyOCD, probe-rs, Pico SDK tools |

```bash
hybridlink set-type dap --serial <serial>
```

```bash
hybridlink set-type stlink --serial <serial>
```

The mode is stored in the configuration page and the probe restarts in it.
Both modes keep the virtual COM port, the serial number, the bootloader and
`hybridlink update`.

CMSIS-DAP mode implements CMSIS-DAP v2 (bulk endpoints, WinUSB through
Microsoft OS 2.0 descriptors, so no driver on Windows) for SWD: info,
connect, transfer and transfer block with posted reads and value matching,
SWJ pins / clock / sequence, SWD sequence (dormant wake-up, multidrop
TARGETSEL), SWO UART through `DAP_SWO_Data`, and execute/queue commands.
JTAG is not supported (the Jolt Connect only routes SWD); there is no HID
(CMSIS-DAP v1) interface, the F103 USB packet memory does not fit it next to
the VCP. The command processor is written from the published protocol
([app/dap.c](app/dap.c)), not taken from Arm's reference code.

`0D28:0204` is the DAPLink identity, kept so existing udev rules and tool
setups apply; tools find CMSIS-DAP probes by the product string, so any
VID/PID works (`DAP_USB_VID` / `DAP_USB_PID` at build time).

## Downloads

No toolchain needed: each [release](https://github.com/STM32World/hybridlink/releases)
has

* `hybridlink-<version>.tar.gz` / `.zip`: the `hybridlink` tool (`bin/`) and
  all firmware images (`share/hybridlink/`). Unpack it and run
  `bin/hybridlink install` (on Windows `python bin\hybridlink install`). It
  needs Python 3 with `pyusb` and `cryptography`, and `dfu-util`.
* `hybridlink-daplink.bin`: for probes running DAPLink, drag and drop onto
  the DAPLINK drive (see [Installing](#installing)).
* `hybridlink-full.bin`: bootloader and application, for flashing over SWD.

Releases are built by [.github/workflows/release.yml](.github/workflows/release.yml)
when a tag `v<version>` matching `HYBRIDLINK_VERSION` in
[common/usb_ids.h](common/usb_ids.h) is pushed; `make dist` builds the same
archives locally.

## Building

Needs `arm-none-eabi-gcc` and a C library (picolibc is used by default;
override `LIBC_SPECS`, `LIBC_INC` and `PORT_LIBDIR` for another one).

```bash
git submodule update --init lib/tinyusb
```

```bash
make
```

Outputs in `build/`:

* `bootloader.bin` – flash at `0x08000000` (does not touch the configuration page)
* `app.bin` – flash at `0x08004000`, or download through DFU
* `hybridlink-full.bin` – both together, for the first installation over SWD
* `installer.bin` – bootloader installer, used by `hybridlink install`
* `hybridlink-daplink.bin` – the full image as a DAPLink bootloader update
  (drag and drop onto the DAPLINK drive)

The host tool `tools/hybridlink` (Python 3 with `pyusb`; `cryptography` for
talking to ST's bootloader; `dfu-util`) runs from the source tree as it is,
or is installed with the images it uses:

```bash
sudo make install
```

That puts `hybridlink` in `/usr/local/bin` and the images in
`/usr/local/share/hybridlink`; `make install PREFIX=~/.local` installs for
the current user only, `make uninstall` removes it again. The tool takes
its images from `../build` next to itself, otherwise from
`../share/hybridlink`, so `hybridlink install` and `hybridlink update` need
no file names. The examples below use the installed name; from the source
tree run `tools/hybridlink` instead.

## Installing

First time, over SWD with any other probe:

```bash
st-flash --reset write build/hybridlink-full.bin 0x08000000
```

Or over USB only, on a probe that still has ST's bootloader (ST-Link/V2-1
firmware, as shipped) or an older hybridlink bootloader:

```bash
hybridlink install
```

This puts the probe into its bootloader, uploads `installer.bin` through the
bootloader's ST update protocol (the same encrypted transfer ST's updater
uses), and lets it run. The installer is an ordinary application at
`0x08004000` that carries the hybridlink bootloader. It:

* does nothing if the bootloader is already the one it carries;
* refuses if the bootloader area is read or write protected (option bytes;
  many ST-Links leave the factory with read protection on). In that case it
  erases the "application valid" marker in the last flash word, so ST's
  bootloader stays in update mode, and the tool installs `app.bin` under
  ST's bootloader instead (see below);
* otherwise writes the bootloader page by page, the vector table page last,
  and restarts into the new bootloader's DFU mode.

The tool then installs `app.bin` with dfu-util (`--app` picks another
image). The configuration page is left alone, so the probe mode and the
reported version survive; a version older than J46 (ST's updater leaves
the version it installed there, e.g. J32 on early probes) is reset to the
default, which ST's current tools accept. Unplugging before the
application is installed is harmless: the installer finds its bootloader in
place and returns to DFU. Losing power during the bootloader write itself
(well under a second) leaves a probe that needs an SWD reflash.

Uploading through ST's bootloader needs the Python `cryptography` package.

A probe whose bootloader is protected keeps ST's bootloader: read
protection makes the first 4 KiB of flash unwritable, and lifting it erases
the whole chip, which without SWD access would leave a dead probe. The
hybridlink application runs under ST's bootloader just as it does under its
own. `update` then writes through ST's update protocol instead of
dfu-util, and `set-type` tells ST's bootloader to start the application
again (it stays in update mode after a software reset). STLinkUpgrade can
put ST's firmware back at any time.

On a probe running DAPLink (STM32F103 hardware, HIC ID `97969908`, as on a
Jolt Connect or Streamline Connect), `hybridlink install` uses
DAPLink's own bootloader update instead. It copies
`build/hybridlink-daplink.bin` to the DAPLINK drive, checks that DAPLink
accepted it, and restarts the probe. The same works by hand without any
tools: drag `hybridlink-daplink.bin` onto the DAPLINK drive, wait for the
drive to come back (no `FAIL.TXT`), and reconnect the probe.

`hybridlink-daplink.bin` (made by `tools/mkdaplink.py`) is the full image
(bootloader, blank configuration page, application) padded to DAPLink's
48 KiB bootloader region, with DAPLink's info block in reserved vector
table slots at 0x20 and a CRC-32 in the last word. DAPLink writes it in a
power-fail safe way: the first sector keeps pointing at the DAPLink
interface until everything else is written and the CRC is checked.
DAPLink must be running its interface firmware (the DAPLINK drive, not
MAINTENANCE) with "Automation allowed: 1" in `DETAILS.TXT`. Going back to
DAPLink afterwards needs SWD, as DAPLink's bootloader is replaced.

Updating the application later, over USB only:

```bash
hybridlink update
```

This writes `app.bin` (or the image given after `update`): it sends the
bootloader command (`F9 01`) to the running probe, waits for the DFU device
and runs `dfu-util`. Done by hand:

```bash
hybridlink dfu
```

```bash
dfu-util -d 0483:3748 -a 0 -D build/app.bin -R
```

The usual ST-Link udev rules (as shipped with STM32CubeIDE / stlink) already
cover `0483:3748`, so no extra rule is needed.

`hybridlink-full.bin` covers `0x08000000-0x08003FFF` including the
configuration page; flash `bootloader.bin` and `app.bin` separately to keep
a configuration page written by ST's updater.

### Installing ST firmware (STM32CubeIDE / ST-LinkUpgrade)

The bootloader implements ST's update protocol, so ST's own upgrade tool
works on an hybridlink probe, from the hybridlink application as well as
from ST's firmware:

```bash
java -jar STLinkUpgrade.jar -sn <serial> -update
```

(or the GUI from STM32CubeIDE). It installs the firmware only when the
reported version is older than the one it ships; `-force_prog` installs it
anyway. On a probe whose configuration page is still blank (hybridlink
installed over SWD) the bootloader reports the values ST's updater records
for an ST-Link/V2-1 with "STM32 Debug+VCP" firmware, so the probe is
recognised without a type option. This installs ST's proprietary,
encrypted firmware in place of the hybridlink application. ST's firmware
finds the bootloader through the header at `0x08000100`, so going back is
the normal update path:

```bash
hybridlink update
```

The protocol: commands `F3 <request>` on the bulk endpoints tunnel a
DfuSe-like state machine (download, get status, ...). Block 0 carries
"set address" (`0x21`) and "erase page" (`0x41`) commands, data blocks are
AES-128 encrypted with a per-device key that is derived from the chip's
unique ID with ST's master key, and checksummed over the plain text. See
[bootloader/st_proto.c](bootloader/st_proto.c). Build with `ST_COMPAT=0`
to leave this out.

### Reported version and STM32CubeIDE updates

ST-LINK_gdbserver (used by STM32CubeIDE) reads the firmware version
(`F1`) when it starts. Below a minimum it refuses to run (*"ST-LINK firmware
upgrade required"*); below the version it knows as current, newer releases
only warn (*"ST-LINK firmware update available"*). Measured with V2-1
firmware versions:

| gdbserver | from | refuses below | warns below |
|-----------|------|---------------|-------------|
| 7.10.0 | STM32CubeCLT 1.18 | J45 | – |
| (2.2.400 plugin) | STM32CubeIDE 2.1.x | J46 | – |
| 7.14.0 | STM32CubeIDE 2.2.0 | – | J46 |

Reporting a higher version than ST ships changes nothing else: gdbserver
and STM32CubeProgrammer send the same commands at J48 and at J63.

So the reported version is a setting, not part of the build. It is the
last halfword of the configuration page (`0x08003FFE`, big endian), the same
place ST's updater records the version it installed; while it is blank
(`0xFFFF`) the compiled-in default (V2J48M35) is reported. The bootloader
reports the same word to ST-LinkUpgrade. Changing it needs no ST firmware
and no rebuild:

```bash
hybridlink set-version V2J49M36 --serial <serial>
```

```bash
hybridlink set-version auto --serial <serial>
```

```bash
hybridlink set-version default --serial <serial>
```

`auto` runs every ST-LINK_gdbserver it finds (STM32CubeIDE plugin folders
and STM32CubeCLT under `~`, `~/st`, `/opt/st`, ...; or `--gdbserver PATH`)
against the probe and raises the J number until none refuses it or asks for
an update. It never lowers the version. Run it after installing a new
STM32CubeIDE. `info` shows where the reported version comes from.

The tool uses two vendor control requests of the application (see
[common/settings.h](common/version_cfg.h)); the rest of the configuration
page is kept when the version is rewritten.

### Bootloader behaviour

* Starts the application unless the application asked to stay in DFU mode
  (RAM mailbox at the top of RAM, see [common/layout.h](common/layout.h)) or
  the application vector table is not valid.
* During an update (DFU or ST protocol) the first application page is kept
  in RAM and written last, so an interrupted update never leaves a
  half-written image that looks valid. The bootloader then simply stays in
  update mode.
* DFU upload (`dfu-util -U`) reads the application back, for hybridlink
  images only (marked with `HLNK` in the first reserved vector table slot):
  other firmware installed through the ST protocol, such as ST's own, is
  not readable over USB. `make DFU_UPLOAD_ANY=1` builds a bootloader that
  reads back any application.
* Leaves for the application on DFU detach or the bus reset after
  `dfu-util -R`.

## Source layout

```
common/            portable: HAL interface, flash layout, USB IDs, serial number
boards/<board>/    pin assignment (board.h)
port/<mcu>/        MCU support: startup, linker scripts, HAL, SWD pin I/O,
                   a minimal device header for TinyUSB
bootloader/        DFU bootloader (TinyUSB DFU class)
port/<mcu>/installer.c
                   bootloader installer (an application carrying bootloader.bin)
app/               probe application
  stlink.c         ST-Link command interpreter (transport independent)
  stlink_usb.c     TinyUSB class driver for the ST-Link debug interface
  dap.c            CMSIS-DAP command processor (transport independent)
  dap_usb.c        TinyUSB class driver for the CMSIS-DAP v2 interface
  settings.c       mode / version settings, vendor control requests
  swd.c            SWD / ADIv5 / MEM-AP engine (bit-banged through swd_io.h)
  vcp.c            CDC <-> UART bridge
tools/             host helpers
```

### SWD clock

The SWD clock is bit-banged; requested clocks (ST-Link divisor table,
`DAP_SWJ_Clock`, OpenOCD `adapter speed`) are mapped to the nearest clock
that is not faster, from measurements on the 72 MHz F103: full speed is
about 3.7 MHz reading / 2.8 MHz writing, below that within about 10 % of
the request down to 5 kHz. A port measures its own timing with the
`SWD_CALIBRATE` build (`CFLAGS=-DSWD_CALIBRATE make`): it records CPU cycles
per bit for a range of delays in `swd_cal[]` at start-up, to be read over
the probe's own SWD port; the mapping lives in the port's `swd_io.h`.

### Porting

Everything outside `port/` and `boards/` only uses
[common/hal.h](common/hal.h), `swd_io.h` and TinyUSB. A new MCU needs a
`port/<mcu>/` directory with `port.mk`, `hal.c`, `swd_io.h`, startup code and
linker scripts; a new board needs a `boards/<board>/board.h`. Select them with
`make PORT=... BOARD=...`.

## Jolt Connect pin map

| Pin | Function |
|-----|----------|
| PB13 (+PA5) | SWCLK |
| PB14 | SWDIO driver (tri-stated while reading) |
| PB12 | SWDIO sense |
| PB0 | target NRST (open drain) |
| PA2 / PA3 | VCP TX / RX (USART2) |
| PA10 | SWO (USART1 RX) |
| PA0 | target voltage (1:2 divider) |
| PA9 | status LED |
| PA15 | USB D+ pull-up control |

## Protocol coverage

Implemented: `F1` version, `F5` mode, `F7` target voltage, `F3 07` DFU exit,
`F9 00/01` bootloader query/enter and the debug command set (`F2`):
enter/exit SWD, IDCODEs, status, halt/run/step, system reset, NRST control,
core register read/write (single and all, incl. FP), debug register
read/write, 8/16/32-bit memory read/write with and without address increment
(any length, streamed in chunks), last R/W status (both forms), SWD clock,
DAP register read/write, AP init/close, SWO trace start/stop/count and the
board/capability queries (`56`, `57`, `58`) that current ST tools send.

Not implemented: JTAG (the Jolt Connect only routes SWD), SWIM and the USB
mass storage drag-and-drop function.

## Status

Tested on a Jolt Connect against an STM32F407:

* STM32CubeProgrammer: probe listed as `V2J48`, connect, read (~136 KB/s),
  program + verify
* ST-LINK_gdbserver (STM32CubeIDE): connect, reset, step, hardware breakpoint
* OpenOCD 0.12: reset/halt, step, 8/16/32-bit memory access, registers,
  breakpoints, 128 KiB RAM download in 0.9 s at 4 MHz (byte exact from
  4 MHz down to 100 kHz)
* stlink (st-info / st-flash): probe, read 512 KiB, write + verify
* bootloader: app -> DFU switch, dfu-util download/upload, return to app
* ST-LinkUpgrade 3.17.11: installs ST's V2J48M35 on a blank probe and from
  the hybridlink application; ST's firmware runs on the hybridlink
  bootloader (mass storage, VCP, debugging work) and switches back to the
  bootloader for hybridlink updates
* `hybridlink install` over USB only: stock ST firmware V2J48M35 on ST's
  bootloader -> hybridlink bootloader + application; repeated on a converted
  probe in ST-Link and in CMSIS-DAP mode (mode kept); from DAPLink 0259 on a
  Streamline Connect (RP2354 blade examined over CMSIS-DAP afterwards);
  on a read-protected rev G DIY probe with stock V2J48M35: the installer
  refuses, the application is installed under ST's bootloader, then
  `update` and `set-type` in both directions;
  on a rev D DIY probe (STM32F103C8, 64 KiB) with ST's V2J32M1 "STM32+Audio"
  firmware (USB 0483:374a): full conversion, old version reset; CubeIDE's
  ST-Link update installs ST's firmware on the hybridlink bootloader
* VCP: line coding, host -> target transmit, target -> host receive
  (16 KiB burst at 921600 baud, byte exact, while SWD is busy)
* SWO: 2 Mbaud UART/NRZ trace, sustained full rate (100 KB, ~200 KB/s)
  and paced streams byte exact with a polling host, also with 256-byte
  host reads as OpenOCD does; OpenOCD capture of 8 x 100000 ITM characters
  at full rate byte exact

CMSIS-DAP mode, against an RP2354 (RP2350, Raspberry Pi OpenOCD 0.12.0+dev):

* both Cortex-M33 cores examined at 4 MHz; program + verify of an 80 KB
  image in 4.0 s (DAPLink's CMSIS-DAP v1 build on the same hardware: 8.2 s)
* 256 KiB flash read and RAM write at about 125 KB/s, byte exact
* GDB: reset halt, hardware breakpoint, step, hardware watchpoint
* pyOCD 0.45 (target rp2350): connect, memory access
* VCP: target UART output at 115200
CMSIS-DAP mode, against an STM32F407 (OpenOCD 0.12):

* program + verify of 512 KiB flash in 16.5 s
* GDB: reset halt, step, hardware breakpoint; pyOCD (generic cortex_m)
* SWO: 2 Mbaud UART trace through `DAP_SWO_Data`, byte exact at full rate

Not tested yet: multidrop TARGETSEL (the RP2350 configuration does not use
it; an RP2040 would), Windows.

### SWO notes

The trace endpoint is a stream: full 32-byte packets are fed from a 4 KiB
receive buffer while the host drains them, a short packet only after 5 ms of
silence. `GET_TRACE_NB` reports everything not yet delivered, so a host may
read any amount per poll. If the host polls too slowly the oldest data is
dropped. OpenOCD 0.12 does not poll trace while blocked in `wait_halt`, so a
long full-rate burst captured that way gets truncated; that is a host-side
limit.
