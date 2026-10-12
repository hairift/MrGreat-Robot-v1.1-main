#include "servo_controller.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <esp_log.h>
#include <driver/ledc.h>

#define TAG "ServoCtrl"

#define SERVO_FREQ_HZ   50
#define SERVO_RES       LEDC_TIMER_13_BIT
#define SERVO_MIN_US    500
#define SERVO_MAX_US    2500
#define SERVO_PERIOD_US 20000
#define SERVO_TIMER     LEDC_TIMER_1
#define SERVO_MODE      LEDC_LOW_SPEED_MODE
#define MANUAL_OVERRIDE_MS 6000
// Jendela abaikan perintah kembar tidak boleh lebih panjang daripada masa
// override manual. Bila lebih panjang, ada selang waktu ketika override sudah
// berakhir sehingga gestur emosi dapat menggerakkan lengan, tetapi perintah
// suara yang sama masih ditolak sebagai kembar. Karena itu nilainya
// disamakan langsung dengan MANUAL_OVERRIDE_MS, bukan ditulis terpisah.
#define DUPLICATE_MOVE_IGNORE_MS MANUAL_OVERRIDE_MS
// Lama posisi manual ditahan sebelum gestur otomatis boleh mengambil alih.
// Disamakan dengan masa override manual supaya kedua penanda berakhir
// bersamaan dan tidak ada selang waktu yang tidak konsisten.
#define MANUAL_POSE_HOLD_MS MANUAL_OVERRIDE_MS
#define SERVO_FRAME_MS  20
#define SERVO_REST_RELEASE_MS 350
#define SPEAKING_MOTION_INTERVAL_MS 850
#define SPEAKING_SENTENCE_FALLBACK_MS 3000

// ---------------------------------------------------------------------------
// Penjagaan arus servo
//
// Tiga servo SG90 dapat menarik arus puncak yang besar, terutama saat mulai
// bergerak dan saat berakselerasi tajam. Arus puncak itulah yang paling sering
// menjatuhkan tegangan pasokan dan membuat perangkat memulai ulang. Ketiga
// angka di bawah menahan arus puncak itu hanya dengan mengatur perangkat lunak,
// tanpa mengubah rangkaian.
// ---------------------------------------------------------------------------

// Jeda antar penyalaan servo saat boot. Menyalakan tiga servo sekaligus
// membuat lonjakan arusnya menumpuk pada satu titik.
#define SERVO_INIT_STAGGER_MS 150

// Batas laju sudut servo dalam derajat per detik. Bila sebuah langkah meminta
// gerakan jauh dalam waktu singkat, durasinya diperpanjang otomatis supaya
// servo tidak berakselerasi tajam. Turunkan angkanya bila perangkat masih
// memulai ulang; naikkan bila gerakan terasa terlalu lambat.
#define SERVO_MAKS_DERAJAT_PER_DETIK 420

// Selisih awal gerak antar saluran, dalam jumlah frame. Kepala mulai lebih
// dulu, lalu lengan kanan, lalu lengan kiri, supaya ketiganya tidak
// berakselerasi pada saat yang sama.
#define SERVO_STAGGER_FRAME 2

// Ukuran tumpukan tugas servo. Tugas ini memanggil fungsi PWM dan pencatatan
// log, jadi ruangnya diberi kelegaan supaya tidak meluap dan memicu restart.
#define SERVO_TASK_STACK 6144

// Kepala tanpa inversi, ditukar agar sesuai dengan posisi fisik
#define H_C   90
#define H_L  140
#define H_R   40
#define H_SL 110
#define H_SR  70
#define H_TALK_L 98
#define H_TALK_R 82

// Tangan memiliki tiga tingkat posisi
// Tangan kiri (invert=true): posisi fisik = 180 - urutan
// Tangan kanan (invert=false, horn dibalik): posisi fisik = urutan
// DN=10, STR=80 (lurus), UP=130 (posisi tertinggi)
#define A_DN   10      // ke bawah (istirahat)
#define A_MID  45      // tengah (untuk animasi)
#define A_STR  80      // lurus (tangan lurus horizontal)
#define A_UP  130      // ke atas (paling tinggi)
#define A_SCROLL_LOW 32
#define A_SCROLL_HIGH 62

struct CmdPacket { uint8_t type; char data[31]; };
static constexpr int KEEP = -1;
#define SEQ(s) s, (int)(sizeof(s)/sizeof(s[0]))

// =====================================================================
//  URUTAN EMOSI, setiap emosi memiliki gerakan unik
//  Langkah terakhir adalah posisi diam dan kembali otomatis
// =====================================================================

// NETRAL: tetap diam
static const ServoController::ServoStep SEQ_NEUTRAL[] = {
    {H_C, A_DN, A_DN, 300}};

// SENANG: kepala bergoyang ceria dan tangan bergantian naik turun
static const ServoController::ServoStep SEQ_HAPPY[] = {
    {H_SL, A_DN,  A_DN,  180},
    {H_SR, A_STR, A_DN,  200},
    {H_SL, A_DN,  A_STR, 200},
    {H_SR, A_STR, A_STR, 200},
    {H_C,  A_DN,  A_DN,  150}};

// TERTAWA: geleng cepat dan tangan bergantian naik turun cepat
static const ServoController::ServoStep SEQ_LAUGHING[] = {
    {H_L,  A_STR, A_DN,  130},
    {H_R,  A_DN,  A_STR, 130},
    {H_L,  A_STR, A_DN,  130},
    {H_R,  A_DN,  A_STR, 130},
    {H_L,  A_STR, A_DN,  130},
    {H_C,  A_DN,  A_DN,  150}};

// LUCU: gerakan konyol dengan kepala bergeleng dan satu tangan naik
static const ServoController::ServoStep SEQ_FUNNY[] = {
    {H_L,  A_DN,  A_DN,  180},
    {H_R,  A_UP,  A_DN,  200},
    {H_L,  A_DN,  A_UP,  200},
    {H_R,  A_STR, A_DN,  180},
    {H_C,  A_DN,  A_DN,  150}};

// SEDIH: kepala perlahan condong ke samping, tangan diam
static const ServoController::ServoStep SEQ_SAD[] = {
    {H_SL, A_DN, A_DN, 900},
    {H_C,  A_DN, A_DN, 500},
    {H_SL, A_DN, A_DN, 700},
    {H_C,  A_DN, A_DN, 200}};

// MARAH: kepala bergeleng tajam dan cepat, kedua tangan naik tegas
static const ServoController::ServoStep SEQ_ANGRY[] = {
    {H_L,  A_UP, A_UP, 130},
    {H_R,  A_UP, A_UP, 130},
    {H_L,  A_UP, A_UP, 130},
    {H_R,  A_UP, A_UP, 130},
    {H_L,  A_UP, A_UP, 130},
    {H_C,  A_DN, A_DN, 200}};

