/* pg_audio.c - tiny synthesiser + mixer.
 *
 * One SDL audio device (48 kHz, S16, stereo) is opened at start and streams silence for the whole
 * run, so the HDMI sink never has to wake up for the first sound. Voices are generated live:
 * sine / triangle / inharmonic bell / low-passed noise, each with a short attack and a smooth
 * decay (no clicks). The sum is scaled by a child-safe master volume and soft-limited.
 * If the device cannot be opened every call is a silent no-op.
 */
#include "pg_internal.h"

#include <math.h>
#include <string.h>

#define RATE        48000
#define MAX_VOICES  24
#define BLOCK       64

typedef struct {
    bool     active;
    PG_Wave  wave;
    float    f0, f1, f;
    float    dur, t, delay, attack;
    float    vol;
    float    ph1, ph2, ph3;
    float    lp;
    uint32_t rng, serial;
} Voice;

static SDL_AudioDeviceID dev;
static Voice    voices[MAX_VOICES];
static float    master = 0.5f;
static uint32_t serial_ctr;

static inline float fast_tanh(float x)
{
    if (x > 3.0f) x = 3.0f; else if (x < -3.0f) x = -3.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int16_t *out = (int16_t *)stream;
    int frames = len / 4;
    for (int base = 0; base < frames; base += BLOCK) {
        int n = frames - base < BLOCK ? frames - base : BLOCK;
        float mix[BLOCK];
        memset(mix, 0, sizeof mix);
        for (int vi = 0; vi < MAX_VOICES; vi++) {
            Voice *v = &voices[vi];
            if (!v->active) continue;
            float p = v->t / v->dur; p = p < 0 ? 0 : p > 1 ? 1 : p;
            v->f = v->f0 * powf(v->f1 / v->f0, p);
            float inc = v->f / RATE;
            float lpa = 0;
            if (v->wave == PG_WAVE_NOISE) { lpa = 6.2831853f * v->f / RATE; if (lpa > 0.95f) lpa = 0.95f; }
            for (int i = 0; i < n; i++) {
                if (v->delay > 0) { v->delay -= 1.0f / RATE; continue; }
                float e;
                if (v->t < v->attack) e = v->t / v->attack;
                else { e = 1.0f - (v->t - v->attack) / (v->dur - v->attack); if (e < 0) e = 0; e *= e; }
                float s;
                switch (v->wave) {
                case PG_WAVE_SINE: s = sinf(6.2831853f * v->ph1); break;
                case PG_WAVE_TRI:  s = 4.0f * fabsf(v->ph1 - 0.5f) - 1.0f; break;
                case PG_WAVE_BELL:
                    s = sinf(6.2831853f * v->ph1) + 0.45f * sinf(6.2831853f * v->ph2) * e + 0.2f * sinf(6.2831853f * v->ph3) * e * e;
                    s *= 0.75f; break;
                default: {
                    v->rng ^= v->rng << 13; v->rng ^= v->rng >> 17; v->rng ^= v->rng << 5;
                    float w = (float)(int32_t)v->rng * (1.0f / 2147483648.0f);
                    v->lp += lpa * (w - v->lp);
                    s = v->lp * 2.2f;
                }}
                mix[i] += s * e * v->vol;
                v->ph1 += inc; if (v->ph1 >= 1.0f) v->ph1 -= 1.0f;
                v->ph2 += inc * 2.76f; if (v->ph2 >= 1.0f) v->ph2 -= floorf(v->ph2);
                v->ph3 += inc * 5.40f; if (v->ph3 >= 1.0f) v->ph3 -= floorf(v->ph3);
                v->t += 1.0f / RATE;
                if (v->t >= v->dur) { v->active = false; break; }
            }
        }
        for (int i = 0; i < n; i++) {
            float y = fast_tanh(mix[i] * master) * 0.85f;
            int16_t s16 = (int16_t)(y * 32767.0f);
            out[(base + i) * 2] = s16; out[(base + i) * 2 + 1] = s16;
        }
    }
}

void pg_audio_init(bool headless)
{
    (void)headless;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { pg_log("audio unavailable (%s): running silent", SDL_GetError()); return; }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = audio_cb;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);     /* 0: SDL converts if the hardware differs */
    if (!dev) { pg_log("no audio device (%s): running silent", SDL_GetError()); return; }
    pg_log("audio=%s %dHz ch=%d buf=%d", SDL_GetCurrentAudioDriver(), have.freq, have.channels, have.samples);
    SDL_PauseAudioDevice(dev, 0);
}

void pg_audio_shutdown(void)
{
    if (dev) { SDL_CloseAudioDevice(dev); dev = 0; }
    memset(voices, 0, sizeof voices);
}

