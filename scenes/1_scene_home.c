#include "1_scene_home.h"
#include "../core/scene_manager.h"
#include "../core/input.h"
#include "../core/engine.h"
#include "../util/texture.h"
#include "../ui/ui_text.h"
#include "../ui/ui_button.h"
#include "../net/net_client.h"

// ホーム画面データ
static SDL_Texture *bg_title = NULL;
static SDL_Texture *bg_true = NULL;
static TTF_Font *font_main = NULL;

// focus: 0=START, 1=対戦, 2=SETTINGS, 3=EXIT
static int focus = 0;
#define HOME_MENU_COUNT 4

// 「愛を始める」演出
static bool start_transition = false;
static float start_timer = 0.0f;
const float START_DELAY = 2.0f;


// ------------------------------------
// 初期化
// ------------------------------------
void scene_home_enter(void)
{
    if (!font_main)
        font_main = ui_load_font("assets/font/main.otf", 48);

    if (!bg_title)
        bg_title = load_texture(g_renderer, "assets/bg/home.png");

    if (!bg_true)
        bg_true = load_texture(g_renderer, "assets/bg/true_home.png");
}

// ------------------------------------
// 更新処理
// ------------------------------------
void scene_home_update(float dt)
{
    // 「愛を始める」演出中
    if (start_transition) {
        start_timer += dt;
        if (start_timer >= START_DELAY)
            change_scene(SCENE_SELECT);
        return;
    }

    // 通常メニュー操作
    if (input_is_pressed(SDL_SCANCODE_DOWN))
        focus = (focus + 1) % HOME_MENU_COUNT;

    if (input_is_pressed(SDL_SCANCODE_UP))
        focus = (focus + HOME_MENU_COUNT - 1) % HOME_MENU_COUNT;

    if (input_is_pressed(SDL_SCANCODE_RETURN)) {
        if (focus == 0) {
            // サーバ接続を試みる（失敗してもオフラインで続行）
            net_connect(g_net_host, g_net_port);
            if (net_is_online())
                net_send_ready();
            start_transition = true;
            start_timer = 0.0f;
        }
        else if (focus == 1) {
            // 対戦（未実装）
        }
        else if (focus == 2) {
            SDL_Log("Settings (未実装)");
        }
        else if (focus == 3) {
            g_running = false;
        }
    }
}

// ------------------------------------
// 描画処理
// ------------------------------------
void scene_home_render(SDL_Renderer *r)
{
    SDL_Rect fullscreen = {0, 0, 1280, 720};

    if (start_transition)
        SDL_RenderCopy(r, bg_true, NULL, &fullscreen);
    else
        SDL_RenderCopy(r, bg_title, NULL, &fullscreen);

    if (start_transition)
        return;

    // 通常メニュー
    int bx = 500;
    int by = 240;
    int w = 280;
    int h = 60;
    int gap = 80;

    // START
    ui_button_draw(r, (SDL_Rect){bx, by + 0 * gap, w, h}, focus == 0);
    ui_text_draw(r, font_main, "愛を始める", bx + 23, by + 0 * gap + 5);

    // 対戦
    ui_button_draw(r, (SDL_Rect){bx, by + 1 * gap, w, h}, focus == 1);
    ui_text_draw(r, font_main, "対戦", bx + 95, by + 1 * gap + 5);

    // SETTINGS
    ui_button_draw(r, (SDL_Rect){bx, by + 2 * gap, w, h}, focus == 2);
    ui_text_draw(r, font_main, "SETTINGS", bx + 45, by + 2 * gap + 5);

    // EXIT
    ui_button_draw(r, (SDL_Rect){bx, by + 3 * gap, w, h}, focus == 3);
    ui_text_draw(r, font_main, "EXIT", bx + 85, by + 3 * gap + 5);
}
