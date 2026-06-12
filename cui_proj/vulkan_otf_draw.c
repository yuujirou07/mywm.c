#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include "vulkan_otf_draw.h"
#include "pty_make.h"
#include FT_FREETYPE_H

int load_otf(char *file_path, struct pos font_size, struct bmf_data *bmf)
{
    FT_Library lib;
    FT_Face face;

    FT_Init_FreeType(&lib);
    FT_New_Face(lib, "/home/yuujirou07/myfont.otf", 0, &face);
    FT_Set_Pixel_Sizes(face, 0, font_size.h * 2);

    FT_Load_Char(face, 'A', FT_LOAD_RENDER);
    bmf->bitmap_buffer = face->glyph->bitmap.buffer;
    bmf->width  = face->glyph->bitmap.width;
    bmf->height = face->glyph->bitmap.rows;

    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    return 0;
}

int load_otf_glyphs(const char *file_path, struct pos font_size,
                    struct glyph_data glyphs[128], int *ascender_out)
{
    FT_Library lib;
    FT_Face face;

    if (FT_Init_FreeType(&lib)) return -1;
    if (FT_New_Face(lib, file_path, 0, &face)) {
        FT_Done_FreeType(lib);
        return -1;
    }
    FT_Set_Pixel_Sizes(face, 0, font_size.h * 2);

    if (ascender_out)
        *ascender_out = (int)(face->size->metrics.ascender >> 6);

    memset(glyphs, 0, sizeof(struct glyph_data) * 128);

    for (int c = 32; c < 127; c++) {
        if (FT_Load_Char(face, (unsigned long)c, FT_LOAD_RENDER))
            continue;
        FT_GlyphSlot g = face->glyph;
        int w = (int)g->bitmap.width;
        int h = (int)g->bitmap.rows;
        glyphs[c].width     = w;
        glyphs[c].height    = h;
        glyphs[c].bearing_x = g->bitmap_left;
        glyphs[c].bearing_y = g->bitmap_top;
        glyphs[c].advance_x = (int)(g->advance.x >> 6);
        if (w > 0 && h > 0) {
            glyphs[c].bitmap = malloc((size_t)(w * h));
            if (glyphs[c].bitmap)
                memcpy(glyphs[c].bitmap, g->bitmap.buffer, (size_t)(w * h));
        }
    }

    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    return 0;
}

void free_otf_glyphs(struct glyph_data glyphs[128])
{
    for (int c = 0; c < 128; c++) {
        free(glyphs[c].bitmap);
        glyphs[c].bitmap = NULL;
    }
}
