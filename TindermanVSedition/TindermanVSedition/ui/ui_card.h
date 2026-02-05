#ifndef UI_CARD_H
#define UI_CARD_H

#include <SDL2/SDL.h>

// SELECT画面用のカードUI（今はダミー）
typedef struct {
    SDL_Rect rect;
} UiCard;

static inline UiCard ui_card_create(SDL_Rect rect)
{
    UiCard c;
    c.rect = rect;
    return c;
}

void ui_card_draw(SDL_Renderer* r, UiCard* c, int focused);

#endif
