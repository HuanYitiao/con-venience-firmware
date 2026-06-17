# con-venience-firmware
[![Build Firmware](https://github.com/HuanYitiao/con-venience-firmware/actions/workflows/build.yml/badge.svg)](https://github.com/HuanYitiao/con-venience-firmware/actions/workflows/build.yml)
![Platform](https://img.shields.io/badge/platform-ESP32--c6-blue)
![Framework](https://img.shields.io/badge/framework-Arduino%20%7C%20PlatformIO-orange)
![License](https://img.shields.io/badge/license-MIT-green)

Firmware for the con-venience wearable social device, built for ESP32-C6 with Arduino framework via PlatformIO.

Designed for fursuiters — two wearers touch devices to exchange contact information.

## Hardware

- ESP32-C6-DevKitC-1
- 2.42" SSD1309 OLED (128×64)
- MicroSD card storage

## Status

- ACOM physical contact → BLE MAC exchange
- BLE bidirectional profile transfer
- Production display upgrade (SSD1309 → ST75256)

## Build

Requires PlatformIO. Open this repository as the workspace root in VS Code.

## License

[MIT](LICENSE)