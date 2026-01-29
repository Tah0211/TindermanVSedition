#ifndef UI_TEXT_H
#define UI_TEXT_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>

TTF_Font *ui_load_font(const char *path, int size);

// 旧：毎回生成（重い）
// void ui_text_draw(SDL_Renderer *r, TTF_Font *font, const char *text, int x, int y);

// 新：キャッシュ付き（推奨）
void ui_text_cache_init(SDL_Renderer *r);
void ui_text_cache_shutdown(void);

// 文字を描く（同じ text は使い回し）
void ui_text_draw(SDL_Renderer *r, TTF_Font *font, const char *text, int x, int y);

// 色付きが必要ならこちらも使える
void ui_text_draw_color(SDL_Renderer *r, TTF_Font *font, const char *text,
                        int x, int y, SDL_Color col);

// キャッシュを全部捨てたい時（シーン切替など）
void ui_text_cache_clear(void);

#endif
