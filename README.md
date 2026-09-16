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

Al primo utilizzo su un nuovo PC:

```powershell
# 1. Clona il repository
git clone https://github.com/ennio64/ESP32-S3-Frontend-IDF.git
cd ESP32-S3-Frontend-IDF

# 2. Attiva l'ambiente ESP-IDF (una volta per sessione)
. C:\esp\v6.1\esp-idf\export.ps1

# 3. Configura il target (solo la prima volta)
idf.py set-target esp32s3

# 4. Compila
idf.py build
```

Per le build successive, dalla stessa cartella:

```powershell
. C:\esp\v6.1\esp-idf\export.ps1   # solo se hai aperto un nuovo terminale
idf.py build
```

Flash e monitor seriale:

```powershell
idf.py -p COM<N> flash monitor
```

Sostituisci `<N>` con la tua porta COM (es. `COM4`). Per uscire dal monitor: `Ctrl+]`.

## Workflow Git

### Modifiche quotidiane

Dopo aver modificato il codice sul PC di sviluppo:

```powershell
cd C:\ESP_IDF_Project\ESP32-S3-Frontend-IDF
git add .
git commit -m "Descrizione della modifica"
git push
```

### Lavorare da più PC

Se lavori su più computer, **prima di iniziare** sincronizza il repository locale con GitHub:

```powershell
git pull
```

E **prima di spegnere**, assicurati di aver pushato tutte le modifiche:

```powershell
git status     # deve dire "working tree clean"
git push
```

### Clonare su un nuovo PC

```powershell
git clone https://github.com/ennio64/ESP32-S3-Frontend-IDF.git
cd ESP32-S3-Frontend-IDF
. C:\esp\v6.1\esp-idf\export.ps1
idf.py set-target esp32s3
idf.py build
```

### Note

- **`sdkconfig`** (senza `.defaults`) è **escluso** dal versionamento: contiene solo valori generati.
  Le impostazioni essenziali sono in **`sdkconfig.defaults`**.
- **`build/`** e **`managed_components/`** sono escluse: si rigenerano al primo `idf.py build`.
- I file **`.bak`** e **`" - Copia"`** sono ignorati: Git fa già da storico, non servono copie manuali.
idf.py set-target esp32s3
idf.py build

## Flash + monitor seriale
idf.py -p COM<N> flash monitor
