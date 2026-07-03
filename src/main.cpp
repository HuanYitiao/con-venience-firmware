#include <Arduino.h>

#include <Wire.h>
#include <math.h>

#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"

#define NOTE_C4 262
#define NOTE_D4 294
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_G4 392

typedef struct
{
    uint32_t freq;
    uint32_t dur;
} music_note_t;

static const music_note_t odeToJoy[] = {
    {NOTE_E4, 300}, {NOTE_E4, 300}, {NOTE_F4, 300}, {NOTE_G4, 300}, {NOTE_G4, 300},
    {NOTE_F4, 300}, {NOTE_E4, 300}, {NOTE_D4, 300}, {NOTE_C4, 300}, {NOTE_C4, 300},
    {NOTE_D4, 300}, {NOTE_E4, 300}, {NOTE_E4, 450}, {NOTE_D4, 150}, {NOTE_D4, 600},

    {NOTE_E4, 300}, {NOTE_E4, 300}, {NOTE_F4, 300}, {NOTE_G4, 300}, {NOTE_G4, 300},
    {NOTE_F4, 300}, {NOTE_E4, 300}, {NOTE_D4, 300}, {NOTE_C4, 300}, {NOTE_C4, 300},
    {NOTE_D4, 300}, {NOTE_E4, 300}, {NOTE_D4, 450}, {NOTE_C4, 150}, {NOTE_C4, 600},
};

// ---- 按实际接线改 ----
#define PIN_I2S_BCLK 10
#define PIN_I2S_LRC 11
#define PIN_I2S_DIN 18

#define PIN_I2C_SDA 6
#define PIN_I2C_SCL 7

#define MCP_ADDR 0x20
#define MCP_REG_GPIO 0x09
#define MCP_REG_IODIR 0x00
#define MCP_AUDIO_SD_CH 6
// ---------------------

#define SAMPLE_RATE 44100
#define AMPLITUDE 4000

static const float       TWO_PI_F = 6.28318530718f;
static i2s_chan_handle_t txHandle = NULL;

static void mcpSetBit(uint8_t ch, bool high)
{
    Wire.beginTransmission(MCP_ADDR);
    Wire.write(MCP_REG_GPIO);
    Wire.endTransmission(false);
    Wire.requestFrom(MCP_ADDR, (uint8_t)1);
    uint8_t val = Wire.read();
    if (high)
    {
        val |= (1 << ch);
    }
    else
    {
        val &= ~(1 << ch);
    }
    Wire.beginTransmission(MCP_ADDR);
    Wire.write(MCP_REG_GPIO);
    Wire.write(val);
    Wire.endTransmission();
}

static void mcpSetOutput(uint8_t ch)
{
    Wire.beginTransmission(MCP_ADDR);
    Wire.write(MCP_REG_IODIR);
    Wire.endTransmission(false);
    Wire.requestFrom(MCP_ADDR, (uint8_t)1);
    uint8_t dir = Wire.read();
    dir &= ~(1 << ch);
    Wire.beginTransmission(MCP_ADDR);
    Wire.write(MCP_REG_IODIR);
    Wire.write(dir);
    Wire.endTransmission();
}

static void audioInit(void)
{
    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t         err = i2s_new_channel(&chanCfg, &txHandle, NULL);
    Serial0.printf("i2s_new_channel: %d, handle=%p\n", err, txHandle);

    i2s_std_config_t stdCfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = (gpio_num_t)PIN_I2S_BCLK,
                .ws = (gpio_num_t)PIN_I2S_LRC,
                .dout = (gpio_num_t)PIN_I2S_DIN,
                .din = I2S_GPIO_UNUSED,
                .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
            },
    };
    i2s_channel_init_std_mode(txHandle, &stdCfg);
    i2s_channel_enable(txHandle);

    mcpSetBit(MCP_AUDIO_SD_CH, false);  // 先把锁存值设为低
    mcpSetOutput(MCP_AUDIO_SD_CH);      // 再切输出 → 开机功放 shutdown,无嘶
}

static void audioEnable(void)
{
    mcpSetBit(MCP_AUDIO_SD_CH, true);
}

static void audioShutdown(void)
{
    mcpSetBit(MCP_AUDIO_SD_CH, false);
}

static void playTone(uint32_t freqHz, uint32_t durationMs)
{
    const size_t totalSamples = ((uint64_t)SAMPLE_RATE * durationMs) / 1000;
    const float  step = TWO_PI_F * (float)freqHz / (float)SAMPLE_RATE;
    const size_t fadeSamples = SAMPLE_RATE / 200;  // 5ms 淡入淡出

    int16_t buffer[256];
    float   phase = 0.0f;
    size_t  played = 0;

    while (played < totalSamples)
    {
        size_t n = (totalSamples - played) < 256 ? (totalSamples - played) : 256;
        for (size_t i = 0; i < n; i++)
        {
            size_t idx = played + i;
            float  env = 1.0f;
            if (idx < fadeSamples)
            {
                env = (float)idx / (float)fadeSamples;  // 淡入
            }
            else if (idx >= totalSamples - fadeSamples)
            {
                env = (float)(totalSamples - 1 - idx) / (float)fadeSamples;  // 淡出
            }
            float wave = (phase < 3.14159265f) ? 1.0f : -1.0f;  // 方波:前半周期 +,后半周期 -
            buffer[i] = (int16_t)(wave * AMPLITUDE * env);
            phase += step;
            if (phase >= TWO_PI_F)
            {
                phase -= TWO_PI_F;
            }
        }
        size_t bytesWritten = 0;
        i2s_channel_write(txHandle, buffer, n * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        played += n;
    }
}

static void audioRest(uint32_t durationMs)
{
    const size_t totalSamples = ((uint64_t)SAMPLE_RATE * durationMs) / 1000;
    int16_t      buffer[256] = {0};  // 全 0 静音,喂给 I2S 保持 DMA 不欠载
    size_t       remaining = totalSamples;

    while (remaining > 0)
    {
        size_t n = remaining < 256 ? remaining : 256;
        size_t bytesWritten = 0;
        i2s_channel_write(txHandle, buffer, n * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        remaining -= n;
    }
}

void setup()
{
    Serial0.begin(115200);
    delay(500);
    Serial0.println("=== ode to joy ===");

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    audioInit();

    audioEnable();
    audioRest(20);  // 时钟已在跑,SD 已拉高,喂点静音让功放 settle

    for (size_t i = 0; i < sizeof(odeToJoy) / sizeof(odeToJoy[0]); i++)
    {
        playTone(odeToJoy[i].freq, odeToJoy[i].dur);
        audioRest(30);  // 音符间隔,让连续同音(E E / G G / C C)能分开
    }

    audioShutdown();
    Serial0.println("=== done ===");
}

void loop()
{
}