#include "bootseq.h"
#include <stdlib.h>
#include "raylib.h"

Sound beep_sound = {0};

bootup_sequence_line bootup_sequence[] = {
    {"NyxLoader Boot Loader v1.4", 500, false, &bootseq_beep},
    {"Loading Kernel", 500, false, NULL},
    {"Loading Kernel .", 500, true, NULL},
    {"Loading Kernel ..", 1000, true, NULL},
    {"Loading Kernel ...", 500, true, NULL},
    {"Loading Kernel ... OK", 200, true, NULL},
    {"Passing control to kernel", 200, false, NULL},
    {"", 600, false, NULL},
    {"HemeraKernel v2.4.1", 100, false, NULL},
    {"Copyright (c) 1987 HemeraLabs", 100, false, NULL},
    {"", 600, false, NULL},
    {"CPU: h512X3 @ 66MHz", 100, false, NULL},
    {"Memory: 12MB detected", 100, false, NULL},
    {"", 600, false, NULL},
    {"Probing system components", 1000, false, NULL},
    {"Probing system components .", 1000, true, NULL},
    {"Probing system components ..", 1000, true, NULL},
    {"Probing system components ...", 1000, true, NULL},
    {"ART bus detected", 100, false, NULL},
    {"PRC bus detected", 100, false, NULL},
    {"", 600, false, NULL},
    {"Mounting root filesystem .", 300, false, NULL},
    {"Mounting root filesystem ..", 300, true, NULL},
    {"Mounting root filesystem ...", 300, true, NULL},
    {"Root mounted from AO0 at /", 300, false, NULL},
    {"", 600, false, NULL},
    {"Bringing up network interfaces .", 300, false, NULL},
    {"Bringing up network interfaces ..", 300, true, NULL},
    {"Bringing up network interfaces ...", 300, true, NULL},
    {"Available network interfaces:", 300, false, NULL},
    {"lo0: loopback configured", 300, false, NULL},
    {"eth0: address 00:40:12:3A:9F:2C", 300, false, NULL},
    {"", 600, false, NULL},
    {"Getting IP address for eth0", 1000, false, NULL},
    {"eth0: 192.168.1.96", 300, false, NULL},
    {"", 600, false, NULL},
    {"Network ready.", 300, false, NULL},
    {"Starting up user environment .", 300, false, NULL},
    {"Starting up user environment ..", 300, true, NULL},
    {"Starting up user environment ...", 300, true, NULL},
    {"Starting up user environment ... OK", 300, true, NULL},
    {"", 600, false, NULL},
    {"Welcome to HemeraOS v1.0.2 (Ciros)", 4000, false, NULL},
};
const int bootup_sequence_count = sizeof(bootup_sequence) / sizeof(bootup_sequence[0]);

void bootseq_init() {
    beep_sound = LoadSound("assets/sounds/beep.wav");
}

void bootseq_beep() {
    PlaySound(beep_sound);
}

bootup_sequence_line *bootseq_get_line(int i) {
    if (i >= 0 && i < bootup_sequence_count) {
        return &bootup_sequence[i];
    }
    return NULL;
}
