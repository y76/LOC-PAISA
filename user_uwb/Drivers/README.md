# DW3XXX (QM33) Application Programming Interface (API)

## Folders structure

  - API/Shared/dwt_uwb_driver

    Driver for DW3XXX UWB transceiver ICs. Details about each function can
    be found in DW3XXX API Guide.

  - API/Src/examples

    A set of individual (simple) examples showing how to achieve different
    functionalities, e.g. sending a frame, receiving a frame, putting the
    DW3XXX to sleep, two-way ranging.  The emphasis of theses examples is
    to be readable with explanatory notes to explain and show how the main
    features of the DW3XXX work and can be used.

  - API/Build_Platforms/STM_Nucleo_F429

    Hardware abstraction layer (system start-up code and peripheral
    drivers) for ARM Cortex-M and ST STM32 F1 processors. Provided by ST
    Microelectronics and platform dependant implementation of low-level 
    features (IT management, mutex, sleep, etc) for STM_Nucleo_F429.

  - API/Build_Platforms/nRF52840-DK

    Hardware abstraction layer (system start-up code and peripheral
    drivers) for ARM Cortex-M and nRF52840 processors. Provided by 
    Nordic Semiconductor and platform dependant implementation of low-level 
    features (IT management, mutex, sleep, etc) for nRF52840-DK.

Please refer to DW3XXX API Guide accompanying this package for more details
about provided API and examples.

NOTE: The DW3XXX API/driver code included in this package is an unbundled
      version of the DW3XXX API/driver. This version may be different to
      (generally by being newer than) those bundled with Decawave's other
      products. This particular release covers the DW3XXX hardware.

## Release Notes 

See [Changelog](Changelog.md)