// MENANGIS: kepala bergerak perlahan dan satu tangan naik pelan seperti mengusap air mata
static const ServoController::ServoStep SEQ_CRYING[] = {
    {H_SL, A_STR, A_DN, 600},
    {H_SL, A_MID, A_DN, 400},
    {H_SL, A_STR, A_DN, 600},
    {H_C,  A_DN,  A_DN, 250}};

// SAYANG: ayunan pelan dan kedua tangan naik turun lembut
static const ServoController::ServoStep SEQ_LOVING[] = {
    {H_SL, A_MID, A_DN,  400},
    {H_SR, A_DN,  A_MID, 400},
    {H_C,  A_STR, A_STR, 500},
    {H_C,  A_DN,  A_DN,  250}};

// MALU: kepala menoleh, tangan diam
static const ServoController::ServoStep SEQ_EMBARRASSED[] = {
    {H_SR, A_DN, A_DN, 800},
    {H_C,  A_DN, A_DN, 300},
    {H_SR, A_DN, A_DN, 600},
    {H_C,  A_DN, A_DN, 200}};

// TERKEJUT: kedua tangan naik tinggi dan kepala bergeleng
static const ServoController::ServoStep SEQ_SURPRISED[] = {
    {H_C,  A_UP, A_UP, 250},
    {H_L,  A_UP, A_UP, 200},
    {H_R,  A_UP, A_UP, 200},
    {H_C,  A_UP, A_UP, 200},
    {H_C,  A_DN, A_DN, 250}};

// KAGET: gerakan cepat dan tajam
static const ServoController::ServoStep SEQ_SHOCKED[] = {
    {H_L,  A_UP, A_UP, 110},
    {H_R,  A_UP, A_UP, 110},
    {H_L,  A_UP, A_UP, 110},
    {H_R,  A_UP, A_UP, 110},
    {H_L,  A_UP, A_UP, 110},
    {H_C,  A_DN, A_DN, 250}};

// BERPIKIR: kepala miring dan tangan kanan naik
static const ServoController::ServoStep SEQ_THINKING[] = {
    {H_SR, A_STR, A_DN, 1000},
    {H_SR, A_MID, A_DN, 500},
    {H_SR, A_STR, A_DN, 700},
    {H_C,  A_DN,  A_DN, 250}};

// BERKEDIP: kepala miring dan tangan kanan melambai
static const ServoController::ServoStep SEQ_WINKING[] = {
    {H_SR, A_STR, A_DN, 250},
    {H_SR, A_MID, A_DN, 180},
    {H_SR, A_STR, A_DN, 250},
    {H_SR, A_MID, A_DN, 180},
    {H_C,  A_DN,  A_DN, 200}};

// KEREN: santai, kepala bergeleng pelan, dan tangan bergantian di posisi tengah
static const ServoController::ServoStep SEQ_COOL[] = {
    {H_SR, A_STR, A_DN,  450},
    {H_C,  A_STR, A_STR, 400},
    {H_SL, A_DN,  A_STR, 400},
    {H_C,  A_DN,  A_DN,  250}};

// RILEKS: geleng sangat pelan, tangan diam
static const ServoController::ServoStep SEQ_RELAXED[] = {
    {H_SL, A_DN, A_DN, 1000},
    {H_SR, A_DN, A_DN, 1000},
    {H_C,  A_DN, A_DN, 350}};

// NIKMAT: geleng pelan dan tangan bergantian naik
static const ServoController::ServoStep SEQ_DELICIOUS[] = {
    {H_SL, A_MID, A_DN,  300},
    {H_SR, A_DN,  A_MID, 300},
    {H_SL, A_STR, A_DN,  300},
    {H_C,  A_DN,  A_DN,  200}};

// CIUM: kepala bergerak pelan dan kedua tangan di posisi tengah
static const ServoController::ServoStep SEQ_KISSY[] = {
    {H_SL, A_STR, A_STR, 550},
    {H_SR, A_STR, A_STR, 550},
    {H_C,  A_DN,  A_DN,  250}};

// PERCAYA DIRI: tegap dan tangan bergantian naik tinggi
static const ServoController::ServoStep SEQ_CONFIDENT[] = {
    {H_C,  A_UP, A_DN, 300},
    {H_C,  A_DN, A_UP, 300},
    {H_C,  A_UP, A_UP, 350},
    {H_C,  A_DN, A_DN, 250}};

// MENGANTUK: gerakan sangat lambat dan kepala perlahan miring
static const ServoController::ServoStep SEQ_SLEEPY[] = {
    {H_SL, A_DN, A_DN, 1300},
    {H_C,  A_DN, A_DN,  600},
    {H_SL, A_DN, A_DN, 1100},
    {H_C,  A_DN, A_DN,  300}};

// KOCAK: gerakan acak yang cepat
static const ServoController::ServoStep SEQ_SILLY[] = {
    {H_L,  A_UP,  A_DN, 130},
    {H_R,  A_DN,  A_UP, 130},
    {H_L,  A_STR, A_DN, 130},
    {H_R,  A_DN,  A_STR,130},
    {H_L,  A_UP,  A_DN, 130},
    {H_C,  A_DN,  A_DN, 180}};

// BINGUNG: kepala bergerak bolak-balik pelan dan satu tangan naik
static const ServoController::ServoStep SEQ_CONFUSED[] = {
    {H_SR, A_STR, A_DN, 700},
    {H_C,  A_DN,  A_DN, 400},
    {H_SL, A_DN,  A_STR,600},
    {H_C,  A_DN,  A_DN, 200}};

static const ServoController::EmotionSequence SEQUENCES[] = {
    {"neutral",     SEQ(SEQ_NEUTRAL)},
    {"happy",       SEQ(SEQ_HAPPY)},
    {"laughing",    SEQ(SEQ_LAUGHING)},
    {"funny",       SEQ(SEQ_FUNNY)},
    {"sad",         SEQ(SEQ_SAD)},
    {"angry",       SEQ(SEQ_ANGRY)},
    {"crying",      SEQ(SEQ_CRYING)},
    {"loving",      SEQ(SEQ_LOVING)},
    {"embarrassed", SEQ(SEQ_EMBARRASSED)},
    {"surprised",   SEQ(SEQ_SURPRISED)},
    {"shocked",     SEQ(SEQ_SHOCKED)},
    {"thinking",    SEQ(SEQ_THINKING)},
    {"winking",     SEQ(SEQ_WINKING)},
    {"cool",        SEQ(SEQ_COOL)},
    {"relaxed",     SEQ(SEQ_RELAXED)},
    {"delicious",   SEQ(SEQ_DELICIOUS)},
    {"kissy",       SEQ(SEQ_KISSY)},
    {"confident",   SEQ(SEQ_CONFIDENT)},
    {"sleepy",      SEQ(SEQ_SLEEPY)},
    {"silly",       SEQ(SEQ_SILLY)},
    {"confused",    SEQ(SEQ_CONFUSED)},
};
static const int NUM_SEQUENCES = sizeof(SEQUENCES) / sizeof(SEQUENCES[0]);

