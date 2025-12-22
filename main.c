#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include <stdio.h>
#include <stdlib.h>

#include "core/engine.h"
#include "core/scene_manager.h"
#include "core/input.h"
#include "net/net_client.h"   // ★ 追加

int main(int argc, char **argv)
{
    /* -------------------------------
     * 起動引数チェック（server IP / port）
     * ------------------------------- */
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);

    /* -------------------------------
     * SDL2 本体の初期化
     * ------------------------------- */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        SDL_Log("SDL_Init ERROR: %s", SDL_GetError());
        return 1;
    }

    /* -------------------------------
     * SDL_mixer の初期化
     * ------------------------------- */
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0)
    {
        SDL_Log("Mix_OpenAudio ERROR: %s", Mix_GetError());
        return 1;
    }

    Mix_AllocateChannels(32);
    Mix_Volume(-1, SDL_MIX_MAXVOLUME);

    /* -------------------------------
     * エンジン初期化
     * ------------------------------- */
    if (!engine_init())
    {
        SDL_Log("Engine init failed");
        return 1;
    }

    scene_manager_init();

    /* -------------------------------
     * ネットワーク接続（★追加）
     * ------------------------------- */
    net_connect(server_ip, server_port);

    Uint32 last = SDL_GetTicks();
    g_running = true;

    /* -------------------------------
     * メインループ
     * ------------------------------- */
    while (g_running)
    {
        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        last = now;

        input_update();

        net_poll();          // ★ 追加：START受信チェック

        scene_update(dt);

        SDL_RenderClear(g_renderer);
        scene_render(g_renderer);
        SDL_RenderPresent(g_renderer);

        SDL_Delay(1);
    }

    /* -------------------------------
     * 終了処理
     * ------------------------------- */
    Mix_CloseAudio();
    engine_cleanup();
    SDL_Quit();

    return 0;
}
