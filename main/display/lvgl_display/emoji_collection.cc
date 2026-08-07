#include "emoji_collection.h"

#include <esp_log.h>

#define TAG "EmojiCollection"

void EmojiCollection::AddEmoji(const std::string& name, LvglImage* image) {
    emoji_collection_[name] = image;
}

const LvglImage* EmojiCollection::GetEmojiImage(const char* name) {
    auto it = emoji_collection_.find(name);
    if (it != emoji_collection_.end()) {
        return it->second;
    }

    ESP_LOGW(TAG, "Emoji not found: %s", name);
    return nullptr;
}

EmojiCollection::~EmojiCollection() {
    for (auto it = emoji_collection_.begin(); it != emoji_collection_.end(); ++it) {
        delete it->second;
    }
    emoji_collection_.clear();
}

// Deklarasi gambar emoji 64 piksel dari komponen mrgreat_emoji.
extern const lv_image_dsc_t emoji_1f636_64; // netral
extern const lv_image_dsc_t emoji_1f642_64; // senang
extern const lv_image_dsc_t emoji_1f606_64; // tertawa
extern const lv_image_dsc_t emoji_1f602_64; // lucu
extern const lv_image_dsc_t emoji_1f614_64; // sedih
extern const lv_image_dsc_t emoji_1f620_64; // marah
extern const lv_image_dsc_t emoji_1f62d_64; // menangis
extern const lv_image_dsc_t emoji_1f60d_64; // sayang
extern const lv_image_dsc_t emoji_1f633_64; // malu
extern const lv_image_dsc_t emoji_1f62f_64; // terkejut
extern const lv_image_dsc_t emoji_1f631_64; // kaget
extern const lv_image_dsc_t emoji_1f914_64; // berpikir
extern const lv_image_dsc_t emoji_1f609_64; // mengedip
extern const lv_image_dsc_t emoji_1f60e_64; // keren
extern const lv_image_dsc_t emoji_1f60c_64; // rileks
extern const lv_image_dsc_t emoji_1f924_64; // lezat
extern const lv_image_dsc_t emoji_1f618_64; // cium
extern const lv_image_dsc_t emoji_1f60f_64; // percaya diri
extern const lv_image_dsc_t emoji_1f634_64; // mengantuk
extern const lv_image_dsc_t emoji_1f61c_64; // konyol
extern const lv_image_dsc_t emoji_1f644_64; // bingung

MrGreatEmoji64::MrGreatEmoji64() {
    AddEmoji("neutral", new LvglSourceImage(&emoji_1f636_64));
    AddEmoji("happy", new LvglSourceImage(&emoji_1f642_64));
    AddEmoji("laughing", new LvglSourceImage(&emoji_1f606_64));
    AddEmoji("funny", new LvglSourceImage(&emoji_1f602_64));
    AddEmoji("sad", new LvglSourceImage(&emoji_1f614_64));
    AddEmoji("angry", new LvglSourceImage(&emoji_1f620_64));
    AddEmoji("crying", new LvglSourceImage(&emoji_1f62d_64));
    AddEmoji("loving", new LvglSourceImage(&emoji_1f60d_64));
    AddEmoji("embarrassed", new LvglSourceImage(&emoji_1f633_64));
    AddEmoji("surprised", new LvglSourceImage(&emoji_1f62f_64));
    AddEmoji("shocked", new LvglSourceImage(&emoji_1f631_64));
    AddEmoji("thinking", new LvglSourceImage(&emoji_1f914_64));
    AddEmoji("winking", new LvglSourceImage(&emoji_1f609_64));
    AddEmoji("cool", new LvglSourceImage(&emoji_1f60e_64));
    AddEmoji("relaxed", new LvglSourceImage(&emoji_1f60c_64));
    AddEmoji("delicious", new LvglSourceImage(&emoji_1f924_64));
    AddEmoji("kissy", new LvglSourceImage(&emoji_1f618_64));
    AddEmoji("confident", new LvglSourceImage(&emoji_1f60f_64));
    AddEmoji("sleepy", new LvglSourceImage(&emoji_1f634_64));
    AddEmoji("silly", new LvglSourceImage(&emoji_1f61c_64));
    AddEmoji("confused", new LvglSourceImage(&emoji_1f644_64));
}