// =====================================================================
//  GERAK REALTIME
//  HOLD berarti posisi terakhir dipertahankan dan tidak kembali otomatis
//  ANIM berarti langkah terakhir adalah posisi diam dan kembali otomatis
// =====================================================================

// --- KEPALA ---

// Geleng kanan-kiri, kembali ke tengah secara otomatis
static const ServoController::ServoStep MOVE_HEAD_SHAKE[] = {
    {H_L, KEEP, KEEP, 180}, {H_R, KEEP, KEEP, 180},
    {H_L, KEEP, KEEP, 180}, {H_R, KEEP, KEEP, 180},
    {H_L, KEEP, KEEP, 180}, {H_C, KEEP, KEEP, 150}};

// Lihat kanan dengan posisi dipertahankan
static const ServoController::ServoStep MOVE_HEAD_RIGHT[] = {
    {H_R, KEEP, KEEP, 400}};

// Lihat kiri dengan posisi dipertahankan
static const ServoController::ServoStep MOVE_HEAD_LEFT[] = {
    {H_L, KEEP, KEEP, 400}};

// Lihat depan dengan posisi dipertahankan
static const ServoController::ServoStep MOVE_HEAD_CENTER[] = {
    {H_C, KEEP, KEEP, 300}};

// --- TANGAN KANAN ---

// Lambaikan tangan kanan
static const ServoController::ServoStep MOVE_WAVE_RIGHT[] = {
    {KEEP, A_STR, KEEP, 200}, {KEEP, A_MID, KEEP, 160},
    {KEEP, A_STR, KEEP, 200}, {KEEP, A_MID, KEEP, 160},
    {KEEP, A_STR, KEEP, 200}, {KEEP, A_DN,  KEEP, 180}};

// Tangan kanan ke atas, posisi tertinggi
static const ServoController::ServoStep MOVE_RAISE_RIGHT[] = {
    {KEEP, A_UP, KEEP, 400}};

// Tangan kanan lurus pada posisi sedang
static const ServoController::ServoStep MOVE_STRAIGHT_RIGHT[] = {
    {KEEP, A_STR, KEEP, 400}};

// Tangan kanan ke bawah sebagai animasi singkat, lalu boleh kembali otomatis
static const ServoController::ServoStep MOVE_LOWER_RIGHT[] = {
    {KEEP, A_DN, KEEP, 300}};

// --- TANGAN KIRI ---

// Lambaikan tangan kiri
static const ServoController::ServoStep MOVE_WAVE_LEFT[] = {
    {KEEP, KEEP, A_STR, 200}, {KEEP, KEEP, A_MID, 160},
    {KEEP, KEEP, A_STR, 200}, {KEEP, KEEP, A_MID, 160},
    {KEEP, KEEP, A_STR, 200}, {KEEP, KEEP, A_DN,  180}};

// Tangan kiri ke atas dengan posisi dipertahankan
static const ServoController::ServoStep MOVE_RAISE_LEFT[] = {
    {KEEP, KEEP, A_UP, 400}};

// Tangan kiri lurus dengan posisi dipertahankan
static const ServoController::ServoStep MOVE_STRAIGHT_LEFT[] = {
    {KEEP, KEEP, A_STR, 400}};

// Tangan kiri ke bawah sebagai animasi singkat, lalu boleh kembali otomatis
static const ServoController::ServoStep MOVE_LOWER_LEFT[] = {
    {KEEP, KEEP, A_DN, 300}};

// --- KEDUA TANGAN ---

// Lambaikan kedua tangan
static const ServoController::ServoStep MOVE_WAVE_BOTH[] = {
    {KEEP, A_STR, A_DN,  180}, {KEEP, A_DN,  A_STR, 180},
    {KEEP, A_STR, A_DN,  180}, {KEEP, A_DN,  A_STR, 180},
    {KEEP, A_STR, A_STR, 250}, {KEEP, A_DN,  A_DN,  180}};

// Angkat kedua tangan ke atas dan pertahankan
static const ServoController::ServoStep MOVE_RAISE_BOTH[] = {
    {KEEP, A_UP, A_UP, 400}};

// Kedua tangan lurus ke depan dan dipertahankan
static const ServoController::ServoStep MOVE_STRAIGHT_BOTH[] = {
    {KEEP, A_STR, A_STR, 380}};

// --- AKSI ---

// Joget
static const ServoController::ServoStep MOVE_DANCE[] = {
    {H_L, A_STR, A_DN,  150}, {H_R, A_DN,  A_STR, 150},
    {H_L, A_UP,  A_DN,  150}, {H_R, A_DN,  A_UP,  150},
    {H_C, A_UP,  A_UP,  200}, {H_L, A_DN,  A_DN,  150},
    {H_R, A_STR, A_STR, 150}, {H_L, A_DN,  A_DN,  150},
    {H_R, A_UP,  A_UP,  200}, {H_C, A_STR, A_STR, 180},
    {H_C, A_DN,  A_DN,  150}};

// Salam, tangan kanan naik halus lalu kembali
static const ServoController::ServoStep MOVE_SALAM[] = {
    {KEEP, A_MID, KEEP, 250},
    {KEEP, A_STR, KEEP, 350},
    {KEEP, A_UP,  KEEP, 400},
    {KEEP, A_STR, KEEP, 300},
    {KEEP, A_DN,  KEEP, 200}};

// Hormat, tangan kanan naik ke atas dan dipertahankan
static const ServoController::ServoStep MOVE_HORMAT[] = {
    {KEEP, A_UP, KEEP, 400}};

// Tepuk tangan
static const ServoController::ServoStep MOVE_TEPUK[] = {
    {KEEP, A_STR, A_STR, 150}, {KEEP, A_MID, A_MID, 120},
    {KEEP, A_STR, A_STR, 150}, {KEEP, A_MID, A_MID, 120},
    {KEEP, A_STR, A_STR, 150}, {KEEP, A_MID, A_MID, 120},
    {KEEP, A_STR, A_STR, 150}, {KEEP, A_DN,  A_DN,  180}};

