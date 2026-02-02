// =============================================================
//  scenes/3_scene_chat.c
//  Chatシーン（自動改行 + 自動スクロール + スライドイン演出）
//  ★仕様：フェーズ廃止、残りターンのみ（TOTAL_TURNS）
//  ★1ターンごとに15秒（TURN_LIMIT_SEC）
//    - Enterで手動送信OK
//    - タイムアウトで途中入力をそのまま自動送信（空なら "…"）
//  ★全ターン終了：ログ表示 → Enterでステータス配分へ
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
// ★ここだけ触れば仕様変更できる（最重要）
// =============================================================

// 合計ターン数（= 会話の往復回数）
#define TOTAL_TURNS 8

// 1ターンの制限時間（秒）
#define TURN_LIMIT_SEC 15.0f

// intro演出時間
#define INTRO_DURATION 3.0f

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

        size_t len = (size_t)(end - p);
        if (len >= sizeof(buf))
            len = sizeof(buf) - 1;

        memcpy(buf, p, len);
        buf[len] = 0;
        return buf;
    }

    const char *end = p;
    while (*end && *end != ',' && *end != '}' && *end != '\n')
        end++;

    size_t len = (size_t)(end - p);
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
    {"himari", "ひまり"},
    {"kiritan", "きりたん"},
    {"sayo", "小夜"},
    {NULL, NULL}};

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
    if (!s)
        return;

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
    if (!s)
        return;
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
    "disgust"};

static int emotion_key_to_id(const char *key)
{
    if (!key || !key[0])
        return -1;
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
    if (id < 0)
        return;

    if (!g_portrait_by_emotion[id])
        return;

    g_portrait_current_id = id;
    g_portrait_current = g_portrait_by_emotion[id];
}

static void render_texture_fit(SDL_Renderer *r, SDL_Texture *tex, SDL_Rect dst)
{
    if (!tex)
        return;

    int tw = 0, th = 0;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    if (tw <= 0 || th <= 0)
        return;

    float sx = (float)dst.w / (float)tw;
    float sy = (float)dst.h / (float)th;
    float s = (sx < sy) ? sx : sy;

    int w = (int)(tw * s);
    int h = (int)(th * s);

    SDL_Rect out = {
        dst.x + (dst.w - w) / 2,
        dst.y + (dst.h - h) / 2,
        w,
        h};

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
    // neutral廃止：取れなければ空→直前維持
    snprintf(out->emotion, sizeof(out->emotion), "%s", v ? v : "");

    v = find_json_value(json_line, "affection");
    out->affection = v ? atoi(v) : 0;

    return true;
}

// =============================================================
// UI / 状態
// =============================================================
#define CHAT_INPUT_MAX 256

static TTF_Font *g_font_main = NULL;
static TTF_Font *g_font_aff = NULL;
static TTF_Font *g_font_delta = NULL;

static char input_buf[CHAT_INPUT_MAX];
static int input_len = 0;

static SDL_Texture *tex_intro = NULL;

// HUD
static int g_current_affection = 30;
static int g_last_delta = 0;
static char g_current_emotion[64] = "trust";

// intro
static float intro_timer = 0.0f;
static bool intro_done = false;

// ターン制
static int g_turn_done = 0;                 // 既に完了したターン数（0..TOTAL_TURNS）
static float g_turn_time_left = TURN_LIMIT_SEC;

// 終了
static bool g_chat_ended = false;
static bool g_end_notice_added = false;

// =============================================================
// UTF-8
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
// draw_text
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
    if (!tex)
    {
        SDL_FreeSurface(surf);
        return;
    }

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
        memcpy(candidate, line, (size_t)line_len);
        memcpy(candidate + line_len, p, (size_t)clen);
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

        memcpy(line, candidate, (size_t)(line_len + clen));
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
        snprintf(out, out_sz, "You: %s", ln->text);
    else if (ln->type == CHAT_CHAR)
        snprintf(out, out_sz, "%s: %s", g_char_name, ln->text);
    else
        snprintf(out, out_sz, "%s", ln->text);
}

// =============================================================
// 終了導線ログ（1回だけ）
// =============================================================
static void add_end_notice_once(void)
{
    if (g_end_notice_added)
        return;

    if (chat_line_count < CHAT_MAX)
    {
        chat_lines[chat_line_count++] =
            (ChatLine){CHAT_SYSTEM, "── 会話終了：Enterでステータス配分へ ──"};
    }
    g_end_notice_added = true;
}

// =============================================================
// ターン残数
// =============================================================
static int turns_left(void)
{
    int left = TOTAL_TURNS - g_turn_done;
    if (left < 0)
        left = 0;
    return left;
}

