# SawBladeCabinet-node

This **saw blade cabinet node** is employed at MakerSpace Leiden to manage the two doors of the saw blade cabinet. This node is responsible for controlling the locks on the doors, providing access to various saw blades.

## Features
- The saw blade cabinet comprises two doors, each granting access to different sets of saw blades.
- The outer door lock allows access to a selection of saw blades, while an additional inner door provides access to more options.
- Door locks are exclusively unlocked for MakerSpace members with the necessary permissions. Note that not all members authorized to unlock the outer door have permission for the inner door.
- When the outer door lock opens, a buzzer emits a buzzing sound until the lock closes.

## Contents

- **KiCad_files:** KiCad schematic of the wiring between various modules.
- **Photos:** Images showcasing the Node.
- **PlatformIO_Files:** Source code for the Node, stored as a complete PlatformIO project. PlatformIO is an extension of Visual Studio Code.
- **Schematics:** Schematic wiring diagrams for the different modules used.

## Build

This project is compiled against the ACNode Arduino library available at [MakerSpaceLeiden/nodevoordeurnw](https://github.com/MakerSpaceLeiden/nodevoordeurnw/tree/main/PlatformIO_Files/NodeVoordeur/lib/ACNode). Please refer to its [README.md](PlatformIO_Files/sawblade-node/lib/ACNode/README.md) for additional information.

### Notes:

**Bug Fix in ACNode.cpp:**
   There appears to be a bug in `ACNode.cpp` (ACNode::handle_cmd:)

   ```cpp
   if ((bc - _lastSwipe) > 1) {
   ```
   
   Should be:
   ```cpp
   if ((bc - _lastSwipe) > 10) {
   ```


### Configuration:
In the 'src' directory of the project, add a file named 'passwd.h' with the following content for WiFi connection. Replace your WiFi and OTA credentials.

```cpp
#pragma once

#define WIFI_NETWORK "my wifi network"
#define WIFI_PASSWD "my wifi password"

#define OTA_PASSWD "my OTA password"
```