// Menyapa, tangan kanan melambai sambil kepala bergeleng
static const ServoController::ServoStep MOVE_MENYAPA[] = {
    {H_SR, A_STR, KEEP, 200}, {H_SL, A_MID, KEEP, 180},
    {H_SR, A_STR, KEEP, 200}, {H_SL, A_MID, KEEP, 180},
    {H_SR, A_STR, KEEP, 200}, {H_C,  A_DN,  KEEP, 180}};

// Sapaan wake word hanya melambaikan tangan kanan agar tidak bentrok dengan emosi.
static const ServoController::ServoStep MOVE_WAKE_GREETING[] = {
    {KEEP, A_STR, KEEP, 200}, {KEEP, A_MID, KEEP, 160},
    {KEEP, A_STR, KEEP, 200}, {KEEP, A_MID, KEEP, 160},
    {KEEP, A_STR, KEEP, 200}, {KEEP, A_DN,  KEEP, 180}};

// Jawaban umum hanya menggerakkan kepala agar gesturnya tenang dan alami.
static const ServoController::ServoStep MOVE_SPEAKING_HEAD_A[] = {
    {H_TALK_L, KEEP, KEEP, 180},
    {H_C,      KEEP, KEEP, 220}};

static const ServoController::ServoStep MOVE_SPEAKING_HEAD_B[] = {
    {H_TALK_R, KEEP, KEEP, 180},
    {H_C,      KEEP, KEEP, 220}};

// Jawaban kampus atau web menambahkan sapuan tangan kanan seperti menggulir.
// Batas atas tetap di bawah posisi lurus agar tidak menyerupai perintah tangan.
static const ServoController::ServoStep MOVE_SPEAKING_SEARCH_A[] = {
    {H_TALK_L, A_SCROLL_HIGH, KEEP, 340},
    {H_C,      A_SCROLL_LOW,  KEEP, 300},
    {H_C,      A_DN,          KEEP, 280}};

static const ServoController::ServoStep MOVE_SPEAKING_SEARCH_B[] = {
    {H_TALK_R, A_SCROLL_LOW,  KEEP, 300},
    {H_C,      A_SCROLL_HIGH, KEEP, 340},
    {H_C,      A_DN,          KEEP, 280}};

// Reset ke posisi semula
static const ServoController::ServoStep MOVE_RESET[] = {
    {H_C, A_DN, A_DN, 300}};

// =====================================================================
//  IMPLEMENTASI
// =====================================================================

int ServoController::AngleToUs(int angle_deg) {
    if (angle_deg < 0) angle_deg = 0;
    if (angle_deg > 180) angle_deg = 180;
    return SERVO_MIN_US + (angle_deg * (SERVO_MAX_US - SERVO_MIN_US)) / 180;
}

static int32_t EaseMinimumJerkQ16(int32_t start, int32_t target, int frame, int frames) {
    if (frames <= 1) {
        return target;
    }

    double t = static_cast<double>(frame) / static_cast<double>(frames);
    double t2 = t * t;
    double t3 = t2 * t;
    double progress = t3 * (10.0 + t * (-15.0 + 6.0 * t));
    return start + static_cast<int32_t>(
        std::llround(static_cast<double>(target - start) * progress));
}

void ServoController::SetAngle(int channel, int angle_deg, bool invert) {
    if (angle_deg < 0) {
        angle_deg = 0;
    } else if (angle_deg > 180) {
        angle_deg = 180;
    }
    SetAngleQ16(channel, angle_deg << 16, invert);
}

void ServoController::SetAngleQ16(int channel, int32_t angle_q16, bool invert) {
    constexpr int32_t kMaxAngleQ16 = 180 << 16;
    if (angle_q16 < 0) {
        angle_q16 = 0;
    } else if (angle_q16 > kMaxAngleQ16) {
        angle_q16 = kMaxAngleQ16;
    }

    int* current_angle = nullptr;
    int32_t* current_angle_q16 = nullptr;
    int* written_pulse_us = nullptr;
    if (channel == head_channel_) {
        current_angle = &current_head_angle_;
        current_angle_q16 = &current_head_angle_q16_;
        written_pulse_us = &written_head_pulse_us_;
    } else if (channel == rarm_channel_) {
        current_angle = &current_rarm_angle_;
        current_angle_q16 = &current_rarm_angle_q16_;
        written_pulse_us = &written_rarm_pulse_us_;
    } else if (channel == larm_channel_) {
        current_angle = &current_larm_angle_;
        current_angle_q16 = &current_larm_angle_q16_;
        written_pulse_us = &written_larm_pulse_us_;
    }

    if (current_angle_q16 != nullptr) {
        *current_angle_q16 = angle_q16;
    }
    if (current_angle != nullptr) {
        *current_angle = static_cast<int>((angle_q16 + (1 << 15)) >> 16);
    }

    int32_t physical_angle_q16 = invert ? kMaxAngleQ16 - angle_q16 : angle_q16;
    int pulse_us = SERVO_MIN_US + static_cast<int>(
        (static_cast<int64_t>(physical_angle_q16) * (SERVO_MAX_US - SERVO_MIN_US)) /
        kMaxAngleQ16);
    if (!servo_outputs_released_ && written_pulse_us != nullptr &&
        *written_pulse_us == pulse_us) {
        return;
    }

    uint32_t duty = (static_cast<uint32_t>(pulse_us) * ((1 << 13) - 1)) / SERVO_PERIOD_US;
    ledc_set_duty(SERVO_MODE, (ledc_channel_t)channel, duty);
    ledc_update_duty(SERVO_MODE, (ledc_channel_t)channel);

    if (written_pulse_us != nullptr) {
        *written_pulse_us = pulse_us;
    }
    servo_outputs_released_ = false;
}

void ServoController::InitServo(int channel, int gpio, int init_angle_deg) {
    ledc_channel_config_t cfg = {};
    cfg.gpio_num   = gpio;
    cfg.speed_mode = SERVO_MODE;
    cfg.channel    = (ledc_channel_t)channel;
    cfg.intr_type  = LEDC_INTR_DISABLE;
    cfg.timer_sel  = SERVO_TIMER;
    cfg.duty       = 0;
    cfg.hpoint     = 0;
    cfg.flags.output_invert = 0;
    esp_err_t err = ledc_channel_config(&cfg);
    ESP_LOGI(TAG, "LEDC ch%d GPIO%d %s", channel, gpio, err == ESP_OK ? "OK" : "FAIL");
    bool inv = (channel == rarm_channel_) ? invert_rarm_
             : (channel == larm_channel_) ? invert_larm_
             : invert_head_;
    SetAngle(channel, init_angle_deg, inv);
}

