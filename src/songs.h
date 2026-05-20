#pragma once
#include "music.h"

Note bootMusic[] = {
    {NOTE_D5, 4},
    {NOTE_D5, 4},
    {NOTE_D5, 4},
    {NOTE_D5, 4},
    {NOTE_D5, 4},
    {NOTE_E5, 4},
    {NOTE_D5, 2},
    {NOTE_D5, 4},
    {NOTE_G5, 4},
    {NOTE_FS5, 4},
    {NOTE_E5, 4},
    {NOTE_D5, 4},
    {NOTE_E5, 4},
    {NOTE_D5, 2}
};

Note jasmineMusic[] = {
    {NOTE_B4, 4},
    {NOTE_B4, 4},
    {NOTE_D5, 4},
    {NOTE_E5, 4},
    {NOTE_G4, 2},
    {NOTE_E5, 4},
    {NOTE_D5, 2},
    {NOTE_E5, 4},
    {NOTE_D5, 4},
};

Note kangdingMusic[] = {
    {NOTE_A4, 4},
    {NOTE_C5, 4},
    {NOTE_D5, 4},
    {NOTE_D5, 4},
    {NOTE_C5, 4},
    {NOTE_D5, 4},
    {NOTE_A4, 4},
    {NOTE_G4, 4},
    {NOTE_A4, 4},
    {NOTE_C5, 4},
    {NOTE_D5, 4},
    {NOTE_D5, 4},
    {NOTE_C5, 4},
    {NOTE_D5, 4}, 
    {NOTE_A4, 4},
};

Music boot = {240, bootMusic, sizeof(bootMusic)/sizeof(bootMusic[0])};
Music jasmine = {240, jasmineMusic, sizeof(jasmineMusic)/sizeof(jasmineMusic[0])};
Music kangding = {240, kangdingMusic, sizeof(kangdingMusic)/sizeof(kangdingMusic[0])};

Music musics[] = {boot, jasmine, kangding};