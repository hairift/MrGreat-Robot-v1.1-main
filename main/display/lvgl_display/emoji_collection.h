#ifndef EMOJI_COLLECTION_H
#define EMOJI_COLLECTION_H

#include "lvgl_image.h"

#include <lvgl.h>

#include <map>
#include <memory>
#include <string>

// Define interface for emoji collection
class EmojiCollection {
public:
    virtual void AddEmoji(const std::string& name, LvglImage* image);
    virtual const LvglImage* GetEmojiImage(const char* name);
    virtual ~EmojiCollection();

private:
    std::map<std::string, LvglImage*> emoji_collection_;
};

/**
 * Emoji wajah 64 piksel milik Mr Great.
 *
 * Arsitektur baru menampilkan emosi memakai font emoji, sedangkan Mr Great
 * memakai gambar berwarna yang sudah disiapkan sendiri. Gambarnya berada di
 * komponen mrgreat_emoji dan dirujuk lewat deklarasi extern.
 *
 * Nama emosi mengikuti nama yang dikirim server, jadi daftarnya harus sama
 * persis dengan yang dipakai tema bawaan.
 */
class MrGreatEmoji64 : public EmojiCollection {
public:
    MrGreatEmoji64();
};

#endif
