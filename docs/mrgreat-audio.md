# Jalur Audio Mr Great

Dokumen ini menjelaskan jalur audio Mr Great dan alasan di balik perbaikan
suara yang sebelumnya terdengar mendem.

## Ringkasan Jalur

```text
INMP441 -> I2S 32 bit -> geser 12 bit -> penegas nada tinggi -> AFE
        -> kata pemicu dan Opus -> server
server  -> Opus -> antrean dekode -> antrean pemutaran -> I2S -> MAX98357
```

Laju cuplik masukan 16 kHz dan keluaran 24 kHz, mengikuti `AUDIO_INPUT_SAMPLE_RATE`
dan `AUDIO_OUTPUT_SAMPLE_RATE` di `config.h`.

## Kenapa Suaranya Terdengar Mendem

Rekaman uji dari perangkat versi sebelumnya diukur sebaran tenaganya per pita.
Hasilnya:

| Pita | Bagian tenaga |
|---|---|
| 0 sampai 1000 Hz | 91,3 persen |
| 1000 sampai 2000 Hz | 7,6 persen |
| 2000 sampai 3000 Hz | 0,9 persen |
| 3000 sampai 4000 Hz | 0,04 persen |
| 4000 sampai 8000 Hz | 0,15 persen |

Sembilan puluh satu persen tenaga menumpuk di bawah 1 kHz, dan 95 persennya
berada di bawah 1749 Hz. Kejelasan ucapan justru hidup di pita 1 sampai 4 kHz,
tempat konsonan berada. Pita itu berada 30 sampai 50 dB di bawah puncak,
sedangkan ucapan yang jernih biasanya sekitar 25 dB di bawahnya.

Penyebabnya bukan perangkat lunak. Mikrofon berada di dalam badan robot, dan
badan itulah yang meredam nada tingginya. Tidak ada pengaturan I2S yang dapat
mengembalikan tenaga yang memang sudah teredam sebelum sampai ke mikrofon.

## Penegas Nada Tinggi

Firmware memasang rak nada tinggi pada jalur mikrofon:

| Pengaturan | Nilai |
|---|---|
| Frekuensi sudut | 1800 Hz |
| Penguatan | 12 dB |
| Penguatan awal | 0,7 |

Rak nada tinggi menaikkan seluruh pita di atas frekuensi sudut tanpa menyentuh
pita dasar suara yang memang sudah kuat. Penguatan awal 0,7 memberi ruang
puncak, karena tanpa itu hasilnya akan menyentuh batas dan terpotong.

Hasil pengukuran pada rekaman yang sama setelah filter dipasang:

| Pita | Sebelum | Sesudah |
|---|---|---|
| 0 sampai 1000 Hz | 91,3 persen | 73,3 persen |
| 1000 sampai 2000 Hz | 7,6 persen | 20,1 persen |
| 2000 sampai 3000 Hz | 0,9 persen | 4,7 persen |
| Puncak | 99,9 persen | 91,3 persen |
| Cuplikan terpotong | 1 | 0 |

Pita konsonan naik dari 8,5 persen menjadi 24,8 persen, yang sudah masuk
rentang ucapan jernih, dan tidak ada satu pun cuplikan yang terpotong.

Filternya berupa biquad rak nada tinggi mengikuti audio cookbook Robert
Bristow-Johnson, dengan Q sebesar 0,707 supaya lerengnya halus tanpa gundukan
di sekitar frekuensi sudut. Perhitungannya memakai bilangan pecahan karena satu
bingkai audio hanya berisi ratusan cuplikan pada 16 kHz, sehingga bebannya jauh
di bawah satu persen waktu prosesor.

Pengaturannya berada di `CompactWifiBoard::GetAudioCodec` lewat
`AudioCodec::SetInputHighShelf`. Untuk mematikannya, isi penguatan dengan nol.

## Catatan tentang SetInputGain

`AudioCodec::SetInputGain` tidak berpengaruh pada papan ini. Jalur baca
`NoAudioCodec::Read` yang dipakai INMP441 mengubah cuplikan 32 bit menjadi
16 bit lewat geseran 12 bit, dan tidak pernah membaca `input_gain_`. Hanya
jalur PDM yang memakainya.

Papan versi sebelumnya memanggil `SetInputGain(2.0f)` dan mencatatnya di log,
sehingga terlihat seolah penguatan sedang bekerja, padahal tidak. Geseran 12 bit
sendiri sudah menghasilkan tingkat yang mendekati skala penuh, jadi memaksa
penguatan dua kali justru akan memotong puncak.

## Suara Patah-Patah dan Paket Tertukar

Paket audio yang datang tidak berurutan disusun ulang lebih dulu, bukan dibuang.
Jaringan nirkabel rumah kerap mengirimkan paket sedikit tertukar urutannya, dan
membuangnya meninggalkan celah beberapa puluh milidetik pada suara robot.
Rinciannya ada di [docs/mqtt-udp.md](mqtt-udp.md).
