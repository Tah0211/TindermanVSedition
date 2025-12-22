// =============================================================
//  scenes/3_scene_chat.c
//  Chatシーン（自動改行 + 自動スクロール + スライドイン演出）
//  フェーズ方式（ターン制）完成版
//  ★追加：8大感情の表情差分を左側に表示（assets/girls/<id>/portrait/<emotion>.png）
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
#include <stdlib.h> // atoi用

// =============================================================
// Chatログ
// =============================================================
#define CHAT_MAX 200

typedef enum
{
    CHAT_USER = 0,
    CHAT_CHAR,
    CHAT_SYSTEM
} ChatLineType;

typedef struct
{
    ChatLineType type;
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
// AI 返信
// =============================================================
typedef struct
{
    char text[512];
    int heart_delta;
    char emotion[64];
    int affection;
} ChatAIReply;

// =============================================================
// フェーズ定義
// =============================================================
typedef enum
{
    PHASE_INTRO = 0,
    PHASE_EXPAND,
    PHASE_DIVE,
    PHASE_RESOLVE
} ChatPhase;

static ChatPhase g_phase = PHASE_INTRO;
static int g_turn_left = 2;

static int phase_turns(ChatPhase p)
{
    switch (p)
    {
    case PHASE_INTRO:   return 2;
    case PHASE_EXPAND:  return 3;
    case PHASE_DIVE:    return 5;
    case PHASE_RESOLVE: return 2;
    default:            return 0;
    }
}

static const char *phase_label(ChatPhase p)
{
    switch (p)
    {
    case PHASE_INTRO:   return "フェーズ1：まずは名前を知るところから";
    case PHASE_EXPAND:  return "フェーズ2：話を広げてみよう";
    case PHASE_DIVE:    return "フェーズ3：一歩踏みこんでみるのも？";
    case PHASE_RESOLVE: return "フェーズ4：君の思いを伝えよう";
    default:            return "";
    }
}

// =============================================================
// girl_id -> 表示名
// =============================================================
static char g_char_name[64] = "She";
static char g_girl_id[64] = "";

typedef struct
{
    const char *id;
    const char *name;
} GirlNameMap;

static const GirlNameMap g_girl_name_map[] = {
    {"himari",  "ひまり"},
    {"kiritan", "きりたん"},
    {"sayo",    "小夜"},
    {NULL, NULL}
};

static const char *resolve_girl_name(const char *girl_id)
{
    for (int i = 0; g_girl_name_map[i].id; i++)
    {
        if (strcmp(g_girl_name_map[i].id, girl_id) == 0)
            return g_girl_name_map[i].name;
    }
    return "She";
}

// =============================================================
// JSON文字列の簡易アンエスケープ
// =============================================================
static void unescape_json_basic_inplace(char *s)
{
    if (!s) return;

    char *src = s;
    char *dst = s;

    while (*src)
    {
        if (src[0] == '\\' && src[1])
        {
            char c = src[1];
            if (c == 'n' || c == 'r' || c == 't')
            {
                *dst++ = ' ';
                src += 2;
                continue;
            }
            if (c == '"')
            {
                *dst++ = '"';
                src += 2;
                continue;
            }
            if (c == '\\')
            {
                *dst++ = '\\';
                src += 2;
                continue;
            }
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

static void normalize_newlines(char *s)
{
    if (!s) return;
    for (; *s; s++)
    {
        if (*s == '\n' || *s == '\r')
            *s = ' ';
    }
}

// =============================================================
// ★追加：8大感情（固定）
// =============================================================
typedef enum
{
    EMO_TRUST = 0,
    EMO_JOY,
    EMO_ANTICIPATION,
    EMO_SURPRISE,
    EMO_FEAR,
    EMO_SADNESS,
    EMO_ANGER,
    EMO_DISGUST,
    EMO_COUNT
} EmotionId;

static const char *g_emotion_keys[EMO_COUNT] = {
    "trust",
    "joy",
    "anticipation",
    "surprise",
    "fear",
    "sadness",
    "anger",
    "disgust"
};

static int emotion_key_to_id(const char *key)
{
    if (!key || !key[0]) return -1;
    for (int i = 0; i < EMO_COUNT; i++)
    {
        if (strcmp(key, g_emotion_keys[i]) == 0)
            return i;
    }
    return -1;
}

// =============================================================
// ★追加：立ち絵（表情差分）
// =============================================================
static SDL_Texture *g_portrait_by_emotion[EMO_COUNT] = {0};
static SDL_Texture *g_portrait_current = NULL;
static int g_portrait_current_id = EMO_TRUST; // 初期は trust

static void unload_portraits(void)
{
    for (int i = 0; i < EMO_COUNT; i++)
    {
        if (g_portrait_by_emotion[i])
        {
            SDL_DestroyTexture(g_portrait_by_emotion[i]);
            g_portrait_by_emotion[i] = NULL;
        }
    }
    g_portrait_current = NULL;
    g_portrait_current_id = EMO_TRUST;
}

static void load_portraits_for_girl(SDL_Renderer *r, const char *girl_id)
{
    unload_portraits();

    if (!girl_id || !girl_id[0])
        return;

    for (int i = 0; i < EMO_COUNT; i++)
    {
        char path[512];
        snprintf(path, sizeof(path),
                 "assets/girls/%s/portrait/%s.png",
                 girl_id, g_emotion_keys[i]);

        g_portrait_by_emotion[i] = load_texture(r, path);
    }

    // 初期：trust があれば trust、なければ最初に見つかったもの
    if (g_portrait_by_emotion[EMO_TRUST])
    {
        g_portrait_current_id = EMO_TRUST;
        g_portrait_current = g_portrait_by_emotion[EMO_TRUST];
    }
    else
    {
        for (int i = 0; i < EMO_COUNT; i++)
        {
            if (g_portrait_by_emotion[i])
            {
                g_portrait_current_id = i;
                g_portrait_current = g_portrait_by_emotion[i];
                break;
            }
        }
    }
}

static void set_current_emotion(const char *emotion_key)
{
    int id = emotion_key_to_id(emotion_key);
    if (id < 0) return; // 失敗なら「直前維持」（neutralは作らない）

    // テクスチャが無い感情なら、ここでも直前維持（素材未準備でも落ちない）
    if (!g_portrait_by_emotion[id])
        return;

    g_portrait_current_id = id;
    g_portrait_current = g_portrait_by_emotion[id];
}

static void render_texture_fit(SDL_Renderer *r, SDL_Texture *tex, SDL_Rect dst)
{
    if (!tex) return;

    int tw = 0, th = 0;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    if (tw <= 0 || th <= 0) return;

    float sx = (float)dst.w / (float)tw;
    float sy = (float)dst.h / (float)th;
    float s = (sx < sy) ? sx : sy;

    int w = (int)(tw * s);
    int h = (int)(th * s);

    SDL_Rect out = {
        dst.x + (dst.w - w) / 2,
        dst.y + (dst.h - h) / 2,
        w, h
    };
    SDL_RenderCopy(r, tex, NULL, &out);
}

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

    v = find_json_value(json_line, "text");
    snprintf(out->text, sizeof(out->text), "%s", v ? v : "……");
    unescape_json_basic_inplace(out->text);
    normalize_newlines(out->text);

    v = find_json_value(json_line, "heart_delta");
    out->heart_delta = v ? atoi(v) : 0;

    v = find_json_value(json_line, "emotion");
    // ★neutralは使わない：取れなかったら空にして「直前維持」に回す
    snprintf(out->emotion, sizeof(out->emotion), "%s", v ? v : "");

    v = find_json_value(json_line, "affection");
    out->affection = v ? atoi(v) : 0;

    return true;
}

// =============================================================
// UI / 状態
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

static int g_current_affection = 30;
static int g_last_delta = 0;
// ★neutral廃止：初期は trust
static char g_current_emotion[64] = "trust";

static float intro_timer = 0.0f;
static bool intro_done = false;

static float g_time_left = CHAT_DURATION;
static bool g_chat_ended = false;

// =============================================================
// UTF-8
// =============================================================
static int utf8_char_len(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

// =============================================================
// draw_text
// =============================================================
static void draw_text(SDL_Renderer *r, TTF_Font *font, int x, int y, const char *s)
{
    if (!font || !s || !s[0])
        return;

    SDL_Color col = {255, 255, 255, 255};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, s, col);
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

// =============================================================
// 自動改行
// =============================================================
static int wrap_text_internal(SDL_Renderer *r, TTF_Font *font,
                             int x, int y, int max_width,
                             const char *text, int line_spacing, bool render)
{
    const char *p = text;
    char line[1024] = {0};
    int line_len = 0;
    int draw_y = y;
    int lines = 0;

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
// 表示用prefix込み文字列を作る
// =============================================================
static void build_line_with_prefix(char *out, size_t out_sz, const ChatLine *ln)
{
    if (!out || out_sz == 0 || !ln)
        return;

    if (ln->type == CHAT_USER)
    {
        snprintf(out, out_sz, "You: %s", ln->text);
    }
    else if (ln->type == CHAT_CHAR)
    {
        snprintf(out, out_sz, "%s: %s", g_char_name, ln->text);
    }
    else
    {
        snprintf(out, out_sz, "%s", ln->text);
    }
}

// =============================================================
// チャット送信（フェーズ制）
// =============================================================
static void chat_send_message(const char *msg)
{
    if (!msg || !msg[0])
        return;

    if (chat_line_count >= CHAT_MAX - 2)
        return;

    chat_lines[chat_line_count++] = (ChatLine){CHAT_USER, ""};
    snprintf(chat_lines[chat_line_count - 1].text, 512, "%s", msg);

    ChatAIReply ai = {0};
    call_grok_bridge(msg, &ai);

    g_last_delta = ai.heart_delta;
    g_current_affection = ai.affection;

    // ★感情：8大感情のみ。取れなかったら直前維持。
    if (ai.emotion[0])
    {
        snprintf(g_current_emotion, sizeof(g_current_emotion), "%s", ai.emotion);
        set_current_emotion(g_current_emotion);
    }

    if (chat_line_count >= CHAT_MAX)
        return;

    chat_lines[chat_line_count++] = (ChatLine){CHAT_CHAR, ""};
    snprintf(chat_lines[chat_line_count - 1].text, 512, "%s", ai.text);

    g_turn_left--;

    if (g_turn_left <= 0)
    {
        if (g_phase < PHASE_RESOLVE)
        {
            g_phase++;
            g_turn_left = phase_turns(g_phase);

            if (chat_line_count < CHAT_MAX)
            {
                chat_lines[chat_line_count++] =
                    (ChatLine){CHAT_SYSTEM, "── フェーズが進行した ──"};
            }
        }
        else
        {
            g_chat_ended = true;
        }
    }
}

// =============================================================
// Enter
// =============================================================
void scene_chat_enter(void)
{
    SDL_StartTextInput();

    // build.json から girl_id を1回だけ読み、表示名に解決
    {
        FILE *fp = fopen("build.json", "r");
        if (fp)
        {
            char json[4096] = {0};
            size_t n = fread(json, 1, sizeof(json) - 1, fp);
            json[n] = '\0';
            fclose(fp);

            const char *id = find_json_value(json, "girl_id");
            if (id && id[0])
            {
                snprintf(g_girl_id, sizeof(g_girl_id), "%s", id);
                snprintf(g_char_name, sizeof(g_char_name), "%s",
                         resolve_girl_name(g_girl_id));
            }
        }
    }

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
    g_phase = PHASE_INTRO;
    g_turn_left = phase_turns(g_phase);

    // ★追加：表情差分のロード（左ペイン用）
    // 初期感情は trust（neutralなし）
    snprintf(g_current_emotion, sizeof(g_current_emotion), "%s", "trust");
    load_portraits_for_girl(g_renderer, g_girl_id);
    set_current_emotion(g_current_emotion);
}

// =============================================================
// Leave
// =============================================================
void scene_chat_leave(void)
{
    SDL_StopTextInput();

    if (g_font_main) TTF_CloseFont(g_font_main);
    if (g_font_timer) TTF_CloseFont(g_font_timer);
    if (g_font_aff) TTF_CloseFont(g_font_aff);
    if (g_font_delta) TTF_CloseFont(g_font_delta);

    if (tex_white) SDL_DestroyTexture(tex_white);
    if (tex_intro) SDL_DestroyTexture(tex_intro);

    // ★追加：表情差分 解放
    unload_portraits();
}

// =============================================================
// Update
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

    const char *typed = input_get_text();
    if (typed && input_len + (int)strlen(typed) < CHAT_INPUT_MAX - 1)
    {
        strcat(input_buf, typed);
        input_len += (int)strlen(typed);
    }

    if (input_is_pressed(SDL_SCANCODE_BACKSPACE) && input_len > 0)
    {
        input_len--;
        input_buf[input_len] = '\0';
    }

    if (input_is_pressed(SDL_SCANCODE_RETURN) && input_len > 0)
    {
        chat_send_message(input_buf);
        input_buf[0] = '\0';
        input_len = 0;
    }
}

// =============================================================
// Render
// =============================================================
void scene_chat_render(SDL_Renderer *r)
{
    // スライドイン演出
    if (!intro_done)
    {
        if (tex_intro)
        {
            int w = 500, h = 500;
            float t = intro_timer / INTRO_DURATION;
            if (t > 1.0f) t = 1.0f;

            float pos_x = -w + (1280 + w) * t;
            int pos_y = 180;

            SDL_Rect dst = {(int)pos_x, pos_y, w, h};
            SDL_RenderCopy(r, tex_intro, NULL, &dst);
        }
        return;
    }

    SDL_SetRenderDrawColor(r, 32, 32, 32, 255);
    SDL_RenderClear(r);

    draw_text(r, g_font_aff, 10, 20, phase_label(g_phase));

    char tmp[64];
    snprintf(tmp, sizeof(tmp), "残りターン：%d", g_turn_left);
    draw_text(r, g_font_aff, 10, 50, tmp);

    // =============================================================
    // ★追加：左ペイン（表情差分）
    // =============================================================
    // 左ペイン領域：ロゴ/テキストを邪魔しない位置に固定
    SDL_Rect leftPane = {20, 100, 300, 500};

    SDL_SetRenderDrawColor(r, 40, 40, 40, 255);
    SDL_RenderFillRect(r, &leftPane);

    // 現在感情（デバッグ表示したいなら）
    // draw_text(r, g_font_main, 30, 110, g_current_emotion);

    if (g_portrait_current)
    {
        render_texture_fit(r, g_portrait_current, leftPane);
    }
    else
    {
        // 素材が無い場合のフォールバック
        draw_text(r, g_font_main, 30, 330, "No portrait");
    }

    // =============================================================
    // ログ領域
    // =============================================================
    const int log_x = 350;
    const int log_top = 100;
    const int log_bottom = 600;
    const int visible_h = log_bottom - log_top;
    const int max_width = 700;
    const int line_spacing = 32;

    int heights[CHAT_MAX];
    int total_h = 0;

    for (int i = 0; i < chat_line_count; i++)
    {
        char buf[600];
        build_line_with_prefix(buf, sizeof(buf), &chat_lines[i]);
        normalize_newlines(buf);

        heights[i] = measure_text_wrap_height(
            g_font_main, max_width, buf, line_spacing);
        total_h += heights[i];
    }

    int start = 0;
    int h = 0;

    for (int i = chat_line_count - 1; i >= 0; i--)
    {
        if (h + heights[i] > visible_h)
            break;
        h += heights[i];
        start = i;
    }

    int y = log_bottom - h;

    for (int i = start; i < chat_line_count; i++)
    {
        char buf[600];
        build_line_with_prefix(buf, sizeof(buf), &chat_lines[i]);
        normalize_newlines(buf);

        draw_text_wrap(r, g_font_main, log_x, y, max_width, buf, line_spacing);
        y += heights[i];
    }

    // 右HUD
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
