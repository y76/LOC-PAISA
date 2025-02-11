:orphan:

Release folder structure:

    ├─── README.md                                                              <--- Current file - it describes the folder structure
    ├─── Drivers                                                                <--- Drivers APIs to interact directly with the UWB transceiver
    │    ├── API
    │    │   ├── Build_Platforms                                                <--- Projects using the drivers for a specific hardware platform
    │    │   │   └── ...
    │    │   ├── Shared                                                         <--- Drivers source code
    │    │   │   └── ...
    │    │   └── Src                                                            <--- Drivers simple examples (including ranging)
    │    │       └── ...
    │    ├── LICENSES
    │    │   └── ...
    │    ├── Changelog.md
    │    ├── QM33XXX_DW3XXX_Software_API_Guide-<x>p<y>.pdf
    │    └── README.md
    └─── SDK                                                                    <--- SDK main directory
         ├─── Binaries                                                          <--- Prebuilt binary files ready to be flashed
         │    ├── <target_1>
         │    │    ├── <target_1>-DW3_QM33_SDK_CLI-<OS>.hex
         │    │    └── <target_1>-DW3_QM33_SDK_UCI-<OS>.hex
         │    └── <target_2>
         │         └── ...
         ├─── Documentation                                                     <--- All the documentation for this SDK
         │          ├─── Quick Start Guide                                      <--- Quick Start Guides for each target
         │          │    ├── <target_1>_Quick_Start_Guide_QM33SDK-<x.x.x>.pdf
         │          │    ├── <target_2>_Quick_Start_Guide_QM33SDK-<x.x.x>.pdf
         │          │    └── ...
         │          ├─── Developer Manual                                       <--- Developer Manuals for each target
         │          │    ├── <target_1>_Developer_Manual_QM33SDK-<x.x.x>.pdf
         │          │    ├── <target_2>_Developer_Manual_QM33SDK-<x.x.x>.pdf
         │          │    └── ...
         │          ├─── Drivers                                                <--- Drivers APIs documentation to interact directly with the UWB transceiver
         │          │    └── QM33XXX_DW3XXX_Software_API_Guide-<x>p<y>.pdf
         │          └─── uwb-stack                                              <--- UWB stack documentation
         │               ├── uwb-fira-protocol-R<x.x.x-y>.pdf
         │               ├── uwb-l1-api-R<x.x.x-y>.pdf
         │               ├── uwb-l1-configuration-R<x.x.x-y>.pdf
         │               ├── uwb-qhal-api-R<x.x.x-y>.pdf
         │               ├── uwb-qosal-api-R<x.x.x-y>.pdf
         │               ├── uwb-qplatform-api-R<x.x.x-y>.pdf
         │               ├── uwb-uci-messages-api-R<x.x.x-y>.pdf
         │               └── uwb-uwbmac-api-R<x.x.x-y>.pdf
         ├─── Firmware                                                          <--- SDK source code files and CMake projects
         │    ├── README.md
         │    └── DW3_QM33_SDK_<x.x.x>.zip
         ├─── Tools                                                             <--- Additional tools to interact with Qorvo's UWB devices
         │    ├── GUI                                                           <--- Qorvo One GUI to demonstrate two-way ranging capabilities
         │    │    ├── linux
         │    │    │   └── QorvoOneTWR-<x.x.x>-x86_64-install.sh
         │    │    ├── macOS
         │    │    │   └── QorvoOneTWR-<x.x.x>.dmg
         │    │    └── Windows
         │    │        └── QorvoOneTWR-<x.x.x>-setup.exe
         │    └── uwb-qorvo-tools                                               <--- Python scripts for more complex use cases
         │        └── ...
         └─── Release_Notes.pdf
