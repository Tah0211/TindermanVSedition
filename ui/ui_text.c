#include "ui_text.h"

TTF_Font *ui_load_font(const char *path, int size)
{
    TTF_Font *f = TTF_OpenFont(path, size);
    if (!f)
    {
        SDL_Log("Failed to load font %s : %s", path, TTF_GetError());
    }
    return f;
}

void ui_text_draw(SDL_Renderer *r, TTF_Font *font,
                  const char *text, int x, int y)
{
    if (!font || !text)
        return;

    SDL_Color col = {255, 255, 255, 255};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, col);
    if (!surf)
        return;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};

    SDL_FreeSurface(surf);
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}
