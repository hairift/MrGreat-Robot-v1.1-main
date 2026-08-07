#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "music_player.h"
#include "servo_controller.h"
#include "servo_mcp_tools.h"
#include "campus_mcp_tools.h"
#include "adc_battery_monitor.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "CompactWifiBoard"

class CompactWifiBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    Button asr_button_;
    AdcBatteryMonitor* adc_battery_monitor_ = nullptr;

    void InitializePowerManager() {
        adc_battery_monitor_ = new AdcBatteryMonitor(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL,
            BATTERY_R_UPPER, BATTERY_R_LOWER, BATTERY_CHARGING_GPIO);
    }

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // Pompa muatan OLED perlu waktu stabil setelah daya dinyalakan.
        ESP_LOGI(TAG, "Menunggu OLED stabil");
        vTaskDelay(pdMS_TO_TICKS(500));

        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .scl_speed_hz = 400 * 1000,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Memasang driver SSD1306");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_LOGI(TAG, "Driver SSD1306 berhasil dipasang");

        // Panel kadang belum siap pada percobaan pertama, jadi inisialisasi
        // diulang beberapa kali sebelum menyerah ke tampilan kosong.
        bool init_ok = false;
        for (int attempt = 1; attempt <= 3; ++attempt) {
            ESP_LOGI(TAG, "Inisialisasi OLED percobaan %d dari 3", attempt);
            ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
            vTaskDelay(pdMS_TO_TICKS(100));
            if (esp_lcd_panel_init(panel_) == ESP_OK) {
                init_ok = true;
                break;
            }
            ESP_LOGW(TAG, "Inisialisasi OLED gagal, mencoba lagi");
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if (!init_ok) {
            // Robot tetap berfungsi penuh tanpa layar; suara dan gerakan
            // servo menjadi penanda statusnya.
            ESP_LOGE(TAG, "OLED tidak dapat diinisialisasi, memakai tampilan kosong");
            display_ = new NoDisplay();
            return;
        }

        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeButtons() {
        auto tangani_tombol_percakapan = [this](const char* sumber) {
            ESP_LOGI(TAG, "Tombol %s ditekan", sumber);

            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }

            // Saat lagu sedang berbunyi, tombol berfungsi sebagai tombol henti
            // dan tidak membangunkan AI.
            //
            // Mr Great tidak punya foil sentuh, jadi tombol inilah satu-satunya
            // kendali fisik. Deteksi kata pemicu memang dimatikan selama lagu
            // diputar, karena tanpa peredam gema mikrofon mendengar lagunya
            // sendiri dan potongan lagu dapat dikenali keliru sebagai kata
            // pemicu, sehingga lagu berhenti sendiri di tengah.
            if (MusicPlayer::GetInstance().IsPlaying()) {
                ESP_LOGI(TAG, "Lagu dihentikan lewat tombol %s", sumber);
                MusicPlayer::GetInstance().StopAndWait();
                return;
            }
            if (app.GetDeviceState() == kDeviceStateWifiConfiguring ||
                app.GetDeviceState() == kDeviceStateAudioTesting) {
                // Papan ini menyediakan Wi-Fi lewat hotspot, jadi tombol tidak
                // boleh masuk ke mode uji mikrofon saat konfigurasi berjalan.
                ESP_LOGW(TAG, "Tombol diabaikan saat konfigurasi Wi-Fi aktif");
                return;
            }
            app.ToggleChatState();
        };

        boot_button_.OnClick([tangani_tombol_percakapan]() {
            tangani_tombol_percakapan("BOOT");
        });

        // Tombol taktil eksternal di GPIO1 dibuat sama perilakunya dengan
        // tombol BOOT bawaan papan.
        asr_button_.OnClick([tangani_tombol_percakapan]() {
            tangani_tombol_percakapan("GPIO1");
        });
    }

    // Inisialisasi alat MCP, servo, dan basis pengetahuan kampus.
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
        ServoController::GetInstance().Initialize(SERVO_HEAD_GPIO, SERVO_RARM_GPIO, SERVO_LARM_GPIO,
            false,  // kepala tanpa pembalikan arah
            false,  // lengan kanan tanpa pembalikan arah
            true);  // lengan kiri dibalik agar geraknya simetris dengan lengan kanan
        static ServoMcpTools servo_mcp;
        static CampusMcpTools campus_mcp;
    }

public:
    CompactWifiBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        asr_button_(ASR_BUTTON_GPIO) {
        InitializePowerManager();
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeTools();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        // Mikrofon INMP441 dan penguat MAX98357 pada mode I2S simpleks.
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        static bool audio_masukan_disetel = false;
        if (!audio_masukan_disetel) {
            // Penegas nada tinggi untuk mengatasi suara yang terdengar mendem.
            //
            // Pengukuran pada rekaman uji 16 kHz menunjukkan tenaga suara
            // menumpuk di bawah 1 kHz sebanyak 91 persen, sementara pita
            // konsonan 1 sampai 3 kHz hanya 8,5 persen. Mikrofon berada di
            // dalam badan robot, dan badan itulah yang meredam nada tingginya.
            //
            // Rak 1800 Hz sebesar 12 dB menaikkan pita 1 sampai 3 kHz menjadi
            // 24,8 persen, yang sudah masuk rentang ucapan jernih. Penguatan
            // awal 0,7 memberi ruang puncak sehingga hasilnya tidak terpotong
            // sama sekali: pada rekaman uji puncaknya 91,3 persen skala penuh
            // dengan nol cuplikan terpotong.
            audio_codec.SetInputHighShelf(1800.0f, 12.0f, 0.7f);
            audio_masukan_disetel = true;
        }
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        static bool ada_nilai_tersimpan = false;
        static int level_tersimpan = 0;
        static bool sedang_mengisi_tersimpan = false;
        static bool sedang_terpakai_tersimpan = false;

        if (adc_battery_monitor_ == nullptr || !adc_battery_monitor_->IsValid()) {
            return false;
        }

        // Pembacaan ADC dihindari saat servo bergerak. Step-up dan SG90 sama-sama
        // menimbulkan riak sesaat pada pembagi tegangan baterai, sehingga nilai
        // yang terbaca saat itu jauh lebih rendah daripada tegangan sebenarnya.
        if (ServoController::GetInstance().IsMotionActive()) {
            if (!ada_nilai_tersimpan) {
                return false;
            }
            level = level_tersimpan;
            charging = sedang_mengisi_tersimpan;
            discharging = sedang_terpakai_tersimpan;
            return true;
        }

        charging = adc_battery_monitor_->IsCharging();
        discharging = adc_battery_monitor_->IsDischarging();
        level = adc_battery_monitor_->GetBatteryLevel();
        level_tersimpan = level;
        sedang_mengisi_tersimpan = charging;
        sedang_terpakai_tersimpan = discharging;
        ada_nilai_tersimpan = true;
        return true;
    }
};

DECLARE_BOARD(CompactWifiBoard);
