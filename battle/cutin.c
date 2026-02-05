// battle/cutin.c
#define _POSIX_C_SOURCE 200809L
#include "cutin.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>

// ======================================================
//  重要：cutin はイベントを「消費」してはいけない
//  main / input が SDL_PollEvent を担当する前提。
//  ここでやってよいのは PumpEvents（状態更新）まで。
// ======================================================
static void pump_events_non_consuming(void)
{
    SDL_PumpEvents();
}

// （現状未使用だが、将来フェード等の拡張で使えるので残す）
static uint8_t clamp_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static void draw_fade(SDL_Renderer* r, int w, int h, uint8_t alpha)
{
    if (!r) return;

    SDL_BlendMode prev = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(r, &prev);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, alpha);

    SDL_Rect rect = {0, 0, w, h};
    SDL_RenderFillRect(r, &rect);

    SDL_SetRenderDrawBlendMode(r, prev);
}

static void fade_out(SDL_Renderer* r, int w, int h, int ms)
{
    const int step_ms = 16; // ~60fps
    int elapsed = 0;
    if (ms < 1) ms = 1;

    while (elapsed < ms) {
        pump_events_non_consuming();

        float t = (float)elapsed / (float)ms;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        uint8_t a = (uint8_t)(t * 255.0f);
        draw_fade(r, w, h, a);
        SDL_RenderPresent(r);

        SDL_Delay(step_ms);
        elapsed += step_ms;
    }

    draw_fade(r, w, h, 255);
    SDL_RenderPresent(r);
}

static void fade_in(SDL_Renderer* r, int w, int h, int ms)
{
    const int step_ms = 16;
    int elapsed = 0;
    if (ms < 1) ms = 1;

    while (elapsed < ms) {
        pump_events_non_consuming();

        float t = (float)elapsed / (float)ms;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        uint8_t a = (uint8_t)((1.0f - t) * 255.0f);
        draw_fade(r, w, h, a);
        SDL_RenderPresent(r);

        SDL_Delay(step_ms);
        elapsed += step_ms;
    }
}

static bool has_bundled_mpv(void)
{
    return (access("./tools/mpv/mpv", X_OK) == 0);
}

static bool has_system_mpv(void)
{
    int rc = system("sh -c 'command -v mpv >/dev/null 2>&1'");
    return (rc == 0);
}

static bool has_any_mpv(void)
{
    return has_bundled_mpv() || has_system_mpv();
}

// 既存LD_LIBRARY_PATHを壊さず、同梱libを先頭に追加
static void prepend_ld_library_path(const char* add_path)
{
    if (!add_path || !add_path[0]) return;

    const char* old = getenv("LD_LIBRARY_PATH");
    if (!old || !old[0]) {
        setenv("LD_LIBRARY_PATH", add_path, 1);
        return;
    }

    // add_path + ":" + old
    // 1024を超えるなら古いものを捨てて add_path のみ
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "%s:%s", add_path, old);
    if (n <= 0 || n >= (int)sizeof(buf)) {
        setenv("LD_LIBRARY_PATH", add_path, 1);
        return;
    }
    setenv("LD_LIBRARY_PATH", buf, 1);
}

