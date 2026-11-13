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

**Bench result (2026-11-13).** Setup: Jolt Connect on J2 of the CH32V307V-R1,
with the on-board link's jumpers removed. Test build: `make RVSWD_RAW=1`, a
bit-level script engine behind HL vendor requests 07/08 (`app/rvswd_raw.c`).

- **The short packet works.** The long packet does not: the target answers it
  with a few bits only.
- **Parity is even**, not odd. The host's parity bit makes the count of ones
  even over `addr<<1|rw` (header) or over the data. Each parity bit is sent
  twice.
  - With odd parity the target ignores writes (status reads `1111`) and reads
    return zeros.
- **Padding:** 4 zeros, driven by the host, after the header parity.
- **Write:** after the data and parity, the target answers status `0111`.
- **Read:** the target sends:
  - data 32 bits, MSB first;
  - its even parity bit;
  - a constant `1`;
  - status `0111`.
- **Turnaround:** none is needed. The host releases DIO right after the
  padding (reads) or the data parity (writes) and samples on the next clocks.
- **Init:** wake-up (100 clocks with DIO high, then STOP), then dmcontrol =
  dmactive.
- **Verified values:**
  - 0x7F reads 0x30700518.
  - dmstatus reads 0x00030c82 while running and 0x00000382 after haltreq;
    resumereq brings 0x00030c82 back.
- **Speed:** works with no extra delay in the script engine, the fastest it
  can run. The real driver will be measured with the DWT later.
- **Not seen yet:** the status nibble for a busy or failed access.

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

When halted, dmstatus reads 0x00000382. After resumereq it reads 0x00000c82.
Reading a nonexistent CSR (0xfff) through an abstract command left cmderr at
0, so this core does not flag that case.

Memory access goes through abstract commands / program buffer (no system bus
access reported). Flash programming: FPEC very similar to the STM32F1 one
(KEYR/CTLR/STATR, plus WCH's 256-byte "fast programming" mode); to be written
from the CH32V307 reference manual.

## 3. Host side: WCH-Link USB protocol (firmware 2.12)

Everything in this section was observed on the genuine on-board WCH-Link
(CH549, shipped with 2.3, updated to 2.12 with MounRiver Studio 3.0.0). The
hosts were wlink 0.1.3, probe-rs 0.32, WCH's OpenOCD (as started by MounRiver
for debugging) and MounRiver's own downloader. They were traced at the usbfs
level, and the link was probed directly for edge cases. With firmware 2.3,
wlink, probe-rs and WCH OpenOCD all refuse the link, so hybridlink's WCH mode
answers like 2.12.

### 3.1 Descriptors

| field | value |
|-------|-------|
| bcdUSB | 1.10, full speed, EP0 64 |
| class | EF/02/01 (IAD) |
| VID:PID | `1a86:8010`, bcdDevice 2.12 |
| strings | manufacturer "wch.cn", product "WCH-Link", 12-character hex serial |
| config | 3 interfaces, bus powered, 100 mA |

- **IAD 0** covers IF0, function class FF, iFunction "WCH-Link".
- **IF0** is vendor class FF/80/55 with 4 bulk endpoints of 64 B:
  - `0x01` OUT and `0x81` IN carry commands;
  - `0x02` OUT and `0x82` IN carry raw data.

  Hosts hard-code these endpoint numbers.
- **IAD 1** covers IF1 and IF2: a CDC ACM (bmCapabilities 0x02).
  - IF1: notification endpoint `0x84`, interrupt, 64 B, bInterval 1.
  - IF2: data endpoints `0x83` IN and `0x03` OUT, bulk 64.
  - The ACM is bound by the OS's own driver, so its endpoint numbers and sizes
    are free to change.

### 3.2 Framing

- **Request** on EP 0x01: `81 CMD LEN payload[LEN]`.
- **Reply** on EP 0x81: `82 CMD LEN payload[LEN]`.
- Multi-byte integers are big endian.
- There is exactly one reply per request, and hosts wait for it before
  sending the next request.
- **Unknown command:** `82 81 01 ff`. The CMD byte of this reply is 0x81, not
  the request's CMD.
- **Unknown subcommand of 0x0d:** `82 0d 01 ff`.
- **One reply has no header at all:** 0x11 (chip info).
- **The length byte is not checked:**
  - `81 0d 02 01` is answered as the version request.
  - With a missing payload (`81 0d 00`), the link acts on stale buffer contents.
  - A first byte other than 0x81 is echoed back unchanged (`80 0d 01 01` →
    `80 0d 01 01`).
  - hybridlink does not need to copy any of this. It should answer the
    well-formed requests and reply `82 81 01 ff` to everything else.
- **Error replies:** the community notes describe `81 REASON LEN` error
  replies (0x55 = cannot connect), but none was ever seen here. That includes
  target-less cases, because the target was always present.

### 3.3 Commands

