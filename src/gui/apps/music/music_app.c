// src/gui/apps/music/music_app.c
//
// Музыкальный плеер: список дорожек, реальное воспроизведение
// вшитых в ядро WAV (через sound_manager) и прогресс по кадрам.

#include <stdint.h>

#include "gui/apps/music/music_app.h"
#include "gui/font.h"
#include "drivers/system/sound_manager.h"

extern void draw_rounded_rect_buf(int x, int y, int w, int h, int r, uint32_t color);
extern void draw_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
extern void draw_rect_buf(int x, int y, int w, int h, uint32_t color);

/* Дорожки вшиты в ядро (kernel/resources.asm), формат: 22.05 kHz,
 * stereo, 16 bit, заголовок WAV 44 байта. */
extern const uint8_t track1_wav_start[];
extern const uint8_t track1_wav_end[];
extern const uint8_t track2_wav_start[];
extern const uint8_t track2_wav_end[];
extern const uint8_t track3_wav_start[];
extern const uint8_t track3_wav_end[];

#define TRACK_RATE 22050

typedef struct {
    const char*    name;
    const uint8_t* data;
    uint32_t       size;
} track_t;

static const track_t tracks[] = {
    {"01. Track_One.wav",     track1_wav_start, 0},
    {"02. Ambient_Theme.wav", track2_wav_start, 0},
    {"03. Cyberpunk_Beat.wav", track3_wav_start, 0},
};
static const uint8_t* const track_ends[] = {
    track1_wav_end, track2_wav_end, track3_wav_end,
};
static int track_count = 3;

static int is_open = 0;
static int is_playing = 0;
static int current_track = 0;

/* Прогресс считается по кадрам (главный цикл ~60 к/с): без таймера
 * точное время недоступно, оценка достаточна для индикатора. */
static uint32_t play_frame = 0;
static uint32_t total_frames = 1;

static uint32_t track_pcm_size(int i)
{
    uint32_t size = (uint32_t)(track_ends[i] - tracks[i].data);
    return (size > 44) ? size - 44 : 0;
}

static void start_track(int i)
{
    uint32_t pcm = track_pcm_size(i);
    if (pcm == 0)
        return;

    sound_play_pcm(tracks[i].data + 44, pcm);

    uint32_t stereo_frames = pcm / 4;             /* 2 канала * 16 бит */
    total_frames = (stereo_frames * 60) / TRACK_RATE;
    if (total_frames == 0)
        total_frames = 1;
    play_frame = 0;
    is_playing = 1;
}

void toggle_music_app(void)
{
    is_open = !is_open;
}

void render_music_app_window(uint32_t* buf, int scr_w, int scr_h, int mx, int my, int btn, int click)
{
    (void)scr_h; /* высота окна фиксированная, параметр нужен для общего интерфейса окон */
    (void)btn;   /* состояние кнопки мыши используется только через click */

    if (!is_open)
        return;

    /* Движение прогресса: ~60 кадров в секунду. */
    if (is_playing) {
        play_frame++;
        if (play_frame >= total_frames)
            is_playing = 0;
    }

    int win_x = 220, win_y = 100;
    int win_w = 500, win_h = 340;

    draw_rounded_rect_alpha(win_x + 4, win_y + 4, win_w, win_h, 12, 0x00000000, 80);
    draw_rounded_rect_buf(win_x, win_y, win_w, win_h, 12, 0x001C1C1E);

    draw_rounded_rect_buf(win_x, win_y, win_w, 32, 12, 0x002C2C2E);
    draw_string("Music Player", win_x + win_w / 2 - 48, win_y + 8, 0x00FFFFFF, buf, scr_w);

    draw_rounded_rect_buf(win_x + 12, win_y + 10, 12, 12, 6, 0x00FF5F56);
    if (click && mx >= win_x + 12 && mx <= win_x + 24 && my >= win_y + 10 && my <= win_y + 22) {
        is_open = 0;
        return;
    }

    draw_rect_buf(win_x + 12, win_y + 44, 210, 220, 0x002C2C2E);
    for (int i = 0; i < track_count; i++) {
        uint32_t item_bg = (current_track == i) ? 0x00007AFF : 0x003A3A3C;
        draw_rounded_rect_buf(win_x + 16, win_y + 50 + i * 36, 202, 30, 6, item_bg);
        draw_string(tracks[i].name, win_x + 24, win_y + 58 + i * 36, 0x00FFFFFF, buf, scr_w);

        if (click && mx >= win_x + 16 && mx <= win_x + 218 &&
            my >= win_y + 50 + i * 36 && my <= win_y + 80 + i * 36) {
            current_track = i;
            start_track(i);
        }
    }

    draw_rounded_rect_buf(win_x + 236, win_y + 44, 252, 160, 10, 0x00FF2D55);
    draw_string("Now Playing:", win_x + 236, win_y + 215, 0x008E8E93, buf, scr_w);
    draw_string(tracks[current_track].name, win_x + 236, win_y + 235, 0x00FFFFFF, buf, scr_w);

    int progress = (int)((uint64_t)play_frame * 100 / total_frames);
    if (progress > 100)
        progress = 100;

    draw_rounded_rect_buf(win_x + 12, win_y + 276, win_w - 24, 6, 3, 0x003A3A3C);
    int fill_w = ((win_w - 24) * progress) / 100;
    draw_rounded_rect_buf(win_x + 12, win_y + 276, fill_w, 6, 3, 0x00007AFF);

    uint32_t btn_color = is_playing ? 0x00FF9500 : 0x0034C759;
    draw_rounded_rect_buf(win_x + win_w / 2 - 30, win_y + 292, 60, 32, 8, btn_color);
    draw_string(is_playing ? "PAUSE" : "PLAY", win_x + win_w / 2 - 20, win_y + 302, 0x00FFFFFF, buf, scr_w);

    if (click && mx >= win_x + win_w / 2 - 30 && mx <= win_x + win_w / 2 + 30 &&
        my >= win_y + 292 && my <= win_y + 324) {
        if (is_playing) {
            is_playing = 0;
        } else {
            start_track(current_track);
        }
    }
}
