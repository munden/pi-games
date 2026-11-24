/* snd.c - a gentle sound governor.
 * A child will pop ten balloons at once. Every sound adds to a "load" that fades out over about a second;
 * the louder the recent past, the quieter (and rarer) each new sound. All volumes stay well under 0.8, notes
 * are bell tones on the C-major pentatonic scale (any mix sounds nice), nothing lasts longer than ~1.1 s.
 * The SDK's own presets already rate-limit; this adds polyphony control and keeps chords musical. */
#include "bp.h"

static float load;
static uint64_t last_bump, last_tap;

void snd_update(void) { load *= 0.9835f; }

static float gain(void) { return 1.0f / (1.0f + 0.28f * load); }

void snd_pop(int col, float sizek)
{
    (void)sizek;
    float g = gain();
    pg_sfx_pop(0.58f * g);
    if (load < 3.5f) {                          /* at most about four bell notes ring together */
        float f = pg_note_hz(col);
        pg_tone(PG_WAVE_BELL, f, f, 1000.0f, 0.46f * g);
    }
    load += 1.0f;
}

void snd_bat(float sizek, float strength)
{
    float g = gain(), pitch = clampf(1.55f - (sizek - 44.0f) / 19.0f * 0.7f, 0.8f, 1.6f);
    pg_sfx_boing(0.50f * g * (0.7f + 0.3f * strength), pitch);
    load += 0.5f;
}

void snd_bump(float impact)
{
    uint64_t now = pg_ticks();
    if (now - last_bump < 7) return;
    last_bump = now;
    float v = clampf((impact - 200.0f) / 500.0f, 0, 1);
    pg_tone(PG_WAVE_SINE, 210.0f, 130.0f, 110.0f, (0.07f + 0.10f * v) * gain());
}

/* a touch that hit nothing: a soft high plink, so every touch answers */
void snd_tap(void)
{
    uint64_t now = pg_ticks();
    if (now - last_tap < 5) return;
    last_tap = now;
    float f = pg_note_hz(pg_rand_int(5, 9));
    pg_tone(PG_WAVE_BELL, f, f, 520.0f, 0.2f * gain());
    load += 0.4f;
}

void snd_prize(void)
{
    pg_sfx_sparkle(0.42f * gain());
    load += 0.8f;
}