| cmd | request payload | reply payload | notes |
|-----|-----------------|---------------|-------|
| 0d 01 | – | `02 0c 01 00` | version 2.12, probe type 1 = WCH-Link-CH549; fw 2.3 sent `02 03` |
| 0d 02 | – | `06 id[4]` | attach: target family + chip ID read live from the target (`30 70 05 18`; after a full erase `e3 39 e3 39`); ≈ 40 ms |
| 0d ff | – | `ff` | detach; DMI keeps working afterwards, the target is not released |
| 0d 04 | – | `02` or `00` | sent by wlink after the protection check; meaning unknown, varies with state |
| 0d 17 | – | `05`, `01`, `00` | sent by OpenOCD before erase/program; meaning unknown, varies with state |
| 0d 03/05/07/10/18 | – | echo of the subcommand | not used by any host |
| 0d 06 | – | `00` | not used by any host |
| 0d other | – | `ff` | 09, 0a, 0b, 0c, 16, 99 tried |
| 0c | `family speed` | `01` | hosts send `01 01`, then `06 01` after attach; OpenOCD's `dbgtool` sends `06 ff`; any value is accepted. Speeds per wlink: 1 high (6 MHz), 2 medium (4 MHz), 3 low (400 kHz) |
| 08 | `addr data[4] op` | `addr data[4] status` | DMI access. op 0 nop, 1 read, 2 write, 3 behaves as nop. Status was 00 in all ≈ 100 000 replies: the link retries busy itself and reports nothing else. Unimplemented addresses read 0 |
| 11 | `06` | **no header**: `ff ff fl[2] uid[8] ff ff ff ff id[4]` (20 B) | chip info: flash size in KiB (BE, `01 00` = 256), ESIG UID, chip ID. Sent by wlink and MounRiver |
| 0b 01 | – | `01` | reset and run (wlink, MounRiver after download) |
| 0b 02 | – | `02` | chip reset (OpenOCD after program) |
| 06 01 | – | `02` unprotected / `01` protected | read protection check |
| 06 04 | – | `00` unprotected / `01` protected | read protection check, newer form (wlink) |
| 06 02 | – | `02` | remove read protection (erases the flash) |
| 06 03 | – | `03` | set read protection |
| 01 | `addr[4] len[4]` | `01` | set write region (flash address, byte count) |
| 01 | `01` or `02` | `01` | probe-rs "config chip" (check / unprotect); fw 2.12 does nothing, even on a protected chip |
| 03 | `addr[4] len[4]` | `01` | set read region |
| 02 01 | – | `01` | full flash erase, done by the link itself (≈ 0.14 s) |
| 02 05 | – | `05` | upload of the flash routine follows on EP 0x02 (512 B) |
| 02 06 | – | `06` | OpenOCD only, before the region; "prepare" |
| 02 07 | – | `07` | after the routine upload (wlink/OpenOCD ≈ 0.4 s, MounRiver 13 ms); meaning unknown |
| 02 02 | – | `02` | start write (wlink); OpenOCD sends no start command and goes straight to data |
| 02 04 | – | `04` | start write with verify (MounRiver) |
| 02 08 | – | `08` | end of programming (wlink, OpenOCD; MounRiver omits it) |
| 02 0c | – | `0c` | read memory: the read region's bytes follow on EP 0x82 |
| 0a | `01` | `01` | exists, no host uses it |
| 0f | `01` | – | **enter IAP**: the link drops off the bus and comes back as its updater (§3.6). Never send it to a genuine link casually |
| 0e | – | – | "disable debug interface" in MounRiver's library; not traced (it would cut the target off) |

`0x00 04 05 07 09 10 12 20 55 7f` are unknown (`82 81 01 ff`).

### 3.4 Data endpoints

- **Write.** The host streams the image on EP 0x02 in 64-byte packets,
  padded with zeros to a multiple of 256 bytes.
  - The region length may be exact (wlink, MounRiver) or already rounded
    (OpenOCD).
  - After every 4096 bytes and after the last packet, the link sends
    `41 01 01 04` on EP 0x82.
  - Hosts wait for each ack before going on, so the ack is the flow control.
- **Flash routine.** The host sends the 512-byte routine on EP 0x02 after
  `02 05`.
  - All three hosts send the identical routine (crc32 87885ed7). It is a WCH
    binary that runs in target RAM.
  - How the genuine link starts it, and feeds it, is invisible from USB.
- **Read.** After `03 addr len` and `02 0c`, the link returns `len` bytes on
  EP 0x82.

### 3.5 Host sequences

All hosts open with `0d 01` (version), `0c 01 01`, `0d 02` (attach) and `0c 06 01`.

- **wlink flash:**
  1. `11 06`, `06 01`, `0d 04`
  2. detach, attach
  3. `06 01`, `06 04`
  4. `01 08000000 len`
  5. `02 05` + routine
  6. `02 07`, `02 02`
  7. data + acks
  8. `02 08`
  9. `0b 01`, `0d ff`
- **wlink erase:** `06 01`, `06 04`, `02 01`, attach, detach.
- **wlink protect / unprotect:** detach, attach, `06 01`, `06 03` or `06 02`, detach.
  - The new state shows only after a reset.
