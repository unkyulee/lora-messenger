# Validation record

Firmware for Wio Tracker L1 OLED and LILYGO T-LoRa Pager, Italy profile.

## Pager boot failure correction

The first hardware boot report showed octal PSRAM initialization failing, followed by an abort. Decoding the supplied addresses with the SX1262 ELF located the abort in `operator new` while `LilyGoDispArduinoSPI::init` allocated its display buffer. The initial project configuration incorrectly selected `qio_opi`. LILYGO's board definition specifies **quad SPI PSRAM** (`qio_qspi`).

The corrected configuration selects `qio_qspi`, `qspi` PSRAM and QIO flash. A compile-time guard rejects a non-quad SDK configuration. Startup now checks `psramFound()` before entering LilyGoLib's framebuffer allocation, and stops initialization if the display/input adapter fails. The separate fuel-gauge timeout and Wire initialization warning are not the decoded abort location. A device reboot with the corrected build is still required to confirm the fix physically.

All three corrected targets built successfully, and `scripts/package.py` verified and replaced the images in `dist/`. Build log: `build/psram-fix-builds.log`. This fix was checked through target compilation and image validation, without a new executed host-test run.

Reference: [LILYGO Pager board definition](https://github.com/Xinyuan-LilyGO/LilyGoLib-PlatformIO/blob/master/boards/lilygo-t-lora-pager.json).

## Automated checks

- Corrected combined build passed for all three targets on 2026-09-15. See `build/psram-fix-builds.log`.
- Wio application-only UF2 generation passed. The package verifier checked all 550 blocks, their family ID, sequential addresses, end markers, application boundaries, and reset/stack vectors.
- Both Pager factory images passed comparison against their corresponding bootloader, partition table, and application binaries; each application fits its app0 partition.
- `dist/manifest.json` contains the final file lengths and SHA-256 hashes.
- Before the Pager boot correction, the host-test source compiled successfully with `--compile-only` and `-Wall -Wextra -Werror`.
- The host suite passed earlier in development. A subsequent build of the suite compiled with `-Wall -Wextra -Werror`, but Windows Application Control rejected execution with `WinError 4551`. The latest source therefore does not have a fresh executed host-test pass on this machine. The project does not change or disable that Windows policy.

The test suite exercises the actual shared C++ protocol and application source with fake storage/radio/input adapters. It does not emulate the MCU, SPI, I2C, display controller, antenna, or RF environment.

| Target | Build | Static RAM | Application flash |
| --- | --- | --- | --- |
| Wio L1 OLED | Pass | 34,376 / 237,568 bytes | 140,516 / 811,008 bytes |
| Pager SX1262 | Pass | 50,972 / 327,680 bytes | 702,754 / 6,553,600 bytes |
| Pager LR1121 | Pass | 50,932 / 327,680 bytes | 700,482 / 6,553,600 bytes |

These figures exclude runtime heap allocations, including the Pager display canvas. The Pager's upstream Arduino variant emits a `digitalPinToInterrupt` macro redefinition warning, and the unused upstream LVGL filesystem helper emits a configuration warning; neither prevented linking. Physical interrupt behavior remains on the device checklist.

## Required device checks

These checks have **not** been completed. The user tested the initial Pager build and reported the boot failure above; the corrected build has not yet been confirmed on hardware. No physical device has been flashed automatically from this session.

1. Flash the UF2 to Wio with its existing S140 7.3.0 bootloader, and the matching SX1262/LR1121 factory image to the Pager. Verify both boot from battery and USB, with working displays and inputs.
2. Enter different names and restart both. Confirm names persist and first-run naming does not reappear.
3. Send from Pager to Wio and back. Check names, body text, maximum-size messages, and Wio long-message paging. A transmit-complete notification alone does not validate reception.
4. Type on one device while sending to it from the other. Confirm the draft remains intact and the incoming message appears in history.
5. Restart after receiving messages. Confirm history survives. Fill more than 24 entries and confirm only the newest 24 remain.
6. Exercise all Wio joystick directions, user-button erase/back, keyboard case/space/delete/send, and the Pager's wheel click/hold and physical symbol keys.
7. Send several messages quickly. Confirm the waiting screen, cancellation, channel-busy expiry, and retry behavior. Verify actual modulation, power and airtime with radio test equipment before treating regulatory behavior as established.
8. Check display orientation, contrast, RGB byte order, and power consumption. Measure battery runtime and low-battery behavior; these builds do not yet implement a validated sleep/battery-management policy.

Saved history is local receive history. Offline catch-up, routing/mesh, delivery acknowledgements, and interoperability with other firmware are outside this version.
