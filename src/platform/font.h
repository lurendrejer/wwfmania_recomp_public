/* A small 5x7 font drawn with SDL rectangles (the F1 menu, the image inspector). */
#ifndef WWF_FONT_H
#define WWF_FONT_H

#include <SDL.h>

/* Draws text with its top left at (x, y) in the renderer's draw colour; each font pixel is s x s pixels and a
 * character is 6 * s wide. Lower case shows as upper case, characters the font lacks as '?'. */
void font_text(SDL_Renderer *ren, int x, int y, int s, const char *str);

#endif
