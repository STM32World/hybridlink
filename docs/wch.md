# WCH RISC-V targets – research notes (wchdev)

Working notes for debugging WCH RISC-V chips (CH32V1xx/2xx/3xx, CH32V4xx later)
with hybridlink. Development board: CH32V307V-R1-1v0 (CH32V307VCT6) with an
on-board WCH-Link. Everything here comes from public, community
reverse-engineering documents and from our own observation of a genuine
WCH-Link. No WCH code and no GPL code is copied, and none should be.

## 1. Wire protocol: RVSWD (two-wire)

The CH32V1/2/3 series use WCH's two-wire "RVSWD" on PA13 (DIO) / PA14 (CLK).
It is **not** ARM SWD. It carries accesses to the RISC-V DTM `dmi` register
(7-bit address, 32-bit data, 2-bit op/status). (CH32V003/00x use the
one-wire SDI instead; out of scope for now.)

Line rules (RINS, fxsheep wiki):
- CLK and DIO idle high; host always initiates; point to point, no addressing.
- START = DIO falls while CLK high. STOP = DIO rises while CLK high.
- DIO changes while CLK is low, is sampled on the CLK rising edge. MSB first.
- Wake-up: 100 clocks with DIO high, then STOP.

Two packet formats are reported:

**Long packet** (seen on WCH-Link captures), host then target, no turnaround clock:

| who    | field   | bits |
|--------|---------|------|
| host   | address | 7    |
| host   | data    | 32   |
| host   | op      | 2    |
| host   | parity  | 1 (odd over addr+data+op) |
| target | address | 7    |
| target | data    | 32   |
| target | status  | 2    |
| target | parity  | 1 (even over addr+data+status per wiki; odd per BMD code – to verify) |

**Short packet** (what the Black Magic Debug RFC uses successfully on CH32V307):
- host: address 7, R/W 1 (1 = write), parity 2 (odd parity of `addr<<1|rw`,
  bit sent twice), 4 padding zeros;
- write: host data 32 + parity 2 (odd, doubled), then target status 4;
- read: target data 32 + parity 2, then target status 4;
- status: upper 2 bits = DMI op status (0 ok, 2 failed, 3 busy), lower 2 seem to be 1s.
- Init seen to work: wake-up (100 clocks DIO high), write `dmcontrol` (0x10) =
  dmactive twice, read 0x7F (WCH register mirroring the chip ID).

Speed: BMD reports CH32 working up to ≈1.6 Mbit/s with bit-banging; the
WCH-Link speed setting offers 400 kHz / 4 MHz / 6 MHz.
Open points to settle on the bench: target-side parity sense, turnaround
behaviour when the host releases DIO, long vs short framing per chip.

Jolt Connect: SWCLK = PB13, SWDIO driver = PB14 (through series R), sense = PB12.
The split driver/sense pins suit RVSWD's half-duplex without a turnaround clock
(host must release PB14 right after the last host bit).

## 2. Debug module (CH32V307)

Standard RISC-V Debug 0.13 DM behind the DMI. Observed through the genuine
WCH-Link (target running user code):

| DMI reg | value        | meaning |
|---------|--------------|---------|
| 0x11 dmstatus   | 0x00030c82 | version 2 (0.13), authenticated, all/any running |
| 0x10 dmcontrol  | 0x00000001 | dmactive |
| 0x16 abstractcs | 0x08000002 | progbufsize 8, datacount 2 |
| 0x12 hartinfo   | 0x00312380 | |
| 0x7F (WCH)      | 0x30700518 | chip ID CH32V307VCT6 |

Memory access goes through abstract commands / program buffer (no system bus
access reported). Flash programming: FPEC very similar to the STM32F1 one
(KEYR/CTLR/STATR, plus WCH's 256-byte "fast programming" mode); to be written
from the CH32V307 reference manual.

## 3. Host side: WCH-Link USB protocol

Genuine on-board WCH-Link (CH549 based), `1a86:8010`, shipped with firmware
2.3, updated to 2.12 with MounRiver Studio 3.0.0. With 2.12, wlink 0.1.3,
probe-rs 0.32 (detects CH32V307VCT6) and WCH's OpenOCD (transport "sdi",
6 MHz, misa 0x40901125, marchid WCH-V4A, 256 KiB flash) all work. hybridlink's
WCH mode must answer in the 2.12 format.

