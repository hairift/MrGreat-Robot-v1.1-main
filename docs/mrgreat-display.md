# Layar dan Emoji Kustom Mr Great

## Layar

Mr Great memakai OLED SSD1306 0,96 inci beresolusi 128 kali 64 lewat I2C.

| Pengaturan | Nilai |
|---|---|
| SDA | `GPIO10` |
| SCL | `GPIO9` |
| Alamat I2C | `0x3C` |
| Kecepatan | 400 kHz |
| Cermin X dan Y | menyala |

Pompa muatan OLED perlu waktu stabil setelah daya menyala, jadi firmware
menunggu 500 milidetik sebelum memasang driver, lalu mencoba inisialisasi
sampai tiga kali. Bila ketiganya gagal, robot beralih ke tampilan kosong dan
tetap berfungsi penuh, karena layar bukan syarat untuk menjawab suara.

## Tata Letak

Layar 128 kali 64 memakai tata letak `SetupUI_128x64`:

```text
+--------------------------------------------------+
| bilah status: jaringan, notifikasi, baterai      |
+----------+---------------------------------------+
| emosi    | pesan percakapan                      |
| 48 x 48  | lebar layar dikurangi 48              |
+----------+---------------------------------------+
```

Ruang emosi dilebarkan menjadi 48 piksel, dari 32 piksel bawaan, supaya gambar
emoji 64 piksel muat setelah diperkecil. Lebar label percakapan ikut menyesuaikan
menjadi lebar layar dikurangi 48, kalau tidak teksnya akan tertimpa gambar.

## Emoji Kustom

Arsitektur firmware terbaru menampilkan emosi memakai font emoji. Mr Great tetap
memakai gambar berwarna 64 piksel miliknya sendiri, jadi urutan pemilihannya
diubah menjadi:

1. Gambar emoji kustom, bila nama emosinya ada di koleksi.
2. Font emoji bawaan.
3. Font ikon bawaan, untuk nama yang bukan ekspresi wajah.
4. Ikon robot, sebagai jalan terakhir.

Gambar diperkecil dengan skala 176 dari 256, yang mengubah 64 piksel menjadi
44 piksel dan menyisakan pinggiran dua piksel di dalam wadah 48 piksel.

### Letak Berkas

Gambar emoji berada di `components/mrgreat_emoji/src`, bukan menimpa komponen
font bawaan di `managed_components/78__xiaozhi-fonts`. Alasannya sederhana:
komponen bawaan dikelola oleh pengelola dependensi dan dapat ditulis ulang
kapan saja saat dependensi diperbarui, sehingga gambar kustom yang ditaruh di
sana akan hilang tanpa jejak.

`components/mrgreat_emoji/CMakeLists.txt` memakai `--whole-archive` karena
gambarnya hanya dirujuk lewat deklarasi `extern` dari kode C++. Tanpa penanda
itu, penaut menganggapnya tidak terpakai dan membuangnya, dan yang tampil di
layar hanya ikon bawaan.

### Menambah atau Mengganti Emoji

1. Ubah gambar menjadi berkas `.c` LVGL memakai `LVGLImage.py` dengan format
   warna yang sama seperti berkas yang sudah ada.
2. Taruh hasilnya di `components/mrgreat_emoji/src`.
3. Tambahkan deklarasi `extern` dan satu baris `AddEmoji` di
   `main/display/lvgl_display/emoji_collection.cc`.

Nama emosi harus sama persis dengan yang dikirim server, yaitu `neutral`,
`happy`, `laughing`, `funny`, `sad`, `angry`, `crying`, `loving`, `embarrassed`,
`surprised`, `shocked`, `thinking`, `winking`, `cool`, `relaxed`, `delicious`,
`kissy`, `confident`, `sleepy`, `silly`, dan `confused`.
