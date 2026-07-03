#include "audio.h"

#include <Wire.h>
#include <math.h>

#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "pins.h"

#define MCP_ADDR 0x20
#define MCP_REG_IODIR 0x00
#define MCP_REG_GPIO 0x09

static const float TWO_PI_F = 6.28318530718f;

static i2s_chan_handle_t txHandle = NULL;
static audio_waveform_t  currentWave = AUDIO_WAVE_SINE;

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

static float audioSample(float phase)
{
    switch (currentWave)
    {
        case AUDIO_WAVE_SQUARE:
            return (phase < 3.14159265f) ? 1.0f : -1.0f;
        case AUDIO_WAVE_PULSE:
            return (phase < TWO_PI_F * 0.25f) ? 1.0f : -1.0f;
        case AUDIO_WAVE_SINE:
        default:
            return sinf(phase);
    }
}

void audio_init(void)
{
    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chanCfg, &txHandle, NULL);

    i2s_std_config_t stdCfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
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

    mcpSetBit(PIN_MCP_AUDIO_SD, false);
    mcpSetOutput(PIN_MCP_AUDIO_SD);
}

void audio_deinit(void)
{
    if (txHandle != NULL)
    {
        mcpSetBit(PIN_MCP_AUDIO_SD, false);
        i2s_channel_disable(txHandle);
        i2s_del_channel(txHandle);
        txHandle = NULL;
    }
}

void audio_setWaveform(audio_waveform_t wave)
{
    currentWave = wave;
}

void audio_enable(void)
{
    mcpSetBit(PIN_MCP_AUDIO_SD, true);
}

void audio_shutdown(void)
{
    mcpSetBit(PIN_MCP_AUDIO_SD, false);
}

void audio_playTone(uint32_t freqHz, uint32_t durationMs)
{
    const size_t totalSamples = ((uint64_t)AUDIO_SAMPLE_RATE * durationMs) / 1000;
    const float  step = TWO_PI_F * (float)freqHz / (float)AUDIO_SAMPLE_RATE;

    size_t fadeSamples = (AUDIO_SAMPLE_RATE * AUDIO_FADE_MS) / 1000;
    if (fadeSamples * 2 > totalSamples)
    {
        fadeSamples = totalSamples / 2;
    }

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
            if (fadeSamples > 0 && idx < fadeSamples)
            {
                env = (float)idx / (float)fadeSamples;
            }
            else if (fadeSamples > 0 && idx >= totalSamples - fadeSamples)
            {
                env = (float)(totalSamples - 1 - idx) / (float)fadeSamples;
            }
            buffer[i] = (int16_t)(audioSample(phase) * AUDIO_AMPLITUDE * env);
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

void audio_rest(uint32_t durationMs)
{
    const size_t totalSamples = ((uint64_t)AUDIO_SAMPLE_RATE * durationMs) / 1000;
    int16_t      buffer[256] = {0};
    size_t       remaining = totalSamples;

    while (remaining > 0)
    {
        size_t n = remaining < 256 ? remaining : 256;
        size_t bytesWritten = 0;
        i2s_channel_write(txHandle, buffer, n * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        remaining -= n;
    }
}

void audio_playSequence(const audio_note_t *notes, size_t count, uint32_t gapMs)
{
    audio_rest(20);
    audio_enable();
    audio_rest(5);

    for (size_t i = 0; i < count; i++)
    {
        audio_playTone(notes[i].freq, notes[i].durationMs);
        if (gapMs > 0)
        {
            audio_rest(gapMs);
        }
    }

    audio_shutdown();
}