- **wlink dump:** `03 addr len`, `02 0c`, data on EP 0x82.
- **wlink halt / resume / regs / memory writes:** DMI only.
- **WCH OpenOCD program** (`program file verify reset`):
  1. `0d 17`, `06 01`
  2. `02 01` erase
  3. attach, `06 01`
  4. `02 06`
  5. `01 region`
  6. `02 05` + routine, `02 07`
  7. data + acks
  8. `02 08`, `0b 02`
  9. Verify runs over DMI.
- **WCH OpenOCD debug session** (`-f wch-riscv.cfg -c dbgtool -c init -c halt`,
  as started by MounRiver): `0c 06 ff`, then DMI only.
  - That includes breakpoints, stepping and memory accesses.
  - On this core GDB gets no watchpoints (no hardware trigger reported).
- **MounRiver download** (its own library, not OpenOCD):
  1. `0d 01` twice
  2. `0c 06 01`, attach
  3. `11 06`, `06 01`
  4. `02 01` erase
  5. attach, `06 01`
  6. `01 region` (exact length)
  7. `02 05` + routine, `02 07`
  8. `02 04`
  9. data + acks
  10. `0b 01`, `0d ff`

  MounRiver accepted 2.12 without offering a link update.
- **probe-rs:** version, `0c 01 01`, attach, `01 01`, `01 02`, then DMI only.
  - That covers its own flash algorithm, run through abstract commands.

### 3.6 Updater mode (after `0f`)

The link comes back as `4348:55e0`:
- bcdDevice 1.00, EP0 8 B, no strings;
- one vendor interface FF/80/55 with endpoints 0x01/0x81/0x02/0x82, bulk 64.

MounRiver's link firmware update then runs on EP 0x02 / 0x82:
1. `81 02 00 00` three times; reply `00 00` each time.
2. On EP 0x01, `81 0f 01 02` → `82 0f 01 00`.
3. Write the firmware: `80 len addr16-LE data[len]`, 60 bytes per packet,
   reply `00 00`.
4. Verify the same image: `82 len addr data`, reply `00 00`.
5. `83 02 00 00` restarts into the new firmware.

The image is the CH549 (8051) firmware, about 42.7 KB, sent unencrypted.
hybridlink's WCH mode has no reason to implement this mode.
- `0f` can answer `82 81 01 ff`.
- Our own update path stays the HL vendor requests and DFU.

### 3.7 Not observed (open)

- DMI status other than 00, and the `81 55` error replies: not seen, because
  no tool ever ran without a target.
- The meanings of `0d 04`, `0d 17`, `02 06`, `02 07` and `0a`.
- MounRiver's protect/unprotect and memory-type (`MRSFunc_SetMemType`) GUI
  functions: not traced yet.
- Target power control: wlink's power commands produced no traffic on this
  link.

## 4. Plan

1. `common/rvswd.c`: portable RVSWD bit-bang on the existing SWD pin macros;
   DMI read/write with retries on busy. Bring-up test: read 0x7F on the CH32V307
   from the Jolt Connect (on-board WCH-Link jumpers removed), compare with the
   values in §2.
2. New mode **WCH-Link** (`set-type wch`, `1a86:8010`, bcdDevice 2.12,
   strings as in §3.1 except the serial, which is our own).
   - Commands `0d`, `0c`, `08`, `0b` and `11` come first. That is enough for
     probe-rs and for MounRiver/OpenOCD debugging.
   - Endpoint memory on the F103 (512 B PMA, minus the BTABLE for EP0–4):
     - EP0 64+64;
     - vendor IF 4 × 64 (fixed by the hosts);
     - CDC notification 8;
     - CDC data 2 × 32;
     - total 456 B, which fits.
3. Programming: `01`, `03`, `02 xx`, `06` and the data endpoints.
   - hybridlink programs the flash controller itself over DMI.
   - It accepts the uploaded routine (`02 05`) and discards it, so no WCH
     binary is ever run or stored.
   - It acks every 4096 B with `41 01 01 04`.
4. Validate each step with all four hosts against the traces of the genuine
   link (tracer: ptrace-based, since wlink/probe-rs use raw syscalls).
5. Later: hybridlink running on the CH32V307 itself (USB HS) as a step towards
   the CH32V407 port.

## Sources

- RINS – WCH DTM documentation project: https://perigoso.github.io/rins
- fxsheep, WCH RVSWD protocol: https://github.com/fxsheep/openocd_wchlink-rv/wiki/WCH-RVSWD-protocol
- Black Magic Debug RFC, native RVSWD: https://codeberg.org/blackmagic-debug/blackmagic/issues/2172
  and https://github.com/blackmagic-debug/blackmagic/pull/1399 (GPL: read for behaviour only)
- wlink protocol notes: https://docs.rs/crate/wlink/0.0.7/source/protocol.md
- openwch CH32V307 board: https://github.com/openwch/ch32v307
