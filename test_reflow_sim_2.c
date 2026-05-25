#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

struct pos { int w, h; };
struct cursor { struct pos cur_pos; };
struct Color { unsigned char r, g, b, a; };
struct bash_parser_required_memb { struct Color now_fg_color, now_bg_color; };
struct term_cell { int character; struct Color fg_color, bg_color; bool is_bold, is_real_chr; };
struct line_info { bool is_wrapped; };

struct term_context {
    struct term_cell *term_cell;
    struct line_info *lines;
    struct pos term_size;
    struct cursor *cur;
    struct bash_parser_required_memb bash_parser_required_memb;
};

void reflow_terminal_text(struct term_context *ctx, struct pos old_term_size, struct term_cell **temp_term_cell_ptr, int term_cell_alloc_size) {
    int old_total = old_term_size.w * old_term_size.h;
    int total = ctx->term_size.w * ctx->term_size.h;
    struct pos term_size = ctx->term_size;
    struct term_cell *temp_term_cell = *temp_term_cell_ptr;

    struct line_info *temp_lines_copy = malloc(sizeof(struct line_info) * old_term_size.h);
    memcpy(temp_lines_copy, ctx->lines, sizeof(struct line_info) * old_term_size.h);

    struct line_info *new_lines_info = calloc(term_size.h, sizeof(struct line_info));
    free(ctx->lines);
    ctx->lines = new_lines_info;

    for(int i=0;i<total;i++){
        temp_term_cell[i].is_real_chr=false;
        temp_term_cell[i].character=' ';
    }

    int max_logical_lines = old_term_size.h;
    if (max_logical_lines == 0) max_logical_lines = 1;
    struct term_cell **log_lines = calloc(max_logical_lines, sizeof(struct term_cell *));
    int *log_line_lengths = calloc(max_logical_lines, sizeof(int));
    int *log_line_caps = calloc(max_logical_lines, sizeof(int));
    int cur_log_line = -1;
    int cursor_log_line = -1;
    int cursor_offset = 0;

    for (int y = 0; y < old_term_size.h; y++) {
        if (y == 0 || !temp_lines_copy[y-1].is_wrapped) {
            cur_log_line++;
            if (cur_log_line >= max_logical_lines) {
                max_logical_lines *= 2;
                log_lines = realloc(log_lines, max_logical_lines * sizeof(struct term_cell *));
                log_line_lengths = realloc(log_line_lengths, max_logical_lines * sizeof(int));
                log_line_caps = realloc(log_line_caps, max_logical_lines * sizeof(int));
            }
            log_line_caps[cur_log_line] = old_term_size.w * 2;
            if (log_line_caps[cur_log_line] == 0) log_line_caps[cur_log_line] = 128;
            log_lines[cur_log_line] = malloc(sizeof(struct term_cell) * log_line_caps[cur_log_line]);
            log_line_lengths[cur_log_line] = 0;
        }
        
        int chars_to_copy = old_term_size.w;
        bool wraps_to_next = temp_lines_copy[y].is_wrapped;
        if (!wraps_to_next) {
           while(chars_to_copy > 0 && !ctx->term_cell[y * old_term_size.w + chars_to_copy - 1].is_real_chr) {
               chars_to_copy--;
           }
        }

        if (ctx->cur->cur_pos.h == y) {
            cursor_log_line = cur_log_line;
            cursor_offset = log_line_lengths[cur_log_line] + ctx->cur->cur_pos.w;
        }

        if (log_line_lengths[cur_log_line] + chars_to_copy > log_line_caps[cur_log_line]) {
            while (log_line_lengths[cur_log_line] + chars_to_copy > log_line_caps[cur_log_line]) {
                log_line_caps[cur_log_line] *= 2;
            }
            log_lines[cur_log_line] = realloc(log_lines[cur_log_line], sizeof(struct term_cell) * log_line_caps[cur_log_line]);
        }
        if (chars_to_copy > 0) {
            memcpy(&log_lines[cur_log_line][log_line_lengths[cur_log_line]], 
                   &ctx->term_cell[y * old_term_size.w], 
                   sizeof(struct term_cell) * chars_to_copy);
        }
        log_line_lengths[cur_log_line] += chars_to_copy;
    }
    int total_logical_lines = cur_log_line + 1;

    int max_new_lines = term_size.h * 2;
    if (max_new_lines == 0) max_new_lines = 128;
    struct term_cell **new_lines = calloc(max_new_lines, sizeof(struct term_cell *));
    bool *new_lines_wrapped = calloc(max_new_lines, sizeof(bool));
    int new_line_count = 0;
    int new_cursor_h = 0;
    int new_cursor_w = 0;

    for (int i = 0; i < total_logical_lines; i++) {
        int len = log_line_lengths[i];
        int processed = 0;
        
        int needed_lines = (len + term_size.w - 1) / term_size.w;
        if (needed_lines == 0) needed_lines = 1;
        
        if (cursor_log_line == i) {
            int cursor_line = cursor_offset / term_size.w;
            if (cursor_line + 1 > needed_lines) {
                needed_lines = cursor_line + 1;
            }
        }
        
        for (int k = 0; k < needed_lines; k++) {
            if (new_line_count >= max_new_lines) {
                max_new_lines *= 2;
                new_lines = realloc(new_lines, max_new_lines * sizeof(struct term_cell *));
                new_lines_wrapped = realloc(new_lines_wrapped, max_new_lines * sizeof(bool));
            }
            new_lines[new_line_count] = calloc(term_size.w, sizeof(struct term_cell));
            new_lines_wrapped[new_line_count] = (k < needed_lines - 1);
            
            int to_copy = len - processed;
            if (to_copy > term_size.w) to_copy = term_size.w;
            if (to_copy < 0) to_copy = 0;
            
            if (to_copy > 0) {
                memcpy(new_lines[new_line_count], &log_lines[i][processed], to_copy * sizeof(struct term_cell));
            }
            
            if (cursor_log_line == i) {
                if (cursor_offset >= processed && cursor_offset < processed + term_size.w) {
                    new_cursor_h = new_line_count;
                    new_cursor_w = cursor_offset - processed;
                } else if (cursor_offset == processed + term_size.w && k == needed_lines - 1) {
                    new_cursor_h = new_line_count;
                    new_cursor_w = term_size.w;
                }
            }
            
            processed += term_size.w;
            new_line_count++;
        }
    }

    // NEW START LINE LOGIC
    int start_line = 0;
    if (new_cursor_h >= term_size.h) {
        start_line = new_cursor_h - term_size.h + 1;
    }
    // Also, if the new lines are fewer than term_size.h, and there were lines pushed up before?
    // Without a history buffer, we only have what was on screen. So start_line is at most the end of what we have.
    // If the text is shorter than the screen, start_line is 0.

    for (int y = 0; y < term_size.h; y++) {
        int src_y = start_line + y;
        if (src_y >= 0 && src_y < new_line_count) {
            memcpy(&temp_term_cell[y * term_size.w], new_lines[src_y], term_size.w * sizeof(struct term_cell));
            ctx->lines[y].is_wrapped = new_lines_wrapped[src_y];
        } else {
            for (int x = 0; x < term_size.w; x++) {
                temp_term_cell[y * term_size.w + x].character = '.'; // Empty
                temp_term_cell[y * term_size.w + x].is_real_chr = false;
            }
            ctx->lines[y].is_wrapped = false;
        }
    }

    ctx->cur->cur_pos.h = new_cursor_h - start_line;
    ctx->cur->cur_pos.w = new_cursor_w;

    if (ctx->cur->cur_pos.h < 0) ctx->cur->cur_pos.h = 0;
    if (ctx->cur->cur_pos.h >= term_size.h) ctx->cur->cur_pos.h = term_size.h - 1;

    struct term_cell * swap = ctx->term_cell;
    ctx->term_cell = temp_term_cell;
    *temp_term_cell_ptr = swap;
}

