// =============================================================
//  scenes/3_scene_chat.c  (修正版・UI非変更・警告ゼロ対応)
//  Chatシーン（自動改行 + 自動スクロール + スライドイン演出）
// =============================================================

#include "3_scene_chat.h"
#include "../core/scene_manager.h"
#include "../core/input.h"
#include "../core/engine.h"

#include "../util/texture.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// =============================================================
// 新AI用：最小限ログ（UI描画用）
// =============================================================
#define CHAT_MAX 200

typedef struct
{
    bool from_user;
    char text[512];
} ChatLine;

static ChatLine chat_lines[CHAT_MAX];
static int chat_line_count = 0;

// =============================================================
// JSON パーサ（最小限）
// =============================================================
static const char *find_json_value(const char *json, const char *key)
{
    static char buf[512];
    buf[0] = 0;

    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    const char *p = strstr(json, pattern);
    if (!p)
        return NULL;

    p += strlen(pattern);
    while (*p == ' ' || *p == '\t')
        p++;

    // 文字列型
    if (*p == '"')
    {
        p++;
        const char *end = strchr(p, '"');
        if (!end)
            return NULL;
        size_t len = end - p;
        if (len >= sizeof(buf))
            len = sizeof(buf) - 1;
        memcpy(buf, p, len);
        buf[len] = 0;
        return buf;
    }

    // 数値型
    const char *end = p;
    while (*end && *end != ',' && *end != '}' && *end != '\n')
        end++;
    size_t len = end - p;
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    memcpy(buf, p, len);
    buf[len] = 0;
    return buf;
}

// =============================================================
// ChatAIReply（新AI用）
// =============================================================
typedef struct
{
    char text[512];
    int heart_delta;
    char emotion[64];
    int affection;
} ChatAIReply;

// =============================================================
// Python grok_bridge.py 呼び出し
// =============================================================
static bool call_grok_bridge(const char *msg, ChatAIReply *out)
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "python3 python/grok_bridge.py \"%s\"", msg);

    FILE *fp = popen(cmd, "r");
    if (!fp)
        return false;

    char json_line[2048];
    if (!fgets(json_line, sizeof(json_line), fp))
    {
        pclose(fp);
        return false;
    }
    pclose(fp);

    const char *v;

    // text
    v = find_json_value(json_line, "text");
    if (v)
        snprintf(out->text, sizeof(out->text), "%s", v);
    else
        strcpy(out->text, "（エラー：解析失敗）");

    // heart_delta
    v = find_json_value(json_line, "heart_delta");
    out->heart_delta = v ? atoi(v) : 0;

    // emotion
    v = find_json_value(json_line, "emotion");
    if (v)
        snprintf(out->emotion, sizeof(out->emotion), "%s", v);
    else
        strcpy(out->emotion, "neutral");

    // affection
    v = find_json_value(json_line, "affection");
    out->affection = v ? atoi(v) : 0;

    return true;
}

// =============================================================
// UI系
// =============================================================
#define CHAT_INPUT_MAX 256
#define CHAT_DURATION 120.0f
#define INTRO_DURATION 3.0f

static TTF_Font *g_font_main = NULL;
static TTF_Font *g_font_timer = NULL;
static TTF_Font *g_font_aff = NULL;
static TTF_Font *g_font_delta = NULL;

static char input_buf[CHAT_INPUT_MAX];
static int input_len = 0;

static SDL_Texture *tex_white = NULL;
static SDL_Texture *tex_intro = NULL;

// 新ステータス
static int g_current_affection = 30;
static int g_last_delta = 0;
static char g_current_emotion[64] = "neutral";

static float intro_timer = 0.0f;
static bool intro_done = false;

static float g_time_left = CHAT_DURATION;
static bool g_chat_ended = false;

// =============================================================
// UTF-8 文字長
// =============================================================
static int utf8_char_len(unsigned char c)
{
    if (c < 0x80)
        return 1;
    if ((c >> 5) == 0x6)
        return 2;
    if ((c >> 4) == 0xE)
        return 3;
    if ((c >> 3) == 0x1E)
        return 4;
    return 1;
}

// =============================================================
// draw_text（UIそのまま）
// =============================================================
static void draw_text(SDL_Renderer *r, TTF_Font *font, int x, int y, const char *s)
{
    if (!font || !s || !s[0])
        return;

    SDL_Color col = {255, 255, 255, 255};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, s, col);
    if (!surf)
        return;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(r, tex, NULL, &dst);

    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