void ServoController::Initialize(int head_gpio, int right_arm_gpio, int left_arm_gpio,
                                  bool invert_head, bool invert_rarm, bool invert_larm) {
    if (initialized_) return;
    head_gpio_ = head_gpio; invert_head_ = invert_head;
    invert_rarm_ = invert_rarm; invert_larm_ = invert_larm;
    head_channel_ = 0; rarm_channel_ = 1; larm_channel_ = 2;

    ledc_timer_config_t tcfg = {};
    tcfg.speed_mode = SERVO_MODE; tcfg.duty_resolution = SERVO_RES;
    tcfg.timer_num = SERVO_TIMER; tcfg.freq_hz = SERVO_FREQ_HZ;
    tcfg.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));

    // Ketiga servo dinyalakan bergantian, bukan sekaligus, supaya lonjakan
    // arus saat boot tidak menumpuk pada satu titik dan menjatuhkan tegangan.
    InitServo(head_channel_, head_gpio, H_C);
    vTaskDelay(pdMS_TO_TICKS(SERVO_INIT_STAGGER_MS));
    InitServo(rarm_channel_, right_arm_gpio, A_DN);
    vTaskDelay(pdMS_TO_TICKS(SERVO_INIT_STAGGER_MS));
    InitServo(larm_channel_, left_arm_gpio, A_DN);
    vTaskDelay(pdMS_TO_TICKS(SERVO_REST_RELEASE_MS));
    ReleaseServoOutputs();

    cmd_queue_ = xQueueCreate(8, sizeof(CmdPacket));
    xTaskCreate(ServoTask, "servo_task", SERVO_TASK_STACK, this, 3, &task_handle_);
    enabled_ = true;
    initialized_ = true;
    ESP_LOGI(TAG, "READY h=%d r=%d(i=%d) l=%d(i=%d) default=ON",
             head_gpio, right_arm_gpio, invert_rarm, left_arm_gpio, invert_larm);
}

void ServoController::Enable()  {
    enabled_ = true;
    manual_override_until_ticks_ = 0;
    manual_pose_until_ticks_ = 0;
    knowledge_search_active_ = false;
    last_manual_move_ = ServoMove::NONE;
    last_manual_move_ticks_ = 0;
    ESP_LOGI(TAG, "ON");
}
void ServoController::Disable() {
    enabled_ = false;
    manual_override_until_ticks_ = 0;
    speaking_ = false;
    tts_stream_active_ = false;
    last_sentence_gesture_ticks_ = 0;
    knowledge_search_active_ = false;
    manual_pose_until_ticks_ = 0;
    last_manual_move_ = ServoMove::NONE;
    last_manual_move_ticks_ = 0;
    if (initialized_) {
        ClearCommandQueue();
        SetAngle(head_channel_, H_C,  invert_head_);
        SetAngle(rarm_channel_, A_DN, invert_rarm_);
        SetAngle(larm_channel_, A_DN, invert_larm_);
    }
    ESP_LOGI(TAG, "OFF");
}

void ServoController::SetEmotion(const char* emotion) {
    if (!initialized_ || !enabled_ || !emotion) return;
    if (IsManualOverrideActive() || IsManualPoseActive()) {
        ESP_LOGI(TAG, "Skip emotion '%s' during manual override", emotion);
        return;
    }
    CmdPacket pkt = {}; pkt.type = 0;
    strncpy(pkt.data, emotion, sizeof(pkt.data) - 1);
    if (!uxQueueSpacesAvailable(cmd_queue_)) { CmdPacket d; xQueueReceive(cmd_queue_, &d, 0); }
    xQueueSend(cmd_queue_, &pkt, 0);
}

void ServoController::SetSpeaking(bool speaking) {
    speaking_ = speaking;
    if (speaking) {
        next_speaking_motion_ticks_ =
            xTaskGetTickCount() + pdMS_TO_TICKS(SPEAKING_MOTION_INTERVAL_MS);
    } else {
        next_speaking_motion_ticks_ = 0;
        last_sentence_gesture_ticks_ = 0;
    }
}

void ServoController::SetTtsStreamActive(bool active) {
    tts_stream_active_ = active;
}

void ServoController::NotifyTtsSentence() {
    if (!initialized_ || !enabled_ || !tts_stream_active_.load() ||
        IsManualOverrideActive() || IsManualPoseActive()) {
        return;
    }
    last_sentence_gesture_ticks_ = xTaskGetTickCount();
    CmdPacket pkt = {};
    pkt.type = 3;
    if (!uxQueueSpacesAvailable(cmd_queue_)) {
        CmdPacket dropped;
        xQueueReceive(cmd_queue_, &dropped, 0);
    }
    xQueueSend(cmd_queue_, &pkt, 0);
}

void ServoController::SetKnowledgeSearchActive(bool active) {
    knowledge_search_active_ = active;
}

bool ServoController::IsMotionActive() const {
    return IsManualOverrideActive() || speaking_.load() || tts_stream_active_.load();
}

void ServoController::WakeGreeting() {
    if (!initialized_ || !enabled_ || IsManualPoseActive()) {
        return;
    }
    CmdPacket pkt = {};
    pkt.type = 2;
    if (!uxQueueSpacesAvailable(cmd_queue_)) {
        CmdPacket dropped;
        xQueueReceive(cmd_queue_, &dropped, 0);
    }
    xQueueSendToFront(cmd_queue_, &pkt, 0);
}

void ServoController::ExecuteMove(ServoMove move) {
    if (!initialized_ || !enabled_ || move == ServoMove::NONE) return;
    if (ShouldSkipDuplicateMove(move)) {
        ESP_LOGI(TAG, "Skip duplicate move: %d", (int)move);
        return;
    }
    ExtendManualOverride(pdMS_TO_TICKS(MANUAL_OVERRIDE_MS));
    ClearCommandQueue();
    CmdPacket pkt = {}; pkt.type = 1; pkt.data[0] = (uint8_t)move;
    if (!uxQueueSpacesAvailable(cmd_queue_)) { CmdPacket d; xQueueReceive(cmd_queue_, &d, 0); }
    xQueueSendToFront(cmd_queue_, &pkt, 0);
}

void ServoController::ClearCommandQueue() {
    if (cmd_queue_ == nullptr) {
        return;
    }
    CmdPacket dropped;
    while (xQueueReceive(cmd_queue_, &dropped, 0) == pdTRUE) {
    }
}

