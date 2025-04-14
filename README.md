# LOCO: Secure Localization and Awareness of Ambient IoT Devices

## Repository Structure

```
LOCO
├── 1_IoTDev
│   ├── 1_NXPBoard
│   ├── 2_ESP32-C3
│   └── 3_ESP32-C3_user_device
├── 2_ManufacturerServer
├── IoT_uwb
│   └── Drivers/API/Build_Platforms/nRF52840-DK/IoT_Uwb.emProject
├── user_uwb
│   └── Drivers/API/Build_Platforms/nRF52840-DK/user_uwb.emProject
├── keys
└── tamarin
```

## Hardware Requirements

The LOCO implementation has three components: an IoT device (𝐼𝑑𝑒𝑣), a user device (𝑈𝑑𝑒𝑣), and a manufacturer server (𝑀𝑠𝑣𝑟).

### IoT Device (𝐼𝑑𝑒𝑣)
- [NXP LPC55S69-EVK](https://www.nxp.com/design/software/development-software/mcuxpresso-software-and-tools-/lpcxpresso-boards/lpcxpresso55s69-development-board:LPC55S69-EVK) development board (based on ARM Cortex-M33 with TrustZone-M)
  - Runs at 150MHz with 640KB flash and 320KB SRAM
- [Quorvo QM33120WDK1](https://www.qorvo.com/products/p/QM33120) single-antenna UWB development board
  - Connected via UART2
- [ESP32-C3-DevKitC-02](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/hw-reference/esp32c3/user-guide-devkitc-02.html) Bluetooth board
  - Connected via UART4

### User Device (𝑈𝑑𝑒𝑣)
- Dual-antenna variant of [Quorvo QM33120WDK1](https://www.qorvo.com/products/p/QM33120)
  - Enables accurate PDoA-based AoA measurements beyond standard distance ranging
- [ESP32-C3-DevKitC-02](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/hw-reference/esp32c3/user-guide-devkitc-02.html)
  - Provides WiFi and Bluetooth connectivity

### Manufacturer Server (𝑀𝑠𝑣𝑟)
- Implementation Environment:
  - Ubuntu 20.04 LTS
  - Intel i5-11400 processor running at 2.6GHz
  - 16GB RAM

## Software Requirements

### NXP Board
1. IDE: [MCUXpresso IDE v11.6.1](https://www.nxp.com/design/software/development-software/mcuxpresso-software-and-tools-/mcuxpresso-integrated-development-environment-ide:MCUXpresso-IDE) (released on 2022-10-03)
2. SDK: v2.12.0 (released 2022-07-14)

SDK can be built using [MCUXpresso SDK Builder](https://mcuxpresso.nxp.com/en/welcome), or it can be downloaded via MCUXpresso IDE.
Note that the LOCO implementation on the NXP board is based on the secure_gpio example, provided by NXP.

#### Secure Configuration (TrustZone-M)
The following peripherals, memory regions (flash and RAM), and interrupts are configured as secure:
- Peripherals
  - CTimer2 - a secure timer for triggering announcements
  - FlexComm4 - a secure network peripheral for UART4
  - FlexComm2 - a secure network peripheral for UART2
  - HashCrypt - a hardware accelerator for SHA256
  - Casper - a hardware accelerator for ECDSA schemes
- Memory
  - RAM - 0x3000_0000 ~ 0x3002_FFFF, 192KB
  - Flash - 0x1000_0000 ~ 0x1003_FDFF, 260KB
- Interrupt
  - CTimer2
  - FlexComm4

For more details about how to use IDE, please refer to [MCUXpresso IDE User Guide](https://community.nxp.com/pwmxy87654/attachments/pwmxy87654/Layerscape/4742/1/MCUXpresso_IDE_User_Guide.pdf).

### ESP32-C3 Boards
- IDE: [VSCode Extension](https://github.com/espressif/vscode-esp-idf-extension/blob/master/docs/tutorial/install.md)
  - Espressif IDF extension



### Manufacturer Server (𝑀𝑠𝑣𝑟)
Implementation is in Python with the following dependency:
```
python3 -m pip install python-mbedtls
```

## Building and Running

### NXP Board
The project for the NXP board consists of two components, one for Normal world (non-secure) and one for Secure world. To build binaries:

1. Normal world binary:
   - Open the freertos_blinky_ns project
   - Click 'build' button in Quickstart panel or select 'Project' > 'Build Project'
   - Click 'LS' next to 'Debug your project' in Quickstart panel, then 'attach to a running target using LinkServer'

2. Secure world binary:
   - Open the freertos_blinky_s project
   - Click 'build' button in Quickstart panel or select 'Project' > 'Build Project'
   - Click 'Debug' on 'Debug your project' in Quickstart panel
   - If SWD Configuration pops up, select 'Device 0' and click 'OK'
   - The device will stop at the first line of main function for debugging; execute code by clicking 'Step Into' (F5), 'Step Over' (F6), or 'Resume' (F8)

**Important Notes:**
- Ensure the NXP board is properly connected to your development machine
- Verify that the NXP board is correctly wired to the ESP and UWB boards with the following connections:
  - NXP to ESP32-C3:
    - D15 on NXP → Pin 7 on ESP32-C3
    - D14 on NXP → Pin 6 on ESP32-C3
  - NXP to nRF (UWB board):
    - D0 on NXP → P0.06 on nRF
    - D1 on NXP → P0.08 on nRF
- The ESP board should be powered and running with the proper firmware
- The manufacturer server should be running before testing the full system

### UWB Boards
- IDE: [Segger Embedded Studio 8.22a](https://www.segger.com/products/development-tools/embedded-studio/)
- Project locations:
  - IoT device: `/IoT_uwb/Drivers/API/Build_Platforms/nRF52840-DK/IoT_Uwb.emProject`
  - User device: `/user_uwb/Drivers/API/Build_Platforms/nRF52840-DK/user_uwb.emProject`

### ESP32-C3 Boards
Using VSCode with the Espressif IDF extension:
1. Open the Command Palette (Ctrl+Shift+P)
2. Select 'ESP-IDF: Select port to use'
   - Choose the port for the ESP Board (typically /dev/ttyUSB* in Ubuntu)
   - Select the directory containing the ESP source code
3. Select 'ESP-IDF: Set Espressif Device Target'
   - Choose the ESP source code directory
   - Select 'esp32c3'
   - Select 'ESP32-C3 chip (via ESP-PROG)'
4. Use 'ESP-IDF: SDK Configuration editor (menuconfig)' to confirm UART settings
   - Port number: 1
   - Communication speed: 115200
   - RXD pin: 7
   - TXD pin: 6
5. Run 'ESP-IDF: Build your project'
6. Run 'ESP-IDF: Flash your project'

### Manufacturer Server
To run the manufacturer server:
```
python3 2_ManufacturerServer/ttp_time_srv.py
```

If you encounter a "address already in use" error, check if the software is already running or change the port number in the file.

## Tamarin Security Models

The security protocol of LOCO has been formally verified using the [Tamarin Prover](https://tamarin-prover.github.io/). The formal models are available in the `tamarin` folder:

```
tamarin/
├── key_exchange.spthy    # Model for key exchange protocol
└── message_announcement.spthy  # Model for secure message announcement
```

These models can be verified using the Tamarin Prover to ensure the security properties of the LOCO protocols.# LOCO: Secure Localization and Awareness of Ambient IoT Devices
