# Daftar Validasi Perangkat Keras Produksi

Dokumen ini dipakai sebelum firmware Mr Great dinyatakan siap demo atau produksi.

## Firmware

- `idf.py build` selesai tanpa error.
- Ukuran `xiaozhi.bin` masih lebih kecil dari partisi aplikasi.
- `generated_assets.bin` berhasil dibuat dan berisi aset bahasa Indonesia serta Inggris.
- Tidak ada reboot loop setelah flash.

## Audio

- INMP441 memakai `GPIO4`, `GPIO5`, dan `GPIO6`.
- MAX98357 memakai `GPIO7`, `GPIO15`, dan `GPIO16`.
- File OGG di `main/assets/common` dan `main/assets/locales/id-ID` dapat diputar tanpa patah pada kondisi jaringan normal.
- Jika suara TTS patah saat jaringan buruk, cek kualitas Wi-Fi, antrean audio, dan log MQTT.

## Layar dan Emoji

- OLED memakai `GPIO10` untuk SDA dan `GPIO9` untuk SCL.
- Ikon non-emosi seperti `microchip_ai` tetap memakai Font Awesome.
- Emosi wajah memakai file 64-bit dari `components/78__xiaozhi-fonts/src/emoji`.
- Ikon Wi-Fi dan baterai tetap berasal dari Font Awesome.

## Servo

- Kepala memakai `GPIO17`.
- Tangan kanan memakai `GPIO38`.
- Tangan kiri memakai `GPIO39`.
- Servo mendapat 5V stabil dan ground bersama dengan ESP32-S3.
- Perintah kanan dan kiri tidak tertukar.

## Tombol

- Tombol manual memakai `GPIO1` ke GND.
- Pull-up internal aktif.
- Menekan tombol tidak menyebabkan restart.
- Untuk tombol tactile 4 kaki, `GPIO1` dan GND harus berada di dua sisi atau rail yang berbeda, bukan `kiri atas` + `kiri bawah` atau `kanan atas` + `kanan bawah` jika pasangan itu sudah tersambung permanen.

## USB Type-C dan Charging

- USB breakout `V` dan `G` masuk ke TP4056 untuk charging.
- USB breakout `D+` masuk ke `GPIO20`.
- USB breakout `D-` masuk ke `GPIO19`.
- `D+` dan `D-` hanya jalur data, bukan jalur charging atau jalur power utama.
- `D+` dan `D-` tidak bisa menyalakan ESP32. ESP32 tetap harus diberi daya dari baterai, boost, atau jalur 5V yang aman.
- Jika komputer tidak mendeteksi ESP32-S3, cek kabel data, driver, ground bersama, dan konfigurasi USB Serial/JTAG.
- Untuk flash lewat USB native, komputer harus menampilkan port COM USB Serial/JTAG. Jika tidak muncul, tahan BOOT `GPIO0` ke GND saat reset atau power dinyalakan untuk masuk download mode.

## Baterai

- Pembagi tegangan memakai dua resistor `100 kOhm`.
- Kapasitor `104` atau `100 nF` dipasang dari node ADC ke GND.
- Node ADC masuk ke `GPIO2`.
- Titik ukur sebaiknya dari baterai atau output TP4056, bukan output 5V MT3608.

## Data Kampus

- Pertanyaan kampus harus memprioritaskan `main/campus_data.cc`.
- Jika Campus RAG Server aktif, firmware memakai server terlebih dahulu lalu fallback ke data lokal.
- Jawaban tidak boleh menyatakan data tidak ada jika entri tersedia di dataset.

## Keputusan Rilis

Firmware layak dipakai jika seluruh poin di atas lulus dan tidak ada error, panic, brownout, atau regresi fitur utama.
