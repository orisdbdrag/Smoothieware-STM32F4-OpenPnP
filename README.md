# Smoothieware STM32F4 Low-Latency Fork for High-Speed OpenPnP (MKS Monster8)

A high-performance port of **Smoothieware v1** for the **STM32F407VET6/VGT6** microcontroller (tailored for **MKS Monster8 V2**), engineered for high-speed OpenPnP SMT machines.

## Lineage & Acknowledgments
* **Base STM32 Port:** `janm012012/Smoothieware-CHMT`
* **Motion Concepts Backport (`feature/best-for-pnp`):** `markmaker/Smoothieware`
* **Original Firmware:** `Smoothieware/Smoothieware`

---

## Measured Cold-Start Latency Benchmarks
Hardware measurements via a 100 MHz logic analyzer (time from final UART stop-bit / `M400` completion to the first `STEP` pulse):

| Controller Platform | Interface | Cold-Start Latency |
| :--- | :--- | :--- |
| **Duet 3 6HC (RepRapFirmware)** | USB / UART / Ethernet | ~35–40 ms |
| **grblHAL (STM32H7)** | USB / UART | ~4.0 ms |
| **Stock Smoothieboard v1 (LPC1769)** | USB VCP | ~3.0 ms |
| **This Firmware (STM32F407 @ 168 MHz)** | **Hardware DMA UART + RTS/CTS** | **~65–68 µs** |

---

## Key Architectural & Low-Level Optimizations

1. **Single-Pass G-code Parsing & CCM RAM Caching:** Parses G-code once upon object creation and stores parameters in zero-wait-state **Core Coupled Memory (CCM RAM)**, eliminating repetitive string scanning and reclaiming ~10% CPU cycles.
2. **Bypassing StepTicker Standstill Overhead:** Primes the Bresenham accumulator immediately on startup so the first `STEP` fires on the very first timer tick.
3. **Deterministic 20 µs DIR-to-Step Hardware Guard:** Uses a non-blocking ISR check to enforce a guaranteed **~20 µs** direction setup window without delay blocks.
4. **Early Motion Gate (`Conveyor::check_queue`):** Gating logic (`blocks_in_queue >= 2 || force`) starts motion immediately after the first two blocks while remaining segments stream via DMA.
5. **Bare-Metal `bxCAN` Driver (Up to 1 Mbps):** Bypasses HAL overhead to control an STM32G0 toolhead node (`M777` for vacuum/valves/lighting) and poll X/Z endstops. Toolhead firmware repository: [CAN_HEAD_BLOW_STM32](https://github.com/orisdbdrag/CAN_HEAD_BLOW_STM32).
6. **Hardware Servo "Position Complete" Sync:** Routes servo *In-Position* signals to endstops for instant settling on `M400`, offloading 3rd-order S-curve filtering to drives (e.g., Panasonic Minas A6) while stripping unused modules.
7. **Reworked Watchdog & Workflow Stability Tweaks:** Includes a reworked hardware Watchdog (WDT) implementation alongside various local optimizations for execution speed and operational stability. Most custom modifications are directly annotated with comments inside the source code.

---

## Host Communication & Hardware Flow Control (`UART0`)

Host communication operates exclusively via hardware **`UART0`** (`PA9` TX / `PA10` RX). At high baud rates (up to 4 Mbps), enabling hardware flow control (`mode 1` or `mode 2`) is strongly recommended to prevent RX buffer overflows during dense G-code streaming.

### Configuration Parameters (`src/config.default`)
```text
uart0.baud_rate                              4000000          # High-speed UART baud rate for smooth controller feedback
rts_cts_handshake                            2                # Hardware flow control mode:
                                                              # 0: no RTS/CTS handshake
                                                              # 1: RTS (PA12) / CTS (PA11)
                                                              # 2: RTS (PA5) only
```

### Flow Control Pinout & Author's Setup
* **Mode 1 (`PA12` RTS / `PA11` CTS):** Uses the default hardware USB D+/D- pins. Since USB VCP is disabled in this firmware, `PA11` and `PA12` are repurposed as dedicated hardware flow control lines.
* **Mode 2 (`PA5` RTS only — Used by Author):** Connects only the `RTS` line (`PA5`) to regulate incoming data from the host PC. Leaving `CTS` unconnected is completely safe here because the controller only transmits short responses (`ok` and current coordinates), which will never overflow the host PC's receive buffer.

---

## Hardware Architecture & Limitations

### Target Setup
* **Main Board & Expansion Shield:** MKS Monster8 V2 (`STM32F407VET6` / `VGT6`) paired with a custom expansion shield (`74ACT244` 5V buffers, dual-transceiver RS422 interface with hardware flow control, and fast `AON7296` MOSFETs). Schematics, EasyEDA source files, PCB layout, and `PA5` jumper instructions are available in the **[`Hardware/`](Hardware/)** directory.
* **Host Interface:** Deterministic hardware TTL UART with RTS/CTS flow control (tested at 4 Mbps via differential RS422 transceivers and EXAR/ASIX PCIe UART cards).

### Important Limitations vs. Stock Smoothieware
* **No USB VCP or Ethernet:** Exclusively uses hardware `UART0` with DMA and RTS/CTS to eliminate polling jitter.
* **No UART Stepper Driver Control:** Software UART configuration for TMC stepper drivers is omitted. If installing StepStick drivers directly onto the MKS Monster8 board, motor current must be adjusted manually via the driver's onboard potentiometer. *(Note: The author's setup uses external servo drives for the XYZ axes and external stepper drivers for the rotary A/B axes).*
* **Compile-Time Configuration:** Configuration is set in **`src/config.default`** prior to compiling rather than read from an SD card.
* **Firmware Updates:** Flashed via the MKS Monster8 SD card bootloader, SWD programmer, or directly over USB in **DFU mode**.

