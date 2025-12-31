#include "raylib.h"

Sound beep_sound = {0};

void bootseq_init() {
    beep_sound = LoadSound("assets/sounds/beep.wav");
}

void bootseq_beep() {
    PlaySound(beep_sound);
}
