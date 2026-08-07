# Campus RAG Server UCIC

Dokumen ini menjelaskan arsitektur pengetahuan kampus terbaru.

## Tujuan

Data kampus tetap berada di `main/campus_data.cc` sebagai fallback firmware. Untuk pencarian yang lebih fleksibel, data yang sama diekspor ke `data/campus_data.json` dan dapat dijalankan sebagai server RAG Python di komputer atau server lokal.

Alur prioritas jawaban:

1. `self.campus.query` mencoba Campus RAG Server jika URL dikonfigurasi.
2. Jika server kosong, mati, timeout, atau respons tidak valid, firmware otomatis memakai `SearchCampusData` lokal.
3. Pertanyaan realtime di luar UCIC tetap memakai `self.web.search`.
4. Pertanyaan umum stabil dijawab oleh model bawaan.

## Menjalankan Server

Ekspor data dari C++ ke JSON:

```powershell
python scripts\export_campus_data.py
```

Jalankan server:

```powershell
python scripts\campus_rag_server.py --host 0.0.0.0 --port 8787
```

Server akan menampilkan URL jaringan lokal, misalnya:

```text
http://192.168.1.10:8787
```

Masukkan URL itu ke konfigurasi `CAMPUS_RAG_SERVER_URL` lewat menuconfig atau sdkconfig.

## Mode AI Opsional

Tanpa dependensi tambahan, server memakai retrieval hybrid berbasis token, fuzzy matching, dan vektor hash karakter. Jika ingin memakai embedding dan FAISS:

```powershell
pip install sentence-transformers faiss-cpu rapidfuzz
python scripts\campus_rag_server.py --embedding-model sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2 --use-faiss
```

Mode opsional ini berjalan di komputer/server, bukan di ESP32.

## Audit

Jalankan audit retrieval:

```powershell
python scripts\audit_campus_rag_server.py
```

Audit ini menguji kasus inti seperti rektor, dosen agama, agama Kristen, algoritma pemrograman, algoritma pemrograman 2, akreditasi prodi, biaya opsi, dan jurusan UCIC.
