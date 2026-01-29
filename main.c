#include <SDL2/SDL.h>

#include "core/engine.h"
#include "core/scene_manager.h"
#include "core/input.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    if (!engine_init()) {
        SDL_Log("Engine init failed");
        return 1;
    }

    scene_manager_init();

    g_running = true;

    // 高精度タイマ
    const double freq = (double)SDL_GetPerformanceFrequency();
    Uint64 last_counter = SDL_GetPerformanceCounter();

    while (g_running)
    {
        // ===== 入力フレーム開始 =====
        input_begin_frame();

        // ===== SDLイベント処理（唯一の PollEvent）=====
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            input_handle_event(&e);
        }

        // ===== 入力確定 =====
        input_end_frame();

        // ===== dt計算（dt=0防止）=====
        Uint64 now_counter = SDL_GetPerformanceCounter();
        double dt = (double)(now_counter - last_counter) / freq;
        last_counter = now_counter;

        if (dt < 0.001) dt = 0.001;   // 1ms 下限
        if (dt > 0.05)  dt = 0.05;    // 50ms 上限

        // ===== 更新 =====
        scene_update((float)dt);

        // ===== 描画 =====
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
        SDL_RenderClear(g_renderer);

        scene_render(g_renderer);
        SDL_RenderPresent(g_renderer);

        SDL_Delay(1);
    }

    engine_cleanup();
    return 0;
}
