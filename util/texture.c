#include "texture.h"
#include <SDL2/SDL_image.h>

SDL_Texture *load_texture(SDL_Renderer *r, const char *path)
{
    SDL_Texture *tex = IMG_LoadTexture(r, path);
    if (!tex)
    {
        SDL_Log("Failed to load texture %s : %s", path, IMG_GetError());
    }
    return tex;
}
