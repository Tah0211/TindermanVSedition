// battle/cutin.h
#pragma once
#include <stdbool.h>
#include <sys/types.h> // pid_t
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

// ======================================================
//  mpv でフルスクリーンMP4を流すための最小ユーティリティ
//
//  - SDL_PollEvent を回さない（入力を奪わない）
//  - 同梱 mpv を優先（./tools/mpv/mpv と ./tools/mpv/lib）
//  - hwdec は切る（大学PCで詰みにくくする）
//  - 2P側は左右反転（hflip）できる
// ======================================================

typedef struct {
    SDL_Renderer* renderer;
    int screen_w;
    int screen_h;
} CutinContext;

// movie_path: MP4ファイルパス
// fade_ms  : 150〜250ms推奨
// stop_bgm : trueならカットイン開始時にBGM停止（終了後の再開は呼び出し側で行う）
// flip_h   : trueで左右反転（2P側の攻撃演出など）
//
// 戻り値：mpvが見つかって起動できたらtrue（見つからない/起動失敗ならfalse）
bool cutin_play_fullscreen_mpv_ex(CutinContext* ctx,
                                  const char* movie_path,
                                  int fade_ms,
                                  bool stop_bgm,
                                  bool flip_h);

// 互換：flipなし
static inline bool cutin_play_fullscreen_mpv(CutinContext* ctx,
                                             const char* movie_path,
                                             int fade_ms,
                                             bool stop_bgm)
{
    return cutin_play_fullscreen_mpv_ex(ctx, movie_path, fade_ms, stop_bgm, false);
}
