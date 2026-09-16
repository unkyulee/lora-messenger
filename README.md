# LoRa Messenger

A small, standalone public chat firmware for the **Wio Tracker L1 OLED** and **LILYGO T-LoRa Pager**, configured for use in **Italy**. Enter a name once, read the conversation, and broadcast a message. There are no contacts, private messages, phone pairing, or radio settings in the interface.

This is a first hardware-test build. Compilation does not establish that the displays, controls, power management, or radio link work correctly on physical devices. Nothing has been flashed automatically.

## Everyday use

- On first startup, enter a name of up to 16 characters and save it.
- Subsequent startups open the conversation. The most recent 24 sent/received messages are saved in flash.
- Messages can contain up to 160 printable ASCII characters. Italian accents are not supported yet; use `e'`, for example. The Wio keyboard currently offers letters, numbers, and spaces; the Pager also offers its physical symbol keys.
- Every transmission is public to compatible devices within direct radio range or through one T1000-E relay. All devices need this firmware; it does **not** exchange messages with Meshtastic, MeshCore, or LoRaWAN.
- Sending allows up to three attempts with the same message identity, a 30-second ACK window per transmission, and a two-minute overall deadline. "Delivered" means at least one other handheld saved the message. Otherwise, "Delivery unconfirmed" keeps your draft available. It does not prove that nobody received the message.
- The top line is a status bar: sending/receiving activity on the left, battery percentage on the right. A received message plays a short chime.
- The display turns off after 10 seconds without input. Any key or button turns it back on; that press only wakes the display. Received messages and send results also turn it on.

### Pager

Start typing from the conversation to write. Enter or a short wheel click sends the message. Backspace erases. For numbers and symbols, press **Sym** and then the key: the top row types 1–0, and the other keys type the symbol printed on them (for example Sym, V types `?`). Press **Shift** and then a letter for a capital. Pressing Sym or Shift again cancels it; the status bar shows which is active. Sym, Backspace or holding the wheel for about 0.65 seconds leaves the editor and keeps your draft. Turn the wheel to read older/newer messages.

Hold the wheel for 3 seconds to power off, then release it. On battery the Pager switches off completely; hold the PWR button to turn it on. While USB is connected the battery cannot power the board off, so the Pager sleeps with its radio, speaker, and keyboard switched off; press the wheel or BOOT button to turn it on. The Pager receives no messages while off or asleep.

### Wio L1 OLED

Press the joystick to write. Move it in four directions to choose a key; press to enter it. The final four keys are `_` (space), `<` (erase), `^` (uppercase/lowercase), and `>` (save name or send). The footer describes the selected key. In the message editor the user button leaves the editor and keeps your draft; while entering your name it erases a character.

In the conversation, up/down selects messages. Right shows the next page of a long message and left the previous page.

Sending is queued while the radio needs a pause or detects another transmission. You can cancel before transmission starts. A failed send keeps the text for retry. Messages received while you type are saved without replacing your draft.

## Build

Install PlatformIO Core (or its VS Code extension), Git, and Python. From the project directory:

```powershell
./scripts/bootstrap.ps1
pio run -e sensecap_t1000_e_relay
pio run -e wio_l1_oled
pio run -e pager_sx1262
# For a Pager fitted with LR1121 instead:
pio run -e pager_lr1121
```

`EU version` identifies the radio band, not the Pager's radio chip. Check the order details or module marking to choose **SX1262** or **LR1121**. The two builds use identical over-the-air settings. CC1101 hardware is not supported.

LilyGoLib is pinned by `bootstrap.ps1`. PlatformIO installs the remaining libraries and compilers; the first build can take several minutes. The Wio build uses a local board variant and an S140 7.3.0 application memory map. It generates both `.hex` and `.uf2` outputs automatically.

The Pager requires **quad SPI PSRAM** (`qio_qspi`). An earlier build incorrectly used octal PSRAM and could reboot during display initialization. Use the corrected files in `dist/`; if updating that earlier build, upload the matching full factory image or use PlatformIO's upload target so the corrected boot image is included.

On this Windows machine, PlatformIO is also available at `C:/Users/unkyu/.platformio/penv/Scripts/platformio.exe`.

## Flash

Use the normal manufacturer bootloader already on the device. Connect the matching EU antenna before running the firmware.

The verified local build files are collected in `dist/`:

| Device | File |
| --- | --- |
| SenseCAP T1000-E relay | `messenger-sensecap-t1000-e-relay-italy.uf2` |
| Wio L1 OLED | `messenger-wio-l1-oled-italy.uf2` |
| Pager with SX1262 | `messenger-pager-sx1262-italy.factory.bin` |
| Pager with LR1121 | `messenger-pager-lr1121-italy.factory.bin` |

`dist/manifest.json` records SHA-256 checksums and flash addresses. Regenerate this folder after rebuilding all targets with `python scripts/package.py`; packaging checks the UF2's address range and validates each Pager factory image against its matching application, bootloader, and partition table. The Pager **factory** images are for address `0x0` when using an ESP32 flasher.

