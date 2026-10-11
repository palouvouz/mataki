# Build και upload (Arduino IDE)

> Το όνομα του αρχείου είναι σταθερό για όλα τα projects. Το Mataki είναι firmware για ESP32-S3, άρα δεν υπάρχει APK.

## 1. Πακέτο πλακετών
Arduino IDE 2, Boards Manager, ψάξε «esp32» (Espressif Systems) και εγκατάστησε την έκδοση **3.1.x**. Οι νεότερες εκδόσεις αλλάζουν το API της οθόνης RGB και μπορεί να μη γίνει compile.

## 2. Βιβλιοθήκες (Library Manager)
- **lvgl** έκδοση **8.3.11** (ή άλλη 8.3.x). Όχι 9.x.
- **JPEGDEC** (bitbank2), τελευταία έκδοση.

## 3. Αρχείο ρυθμίσεων LVGL
Αντίγραψε το `arduino-libraries/lv_conf.h` στον φάκελο `libraries` του Arduino, **δίπλα** στον φάκελο `lvgl` (όχι μέσα του). Συνήθως: `Documents/Arduino/libraries/`.

## 4. Μυστικά και ρυθμίσεις
Στον φάκελο `Mataki`: αντίγραψε το `secrets.example.h` σε `secrets.h` και συμπλήρωσε `WIFI_SSID`, `WIFI_PASSWORD`, `CAMERA_USER`, `CAMERA_PASSWORD`. Αν η κάμερα δεν είναι στο `192.168.1.47`, άλλαξε το `CAMERA_HOST` στο `mataki_config.h`.

## 5. Tools (σημαντικό)
| Ρύθμιση | Τιμή |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| Flash Size | 8MB (ή 16MB αν το επιβεβαίωσες με `esptool flash_id`) |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| PSRAM | **OPI PSRAM** |
| Flash Mode | QIO 80MHz |
| CPU Frequency | 240MHz |
| Upload Speed | 921600 |

## 6. Upload
Άνοιξε το `Mataki/Mataki.ino`, διάλεξε τη θύρα και πάτα Upload. Άνοιξε το Serial Monitor στα 115200 για μηνύματα.
Αν δεν μπαίνει σε flash mode: κράτα πατημένο το BOOT, πάτα RESET, άφησε το BOOT.