// =============================================================
// チャット送信（ターン制）
// - Enter送信：scene_chat_updateから呼ぶ
// - タイムアウト送信：scene_chat_updateから呼ぶ
// =============================================================
static void chat_send_message(const char *msg)
{
    if (g_chat_ended)
        return;

    // ターンが尽きているなら終了扱い
    if (g_turn_done >= TOTAL_TURNS)
    {
        g_chat_ended = true;
        add_end_notice_once();
        return;
    }

    // 空なら "…" を送る（進行停止防止）
    char send_buf[CHAT_INPUT_MAX];
    if (!msg || !msg[0])
        snprintf(send_buf, sizeof(send_buf), "…");
    else
        snprintf(send_buf, sizeof(send_buf), "%s", msg);

    if (chat_line_count >= CHAT_MAX - 2)
        return;

    // ユーザー発言
    chat_lines[chat_line_count++] = (ChatLine){CHAT_USER, ""};
    snprintf(chat_lines[chat_line_count - 1].text, 512, "%s", send_buf);

    // AI呼び出し（ブロッキング）
    ChatAIReply ai = {0};
    call_grok_bridge(send_buf, &ai);

    g_last_delta = ai.heart_delta;
    g_current_affection = ai.affection;

    // 感情：取れなければ直前維持
    if (ai.emotion[0])
    {
        snprintf(g_current_emotion, sizeof(g_current_emotion), "%s", ai.emotion);
        set_current_emotion(g_current_emotion);
    }

    // AI返答
    if (chat_line_count < CHAT_MAX)
    {
        chat_lines[chat_line_count++] = (ChatLine){CHAT_CHAR, ""};
        snprintf(chat_lines[chat_line_count - 1].text, 512, "%s", ai.text);
    }

    // ★ターン消費（往復が完了したら1ターン）
    g_turn_done++;

    // 次ターンがあるなら15秒をリセット
    if (g_turn_done < TOTAL_TURNS)
    {
        g_turn_time_left = TURN_LIMIT_SEC;
        return;
    }

    // ここまで来たら会話終了
    g_chat_ended = true;
    add_end_notice_once();
}

// =============================================================
// Enter
// =============================================================
void scene_chat_enter(void)
{
    SDL_StartTextInput();

    // build.json から girl_id を読み、表示名に解決
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

    // フォント（あなたのCHATに合わせて main.ttf）
    g_font_main = TTF_OpenFont("assets/font/main.ttf", 26);
    g_font_aff = TTF_OpenFont("assets/font/main.ttf", 28);
    g_font_delta = TTF_OpenFont("assets/font/main.ttf", 28);

    // intro
    tex_intro = load_texture(g_renderer, "assets/ui/chat_start.png");
    intro_timer = 0.0f;
    intro_done = false;

    // 入力
    input_buf[0] = '\0';
    input_len = 0;

    // ログ
    chat_line_count = 0;

    // ターン制初期化
    g_turn_done = 0;
    g_turn_time_left = TURN_LIMIT_SEC;

    // 終了
    g_chat_ended = false;
    g_end_notice_added = false;

    // 表情差分
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

    if (g_font_main)
        TTF_CloseFont(g_font_main);
    if (g_font_aff)
        TTF_CloseFont(g_font_aff);
    if (g_font_delta)
        TTF_CloseFont(g_font_delta);

    if (tex_intro)
        SDL_DestroyTexture(tex_intro);

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

    // 終了後：EnterでALLOCATEへ
    if (g_chat_ended)
    {
        add_end_notice_once();
        if (input_is_pressed(SDL_SCANCODE_RETURN))
            change_scene(SCENE_ALLOCATE);
        return;
    }

    // 15秒減算（入力中のみ）
    g_turn_time_left -= dt;

    // 入力
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

    // ★手動送信（空でもOK：内部で "…" になる）
    if (input_is_pressed(SDL_SCANCODE_RETURN))
    {
        chat_send_message(input_buf);
        input_buf[0] = '\0';
        input_len = 0;
        return;
    }

    // ★自動送信（途中入力をそのまま）
    if (g_turn_time_left <= 0.0f)
    {
        g_turn_time_left = 0.0f;
        chat_send_message(input_buf);
        input_buf[0] = '\0';
        input_len = 0;
        return;
    }
}

// =============================================================
// Render
// =============================================================
void scene_chat_render(SDL_Renderer *r)
{
    // intro演出
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

    SDL_SetRenderDrawColor(r, 32, 32, 32, 255);
    SDL_RenderClear(r);

    // 左上：残りターンだけ
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "残りターン：%d", turns_left());
    draw_text(r, g_font_aff, 10, 20, tmp);

    // 15秒（右上）
    {
        int sec = (int)(g_turn_time_left + 0.999f);
        if (sec < 0)
            sec = 0;
        snprintf(tmp, sizeof(tmp), "残り %02d 秒", sec);
        draw_text(r, g_font_aff, 1050, 60, tmp);
    }

    // =============================================================
    // 左ペイン（表情差分）
    // =============================================================
    SDL_Rect leftPane = {20, 100, 300, 500};
    SDL_SetRenderDrawColor(r, 40, 40, 40, 255);
    SDL_RenderFillRect(r, &leftPane);

    if (g_portrait_current)
        render_texture_fit(r, g_portrait_current, leftPane);
    else
        draw_text(r, g_font_main, 30, 330, "No portrait");

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

        heights[i] = measure_text_wrap_height(g_font_main, max_width, buf, line_spacing);
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

    if (!g_chat_ended)
        draw_text(r, g_font_main, 30, 630, input_buf);
    else
        draw_text(r, g_font_main, 30, 630, "Enterでステータス配分へ");
}
