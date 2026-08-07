#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// I2S simpleks: mikrofon dan pengeras suara memakai kanal terpisah.
#define AUDIO_I2S_METHOD_SIMPLEX

#ifdef AUDIO_I2S_METHOD_SIMPLEX

// Mikrofon INMP441
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6

// Penguat MAX98357
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

#else

#define AUDIO_I2S_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7

#endif


#define BUILTIN_LED_GPIO        GPIO_NUM_48
#define BOOT_BUTTON_GPIO        GPIO_NUM_0

#define TOUCH_BUTTON_GPIO       GPIO_NUM_NC
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

// Tombol taktil harian untuk membangunkan Mr Great atau mengganti status
// percakapan. Sambungkan satu kaki tombol ke GPIO1 dan kaki sisi seberangnya
// ke GND; firmware memakai pull-up internal.
//
// Untuk tombol taktil empat kaki, jangan memakai kiri atas + kiri bawah atau
// kanan atas + kanan bawah bila kedua kaki itu sudah tersambung permanen.
// Pakai dua kaki yang berbeda sisi, misalnya kiri atas + kanan atas.
//
// GPIO2 dipakai ADC baterai, jadi tidak boleh dipakai lagi sebagai tombol.
#define ASR_BUTTON_GPIO         GPIO_NUM_1

// USB bawaan ESP32-S3: D- di GPIO19 dan D+ di GPIO20 dipakai untuk data USB.
// Jalur itu bukan pembaca baterai, bukan tombol, dan tidak dapat menyalakan
// papan tanpa jalur daya tersendiri.

// OLED SSD1306 0,96 inci lewat I2C.
#define DISPLAY_SDA_PIN GPIO_NUM_10
#define DISPLAY_SCL_PIN GPIO_NUM_9
#define DISPLAY_WIDTH   128

#if CONFIG_OLED_SSD1306_128X32
#define DISPLAY_HEIGHT  32
#elif CONFIG_OLED_SSD1306_128X64
#define DISPLAY_HEIGHT  64
#elif CONFIG_OLED_SH1106_128X64
#define DISPLAY_HEIGHT  64
#define SH1106
#else
#error "OLED display type is not selected"
#endif

#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true


// Uji MCP: kendalikan lampu.
#define LAMP_GPIO GPIO_NUM_18

// ---------------------------------------------------------------------------
// Servo mikro SG90 Mr Great
//
// Kepala hanya bergerak kiri dan kanan, bukan mengangguk naik turun. Kedua
// tangan bergerak naik turun. Ketiganya diberi daya dari jalur 5V yang stabil,
// dan ground servo wajib tersambung ke ground ESP32-S3 agar sinyal PWM terbaca
// benar.
// ---------------------------------------------------------------------------
#define SERVO_HEAD_GPIO     GPIO_NUM_17   // kepala
#define SERVO_RARM_GPIO     GPIO_NUM_38   // tangan kanan
#define SERVO_LARM_GPIO     GPIO_NUM_39   // tangan kiri

// Pembacaan baterai lewat pembagi tegangan 100 kOhm + 100 kOhm, dengan
// kapasitor 100 nF dari titik ADC ke GND.
//
// Masukan pembagi harus diambil dari sisi baterai atau keluaran TP4056, bukan
// dari keluaran boost 5V MT3608. Bila diambil dari keluaran boost, yang terbaca
// adalah tegangan regulator 5V, bukan tegangan baterai yang sebenarnya.
#define BATTERY_ADC_GPIO      GPIO_NUM_2
#define BATTERY_ADC_UNIT      ADC_UNIT_1
#define BATTERY_ADC_CHANNEL   ADC_CHANNEL_1
#define BATTERY_CHARGING_GPIO GPIO_NUM_NC
#define BATTERY_R_UPPER       100000.0f
#define BATTERY_R_LOWER       100000.0f

#endif // _BOARD_CONFIG_H_
