"""Pencarian web untuk Mr Great.

Pustaka DuckDuckGo tidak lagi menyediakan kelas AsyncDDGS; sejak versi 7 hanya
tersisa kelas DDGS yang sinkron. Supaya tujuan aslinya tetap tercapai, yaitu
permintaan pencarian tidak boleh menahan proses sehingga suara boneka putus,
pemanggilannya dijalankan di kolam utas lewat run_in_executor. Loop kejadian
FastAPI tetap bebas melayani permintaan lain selama pencarian berjalan.

Hasil disinggahkan sepuluh menit agar berita yang sama tidak ditembak berulang
kali dan tidak kena batas laju DuckDuckGo.

Jawaban yang dikirim ke perangkat sengaja dipangkas menjadi dua kalimat pertama
supaya boneka cepat mulai berbicara.
"""

from __future__ import annotations

import asyncio
import hashlib
import logging
import re
from typing import Any, Optional

import cache
from config import pengaturan

log = logging.getLogger("mrgreat.search")

# Kata yang menandakan pengguna meminta berita, bukan pengetahuan umum.
KATA_BERITA = (
    "berita", "kabar", "terkini", "terbaru", "hari ini", "minggu ini",
    "bulan ini", "update", "news", "peristiwa", "kejadian",
)

# Rentang waktu DuckDuckGo: d harian, w mingguan, m bulanan, y tahunan.
RENTANG = {
    "hari ini": "d",
    "harian": "d",
    "minggu ini": "w",
    "mingguan": "w",
    "bulan ini": "m",
    "bulanan": "m",
    "tahun ini": "y",
}

# ---------------------------------------------------------------------------
# Prioritas Cirebon
#
# Mr Great berada di Cirebon, jadi pertanyaan berita yang tidak menyebut tempat
# mana pun hampir selalu berarti berita daerah sendiri. Tanpa penanda tempat,
# DuckDuckGo mengembalikan berita nasional yang sama sekali tidak berhubungan
# dengan lingkungan penggunanya.
#
# Penanda hanya ditambahkan bila kueri belum menyebut tempat, sehingga
# permintaan "berita Jakarta" tetap dijawab tentang Jakarta.
# ---------------------------------------------------------------------------
WILAYAH_UTAMA = "Cirebon"

# Nama tempat yang, bila disebut, berarti pengguna memang meminta daerah lain.
KATA_TEMPAT = (
    "cirebon", "jakarta", "bandung", "surabaya", "medan", "semarang",
    "yogyakarta", "jogja", "bali", "makassar", "palembang", "bekasi",
    "depok", "tangerang", "bogor", "indramayu", "kuningan", "majalengka",
    "brebes", "tegal", "jawa barat", "jabar", "nasional", "indonesia",
    "dunia", "internasional", "amerika", "china", "tiongkok", "jepang",
    "korea", "eropa", "timur tengah", "palestina", "israel",
)

# Kata yang menandakan pertanyaan memang tentang Cirebon, sehingga jawabannya
# perlu dipertajam ke sumber daerah.
KATA_CIREBON = (
    "cirebon", "kejaksan", "kesambi", "harjamukti", "lemahwungkuk",
    "pekalipan", "gunung jati", "keraton kasepuhan", "kanoman",
    "kacirebonan", "empal gentong", "nasi jamblang", "docang", "tahu gejrot",
    "batik trusmi", "grage", "ciayumajakuning",
)


def _kunci(prefiks: str, *bagian: str) -> str:
    mentah = "|".join(bagian)
    return f"mrgreat:{prefiks}:" + hashlib.sha256(mentah.encode("utf-8")).hexdigest()[:24]


def deteksi_berita(kueri: str) -> bool:
    rendah = kueri.lower()
    return any(kata in rendah for kata in KATA_BERITA)


def deteksi_rentang(kueri: str) -> Optional[str]:
    rendah = kueri.lower()
    for frasa, kode in RENTANG.items():
        if frasa in rendah:
            return kode
    return None


def tentang_cirebon(kueri: str) -> bool:
    rendah = kueri.lower()
    return any(kata in rendah for kata in KATA_CIREBON)


def utamakan_cirebon(kueri: str, berita: bool) -> str:
    """Tambahkan penanda tempat pada permintaan berita yang belum menyebut tempat.

    Hanya berlaku untuk permintaan berita. Pertanyaan pengetahuan umum seperti
    "siapa presiden Indonesia" tidak boleh diseret ke daerah tertentu, karena
    jawabannya memang bukan soal tempat.
    """
    if not berita:
        return kueri
    rendah = kueri.lower()
    if any(tempat in rendah for tempat in KATA_TEMPAT):
        return kueri
    return f"{kueri} {WILAYAH_UTAMA}"


