# Emoji wajah 64 piksel milik Mr Great.
#
# Arsitektur baru menampilkan emosi memakai font emoji, sedangkan Mr Great
# memakai gambar berwarna 64 piksel yang sudah disiapkan sendiri. Gambar itu
# disimpan sebagai komponen tersendiri, bukan menimpa komponen font bawaan,
# supaya pembaruan komponen font dari hulu tidak menghapusnya.
#
# Komponen ini harus dicantumkan pada PRIV_REQUIRES di main/CMakeLists.txt,
# kalau tidak pustakanya tidak ikut ke baris penautan dan seluruh gambar
# dilaporkan sebagai undefined reference.

file(GLOB SUMBER_EMOJI "src/*.c")

idf_component_register(
    SRCS ${SUMBER_EMOJI}
    REQUIRES lvgl__lvgl
)

# Setiap berkas gambar memilih cara menyertakan LVGL lewat rangkaian #if.
# Tanpa penanda ini, pilihannya jatuh ke cabang terakhir yaitu
# #include "lvgl/lvgl.h", dan jalur itu memang tidak ada pada tata letak
# komponen terkelola. Penanda diberikan tersurat, bukan mengandalkan warisan
# dari komponen LVGL, karena pewarisannya bergantung pada cara komponen itu
# mengekspor definisinya dan dapat berubah antar versi.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
