# Implementation notes

`src/main.cpp` owns the onboarding, inbox, editor, quick-reply selection, outgoing queue, and persistence. All state changes occur on the Arduino loop task. Device adapters provide input, display drawing, identity, entropy, and file access. `src/radio.cpp` configures the matching RadioLib driver. Radio interrupts only set a flag; SPI and application work happen in the loop.

## Public packet v1

Integers are little-endian. Every valid packet is a broadcast; there is no destination field or private-message operation.

| Offset | Length | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `LMC1` |
| 4 | 1 | Version, 1 |
| 5 | 1 | Sender name length, 1–16 |
| 6 | 1 | Text length, 1–160 |
| 7 | 1 | Reserved, must be 0 |
| 8 | 8 | Device identity |
| 16 | 4 | Random boot/session identity |
| 20 | 4 | Sequence number, nonzero |
| 24 | variable | Name then text, no terminators |
| final 4 | 4 | IEEE CRC-32 over all previous bytes |

The maximum packet is 204 bytes. Text must be printable ASCII and cannot consist entirely of spaces. Decode verifies exact length, version, reserved flags, field limits, nonzero identities, and CRC before changing the output. Messages are deduplicated by device/session/sequence against the 24 retained messages. Own-sender packets are ignored. Neither the CRC nor the hardware identity authenticates the sender.

## Persistence

Two alternating snapshots (`/chat0` and `/chat1`) contain `LMS1`, a 32-bit generation, name length, message count, saved name, length-prefixed public packets, and CRC-32. Each write targets the inactive file, then reads it back and validates it before switching active slots. Startup selects the newest valid snapshot using wrap-aware generation comparison. An interrupted inactive-file write leaves the previous valid snapshot available. Names are not considered saved until write verification succeeds.

Wio uses the Arduino core's 28 KB internal LittleFS area at `0xED000`; the external QSPI chip is not used. Pager uses ESP32 LittleFS in the data partition. These libraries may format an unmountable filesystem on initial mount; the two-slot mechanism protects individual interrupted snapshots, not complete filesystem corruption or chip failure. Drafts are held only in RAM.

## Radio scheduling

The next attempt starts no earlier than 20 times the previous packet's rounded-up airtime plus 100 ms after the attempt began. Failed attempts are also charged. Boot reserves the maximum-packet interval. A queued message observes a randomized initial delay and CAD backoff and expires after 90 seconds; cancellation/expiry/failure preserves its draft. The CAD interrupt wait is bounded to 250 ms; a missing interrupt is treated as a busy channel. There are no automatic repeats or delivery acknowledgements.

Both board drivers must have matching modulation settings, CRC, sync word, headers, and IQ. RadioLib initializes explicit headers and normal IQ. Pager uses the official library's power sequencing and, for LR1121, its RF-switch table. Wio uses SX1262 DIO2 switching, DIO3 1.8 V TCXO, and P1.08 as the external RX-enable signal. See the physical validation list before calling this radio-tested.

## Hardware and dependency references

- [LILYGO Pager documentation](https://wiki.lilygo.cc/products/t-lora-series/t-lora-pager/)
- [Official LilyGoLib PlatformIO dependency list](https://github.com/Xinyuan-LilyGO/LilyGoLib-PlatformIO/blob/master/platformio.ini)
- [LilyGoLib pinned source](https://github.com/Xinyuan-LilyGO/LilyGoLib/tree/c4a10b29b05c95983f2310eaa2a3c5802044dcba)
- [Wio hardware overview](https://wiki.seeedstudio.com/wio_tracker_l1_node/)
- [Wio Arduino pin-to-GPIO reference](https://github.com/meshtastic/firmware/blob/develop/variants/nrf52840/seeed_wio_tracker_L1/variant.cpp)
- [RadioLib v7.1.2](https://github.com/jgromes/RadioLib/tree/7.1.2)

Board pin mappings are factual hardware mappings; this project is not a Meshtastic firmware fork. Dependencies retain their own licenses in their source directories. Before distributing binaries, include the corresponding third-party license notices and satisfy any source-distribution requirements of the linked libraries and Arduino cores.
