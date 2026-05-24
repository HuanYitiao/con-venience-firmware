#include "music.h"

#include <Arduino.h>

void playMusic(Music music)
{
    for (int i = 0; i < music.length; i++)
    {
        Note note = music.melody[i];
        int  delayTime = 60000 / music.bpm * 4 / note.division;
        tone(BUZZER_PIN, note.frequency, delayTime);
        delay(delayTime + 5);
    }
}

void playRandomMusic(Music musics[], int length)
{
    int ran = random(0, length);
    playMusic(musics[ran]);
}