// =============================================================
// 自動改行（UIそのまま）
// =============================================================
static int wrap_text_internal(SDL_Renderer *r, TTF_Font *font,
                              int x, int y, int max_width,
                              const char *text, int line_spacing, bool render)
{
    if (!font || !text)
        return 0;

    const char *p = text;
    char line[1024];
    int line_len = 0;
    int draw_y = y;
    int lines = 0;
    line[0] = '\0';

    while (*p)
    {
        int clen = utf8_char_len((unsigned char)*p);

        char candidate[1024];
        memcpy(candidate, line, line_len);
        memcpy(candidate + line_len, p, clen);
        candidate[line_len + clen] = '\0';

        int w;
        TTF_SizeUTF8(font, candidate, &w, NULL);

        if (w > max_width && line_len > 0)
        {
            if (render)
                draw_text(r, font, x, draw_y, line);
            draw_y += line_spacing;
            lines++;
            line_len = 0;
            line[0] = '\0';
            continue;
        }

        memcpy(line, candidate, line_len + clen);
        line_len += clen;
        line[line_len] = '\0';
        p += clen;
    }

    if (line_len > 0)
    {
        if (render)
            draw_text(r, font, x, draw_y, line);
        draw_y += line_spacing;
        lines++;
    }

    return lines * line_spacing;
}

static int draw_text_wrap(SDL_Renderer *r, TTF_Font *font,
                          int x, int y, int max_width,
                          const char *text, int line_spacing)
{
    return wrap_text_internal(r, font, x, y, max_width, text, line_spacing, true);
}

static int measure_text_wrap_height(TTF_Font *font, int max_width,
                                    const char *text, int line_spacing)
{
    return wrap_text_internal(NULL, font, 0, 0, max_width, text, line_spacing, false);
}

// =============================================================
// 新AI方式：チャット送信（UIはそのまま）
// =============================================================
static void chat_send_message(const char *msg)
{
    if (!msg || !msg[0])
        return;

    // Cログ（User）
    if (chat_line_count < CHAT_MAX)
    {
        chat_lines[chat_line_count].from_user = true;
        snprintf(chat_lines[chat_line_count].text,
                 sizeof(chat_lines[chat_line_count].text),
                 "%s", msg);
        chat_line_count++;
    }

    // Python AI 呼び出し
    ChatAIReply ai = {0};
    bool ok = call_grok_bridge(msg, &ai);

    if (!ok)
    {
        strcpy(ai.text, "……ごめん、接続が不安定みたい。");
        ai.heart_delta = 0;
        strcpy(ai.emotion, "neutral");
        ai.affection = g_current_affection;
    }

    g_last_delta = ai.heart_delta;
    g_current_affection = ai.affection;
    snprintf(g_current_emotion, sizeof(g_current_emotion), "%s", ai.emotion);

    // Cログ（AI）
    if (chat_line_count < CHAT_MAX)
    {
        chat_lines[chat_line_count].from_user = false;
        snprintf(chat_lines[chat_line_count].text,
                 sizeof(chat_lines[chat_line_count].text),
                 "%s", ai.text);
        chat_line_count++;
    }
}

// =============================================================
// Enter
// =============================================================
void scene_chat_enter(void)
{
    SDL_StartTextInput();

    g_font_main = TTF_OpenFont("assets/font/main.ttf", 26);
    g_font_timer = TTF_OpenFont("assets/font/main.ttf", 36);
    g_font_aff = TTF_OpenFont("assets/font/main.ttf", 28);
    g_font_delta = TTF_OpenFont("assets/font/main.ttf", 28);

    tex_white = SDL_CreateTexture(g_renderer,
                                  SDL_PIXELFORMAT_RGBA8888,
                                  SDL_TEXTUREACCESS_TARGET,
                                  1, 1);
    SDL_SetRenderTarget(g_renderer, tex_white);
    SDL_SetRenderDrawColor(g_renderer, 200, 200, 255, 255);
    SDL_RenderClear(g_renderer);
    SDL_SetRenderTarget(g_renderer, NULL);

    tex_intro = load_texture(g_renderer, "assets/ui/chat_start.png");

    intro_timer = 0.0f;
    intro_done = false;

    input_buf[0] = '\0';
    input_len = 0;

    g_time_left = CHAT_DURATION;
    g_chat_ended = false;

    chat_line_count = 0;
}

// =============================================================
// Leave
// =============================================================
void scene_chat_leave(void)
{
    SDL_StopTextInput();

    if (g_font_main)
        TTF_CloseFont(g_font_main);
    if (g_font_timer)
        TTF_CloseFont(g_font_timer);
    if (g_font_aff)
        TTF_CloseFont(g_font_aff);
    if (g_font_delta)
        TTF_CloseFont(g_font_delta);

    if (tex_white)
        SDL_DestroyTexture(tex_white);
    if (tex_intro)
        SDL_DestroyTexture(tex_intro);
}

