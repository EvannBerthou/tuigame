#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "raylib.h"

#define MAX_SAMPLES 512
#define MAX_SAMPLES_PER_UPDATE 4096
#define SAMPLE_RATE 44100

#define FADE_SAMPLES 256
int totalSamples = 0;

#define NOTE_NONE 0.f
#define NOTE_C4 261.63f
#define NOTE_CS4 277.18f
#define NOTE_D4 293.66f
#define NOTE_DS4 311.13f
#define NOTE_E4 329.63f
#define NOTE_F4 349.23f
#define NOTE_FS4 369.99f
#define NOTE_G4 392.00f
#define NOTE_GS4 415.30f
#define NOTE_A4 440.00f
#define NOTE_AS4 466.16f
#define NOTE_B4 493.88f

#define NOTE_C3 130.81f
#define NOTE_F3 174.61f
#define NOTE_G3 196.00f
#define NOTE_C5 523.25f

#define NOTE_D5 587.33f
#define NOTE_E5 659.25f

#define NOTE_C5 523.25f
#define NOTE_D5 587.33f
#define NOTE_E5 659.25f
#define NOTE_F5 698.46f
#define NOTE_G5 783.99f
#define NOTE_A5 880.00f
#define NOTE_B5 987.77f
#define NOTE_C6 1046.50f
#define NOTE_A3 220.00f
#define NOTE_B3 246.94f

float noteTimer = 0.0f;
float tempo = 0.5f;
bool music_playing = true;

typedef enum { WAVE_SINE, WAVE_SQUARE, WAVE_SAW, WAVE_TRIANGLE, WAVE_NOISE, WAVE_COUNT } Waveform;
const char *wave_text[WAVE_COUNT] = {"SINE", "SQUARE", "SAW", "TRIANGLE", "NOISE"};

typedef struct {
    Waveform wave;
    float freq;
    float duration;
    bool started;
} MultiNote;

typedef struct {
    bool active;
    float freq;
    float phase;
    float volume;
    bool muted;

    int samplesLeft;
    int totalSamples;

    float timer;
    float duration;

    MultiNote *queue;
} Voice;

Voice voices[WAVE_COUNT] = {0};

void PlayNoteOnWave(Waveform wave, float freq, float duration) {
    Voice *v = &voices[wave];
    v->freq = freq;
    v->totalSamples = (int)(duration * SAMPLE_RATE);
    v->samplesLeft = v->totalSamples;
    v->active = true;
    v->duration = duration;
    v->timer = 0;
}

unsigned int lfsr = 1;
float generate_noise() {
    unsigned int bit = ((lfsr >> 0) ^ (lfsr >> 1)) & 1;
    lfsr = (lfsr >> 1) | (bit << 14);
    return (lfsr & 1) ? 1.0f : -1.0f;
}

float GenerateWave(Waveform wave, float phase) {
    switch (wave) {
        case WAVE_SINE:
            return sinf(2.0f * PI * phase);
        case WAVE_SQUARE:
            return phase < 0.25f ? 1.0f : -1.0f;
        case WAVE_SAW:
            return 2.0f * phase - 1.0f;
        case WAVE_TRIANGLE:
            return 1.0f - 4.0f * fabsf(roundf(phase - 0.25f) - (phase - 0.25f));
        case WAVE_NOISE:
            return generate_noise();
        default:
            exit(1);
    }
    return 0.0f;
}

void AudioInputCallback(void *buffer, unsigned int frames) {
    short *d = (short *)buffer;

    for (unsigned int i = 0; i < frames; i++) {
        float mix = 0.0f;

        for (int i = 0; i < WAVE_COUNT; i++) {
            Voice *v = &voices[i];
            if (!v->active) {
                continue;
            }
            float volume = v->muted ? 0 : v->volume;

            mix += GenerateWave(i, v->phase) * volume;
            v->phase += v->freq / SAMPLE_RATE;
            if (v->phase > 1.0f)
                v->phase -= 1.0f;
            if (--v->samplesLeft <= 0) {
                if (v->queue != NULL) {
                    PlayNoteOnWave(v->queue->wave, v->queue->freq, v->queue->duration);
                    v->queue = NULL;
                } else {
                    v->active = false;
                }
            }
        }
        mix *= 0.25f;
        d[i] = (short)(mix * 32000.0f);
    }
}