**Wio:** connect USB, double-press reset to enter its UF2 bootloader, then copy `.pio/build/wio_l1_oled/firmware.uf2` to the device's USB drive. This application starts at `0x27000` and expects the normal S140 7.3.0 installation. It does not install or replace a bootloader or SoftDevice. If the device does not expose its usual bootloader drive, use [Seeed's recovery instructions](https://wiki.seeedstudio.com/get_started_with_meshtastic_wio_tracker_l1/) before attempting a flash.

**Pager:** use the environment matching its chip:

```powershell
pio run -e pager_sx1262 -t upload --upload-port COM5
```

Replace `COM5` with its actual port, and replace the environment with `pager_lr1121` if appropriate. PlatformIO uploads the application and the required ESP32 boot/partition images. An application-only `firmware.bin` is not an image to flash at address zero. Follow [LILYGO's USB download-mode instructions](https://wiki.lilygo.cc/products/t-lora-series/t-lora-pager/quick-start.html) if automatic connection fails.

## SenseCAP T1000-E relay

Build `sensecap_t1000_e_relay` for the **T1000-E with nRF52840/LR1110 and the stock S140 7.3.0 UF2 bootloader**. This is not a T1000-A/B or a generic LoRaWAN image. The relay starts listening immediately; it has no naming screen or configuration menu. GPS, sensors and buzzer are disabled. A brief LED pulse every five seconds indicates the loop is running with a successfully initialized radio; fast blinking indicates initialization failure. USB serial at 115200 reports status every ten seconds. Battery runtime has not been measured.

**Power:** hold the T1000-E button for five seconds until the LED stays on, then release it. The relay finishes any active packet, sleeps its radio and enters nRF52840 System OFF. Press once to wake and restart listening. Queued packets are discarded and airtime credit starts empty on wake. A button held during startup must be released before another long press can switch off. USB reconnect may wake the hardware; battery and USB shutdown current still require measurement.

Update the Pager and Wio as well: the new `LMR2` radio packets are incompatible with the earlier firmware. Existing handheld names and stored history keep the same format.

To flash, connect the magnetic USB cable to your computer. Hold the device button while quickly reconnecting the magnetic end twice to enter the stock DFU drive, then copy `dist/messenger-sensecap-t1000-e-relay-italy.uf2` to that drive. See [Seeed's DFU instructions](https://wiki.seeedstudio.com/sensecap_t1000_e/#step-1-enter-dfu-mode) if the drive does not appear. This application-only UF2 does not replace the bootloader or SoftDevice. No automatic flash is performed by the build or packaging scripts.

The relay forwards messages and recipient ACKs, but never claims delivery itself. Packets carry a one-step forwarding limit, and duplicate attempts are suppressed. ACKs have queue priority and reserved slots. All transmissions share the airtime budget. No stored traffic is retransmitted after a relay restart. Place it where it can hear both handhelds; chaining two relays is deliberately unsupported in this version.

## Verification

The host test suite compiles the actual protocol and application code with fake devices:

```powershell
uv run --with ziglang==0.13.0 tests/run.py
```

It checks packet corruption/truncation, maximum lengths, malformed packets with valid checksums, history eviction and duplicate filtering, clock rollover, onboarding, interrupted saves, send/cancel/retry, delivery acknowledgements, incoming messages during composition, long-message paging, leaving the editor, and display sleep. See [docs/validation.md](docs/validation.md) for the actual build/test results and outstanding hardware checks.

If local policy prevents executing newly compiled programs, `tests/run.py --compile-only` checks compilation without executing the tests. This is not a substitute for a passing test run.

## Scope and radio profile

This version supports direct broadcast and one relay forwarding step, with bounded automatic retries. It has no encryption, authenticated names, GPS, or phone connection. History contains messages that the device actually received; it does not fetch messages sent while it was off. Battery runtime, sleep behavior, and low-battery handling are not yet validated for unattended use.

The fixed profile is 869.525 MHz, 125 kHz bandwidth, SF9, coding rate 4/5, 14 dBm chip output, eight-symbol preamble, explicit headers, normal IQ, sync word `0x12`, and packet CRC enabled. Firmware imposes a conservative 5% transmission schedule with an empty airtime budget after boot so rebooting cannot bypass it. Channel activity detection adds randomized backoff. There is no setting to override these limits.

The selected band lies within the EU 869.4–869.65 MHz non-specific short-range-device allocation. The harmonised conditions allow up to 500 mW ERP with applicable access restrictions, including a duty-cycle alternative of 10%; this implementation selects a lower chip output and a 5% schedule. Antenna gain and actual hardware emissions remain part of physical validation. Sources: [EU Decision 2025/105, band 54](https://eur-lex.europa.eu/eli/dec_impl/2025/105/oj/eng), [Meshtastic EU_868 reference](https://meshtastic.org/docs/configuration/radio/lora/).

Implementation and source references are in [docs/architecture.md](docs/architecture.md).