// =============================================================
// Update（UIそのまま）
// =============================================================
void scene_chat_update(float dt)
{
    if (!intro_done)
    {
        intro_timer += dt;
        if (intro_timer >= INTRO_DURATION)
            intro_done = true;
        return;
    }

    if (!g_chat_ended)
    {
        g_time_left -= dt;
        if (g_time_left <= 0)
        {
            g_time_left = 0;
            g_chat_ended = true;
            return;
        }
    }

    if (input_is_pressed(SDL_SCANCODE_ESCAPE))
    {
        g_running = false;
        return;
    }

    const char *typed = input_get_text();
    if (typed)
    {
        size_t l = strlen(typed);
        if (input_len + l < CHAT_INPUT_MAX - 1)
        {
            strcat(input_buf, typed);
            input_len += (int)l;
        }
    }

    if (input_is_pressed(SDL_SCANCODE_BACKSPACE))
    {
        if (input_len > 0)
        {
            int back_len = 1;
            unsigned char c = (unsigned char)input_buf[input_len - 1];

            // UTF-8 後退処理
            if ((c & 0x80) != 0)
            {
                int i = input_len - 1;
                while (i > 0 && (input_buf[i] & 0xC0) == 0x80)
                    i--;
                back_len = input_len - i;
            }

            if (back_len > input_len)
                back_len = input_len;
            input_len -= back_len;
            input_buf[input_len] = '\0';
        }
    }

    if (input_is_pressed(SDL_SCANCODE_RETURN))
    {
        if (input_len > 0)
        {
            chat_send_message(input_buf);
            input_buf[0] = '\0';
            input_len = 0;
        }
    }
}

// =============================================================
// Render（UI完全そのまま）
// =============================================================
void scene_chat_render(SDL_Renderer *r)
{
    SDL_SetRenderDrawColor(r, 32, 32, 32, 255);
    SDL_RenderClear(r);

    // スライドイン
    if (!intro_done)
    {
        if (tex_intro)
        {
            int w = 500, h = 500;
            float t = intro_timer / INTRO_DURATION;
            if (t > 1.0f)
                t = 1.0f;

            float pos_x = -w + (1280 + w) * t;
            int pos_y = 180;

            SDL_Rect dst = {(int)pos_x, pos_y, w, h};
            SDL_RenderCopy(r, tex_intro, NULL, &dst);
        }
        return;
    }

    // 左側の立ち絵領域
    if (tex_white)
    {
        SDL_Rect left = {20, 80, 300, 500};
        SDL_RenderCopy(r, tex_white, NULL, &left);
    }

    // チャットログ（UIそのまま）
    const int log_x = 350;
    const int log_top = 60;
    const int log_bottom = 600;
    const int log_h = log_bottom - log_top;
    const int max_width = 700;
    const int line_spacing = 32;

    int total_h = 0;
    int heights[CHAT_MAX];

    for (int i = 0; i < chat_line_count; i++)
    {
        const char *prefix = chat_lines[i].from_user ? "You: " : "She: ";
        char buf[600];
        snprintf(buf, sizeof(buf), "%s%s", prefix, chat_lines[i].text);
        heights[i] = measure_text_wrap_height(g_font_main, max_width, buf, line_spacing);
        total_h += heights[i];
    }

    int y = log_top;
    if (total_h > log_h)
        y -= (total_h - log_h);

    for (int i = 0; i < chat_line_count; i++)
    {
        const char *prefix = chat_lines[i].from_user ? "You: " : "She: ";
        char buf[600];
        snprintf(buf, sizeof(buf), "%s%s", prefix, chat_lines[i].text);
        draw_text_wrap(r, g_font_main, log_x, y, max_width, buf, line_spacing);
        y += heights[i];
    }

    // HUD
    char tmp[64];

    int sec = (int)g_time_left;
    snprintf(tmp, sizeof(tmp), "%02d:%02d", sec / 60, sec % 60);
    draw_text(r, g_font_timer, 1100, 40, tmp);

    snprintf(tmp, sizeof(tmp), "好感度 = %d", g_current_affection);
    draw_text(r, g_font_aff, 1100, 120, tmp);

    snprintf(tmp, sizeof(tmp), "変化量 = %+d", g_last_delta);
    draw_text(r, g_font_delta, 1100, 160, tmp);

    // 入力欄
    SDL_Rect box = {20, 620, 1240, 50};
    SDL_SetRenderDrawColor(r, 60, 60, 60, 255);
    SDL_RenderFillRect(r, &box);
    draw_text(r, g_font_main, 30, 630, input_buf);
}