// mpv を別プロセスで起動して pid を返す。失敗で -1。
static pid_t spawn_mpv_fullscreen_ex(const char* movie_path, bool flip_h)
{
    pid_t pid = fork();
    if (pid < 0) return (pid_t)-1;

    if (pid == 0) {
        const bool use_bundled = has_bundled_mpv();

        const char* exec_path = use_bundled ? "./tools/mpv/mpv" : "mpv";
        if (use_bundled) {
            // 同梱libを優先（既存を壊さず先頭に追加）
            prepend_ld_library_path("./tools/mpv/lib");
        }

        // 依存が増える hwdec は切る（大学PCで詰みにくくする）
        // 音は pulse -> alsa の順で試す
        // 2P側は左右反転（--vf=hflip）
        char* const argv_no_flip[] = {
            (char*)exec_path,
            (char*)"--fullscreen",
            (char*)"--ontop",
            (char*)"--no-border",
            (char*)"--keep-open=no",
            (char*)"--really-quiet",
            (char*)"--input-default-bindings=no",
            (char*)"--input-vo-keyboard=no",
            (char*)"--hwdec=no",
            (char*)"--ao=pulse,alsa",
            (char*)movie_path,
            NULL
        };

        char* const argv_flip[] = {
            (char*)exec_path,
            (char*)"--fullscreen",
            (char*)"--ontop",
            (char*)"--no-border",
            (char*)"--keep-open=no",
            (char*)"--really-quiet",
            (char*)"--input-default-bindings=no",
            (char*)"--input-vo-keyboard=no",
            (char*)"--hwdec=no",
            (char*)"--ao=pulse,alsa",
            (char*)"--vf=hflip",
            (char*)movie_path,
            NULL
        };

        char* const* argv = flip_h ? argv_flip : argv_no_flip;

        if (use_bundled) {
            execv(exec_path, argv);
        } else {
            execvp(exec_path, argv);
        }

        _exit(127);
    }

    return pid;
}

bool cutin_play_fullscreen_mpv_ex(CutinContext* ctx,
                                  const char* movie_path,
                                  int fade_ms,
                                  bool stop_bgm,
                                  bool flip_h)
{
    if (!ctx || !ctx->renderer || !movie_path) return false;

    // 1) 暗転（イベントは奪わない）
    fade_out(ctx->renderer, ctx->screen_w, ctx->screen_h, fade_ms);

    // 2) BGM停止（再開は呼び出し側責務）
    if (stop_bgm && Mix_PlayingMusic()) {
        Mix_HaltMusic();
    }

    // 3) mpvが無いなら暗転演出だけ
    if (!has_any_mpv()) {
        for (int i = 0; i < 10; i++) {
            pump_events_non_consuming();
            SDL_Delay(15);
        }
        fade_in(ctx->renderer, ctx->screen_w, ctx->screen_h, fade_ms);
        return false;
    }

    // 4) mpv起動（非同期）
    pid_t mpv_pid = spawn_mpv_fullscreen_ex(movie_path, flip_h);
    if (mpv_pid < 0) {
        fade_in(ctx->renderer, ctx->screen_w, ctx->screen_h, fade_ms);
        return false;
    }

    // 5) mpv終了待ち（待機中も「イベントを消費しない」）
    int status = 0;
    pid_t wait_result = -1;
    while (1) {
        pump_events_non_consuming();

        pid_t r = waitpid(mpv_pid, &status, WNOHANG);
        if (r == mpv_pid) {
            // 子プロセスが終了
            wait_result = r;
            break;
        }
        if (r == -1) {
            // waitpidエラー
            wait_result = r;
            break;
        }
        // r == 0: 子プロセスがまだ実行中 -> ループ継続

        SDL_Delay(10);
    }

    // 6) 終了ステータスをチェック
    bool mpv_success = false;
    
    if (wait_result == -1) {
        // waitpid がエラーで終了
        mpv_success = false;
    } else if (WIFEXITED(status)) {
        // 正常終了：終了コードをチェック
        int exit_code = WEXITSTATUS(status);
        mpv_success = (exit_code == 0);
    } else if (WIFSIGNALED(status)) {
        // シグナルで終了（クラッシュなど）
        mpv_success = false;
    }

    // mpv が失敗した場合はフェードインして終了
    if (!mpv_success) {
        fade_in(ctx->renderer, ctx->screen_w, ctx->screen_h, fade_ms);
        return false;
    }

    // 7) 復帰前の黒挟み
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_Rect rect = {0, 0, ctx->screen_w, ctx->screen_h};
    SDL_RenderFillRect(ctx->renderer, &rect);
    SDL_RenderPresent(ctx->renderer);
    SDL_Delay(50);

    // 8) フェードイン
    fade_in(ctx->renderer, ctx->screen_w, ctx->screen_h, fade_ms);
    return true;
}