static void voice_start(PG_Wave w, float f0, float f1, float ms, float vol, float attack_ms, float delay_ms)
{
    if (!dev) return;
    f0 = f0 < 20 ? 20 : f0; f1 = f1 < 20 ? 20 : f1;
    ms = ms < 10 ? 10 : ms;
    vol = vol < 0 ? 0 : vol > 1 ? 1 : vol;
    SDL_LockAudioDevice(dev);
    int pick = -1; uint32_t oldest = 0xFFFFFFFFu;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) { pick = i; break; }
        if (voices[i].serial < oldest) { oldest = voices[i].serial; pick = i; }
    }
    Voice *v = &voices[pick];
    memset(v, 0, sizeof *v);
    v->active = true; v->wave = w; v->f0 = f0; v->f1 = f1; v->f = f0;
    v->dur = ms / 1000.0f;
    v->attack = attack_ms / 1000.0f; if (v->attack > v->dur * 0.5f) v->attack = v->dur * 0.5f;
    if (v->attack < 0.002f) v->attack = 0.002f;
    v->delay = delay_ms / 1000.0f;
    v->vol = vol; v->rng = 0x1234567u + (++serial_ctr) * 2654435761u; v->serial = serial_ctr;
    SDL_UnlockAudioDevice(dev);
}

void pg_tone(PG_Wave wave, float f0, float f1, float ms, float vol) { voice_start(wave, f0, f1, ms, vol, 4.0f, 0.0f); }

float pg_note_hz(int step)
{
    static const int offs[5] = { 0, 2, 4, 7, 9 };
    int oct = step >= 0 ? step / 5 : -((-step + 4) / 5);
    int idx = step - oct * 5;
    return 261.6256f * powf(2.0f, (12.0f * (float)oct + (float)offs[idx]) / 12.0f);
}

void pg_audio_set_master(float v) { master = v < 0 ? 0 : v > 1 ? 1 : v; }

/* ---- presets: each has a minimum retrigger interval so 10 fingers cannot make a wall of noise */
enum { K_POP, K_BOING, K_THUD, K_TAP, K_WHOOSH, K_CHIME, K_SQUISH, K_SPARKLE, K_COUNT };
static uint32_t last_ms[K_COUNT];
static bool gate(int kind, uint32_t min_ms)
{
    uint32_t now = SDL_GetTicks();
    if (now - last_ms[kind] < min_ms) return false;
    last_ms[kind] = now;
    return true;
}

void pg_sfx_pop(float vol)
{
    if (!gate(K_POP, 25)) return;
    voice_start(PG_WAVE_NOISE, 3200, 700, 90, vol * 0.8f, 2, 0);
    voice_start(PG_WAVE_SINE, 560, 170, 80, vol * 0.5f, 2, 0);
}

void pg_sfx_boing(float vol, float pitch)
{
    if (!gate(K_BOING, 40)) return;
    voice_start(PG_WAVE_SINE, 170 * pitch, 520 * pitch, 150, vol * 0.55f, 4, 0);
    voice_start(PG_WAVE_TRI, 340 * pitch, 1040 * pitch, 130, vol * 0.10f, 4, 0);
}

void pg_sfx_thud(float vol)
{
    if (!gate(K_THUD, 50)) return;
    voice_start(PG_WAVE_SINE, 115, 52, 170, vol * 0.8f, 3, 0);
    voice_start(PG_WAVE_NOISE, 500, 120, 70, vol * 0.35f, 2, 0);
}

void pg_sfx_tap(float vol)
{
    if (!gate(K_TAP, 30)) return;
    voice_start(PG_WAVE_TRI, 880, 560, 45, vol * 0.35f, 2, 0);
    voice_start(PG_WAVE_NOISE, 2600, 1400, 25, vol * 0.18f, 1, 0);
}

void pg_sfx_whoosh(float vol)
{
    if (!gate(K_WHOOSH, 120)) return;
    voice_start(PG_WAVE_NOISE, 260, 2300, 420, vol * 0.5f, 140, 0);
}

void pg_sfx_chime(int step, float vol)
{
    if (!gate(K_CHIME, 20)) return;
    float f = pg_note_hz(step);
    voice_start(PG_WAVE_BELL, f, f, 1100, vol * 0.55f, 3, 0);
}

void pg_sfx_squish(float vol, float pitch)
{
    if (!gate(K_SQUISH, 60)) return;
    voice_start(PG_WAVE_SINE, 190 * pitch, 440 * pitch, 120, vol * 0.5f, 6, 0);
    voice_start(PG_WAVE_SINE, 380 * pitch, 880 * pitch, 90, vol * 0.12f, 6, 0);
}

void pg_sfx_sparkle(float vol)
{
    if (!gate(K_SPARKLE, 150)) return;
    for (int i = 0; i < 4; i++) {
        float f = pg_note_hz(7 + i * 2 + (i & 1));
        voice_start(PG_WAVE_BELL, f, f, 520, vol * 0.32f, 2, 55.0f * (float)i);
    }
}
