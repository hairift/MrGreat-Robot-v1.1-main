#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"

#define TAG "main"

// Versi proyek ini, terpisah dari PROJECT_VER.
//
// PROJECT_VER sengaja dibiarkan mengikuti versi firmware hulu karena angka itu
// dipakai server OTA untuk membandingkan versi. Menurunkannya membuat server
// menganggap ada versi yang lebih baru, dan pembaruan berjalan otomatis saat
// boot, sehingga seluruh kustomisasi di sini akan tertimpa firmware hulu.
#define VERSI_MRGREAT "V1.1"

// Nama alasan reboot dalam bahasa Indonesia supaya penyebab perangkat
// memulai ulang dapat langsung dibaca pada monitor serial.
//
// BROWNOUT berarti pasokan daya sempat turun di bawah ambang, biasanya karena
// servo menarik arus besar. PANIC berarti ada kesalahan perangkat lunak.
// TASK_WDT dan INT_WDT berarti ada tugas yang macet. Bila restart terjadi,
// baris "Alasan reboot" inilah yang menentukan arah perbaikannya.
static const char* NamaAlasanReboot(esp_reset_reason_t alasan)
{
    switch (alasan) {
        case ESP_RST_POWERON:   return "POWERON (dinyalakan dari mati)";
        case ESP_RST_EXT:       return "EXT (pin reset eksternal)";
        case ESP_RST_SW:        return "SW (restart perangkat lunak)";
        case ESP_RST_PANIC:     return "PANIC (kesalahan perangkat lunak)";
        case ESP_RST_INT_WDT:   return "INT_WDT (watchdog interupsi)";
        case ESP_RST_TASK_WDT:  return "TASK_WDT (tugas macet)";
        case ESP_RST_WDT:       return "WDT (watchdog lain)";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP (bangun dari tidur dalam)";
        case ESP_RST_BROWNOUT:  return "BROWNOUT (tegangan pasokan turun)";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "TIDAK DIKETAHUI";
    }
}

extern "C" void app_main(void)
{
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Mr Great Robot %s", VERSI_MRGREAT);
    ESP_LOGW(TAG, "Alasan reboot: %s", NamaAlasanReboot(esp_reset_reason()));

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();  // This function runs the main event loop and never returns
}