MultiNote song[] = {
    // Melody (sine)
    {WAVE_SINE, NOTE_E5, 0.5f, false},
    {WAVE_SINE, NOTE_B4, 0.25f, false},
    {WAVE_SINE, NOTE_C5, 0.25f, false},
    {WAVE_SINE, NOTE_D5, 0.5f, false},
    {WAVE_SINE, NOTE_C5, 0.25f, false},
    {WAVE_SINE, NOTE_B4, 0.25f, false},
    {WAVE_SINE, NOTE_A4, 0.5f, false},
    {WAVE_SINE, NOTE_A4, 0.25f, false},
    {WAVE_SINE, NOTE_C5, 0.25f, false},
    {WAVE_SINE, NOTE_E5, 0.5f, false},
    {WAVE_SINE, NOTE_D5, 0.25f, false},
    {WAVE_SINE, NOTE_C5, 0.25f, false},

    // Phrase 2
    {WAVE_SINE, NOTE_B4, 0.75f, false},
    {WAVE_SINE, NOTE_C5, 0.25f, false},
    {WAVE_SINE, NOTE_D5, 0.5f, false},
    {WAVE_SINE, NOTE_E5, 0.5f, false},
    {WAVE_SINE, NOTE_C5, 0.5f, false},
    {WAVE_SINE, NOTE_A4, 0.5f, false},
    {WAVE_SINE, NOTE_A4, 1.0f, false},

    // Phrase 3
    {WAVE_SINE, NOTE_D5, 0.5f, false},
    {WAVE_SINE, NOTE_F5, 0.25f, false},
    {WAVE_SINE, NOTE_A5, 0.5f, false},
    {WAVE_SINE, NOTE_G5, 0.25f, false},
    {WAVE_SINE, NOTE_F5, 0.25f, false},
    {WAVE_SINE, NOTE_E5, 0.75f, false},
    {WAVE_SINE, NOTE_C5, 0.25f, false},
    {WAVE_SINE, NOTE_E5, 0.5f, false},
    {WAVE_SINE, NOTE_D5, 0.25f, false},
    {WAVE_SINE, NOTE_C5, 0.25f, false},

    // Phrase 4
    {WAVE_SINE, NOTE_B4, 0.5f, false},
    {WAVE_SINE, NOTE_B4, 0.25f, false},
    {WAVE_SINE, NOTE_C5, 0.25f, false},
    {WAVE_SINE, NOTE_D5, 0.5f, false},
    {WAVE_SINE, NOTE_E5, 0.5f, false},
    {WAVE_SINE, NOTE_C5, 0.5f, false},
    {WAVE_SINE, NOTE_A4, 0.5f, false},
    {WAVE_SINE, NOTE_A4, 1.0f, false},
    //
    // Bass (square)
    {WAVE_SQUARE, NOTE_A3, 0.5f, false},
    {WAVE_SQUARE, NOTE_A3, 0.5f, false},
    {WAVE_SQUARE, NOTE_B3, 0.5f, false},
    {WAVE_SQUARE, NOTE_B3, 0.5f, false},
    {WAVE_SQUARE, NOTE_C4, 0.5f, false},
    {WAVE_SQUARE, NOTE_C4, 0.5f, false},
    {WAVE_SQUARE, NOTE_D4, 0.5f, false},
    {WAVE_SQUARE, NOTE_D4, 0.5f, false},
    {WAVE_SQUARE, NOTE_A3, 0.5f, false},
    {WAVE_SQUARE, NOTE_A3, 0.5f, false},
    {WAVE_SQUARE, NOTE_B3, 0.5f, false},
    {WAVE_SQUARE, NOTE_B3, 0.5f, false},
    {WAVE_SQUARE, NOTE_C4, 0.5f, false},
    {WAVE_SQUARE, NOTE_C4, 0.5f, false},
    {WAVE_SQUARE, NOTE_D4, 0.5f, false},
    {WAVE_SQUARE, NOTE_D4, 0.5f, false},
    //
    //
    // // Triangle “fills” / op percussion
    {WAVE_TRIANGLE, NOTE_E5, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_B4, 0.25f, false},
    {WAVE_TRIANGLE, NOTE_C5, 0.25f, false},
    {WAVE_TRIANGLE, NOTE_D5, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_C5, 0.25f, false},
    {WAVE_TRIANGLE, NOTE_B4, 0.25f, false},
    {WAVE_TRIANGLE, NOTE_A4, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_A4, 0.25f, false},
    {WAVE_TRIANGLE, NOTE_C5, 0.25f, false},
    {WAVE_TRIANGLE, NOTE_E5, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_D5, 0.25f, false},
    {WAVE_TRIANGLE, NOTE_C5, 0.25f, false},

    // Phrase 2
    {WAVE_TRIANGLE, NOTE_B4, 0.75f, false},
    {WAVE_TRIANGLE, NOTE_C5, 0.25f, false},
    {WAVE_TRIANGLE, NOTE_D5, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_E5, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_C5, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_A4, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_A4, 1.0f, false},

    // Phrase 3
    {WAVE_TRIANGLE, NOTE_D5, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_F5, 0.25f, false},
    {WAVE_TRIANGLE, NOTE_A5, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_G5, 0.25f, false},
    {WAVE_TRIANGLE, NOTE_F5, 0.25f, false},
    {WAVE_TRIANGLE, NOTE_E5, 0.75f, false},
    {WAVE_TRIANGLE, NOTE_C5, 0.25f, false},
    {WAVE_TRIANGLE, NOTE_E5, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_D5, 0.25f, false},
    {WAVE_TRIANGLE, NOTE_C5, 0.25f, false},

    // Phrase 4
    {WAVE_TRIANGLE, NOTE_B4, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_B4, 0.25f, false},
    {WAVE_TRIANGLE, NOTE_C5, 0.25f, false},
    {WAVE_TRIANGLE, NOTE_D5, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_E5, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_C5, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_A4, 0.5f, false},
    {WAVE_TRIANGLE, NOTE_A4, 1.0f, false},

    // Noise
    {WAVE_NOISE, NOTE_NONE, 0.05f, false},
    {WAVE_NOISE, NOTE_NONE, 0.1f, false},
    {WAVE_NOISE, NOTE_NONE, 0.05f, false},
    {WAVE_NOISE, NOTE_NONE, 0.1f, false},
};