> [!WARNING]
> **Status LED (`PC15`) Conflict with Onboard X/Y Stepper Drivers:**
> By default, the firmware uses **`PC15`** as the primary Status LED. On the **MKS Monster8 V2** board, **`PC15`** is wired directly to the **`ENABLE`** pin of the onboard **X and Y stepper driver sockets** (`ENABLE_XY`). Leaving `PC15` assigned to the status LED while using onboard StepStick drivers will cause erratic X/Y motor enabling/disabling.
>
> If you plan to install stepper drivers directly onto the MKS Monster8 board, resolve this conflict using one of the following options:
> 1. **Reassign the Status LED Pin (Recommended):** Open **`src/main.c`** (lines 96–97, `GPIO leds[] = { GPIO(PC_15), ... }`) and replace `PC_15` with an unused pin—for example, **`PC_4`** (connected to the onboard LED next to the SD card slot, which works as a status indicator whenever no SD card is inserted) or an unused MOSFET power output pin that has an onboard indicator LED.
> 2. **Disable Status LEDs:** Set **`leds_disable true`** in **`src/config.default`**.

---

## Building & Flashing (Windows)

### 1. Install the ARM Toolchain
Run **`win_install.cmd`** in the root directory to download and extract the toolchain and generate **`BuildShell.cmd`**.

### 2. Compile the Firmware
1. Double-click **`BuildShell.cmd`**.
2. Edit **`src/config.default`** for your machine parameters.
3. Build the 5-axis target (`STM32F407xG`):
   ```bash
   make clean
   make AXIS=5 PAXIS=5 SHELL=cmd.exe
   ```
   *(Optional)* To disable the CAN module, either set `can_bridge.enable false` in `src/config.default`, or exclude it entirely at compile time by passing `NOCAN=1`:
   ```bash
   make clean
   make AXIS=5 PAXIS=5 NOCAN=1 SHELL=cmd.exe
   ```
4. The binary outputs to **`STM32F407xG/firmware.bin`**.

### 3. Flash the Firmware

> [!IMPORTANT]
> **Stock Bootloader & Offset Requirement:**
> The firmware starts at flash offset **`0x0800C000`** and **requires the stock MKS Monster8 V2 bootloader to be present in the first 48 KB of flash memory** (`0x08000000`–`0x0800BFFF`). Without the bootloader, the MCU will not jump to the firmware on startup.
>
> *If the stock bootloader was previously erased, you can restore it in USB DFU mode by running **`DFU-Upload/DFU-Upload-Bootloader.bat`**.*

Once the bootloader is in place, flash **`STM32F407xG/firmware.bin`** using any of the following three methods:

1. **Via USB DFU (Recommended):**
   * Put the MKS Monster8 into DFU mode (hold `BOOT0` and press `RESET`).
   * Verify WinUSB drivers via Zadig if necessary (`DFU-Upload/zadig-2.4.exe`).
   * Run **`DFU_firmware.bat`** in the root directory, which automatically flashes `STM32F407xG\firmware.bin` to the `0x0800C000` offset using `dfu-util`.
2. **Via SD Card:**
   * Copy **`firmware.bin`** to the root directory of a FAT32-formatted SD card, insert it into the board, and press `RESET`. The stock bootloader will flash the firmware automatically.
3. **Via SWD Programmer (ST-Link / J-Link):**
   * Connect to the SWD header and flash `STM32F407xG/firmware.bin` specifying the start offset address **`0x0800C000`** *(make sure not to perform a full-chip erase that wipes the bootloader region)*.

---

## License & Disclaimer
Released under the **GNU General Public License v3.0 (GPLv3)**. Provided **"as is"** for custom high-speed SMT machinery—use at your own risk.
