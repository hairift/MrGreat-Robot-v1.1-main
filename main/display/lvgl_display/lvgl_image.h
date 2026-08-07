#pragma once

#include <lvgl.h>

// Wrap around lv_img_dsc_t
class LvglImage {
public:
    virtual const lv_img_dsc_t* image_dsc() const = 0;
    virtual bool IsGif() const { return false; }
    virtual ~LvglImage() = default;
};

/**
 * Tambahan Mr Great: pembungkus untuk gambar yang sudah ada di flash.
 *
 * Kelas lain di berkas ini membangun deskriptornya sendiri dari penyangga
 * mentah, sedangkan emoji kustom Mr Great sudah berupa lv_image_dsc_t utuh
 * hasil pengubah gambar LVGL. Pembungkus ini hanya meneruskan penunjuknya
 * tanpa menyalin apa pun, sehingga gambarnya tetap berada di flash dan tidak
 * memakan memori kerja.
 */
class LvglSourceImage : public LvglImage {
public:
    explicit LvglSourceImage(const lv_img_dsc_t* image_dsc) : image_dsc_(image_dsc) {}
    virtual const lv_img_dsc_t* image_dsc() const override { return image_dsc_; }

private:
    const lv_img_dsc_t* image_dsc_ = nullptr;
};

class LvglRawImage : public LvglImage {
public:
    LvglRawImage(void* data, size_t size);
    virtual const lv_img_dsc_t* image_dsc() const override { return &image_dsc_; }
    virtual bool IsGif() const;

private:
    lv_img_dsc_t image_dsc_;
};

class LvglCBinImage : public LvglImage {
public:
    LvglCBinImage(void* data);
    virtual ~LvglCBinImage();
    virtual const lv_img_dsc_t* image_dsc() const override { return image_dsc_; }

private:
    lv_img_dsc_t* image_dsc_ = nullptr;
};

class LvglAllocatedImage : public LvglImage {
public:
    LvglAllocatedImage(void* data, size_t size);
    LvglAllocatedImage(void* data, size_t size, int width, int height, int stride,
                       int color_format);
    virtual ~LvglAllocatedImage();
    virtual const lv_img_dsc_t* image_dsc() const override { return &image_dsc_; }

private:
    lv_img_dsc_t image_dsc_;
};