// MultiNote song[] = {
//     {WAVE_SINE, NOTE_C5, 1.f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false},
//     {WAVE_SINE, NOTE_D5, 1.f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false},
//     {WAVE_SINE, NOTE_C5, 1.f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false},
//     {WAVE_SINE, NOTE_D5, 1.f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false},
//     {WAVE_SINE, NOTE_C5, 1.f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false},
//     {WAVE_SINE, NOTE_D5, 1.f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false},
//     {WAVE_SINE, NOTE_C5, 1.f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false},
//     {WAVE_SINE, NOTE_D5, 1.f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false},
//     {WAVE_SINE, NOTE_C5, 1.f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false},
//     {WAVE_SINE, NOTE_D5, 1.f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false},
//     {WAVE_SINE, NOTE_C5, 1.f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false},
//     {WAVE_SINE, NOTE_D5, 1.f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false},
//     {WAVE_SINE, NOTE_C5, 1.f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false},
//     {WAVE_SINE, NOTE_D5, 1.f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false},
//     {WAVE_SINE, NOTE_C5, 1.f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false},
//     {WAVE_SINE, NOTE_D5, 1.f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false},
//     {WAVE_SINE, NOTE_C5, 1.f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false},
//     {WAVE_SINE, NOTE_D5, 1.f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false},
//     {WAVE_SINE, NOTE_C5, 1.f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false},
//     {WAVE_SINE, NOTE_D5, 1.f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false},
//     {WAVE_SINE, NOTE_C5, 1.f, false}, {WAVE_SINE, NOTE_D5, 0.1f, false}, {WAVE_SINE, NOTE_C5, 0.1f, false},
//     {WAVE_SINE, NOTE_D5, 1.f, false},
// };
const size_t song_length = sizeof(song) / sizeof(*song);
size_t song_index = 0;

void TickSong(float dt) {
    if (!music_playing)
        return;

    noteTimer += dt;
    for (int i = 0; i < WAVE_COUNT; i++) {
        voices[i].timer += dt;
    }
    for (size_t i = 0; i < song_length; i++) {
        MultiNote *note = &song[i];
        Voice *v = &voices[note->wave];
        if (!note->started && v->queue == NULL) {
            // PlayNoteOnWave(note->wave, note->freq, note->duration);
            v->queue = note;
            v->active = true;
            note->started = true;
            song_index++;
        }
    }
}

