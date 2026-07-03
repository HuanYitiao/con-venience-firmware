#pragma once

#include <stddef.h>
#include <stdint.h>

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_AMPLITUDE 8000
#define AUDIO_FADE_MS 5

typedef enum
{
    AUDIO_WAVE_SINE,
    AUDIO_WAVE_SQUARE,
    AUDIO_WAVE_PULSE,
} audio_waveform_t;

typedef struct
{
    uint32_t freq;
    uint32_t durationMs;
} audio_note_t;

void audio_init(void);
void audio_deinit(void);

void audio_setWaveform(audio_waveform_t wave);

void audio_enable(void);
void audio_shutdown(void);

void audio_playTone(uint32_t freqHz, uint32_t durationMs);
void audio_rest(uint32_t durationMs);

void audio_playSequence(const audio_note_t *notes, size_t count, uint32_t gapMs);