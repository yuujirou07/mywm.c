#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef struct Color {
    unsigned char r, g, b, a;
} Color;

struct pos { int w, h; };

struct term_cell {
    int character;
    Color fg_color, bg_color;
    bool is_bold, is_real_chr;
};

struct line_info { bool is_wrapped; };
struct cursor { struct pos cur_pos; };
struct bash_parser_required_memb { Color now_fg_color, now_bg_color; };

struct term_context {
    struct term_cell *term_cell;
    struct line_info *lines;
    struct pos term_size;
    struct cursor *cur;
    struct bash_parser_required_memb bash_parser_required_memb;
};

void reflow_terminal_text(struct term_context *ctx, struct pos old_term_size, struct term_cell **temp_term_cell_ptr, int term_cell_alloc_size);

int main() {
    // We will test the exact scenario
}