int main() {
    InitWindow(1280, 720, "Sound");
    InitAudioDevice();
    SetTargetFPS(60);
    SetWindowState(FLAG_WINDOW_RESIZABLE);

    SetAudioStreamBufferSizeDefault(MAX_SAMPLES_PER_UPDATE);
    AudioStream stream = LoadAudioStream(SAMPLE_RATE, 16, 1);
    SetAudioStreamCallback(stream, AudioInputCallback);
    PlayAudioStream(stream);

    SetMasterVolume(0.5);

    voices[WAVE_SINE].volume = 0.2f;
    voices[WAVE_SQUARE].volume = 0.02;
    voices[WAVE_SAW].volume = 0.0f;
    voices[WAVE_TRIANGLE].volume = 0.2f;
    voices[WAVE_NOISE].volume = 0.05f;

    while (!WindowShouldClose()) {
        const int graph_box_height = GetScreenHeight() / (WAVE_COUNT + 1);
        const int graph_box_width = GetScreenWidth() * 0.25 - 1;
        const int box_x = GetScreenWidth() - graph_box_width;
        const int font_size = GetScreenHeight() / 30;

        float dt = GetFrameTime();
        TickSong(dt);

        if (IsKeyPressed(KEY_R) || song_index >= song_length) {
            for (size_t i = 0; i < song_length; i++) {
                song[i].started = false;
            }
            song_index = 0;
            noteTimer = 0;
        }

        if (IsKeyPressed(KEY_SPACE)) {
            music_playing = !music_playing;
            if (!music_playing) {
                PauseAudioStream(stream);
            } else {
                ResumeAudioStream(stream);
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);
        for (int i = 0; i < WAVE_COUNT; i++) {
            const int box_y = graph_box_height * i + 1;
            DrawRectangleLines(1, box_y, graph_box_width, graph_box_height, WHITE);
            DrawText(TextFormat("%s : %.02f HZ", wave_text[i], voices[i].active ? voices[i].freq : 0), 5, box_y + 5,
                     font_size, WHITE);

            Rectangle mute_button = {5, box_y + font_size * 1.5f, font_size * 1.5f, font_size * 1.5f};
            bool hover = CheckCollisionPointRec(GetMousePosition(), mute_button);
            Color btn_colors[2] = {LIGHTGRAY, DARKGRAY};
            int btn_color = voices[i].muted;
            if (hover) {
                btn_color = 1 - btn_color;
                if (IsMouseButtonPressed(0)) {
                    voices[i].muted = !voices[i].muted;
                }
            }
            DrawRectangleRec(mute_button, btn_colors[btn_color]);
            int x = MeasureText("M", font_size);
            DrawText("M", mute_button.x + x / 2.f, mute_button.y + font_size * 0.25f, font_size, WHITE);
        }

        for (int i = 0; i < WAVE_COUNT; i++) {
            const int box_y = graph_box_height * i + 1;
            const int w = GetScreenWidth() - graph_box_width * 2;
            DrawRectangleLines(graph_box_width + 1, box_y, w, graph_box_height, WHITE);
        }

        for (int i = 0; i < WAVE_COUNT; i++) {
            const int box_y = graph_box_height * i + 1;
            DrawRectangleLines(box_x, box_y, graph_box_width, graph_box_height, WHITE);
            Vector2 prev = {box_x, box_y};
            for (int x = 0; x < graph_box_width; x++) {
                float t = (float)x / (SAMPLE_RATE / 2.0f);
                float phase = fmodf(voices[i].freq * t, 1.0f);
                float y = GenerateWave(i, phase);

                Vector2 position = {0};
                position.x = box_x + x;
                position.y = (box_y + graph_box_height / 2.0f) + 50 * y;
                DrawLineV(prev, position, WHITE);
                prev = position;
            }
        }

        const int sum_y = graph_box_height * (WAVE_COUNT) + 1;
        DrawRectangleLines(graph_box_width + 1, sum_y, GetScreenWidth() - graph_box_width - 1, graph_box_height, WHITE);
        Vector2 prev = {graph_box_width, (sum_y + graph_box_height / 2.0f)};
        for (int x = 0; x < GetScreenWidth(); x++) {
            float t = (float)x / (SAMPLE_RATE / 2.0f);
            float mix = 0.0f;

            for (int i = 0; i < WAVE_COUNT - 1; i++) {
                float phase = fmodf(voices[i].freq * t, 1.0f);
                mix += GenerateWave(i, phase);
            }
            mix /= WAVE_COUNT - 1;
            Vector2 position = {0};
            position.x = graph_box_width + x;
            position.y = (sum_y + graph_box_height / 2.0f) + 50 * mix;
            DrawLineV(prev, position, WHITE);
            prev = position;
        }

        DrawRectangleLines(1, sum_y, graph_box_width, graph_box_height, WHITE);
        DrawText(TextFormat("%05.02f", noteTimer), 5, graph_box_height * WAVE_COUNT, font_size * 2, WHITE);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