// Jalankan langkah tanpa kembali otomatis. Langkah terakhir menjadi posisi akhir.
void ServoController::ExtendManualOverride(TickType_t duration_ticks) {
    manual_override_until_ticks_.store(xTaskGetTickCount() + duration_ticks);
}

bool ServoController::IsManualOverrideActive() const {
    TickType_t now = xTaskGetTickCount();
    TickType_t until = manual_override_until_ticks_.load();
    return until != 0 && static_cast<int32_t>(until - now) > 0;
}

bool ServoController::IsManualPoseActive() const {
    TickType_t until = manual_pose_until_ticks_.load();
    return until != 0 && static_cast<int32_t>(until - xTaskGetTickCount()) > 0;
}

bool ServoController::ShouldSkipDuplicateMove(ServoMove move) {
    TickType_t now = xTaskGetTickCount();
    TickType_t duplicate_window = pdMS_TO_TICKS(DUPLICATE_MOVE_IGNORE_MS);
    // Bila perintah ini kembar dengan yang terakhir dijalankan, cap waktunya
    // sengaja tidak diperbarui. Kalau diperbarui pada jalur ini, jendela
    // abaikan akan bergeser setiap kali perintah diulang sehingga perintah
    // yang sama tidak pernah dijalankan lagi sampai pengguna berhenti
    // memintanya selama satu jendela penuh.
    if (last_manual_move_ == move &&
        static_cast<int32_t>(now - last_manual_move_ticks_) >= 0 &&
        static_cast<int32_t>(now - last_manual_move_ticks_) < static_cast<int32_t>(duplicate_window)) {
        return true;
    }
    last_manual_move_ = move;
    last_manual_move_ticks_ = now;
    return false;
}

bool ServoController::IsHoldMove(ServoMove move) const {
    switch (move) {
        case ServoMove::HEAD_TURN_RIGHT:
        case ServoMove::HEAD_TURN_LEFT:
        case ServoMove::HEAD_CENTER:
        case ServoMove::RAISE_RIGHT_ARM:
        case ServoMove::STRAIGHT_RIGHT_ARM:
        case ServoMove::RAISE_LEFT_ARM:
        case ServoMove::STRAIGHT_LEFT_ARM:
        case ServoMove::RAISE_BOTH_ARMS:
        case ServoMove::STRAIGHT_BOTH_ARMS:
        case ServoMove::HORMAT:
            return true;
        default:
            return false;
    }
}

bool ServoController::IsRestPosition() const {
    return current_head_angle_ == H_C &&
           current_rarm_angle_ == A_DN &&
           current_larm_angle_ == A_DN;
}

void ServoController::ReleaseServoOutputs() {
    if (!initialized_ && written_head_pulse_us_ < 0 &&
        written_rarm_pulse_us_ < 0 && written_larm_pulse_us_ < 0) {
        return;
    }
    ledc_stop(SERVO_MODE, (ledc_channel_t)head_channel_, 0);
    ledc_stop(SERVO_MODE, (ledc_channel_t)rarm_channel_, 0);
    ledc_stop(SERVO_MODE, (ledc_channel_t)larm_channel_, 0);
    servo_outputs_released_ = true;
}

void ServoController::MoveToAngles(int head_angle, int rarm_angle, int larm_angle, int duration_ms, bool manual_move) {
    const int32_t start_head_q16 = current_head_angle_q16_;
    const int32_t start_rarm_q16 = current_rarm_angle_q16_;
    const int32_t start_larm_q16 = current_larm_angle_q16_;
    const int32_t target_head_q16 = head_angle << 16;
    const int32_t target_rarm_q16 = rarm_angle << 16;
    const int32_t target_larm_q16 = larm_angle << 16;
    if (start_head_q16 == target_head_q16 &&
        start_rarm_q16 == target_rarm_q16 &&
        start_larm_q16 == target_larm_q16) {
        return;
    }

    // Batasi laju sudut: langkah yang meminta gerakan jauh dalam waktu singkat
    // diperpanjang durasinya supaya servo tidak berakselerasi tajam dan arus
    // puncaknya tidak melonjak.
    const int delta_head = head_angle > current_head_angle_ ? head_angle - current_head_angle_
                                                            : current_head_angle_ - head_angle;
    const int delta_rarm = rarm_angle > current_rarm_angle_ ? rarm_angle - current_rarm_angle_
                                                            : current_rarm_angle_ - rarm_angle;
    const int delta_larm = larm_angle > current_larm_angle_ ? larm_angle - current_larm_angle_
                                                            : current_larm_angle_ - larm_angle;
    int delta_maks = delta_head > delta_rarm ? delta_head : delta_rarm;
    if (delta_larm > delta_maks) {
        delta_maks = delta_larm;
    }
    const int durasi_minimum_ms = delta_maks * 1000 / SERVO_MAKS_DERAJAT_PER_DETIK;
    if (duration_ms < durasi_minimum_ms) {
        duration_ms = durasi_minimum_ms;
    }

    int frames = (duration_ms + SERVO_FRAME_MS - 1) / SERVO_FRAME_MS;
    if (frames < 1) {
        frames = 1;
    }

    // Tiap saluran memakai jendela gerak yang sama panjang, tetapi dimulai
    // bergeser beberapa frame supaya puncak arusnya tidak berbarengan.
    // Saluran yang tidak bergerak tidak ikut menggeser apa pun.
    const int geser_head = 0;
    int geser_total = 0;
    if (delta_rarm > 0) {
        geser_total += SERVO_STAGGER_FRAME;
    }
    const int geser_rarm = geser_total;
    if (delta_larm > 0) {
        geser_total += SERVO_STAGGER_FRAME;
    }
    const int geser_larm = geser_total;
    const int total_frames = frames + geser_total;

    TickType_t frame_clock = xTaskGetTickCount();
    for (int frame = 1; frame <= total_frames; frame++) {
        if (!enabled_) {
            return;
        }
        if (!manual_move && IsManualOverrideActive()) {
            return;
        }

        int head_frame = frame - geser_head;
        int rarm_frame = frame - geser_rarm;
        int larm_frame = frame - geser_larm;
        if (head_frame < 1) head_frame = 1;
        else if (head_frame > frames) head_frame = frames;
        if (rarm_frame < 1) rarm_frame = 1;
        else if (rarm_frame > frames) rarm_frame = frames;
        if (larm_frame < 1) larm_frame = 1;
        else if (larm_frame > frames) larm_frame = frames;

        int32_t next_head_q16 =
            EaseMinimumJerkQ16(start_head_q16, target_head_q16, head_frame, frames);
        int32_t next_rarm_q16 =
            EaseMinimumJerkQ16(start_rarm_q16, target_rarm_q16, rarm_frame, frames);
        int32_t next_larm_q16 =
            EaseMinimumJerkQ16(start_larm_q16, target_larm_q16, larm_frame, frames);

        SetAngleQ16(head_channel_, next_head_q16, invert_head_);
        SetAngleQ16(rarm_channel_, next_rarm_q16, invert_rarm_);
        SetAngleQ16(larm_channel_, next_larm_q16, invert_larm_);
        vTaskDelayUntil(&frame_clock, pdMS_TO_TICKS(SERVO_FRAME_MS));
    }
}

