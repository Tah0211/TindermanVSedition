// battle/cutin.c
#define _POSIX_C_SOURCE 200809L
#include "cutin.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

// ======================================================
//  重要：cutin はイベントを「消費」してはいけない
//  main / input が SDL_PollEvent を担当する前提。
//  ここでやってよいのは PumpEvents（状態更新）まで。
// ======================================================
static void pump_events_non_consuming(void)
{
    SDL_PumpEvents();
}

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

        uint8_t a = clamp_u8((int)(t * 255.0f));
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

static bool has_bundled_mpv_appimage(void)
{
    return (access("./tools/mpv/mpv.AppImage", X_OK) == 0);
}

static bool has_bundled_mpv_bin(void)
{
    return (access("./tools/mpv/mpv", X_OK) == 0);
}

static bool has_bundled_mpv(void)
{
    return has_bundled_mpv_appimage() || has_bundled_mpv_bin();
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
// 過去のmpvプロセスのゾンビを回収（同時実行対策）
static void reap_zombie_children(void)
{
    int status = 0;
    pid_t r;
    while ((r = waitpid(-1, &status, WNOHANG)) > 0) {
        fprintf(stderr, "[CUTIN] reaped zombie child pid=%d\n", (int)r);
    }
    if (r < 0 && errno != ECHILD) {
        fprintf(stderr, "[CUTIN] zombie cleanup error: errno=%d\n", errno);
    }
}

static pid_t spawn_mpv_fullscreen_ex(const char* movie_path, bool flip_h)
{
    pid_t pid = fork();
    if (pid < 0) return (pid_t)-1;

    if (pid == 0) {
        // AppImage → 同梱bin → system mpv の優先順
        const bool use_appimage = has_bundled_mpv_appimage();
        const bool use_bin      = (!use_appimage) && has_bundled_mpv_bin();
        const bool use_bundled  = use_appimage || use_bin;

        const char* exec_path =
            use_appimage ? "./tools/mpv/mpv.AppImage" :
            use_bin      ? "./tools/mpv/mpv" :
                           "mpv";

        // AppImageは内部に依存を持つのでLD_LIBRARY_PATHは触らない
        // 同梱binの場合のみ同梱libを先頭に追加
        if (use_bin) {
            prepend_ld_library_path("./tools/mpv/lib");
        }

        // 共通オプション
        // hwdec=no: 依存が増えるので切る（大学PCで詰みにくくする）
        // ao=pulse,alsa: 音声出力
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

        // FUSEが無い環境でもAppImageを動かすための設定
        if (use_appimage) {
            setenv("APPIMAGE_EXTRACT_AND_RUN", "1", 1);
        }

        if (use_bundled) {
            execv(exec_path, argv);
        } else {
            execvp(exec_path, argv);
        }

        // execv/execvp が返ってきた＝失敗
        fprintf(stderr, "[CUTIN] execv failed: exec_path=%s errno=%d (%s)\n",
                exec_path, errno, strerror(errno));
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
    if (!ctx || !ctx->renderer || !movie_path) {
        fprintf(stderr, "[CUTIN] invalid args: ctx=%p renderer=%p movie=%s\n",
                (void*)ctx, ctx ? (void*)ctx->renderer : NULL,
                movie_path ? movie_path : "(null)");
        return false;
    }

    fprintf(stderr, "[CUTIN] play: %s (fade=%d, flip=%d)\n", movie_path, fade_ms, flip_h);

    // 1) 暗転（イベントは奪わない）
    fade_out(ctx->renderer, ctx->screen_w, ctx->screen_h, fade_ms);

    // 2) BGM停止（再開は呼び出し側責務）
    if (stop_bgm && Mix_PlayingMusic()) {
        Mix_HaltMusic();
    }

    // 3) mpvが無いなら暗転演出だけ
    fprintf(stderr, "[CUTIN] appimage=%d bin=%d system=%d any=%d\n",
            has_bundled_mpv_appimage(), has_bundled_mpv_bin(),
            has_system_mpv(), has_any_mpv());
    if (!has_any_mpv()) {
        fprintf(stderr, "[CUTIN] no mpv found, skipping\n");
        for (int i = 0; i < 10; i++) {
            pump_events_non_consuming();
            SDL_Delay(15);
        }
        fade_in(ctx->renderer, ctx->screen_w, ctx->screen_h, fade_ms);
        return false;
    }

    // 3.5) 過去のmpvゾンビプロセスをクリーンアップ（同時実行対策）
    reap_zombie_children();

    // 4) mpv起動（非同期）
    pid_t mpv_pid = spawn_mpv_fullscreen_ex(movie_path, flip_h);
    fprintf(stderr, "[CUTIN] spawn pid=%d\n", (int)mpv_pid);
    if (mpv_pid < 0) {
        fprintf(stderr, "[CUTIN] fork failed\n");
        fade_in(ctx->renderer, ctx->screen_w, ctx->screen_h, fade_ms);
        return false;
    }

    // 5) mpv終了待ち（待機中も「イベントを消費しない」）
    while (1) {
        pump_events_non_consuming();

        int status = 0;
        pid_t r = waitpid(mpv_pid, &status, WNOHANG);
        if (r == mpv_pid) {
            fprintf(stderr, "[CUTIN] mpv exited: status=%d exited=%d exitcode=%d signaled=%d\n",
                    status, WIFEXITED(status), WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                    WIFSIGNALED(status));
            break;
        }
        if (r == -1) {
            // EINTR (signal interrupted) の場合はリトライ
            if (errno == EINTR) {
                fprintf(stderr, "[CUTIN] waitpid interrupted by signal, retrying\n");
                continue;
            }
            fprintf(stderr, "[CUTIN] waitpid error: errno=%d\n", errno);
            break;
        }

        SDL_Delay(10);
    }

    // 6) 復帰前の黒挟み
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_Rect rect = {0, 0, ctx->screen_w, ctx->screen_h};
    SDL_RenderFillRect(ctx->renderer, &rect);
    SDL_RenderPresent(ctx->renderer);
    SDL_Delay(50);

    // 7) フェードイン
    fade_in(ctx->renderer, ctx->screen_w, ctx->screen_h, fade_ms);
    return true;
}