void print_term(struct term_context *ctx) {
    printf("Terminal %dx%d Cursor: (%d, %d)\n", ctx->term_size.w, ctx->term_size.h, ctx->cur->cur_pos.w, ctx->cur->cur_pos.h);
    for(int y=0; y<ctx->term_size.h; y++) {
        printf("%02d: ", y);
        for(int x=0; x<ctx->term_size.w; x++) {
            int c = ctx->term_cell[y*ctx->term_size.w + x].character;
            printf("%c", c ? c : '.');
        }
        printf(ctx->lines[y].is_wrapped ? " (wrapped)\n" : "\n");
    }
}

int main() {
    struct term_context ctx = {0};
    struct cursor cur = {0};
    ctx.cur = &cur;
    
    int w = 10, h = 5;
    ctx.term_size.w = w;
    ctx.term_size.h = h;
    ctx.term_cell = calloc(w * h, sizeof(struct term_cell));
    ctx.lines = calloc(h, sizeof(struct line_info));
    
    const char *text = "Hello World! This is a test.";
    int len = strlen(text);
    int y=0, x=0;
    for(int i=0; i<len; i++) {
        ctx.term_cell[y*w + x].character = text[i];
        ctx.term_cell[y*w + x].is_real_chr = true;
        x++;
        if(x == w) {
            ctx.lines[y].is_wrapped = true;
            x = 0;
            y++;
        }
    }
    
    ctx.cur->cur_pos.w = x;
    ctx.cur->cur_pos.h = y;
    
    print_term(&ctx);
    
    printf("\n--- Resizing to 20x5 ---\n");
    struct pos new_size1 = {20, 5};
    struct term_cell *temp1 = calloc(20*5, sizeof(struct term_cell));
    struct pos old_size1 = ctx.term_size;
    ctx.term_size = new_size1;
    reflow_terminal_text(&ctx, old_size1, &temp1, 20*5);
    print_term(&ctx);

    printf("\n--- Resizing to 5x5 ---\n");
    struct pos new_size2 = {5, 5};
    struct term_cell *temp2 = calloc(5*5, sizeof(struct term_cell));
    struct pos old_size2 = ctx.term_size;
    ctx.term_size = new_size2;
    reflow_terminal_text(&ctx, old_size2, &temp2, 5*5);
    print_term(&ctx);
    
    return 0;
}
