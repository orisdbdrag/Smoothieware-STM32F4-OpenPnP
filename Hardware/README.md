# MKS Monster8 V2 Custom Expansion Shield

Custom expansion shield designed for the **MKS Monster8 V2** mainboard to interface with industrial servo drives, high-speed actuators, and a differential host link.

## Key Features
* **5V Logic Level Shifters:** `74ACT244` high-speed line buffers converting 3.3V MCU `STEP` / `DIR` / `ENABLE` signals to clean 5V differential/single-ended drive inputs.
* **Isolated/Differential RS422 Host Interface:** Dual high-speed transceivers (`MAX3491` / dual-channel architecture) providing dedicated differential pairs for `RX`/`TX` and hardware `RTS`/`CTS` flow control (tested at 4 Mbps).
* **High-Speed Power Switches:** Fast `AON7296` MOSFET outputs for direct control of vacuum solenoid valves, blow-off valves, and camera illumination.

## Included Files
* `Schematic_MOSTER_SHIELD.pdf` — Full electrical schematic in PDF format.
* `Easy_EDA_SCH_MOSTER_SHIELD.json` — Editable schematic source file (EasyEDA Standard).
* `Easy_EDA_PCB_MOSTER_SHIELD.json` — Editable PCB layout source file (EasyEDA Standard).
* `PCB.png` — Top/bottom PCB routing preview.

## Hardware Modification for `RTS` Only (`Mode 2`)

If you configure **`rts_cts_handshake 2`** (`RTS` only on pin **`PA5`**, as used in the author's default configuration), solder a thin jumper wire from the `RTS` transceiver input to **`PA5`** on the shield header as shown below:

![PA5 RTS Jumper Wire](rts_pa5_jumper.jpg)