- IF0 vendor class: EP 0x01 OUT / 0x81 IN commands, EP 0x02 OUT / 0x82 IN raw data (64 B).
- IF1/IF2: CDC ACM (VCP).

Framing: request `81 CMD LEN payload`, reply `82 CMD LEN payload`, error
`81 REASON LEN payload` (0x55 = cannot connect). Integers big endian.

| cmd | sub | meaning |
|-----|-----|---------|
| 0x0d | 01 | firmware version → `major minor probe-type 00` (fw 2.12: `02 0c 01 00`, 1 = WCH-Link-CH549; fw 2.3 sent only `02 03`, which wlink, probe-rs and WCH OpenOCD all reject) |
| 0x0d | 02 | attach target → `family chipid[4]` (fw 2.12: `06 30 70 05 18`; fw 2.3: `06 00`) |
| 0x0d | ff | detach |
| 0x08 | –  | DMI: `addr data[4] op` → `addr data[4] status`; op 0 nop/1 read/2 write; status 0 ok/2 fail/3 busy |
| 0x0b | 01/02/03 | reset (quit reset / chip reset / normal) |
| 0x0c | `family speed` | speed 1 = high (6 MHz), 2 = 4 MHz, 3 = 400 kHz |
| 0x01/0x03 | `addr len` | set write / read memory region |
| 0x02 | sub | programming: 01 erase, 02/04 write (+verify), 05 flash-op upload, 06 prepare, 08 end, 0c read memory; data on EP 0x02/0x82 |
| 0x06 | sub | read protection check/set/clear |
| 0x0e | 01 | disable debug |
| 0x11 | –  | chip info / UID |

The flash path uploads a host-supplied "flash op" routine into target RAM;
the hosts carry those blobs (WCH binaries), so hybridlink only has to move
data, never ship them. Chip family codes: 01 CH32V103, 05 CH32V20x,
06 CH32V30x, 09 CH32V003, others per wlink.

## 4. Plan

1. `common/rvswd.c`: portable RVSWD bit-bang on the existing SWD pin macros;
   DMI read/write with retries on busy. Bring-up test: read 0x7F on the CH32V307
   from the Jolt Connect (on-board WCH-Link jumpers removed), compare with the
   values in §2.
2. New mode **WCH-Link** (`set-type wch`, `1a86:8010`, vendor IF + VCP), first
   with 0x0d, 0x08, 0x0b, 0x0c: enough for probe-rs (does its own flashing
   over DMI) and for debugging with WCH's OpenOCD.
3. Programming commands (0x01/0x02/0x03, data endpoints) for wlink, WCH
   OpenOCD and MounRiver flashing.
4. Validate every step against the genuine WCH-Link first (LD_PRELOAD libusb
   shim to trace host tools, as with the ST tools).
5. Later: hybridlink running on the CH32V307 itself (USB HS) as a step towards
   the CH32V407 port.

Host tools to test with (not installed yet, need owner OK): `wlink`
(cargo), `probe-rs` (cargo), WCH's OpenOCD fork.

## Sources

- RINS – WCH DTM documentation project: https://perigoso.github.io/rins
- fxsheep, WCH RVSWD protocol: https://github.com/fxsheep/openocd_wchlink-rv/wiki/WCH-RVSWD-protocol
- Black Magic Debug RFC, native RVSWD: https://codeberg.org/blackmagic-debug/blackmagic/issues/2172
  and https://github.com/blackmagic-debug/blackmagic/pull/1399 (GPL: read for behaviour only)
- wlink protocol notes: https://docs.rs/crate/wlink/0.0.7/source/protocol.md
- openwch CH32V307 board: https://github.com/openwch/ch32v307
