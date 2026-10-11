# Προβλήματα και λύσεις

| Σύμπτωμα | Πιθανή αιτία και λύση |
|---|---|
| Compile: `secrets.h is missing` | Αντίγραψε `Mataki/secrets.example.h` σε `Mataki/secrets.h`. |
| Compile: errors στο `lvgl` ή στο `lv_conf.h` | Λάθος έκδοση lvgl (χρειάζεται 8.3.x) ή το `lv_conf.h` δεν είναι στον φάκελο `libraries` δίπλα στο `lvgl`. |
| Compile: errors στο `esp_lcd_rgb_panel_config_t` (π.χ. `psram_trans_align`) | Πακέτο esp32 νεότερο από 3.1.x. Εγκατέστησε την 3.1.x. |
| Compile: «sketch too big» | Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS). |
| Out of memory / «set Tools > PSRAM» | Tools, PSRAM, **OPI PSRAM**. |
| Συνεχείς επανεκκινήσεις με μήνυμα PSRAM | Η πλακέτα δεν έχει OPI PSRAM. Δοκίμασε PSRAM: QSPI PSRAM. Έλεγξε και το `esptool flash_id`. |
| Μαύρη οθόνη, το Serial Monitor δουλεύει | Ο backlight ελέγχεται από τον CH422G. Έλεγξε ότι δεν υπάρχει μήνυμα `RGB panel init failed`. |
| Δεν βλέπω μηνύματα στο Serial Monitor | Tools, USB CDC On Boot: Enabled, και ταχύτητα 115200. |
| Η εικόνα τρεμοπαίζει ή έχει σχισμές | Κατέβασε το `pclk_hz` στο `board.cpp` (π.χ. 12 MHz) ή μείωσε την ανάλυση της κάμερας. |
| Λάθος χρώματα | Άλλαξε το `JPEG_BIG_ENDIAN` στο `mataki_config.h`. |
| «Camera rejected the login (401)» | Λάθος όνομα ή κωδικός. Αν είναι σωστά και παίρνεις πάλι 401, η κάμερα μάλλον θέλει Digest login (το Mataki υποστηρίζει μόνο Basic)· πες το σε όποιον κάνει το debug να προστεθεί. |
| «Camera not reachable» | Λάθος IP, η κάμερα είναι σβηστή ή σε άλλο δίκτυο. Δοκίμασε την ίδια διεύθυνση από τον υπολογιστή. |
| «Chunked stream not supported» | Η κάμερα στέλνει το stream σε chunked μορφή. Δοκίμασε άλλο mode/URL της κάμερας ή ζήτησε προσθήκη υποστήριξης. |
| «JPEG decode failed» συνεχώς | Δοκίμασε χαμηλότερη ανάλυση ή baseline JPEG στην κάμερα. |
| Χαμηλά fps | Μείωσε την ανάλυση και την ποιότητα JPEG στην κάμερα, βεβαιώσου ότι η πλακέτα έχει καλό σήμα Wi-Fi 2.4 GHz. |
| Η αφή δεν αντιδρά | Στο Serial Monitor δες `touch address`. Αν είναι `0x00`, ο GT911 δεν βρέθηκε. |
| Τα αγγίγματα πέφτουν σε λάθος σημείο | Ανταλλαγή ή αναστροφή των x/y στο `board_touch_read` (`board.cpp`). |
