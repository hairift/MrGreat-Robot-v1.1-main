# Panduan Servo dan Tombol Mr Great

Dokumen ini menjelaskan konfigurasi servo, tombol manual, dan aturan gerakan pada robot Mr Great.

## Pin Servo

| Aktuator | GPIO | Fungsi | Catatan |
|---|---|---|---|
| Kepala | `GPIO17` | Gerakan kiri dan kanan | Tidak dipakai untuk angguk naik turun |
| Tangan kanan | `GPIO38` | Gerakan tangan kanan | Tidak boleh tertukar dengan tangan kiri |
| Tangan kiri | `GPIO39` | Gerakan tangan kiri | Arah servo sudah disesuaikan di firmware |

Semua servo diberi daya dari jalur 5V yang stabil. Ground servo wajib tersambung dengan ground ESP32-S3 agar sinyal PWM terbaca benar.

## Tombol Manual

Tombol tactile 4 kaki dipasang sebagai tombol manual aplikasi, bukan tombol BOOT download mode.

| Kaki tombol | Koneksi |
|---|---|
| Sisi pertama | `GPIO1` |
| Sisi seberang | GND |

Firmware memakai pull-up internal, sehingga tombol aktif saat ditekan ke GND. Pada tactile 4 kaki, dua kaki pada satu sisi biasanya sudah tersambung permanen. Jangan memakai `kiri atas` + `kiri bawah` atau `kanan atas` + `kanan bawah` jika keduanya satu rail. Gunakan dua kaki yang berbeda sisi, misalnya `kiri atas` + `kanan atas`, `kiri bawah` + `kanan bawah`, atau diagonal. Cek continuity dengan multimeter agar `GPIO1` hanya tersambung ke GND saat tombol ditekan.

## Kecepatan Servo

Gerakan servo dihaluskan dengan interpolasi sekitar 20 ms per frame. Durasi tiap aksi tetap mengikuti urutan gerakan, tetapi servo tidak langsung meloncat ke sudut tujuan sehingga gerakan terasa lebih natural dan tidak terlalu cepat.

## Perintah Servo

Perintah yang didukung:

- `hidupkan servo`
- `matikan servo`
- `reset posisi`
- `lihat kanan`
- `lihat kiri`
- `lihat depan`
- `angkat tangan kanan`
- `angkat tangan kiri`
- `turunkan tangan kanan`
- `turunkan tangan kiri`
- `lambaikan tangan kanan`
- `lambaikan tangan kiri`
- `angkat kedua tangan`
- `salam`
- `hormat`
- `tepuk tangan`
- `menari`

## Aturan Gerakan

- Jika pengguna meminta tangan kanan, hanya tangan kanan yang bergerak.
- Jika pengguna meminta tangan kiri, hanya tangan kiri yang bergerak.
- Gerakan emosi otomatis tidak boleh mengambil alih saat ada perintah manual yang sedang berjalan.
- Setelah gerakan manual selesai, posisi robot kembali aman tanpa menggerakkan seluruh badan secara tidak perlu.

## Alat MCP Servo

Firmware mendaftarkan alat MCP berikut:

```text
self.servo.power
self.servo.action
```

Gunakan `self.servo.power` untuk mengaktifkan atau mematikan servo. Gunakan `self.servo.action` untuk menjalankan aksi seperti lambaian, salam, hormat, atau reset.

## Build dan Uji

```powershell
cd E:\MrGreat-IOT-ESP32\MrGreat-esp32-main
idf.py build
idf.py -p COM3 flash
idf.py -p COM3 monitor
```

Pastikan setelah boot tidak ada log panic, brownout, atau reboot loop. Jika servo bergerak tidak stabil, periksa catu daya 5V dan ground bersama terlebih dahulu.