void ServoController::PlaySteps(const ServoController::ServoStep* st, int n, bool manual_move) {
    for (int i = 0; i < n; i++) {
        if (!enabled_) {
            return;
        }
        if (!manual_move && IsManualOverrideActive()) {
            return;
        }

        int head_angle = st[i].head_angle == KEEP ? current_head_angle_ : st[i].head_angle;
        int rarm_angle = st[i].rarm_angle == KEEP ? current_rarm_angle_ : st[i].rarm_angle;
        int larm_angle = st[i].larm_angle == KEEP ? current_larm_angle_ : st[i].larm_angle;
        MoveToAngles(head_angle, rarm_angle, larm_angle, st[i].delay_ms, manual_move);
    }

    if (IsRestPosition()) {
        vTaskDelay(pdMS_TO_TICKS(SERVO_REST_RELEASE_MS));
        ReleaseServoOutputs();
    }
}

void ServoController::PlaySpeakingMotion() {
    if (!enabled_ || !speaking_.load() || IsManualOverrideActive() ||
        IsManualPoseActive()) {
        return;
    }

    bool alternate = (speaking_motion_phase_++ & 1U) != 0U;
    if (knowledge_search_active_.load()) {
        if (alternate) {
            PlaySteps(SEQ(MOVE_SPEAKING_SEARCH_B));
        } else {
            PlaySteps(SEQ(MOVE_SPEAKING_SEARCH_A));
        }
    } else if (alternate) {
        PlaySteps(SEQ(MOVE_SPEAKING_HEAD_B));
    } else {
        PlaySteps(SEQ(MOVE_SPEAKING_HEAD_A));
    }
    next_speaking_motion_ticks_.store(
        xTaskGetTickCount() + pdMS_TO_TICKS(SPEAKING_MOTION_INTERVAL_MS));
}

void ServoController::ServoTask(void* arg) {
    auto* self = static_cast<ServoController*>(arg);
    CmdPacket pkt;
    while (true) {
        if (xQueueReceive(self->cmd_queue_, &pkt, pdMS_TO_TICKS(100)) != pdTRUE) {
            TickType_t now = xTaskGetTickCount();
            TickType_t next_motion = self->next_speaking_motion_ticks_.load();
            if (self->speaking_.load() && next_motion != 0 &&
                static_cast<int32_t>(now - next_motion) >= 0) {
                TickType_t last_sentence = self->last_sentence_gesture_ticks_.load();
                if (last_sentence != 0 &&
                    static_cast<int32_t>(now - last_sentence) <
                        static_cast<int32_t>(pdMS_TO_TICKS(SPEAKING_SENTENCE_FALLBACK_MS))) {
                    self->next_speaking_motion_ticks_ =
                        last_sentence + pdMS_TO_TICKS(SPEAKING_SENTENCE_FALLBACK_MS);
                    continue;
                }
                self->PlaySpeakingMotion();
            }
            continue;
        }

        if (pkt.type == 0) {
            if (!self->enabled_) continue;
            if (self->IsManualOverrideActive() || self->IsManualPoseActive()) continue;
            const EmotionSequence* seq = nullptr;
            for (int i = 0; i < NUM_SEQUENCES; i++)
                if (strcmp(SEQUENCES[i].emotion, pkt.data) == 0) { seq = &SEQUENCES[i]; break; }
            if (!seq) { ESP_LOGW(TAG, "?%s", pkt.data); continue; }
            ESP_LOGI(TAG, "E:%s", seq->emotion);
            self->PlaySteps(seq->steps, seq->num_steps);
        } else if (pkt.type == 2) {
            if (!self->enabled_ || self->IsManualOverrideActive() ||
                self->IsManualPoseActive()) {
                continue;
            }
            ESP_LOGI(TAG, "Sapaan wake word");
            self->PlaySteps(SEQ(MOVE_WAKE_GREETING));
        } else if (pkt.type == 3) {
            self->PlaySpeakingMotion();
        } else {
            if (!self->enabled_) continue;
            auto mv = (ServoMove)pkt.data[0];
            ESP_LOGI(TAG, "M:%d", (int)mv);
            const ServoController::ServoStep* st = nullptr; int n = 0;
            #define C(e,a) case ServoMove::e: st=a; n=sizeof(a)/sizeof(a[0]); break
            switch (mv) {
                C(HEAD_SHAKE,        MOVE_HEAD_SHAKE);
                C(HEAD_TURN_RIGHT,   MOVE_HEAD_RIGHT);
                C(HEAD_TURN_LEFT,    MOVE_HEAD_LEFT);
                C(HEAD_CENTER,       MOVE_HEAD_CENTER);
                C(WAVE_RIGHT_ARM,    MOVE_WAVE_RIGHT);
                C(RAISE_RIGHT_ARM,   MOVE_RAISE_RIGHT);
                C(STRAIGHT_RIGHT_ARM,MOVE_STRAIGHT_RIGHT);
                C(LOWER_RIGHT_ARM,   MOVE_LOWER_RIGHT);
                C(WAVE_LEFT_ARM,     MOVE_WAVE_LEFT);
                C(RAISE_LEFT_ARM,    MOVE_RAISE_LEFT);
                C(STRAIGHT_LEFT_ARM, MOVE_STRAIGHT_LEFT);
                C(LOWER_LEFT_ARM,    MOVE_LOWER_LEFT);
                C(WAVE_BOTH_ARMS,    MOVE_WAVE_BOTH);
                C(RAISE_BOTH_ARMS,   MOVE_RAISE_BOTH);
                C(STRAIGHT_BOTH_ARMS,MOVE_STRAIGHT_BOTH);
                C(DANCE,             MOVE_DANCE);
                C(SALAM,             MOVE_SALAM);
                C(HORMAT,            MOVE_HORMAT);
                C(TEPUK_TANGAN,      MOVE_TEPUK);
                C(MENYAPA,           MOVE_MENYAPA);
                C(RESET_POSITION,    MOVE_RESET);
                default: break;
            }
            #undef C
            if (st && n > 0) {
                self->PlaySteps(st, n, true);
                // Tahan posisi hanya selama jendela waktu tertentu. Tanpa batas
                // waktu, satu perintah seperti "angkat tangan kanan" akan
                // memblokir gestur emosi, sapaan, dan gerakan saat berbicara
                // selamanya karena penandanya tidak pernah turun sendiri.
                self->manual_pose_until_ticks_ = self->IsHoldMove(mv)
                    ? xTaskGetTickCount() + pdMS_TO_TICKS(MANUAL_POSE_HOLD_MS)
                    : 0;
                if (mv == ServoMove::RESET_POSITION) {
                    self->manual_pose_until_ticks_ = 0;
                }
                self->next_speaking_motion_ticks_.store(
                    xTaskGetTickCount() + pdMS_TO_TICKS(SPEAKING_MOTION_INTERVAL_MS));
            }
        }
    }
}

