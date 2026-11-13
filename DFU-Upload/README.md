# MKS-Monster8-DFU

- **Upload bootloader**
  - Press and hold the `BOOT0` button when there is no power (or hold `BOOT0` and press `RESET`)
  - Connect USB to PC to enter DFU mode
  - Run: `DFU-Upload-Bootloader.bat`

- **Update firmware**
  - Press and hold the `BOOT0` button when there is no power (or hold `BOOT0` and press `RESET`)
  - Connect USB to PC to enter DFU mode
  - Verify WinUSB drivers via `zadig-2.4.exe` if necessary
  - Run `DFU_firmware.bat` located in the **root directory** of the repository — it will automatically flash `STM32F407xG\firmware.bin` to the `0x0800C000` offset using `dfu-util`

- **Tips:** Do not modify the bootloader (`mks_monster8_Bootloader.bin`) or compiled firmware (`STM32F407xG\firmware.bin`) file names/paths; otherwise, the batch scripts will not find them.


- More information refer to: https://github.com/makerbase-mks/MKS-Monster8/wiki/DFU_upload