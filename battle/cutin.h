// battle/cutin.h
#pragma once
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

typedef struct {
    SDL_Renderer* renderer;
    int screen_w;
    int screen_h;
} CutinContext;

// movie_path: MP4ファイルパス
// fade_ms  : 150〜250ms推奨
// stop_bgm : trueならカットイン開始時にBGM停止（終了後の再開は呼び出し側で行う）
//
// 戻り値：mpvが見つかって再生できたらtrue（見つからない/失敗ならfalse）
bool cutin_play_fullscreen_mpv(CutinContext* ctx,
                               const char* movie_path,
                               int fade_ms,
                               bool stop_bgm);