def batasi_kalimat(teks: str, maksimum: int) -> str:
    """Ambil beberapa kalimat pertama saja.

    Membacakan seluruh isi halaman membuat jawaban suara sangat panjang dan
    terasa lambat, jadi hanya intinya yang dikirim ke perangkat.
    """
    teks = re.sub(r"\s+", " ", teks or "").strip()
    if not teks:
        return ""
    kalimat = re.split(r"(?<=[.!?])\s+", teks)
    return " ".join(kalimat[:maksimum]).strip()


def _cari_sinkron(kueri: str, region: str, maks: int,
                  berita: bool, rentang: Optional[str]) -> list[dict[str, str]]:
    """Panggilan DuckDuckGo yang sebenarnya. Dijalankan di kolam utas."""
    hasil: list[dict[str, str]] = []
    try:
        from ddgs import DDGS

        with DDGS() as ddgs:
            if berita:
                # news() khusus berita dan mendukung penyaring rentang waktu.
                butir = ddgs.news(
                    kueri, region=region, safesearch="moderate",
                    timelimit=rentang or "w", max_results=maks,
                )
                for b in butir or []:
                    hasil.append({
                        "judul": b.get("title", ""),
                        "isi": b.get("body", ""),
                        "url": b.get("url", ""),
                        "sumber": b.get("source", ""),
                        "tanggal": b.get("date", ""),
                    })
            else:
                butir = ddgs.text(
                    kueri, region=region, safesearch="moderate",
                    timelimit=rentang, max_results=maks,
                )
                for b in butir or []:
                    hasil.append({
                        "judul": b.get("title", ""),
                        "isi": b.get("body", ""),
                        "url": b.get("href", ""),
                        "sumber": "",
                        "tanggal": "",
                    })
    except Exception as exc:
        log.warning("Pencarian DuckDuckGo gagal: %s", exc)
    return hasil


async def cari(kueri: str, region: str = "id-id", maks: int = 5) -> dict[str, Any]:
    """Cari di DuckDuckGo lalu susun jawaban ringkas."""
    cfg = pengaturan()
    berita = deteksi_berita(kueri)
    rentang = deteksi_rentang(kueri)
    kueri_cari = utamakan_cirebon(kueri, berita)
    if kueri_cari != kueri:
        log.info("Berita tanpa penanda tempat diarahkan ke %s: %r", WILAYAH_UTAMA, kueri)

    kunci = _kunci("search", kueri_cari.lower(), region, str(berita), rentang or "-")
    tersimpan = cache.ambil(kunci)
    if tersimpan is not None:
        log.info("Hasil pencarian diambil dari singgahan: %s", kueri_cari)
        return tersimpan

    # Pencarian dijalankan di kolam utas supaya loop kejadian tetap bebas.
    lup = asyncio.get_running_loop()
    hasil = await lup.run_in_executor(
        None, _cari_sinkron, kueri_cari, region, maks, berita, rentang
    )

    # Bila penanda tempat justru membuat hasilnya kosong, pencarian diulang
    # tanpa penanda. Lebih baik menjawab berita nasional daripada tidak
    # menjawab sama sekali.
    if not hasil and kueri_cari != kueri:
        log.info("Tidak ada hasil untuk %s, mencoba tanpa penanda tempat", WILAYAH_UTAMA)
        hasil = await lup.run_in_executor(
            None, _cari_sinkron, kueri, region, maks, berita, rentang
        )

    if not hasil:
        jawaban = ""
    else:
        utama = hasil[0]
        inti = batasi_kalimat(utama.get("isi", ""), cfg.tts_max_sentences)
        bagian = []
        if utama.get("judul"):
            bagian.append(utama["judul"].strip())
        if inti:
            bagian.append(inti)
        jawaban = ". ".join(b.rstrip(".") for b in bagian if b)
        if jawaban:
            jawaban += "."
        # Satu hasil pendukung ditambahkan agar AI punya pembanding, tetapi
        # tetap singkat supaya jawaban suara tidak bertele-tele.
        if len(hasil) > 1:
            kedua = batasi_kalimat(hasil[1].get("isi", ""), 1)
            if kedua:
                jawaban += " " + kedua

    balasan = {
        "status": "found" if jawaban else "not_found",
        "answer": jawaban,
        "is_news": berita,
        "timelimit": rentang,
        "region": region,
        "results": hasil[:maks],
    }

    if jawaban:
        cache.simpan(kunci, balasan, cfg.cache_ttl_search)
    return balasan