// =====================================================================
//  DETEKSI PERINTAH SUARA
// =====================================================================
ServoMove ServoController::DetectCommand(const char* text) {
    if (!text) return ServoMove::NONE;
    std::string s(text);
    for (auto& c : s) c = tolower((unsigned char)c);
    auto has = [&s](const char* needle) {
        return s.find(needle) != std::string::npos;
    };
    bool mentions_head = has("kepala") || has("head");
    bool mentions_arm = has("tangan") || has("lengan") || has("arm");
    bool mentions_both_arms = has("kedua") || has("dua tangan") || has("dua lengan") || has("both arms");
    bool mentions_robot = has("servo") || has("robot") || mentions_head || mentions_arm;
    bool mentions_specific_body_part = mentions_head || mentions_arm ||
        has("kanan") || has("kiri") || has("kedua") || has("dua tangan");
    bool explicit_command_context = mentions_robot || has("tolong") || has("coba") ||
        has("ayo") || has("silakan") || has("gerakkan") || has("gerakan") || has("lakukan");
    // 1. Matikan daya servo
    if (has("matikan servo") || has("servo off") ||
        has("nonaktifkan servo") || has("shutdown servo") ||
        has("servo berhenti") || has("hentikan servo") ||
        has("servo diam") || has("servo mati") ||
        has("stop servo") || has("berhentikan servo"))
        return ServoMove::DISABLE_SERVO;

    // 2. Nyalakan daya servo
    if ((has("hidupkan servo") || has("servo on") ||
        has("aktifkan servo") || has("nyalakan servo") ||
        has("gerakan servo") || has("gerakkan servo") ||
        has("jalankan servo") || has("servo aktif")) &&
        !mentions_specific_body_part)
        return ServoMove::ENABLE_SERVO;

    // 3. Reset posisi
    if (has("posisi semula") || has("posisi awal") ||
        (mentions_robot && has("reset")) || (mentions_robot && has("netral")) ||
        has("kembali semula") || has("posisi normal"))
        return ServoMove::RESET_POSITION;

    // 4. Arah kepala dengan posisi dipertahankan
    if (has("lihat kanan") || has("kepala ke kanan") ||
        has("kepala kekanan") || has("hadap kanan") ||
        has("ngadap kanan") || has("tengok kanan") ||
        has("toleh kanan"))
        return ServoMove::HEAD_TURN_RIGHT;

    if (has("lihat kiri") || has("kepala ke kiri") ||
        has("kepala kekiri") || has("hadap kiri") ||
        has("ngadap kiri") || has("tengok kiri") ||
        has("toleh kiri"))
        return ServoMove::HEAD_TURN_LEFT;

    if (has("lihat depan") || has("kepala ke depan") ||
        has("kepala kedepan") || has("kepala tengah") ||
        has("kepala lurus") || has("hadap depan"))
        return ServoMove::HEAD_CENTER;

    // 5. Geleng kepala
    if (has("geleng") || has("gelengkan") || has("shake head") || (mentions_head && has("shake")))
        return ServoMove::HEAD_SHAKE;

    // 6. Aksi
    if (has("hormat") || has("salute")) return ServoMove::HORMAT;
    if (explicit_command_context && has("salam"))  return ServoMove::SALAM;
    if (has("tepuk tangan") || has("clap")) return ServoMove::TEPUK_TANGAN;
    if (explicit_command_context && (has("menyapa") || has("beri salam") || has("sapalah") || has("robot sapa") || has("sapa ")))
        return ServoMove::MENYAPA;
    if (has("joget") || has("dance") || has("menari") || has("goyang"))
        return ServoMove::DANCE;

    // 7. Arahkan tangan ke depan atau lurus, diperiksa sebelum perintah atas atau bawah.
    if (((has("ke depan") || has("kedepan") || has("majukan")) && mentions_arm) ||
        (has("lurus") && mentions_robot)) {
        if (mentions_head && !mentions_arm) return ServoMove::HEAD_CENTER;
        if (has("kanan")) return ServoMove::STRAIGHT_RIGHT_ARM;
        if (has("kiri"))  return ServoMove::STRAIGHT_LEFT_ARM;
        return ServoMove::STRAIGHT_BOTH_ARMS;
    }

    // 9. Turunkan tangan atau arahkan ke bawah
    if ((has("turunkan") || has("kebawah") || has("ke bawah")) && mentions_robot) {
        if (has("kanan")) return ServoMove::LOWER_RIGHT_ARM;
        if (has("kiri"))  return ServoMove::LOWER_LEFT_ARM;
        return mentions_arm ? ServoMove::RESET_POSITION : ServoMove::NONE;
    }

    // 10. Angkat tangan atau arahkan ke atas dengan posisi dipertahankan
    if ((has("angkat") || has("keatas") || has("ke atas")) && mentions_robot) {
        if (mentions_both_arms) return ServoMove::RAISE_BOTH_ARMS;
        if (has("kanan")) return ServoMove::RAISE_RIGHT_ARM;
        if (has("kiri"))  return ServoMove::RAISE_LEFT_ARM;
        return ServoMove::RAISE_BOTH_ARMS;
    }

    // 11. Lambaikan. Jika sisi tidak jelas, default ke tangan kanan agar tidak menggerakkan dua tangan tanpa diminta.
    if ((has("lambaikan") || has("lambai") || has("kibas")) && mentions_robot) {
        if (mentions_both_arms) return ServoMove::WAVE_BOTH_ARMS;
        if (has("kanan")) return ServoMove::WAVE_RIGHT_ARM;
        if (has("kiri"))  return ServoMove::WAVE_LEFT_ARM;
        return ServoMove::WAVE_RIGHT_ARM;
    }

    // 12. Diam
    if ((has("diam") || has("tenang")) && mentions_robot) return ServoMove::RESET_POSITION;

    return ServoMove::NONE;
}
