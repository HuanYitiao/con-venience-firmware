#pragma once
#include "pitches.h"

#define BUZZER_PIN 18

struct Note {
    int frequency;
    int division;
};

struct Music {
    int bpm;
    Note* melody;
    int length;
};

void playMusic(Music music);
void playRandomMusic(Music musics[], int length);
