# ESP32-S3 Frontend IDF

Firmware personale per frontend CNC basato su **ESP32-S3**.
Comunica via **ESP-NOW** con il modulo backend
[`ESP32-C3-Lahe_SpindleControl_ESPNOW_V2`](https://github.com/ennio64/ESP32-C3-Lahe_SpindleControl_ESPNOW_V2).

## Hardware

- **ESP32-S3** con **8 MB di flash** (16 MB supportati modificando `sdkconfig.defaults`)
- Display TFT (gestito tramite LovyanGFX)
- Tastiera I2C
- ADS1115 su I2C (lettura tensioni/correnti)

## Prerequisiti

- [ESP-IDF v6.1](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
- Python 3.11 (installato automaticamente da `install.ps1` di ESP-IDF)
- Su Windows: `Set-ExecutionPolicy RemoteSigned` (una tantum, per permettere `export.ps1`)

## Build

```powershell
# Attiva ESP-IDF
. C:\esp\v6.1\esp-idf\export.ps1

# Clona (solo la prima volta)
git clone https://github.com/ennio64/ESP32-S3-Frontend-IDF.git
cd ESP32-S3-Frontend-IDF

# Compila
idf.py set-target esp32s3
idf.py build

# Flash + monitor seriale
idf.py -p COM<N> flash monitor
