// battle/cutin.c
#include "cutin.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

// ======================================================
//  重要：cutin はイベントを「消費」してはいけない
//  main / input が SDL_PollEvent を担当する前提。
//  ここでやってよいのは PumpEvents（状態更新）まで。
// ======================================================
static void pump_events_while_blocking(void)
{
    // NOTE:
    // SDL_PollEvent を回すと main 側の入力/ウィンドウイベントを奪う。
    // 「Windowsキー押したら動く」系の不具合の主因になる。
    SDL_PumpEvents();
}

static void render_fade(SDL_Renderer* r, int w, int h, uint8_t alpha)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, alpha);
    SDL_Rect rect = {0, 0, w, h};
    SDL_RenderFillRect(r, &rect);
}

static void fade_out(SDL_Renderer* r, int w, int h, int ms)
{
    const int step_ms = 16; // 約60fps
    int elapsed = 0;
    if (ms < 1) ms = 1;

    while (elapsed < ms) {
        pump_events_while_blocking();

        float t = (float)elapsed / (float)ms;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        uint8_t a = (uint8_t)(t * 255.0f);

        render_fade(r, w, h, a);
        SDL_RenderPresent(r);

        SDL_Delay(step_ms);
        elapsed += step_ms;
    }

    render_fade(r, w, h, 255);
    SDL_RenderPresent(r);
}

static void fade_in(SDL_Renderer* r, int w, int h, int ms)
{
    const int step_ms = 16;
    int elapsed = 0;
    if (ms < 1) ms = 1;

    while (elapsed < ms) {
        pump_events_while_blocking();

        float t = (float)elapsed / (float)ms;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        uint8_t a = (uint8_t)((1.0f - t) * 255.0f);

        render_fade(r, w, h, a);
        SDL_RenderPresent(r);

        SDL_Delay(step_ms);
        elapsed += step_ms;
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static bool has_mpv(void)
{
    int rc = system("sh -c 'command -v mpv >/dev/null 2>&1'");
    return rc == 0;
}

// mpv を別プロセスで起動して pid を返す。失敗で -1。
static pid_t spawn_mpv_fullscreen(const char* movie_path)
{
    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        execlp("mpv",
               "mpv",
               "--fs",
               "--no-border",
               "--ontop",
               "--keep-open=no",
               "--really-quiet",
               "--input-default-bindings=no",
               "--input-vo-keyboard=no",
               movie_path,
               (char*)NULL);

        _exit(127);
    }

    return pid;
}

bool cutin_play_fullscreen_mpv(CutinContext* ctx,
                               const char* movie_path,
                               int fade_ms,
                               bool stop_bgm)
{
    if (!ctx || !ctx->renderer || !movie_path) return false;

    // 1) 暗転（イベントは奪わない）
    fade_out(ctx->renderer, ctx->screen_w, ctx->screen_h, fade_ms);

    // 2) BGM停止（再開は呼び出し側責務）
    if (stop_bgm && Mix_PlayingMusic()) {
        Mix_HaltMusic();
    }

    // 3) mpvが無いなら暗転演出だけ
    if (!has_mpv()) {
        for (int i = 0; i < 10; i++) {
            pump_events_while_blocking();
            SDL_Delay(15);
        }
        fade_in(ctx->renderer, ctx->screen_w, ctx->screen_h, fade_ms);
        return false;
    }

    // 4) mpv起動（非同期）
    pid_t mpv_pid = spawn_mpv_fullscreen(movie_path);
    if (mpv_pid < 0) {
        fade_in(ctx->renderer, ctx->screen_w, ctx->screen_h, fade_ms);
        return false;
    }

    // 5) mpv終了待ち（待機中も「イベントを消費しない」）
    while (1) {
        pump_events_while_blocking();

        int status = 0;
        pid_t r = waitpid(mpv_pid, &status, WNOHANG);
        if (r == mpv_pid) break; // 終了
        if (r == -1) break;      // エラー

        SDL_Delay(10);
    }

    // 6) 復帰前の黒挟み
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);
    SDL_RenderPresent(ctx->renderer);
    SDL_Delay(50);

    // 7) フェードイン
    fade_in(ctx->renderer, ctx->screen_w, ctx->screen_h, fade_ms);
    return true;
}
