#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

struct pos { int w, h; };
struct term_cell { int character; bool is_real_chr; };
struct line_info { bool is_wrapped; };

int main() {
    struct pos old_term_size = {10, 5};
    struct pos term_size = {20, 5};
    
    struct term_cell *ctx_term_cell = calloc(50, sizeof(struct term_cell));
    struct line_info ctx_lines[5] = {0};
    
    // Line 3: "1234567890" (wrapped)
    for(int i=0; i<10; i++) {
        ctx_term_cell[3*10 + i].character = 'A';
        ctx_term_cell[3*10 + i].is_real_chr = true;
    }
    ctx_lines[3].is_wrapped = true;
    
    // Line 4: "12345     "
    for(int i=0; i<5; i++) {
        ctx_term_cell[4*10 + i].character = 'B';
        ctx_term_cell[4*10 + i].is_real_chr = true;
    }
    ctx_lines[4].is_wrapped = false;
    
    struct pos cur_pos = {5, 4}; // w=5, h=4
    
    // Simulate logical line extraction
    int cur_log_line = -1;
    int cursor_log_line = -1;
    int cursor_offset = 0;
    
    int max_logical_lines = 5;
    int *log_line_lengths = calloc(max_logical_lines, sizeof(int));
    
    for (int y = 0; y < old_term_size.h; y++) {
        if (y == 0 || !ctx_lines[y-1].is_wrapped) {
            cur_log_line++;
            log_line_lengths[cur_log_line] = 0;
        }
        
        int chars_to_copy = old_term_size.w;
        bool wraps_to_next = ctx_lines[y].is_wrapped;
        if (!wraps_to_next) {
            while(chars_to_copy > 0 && !ctx_term_cell[y * old_term_size.w + chars_to_copy - 1].is_real_chr) {
                chars_to_copy--;
            }
        }
        
        if (cur_pos.h == y) {
            cursor_log_line = cur_log_line;
            cursor_offset = log_line_lengths[cur_log_line] + cur_pos.w;
        }
        
        log_line_lengths[cur_log_line] += chars_to_copy;
    }
    
    int total_logical_lines = cur_log_line + 1;
    
    printf("cursor_log_line: %d\n", cursor_log_line);
    printf("cursor_offset: %d\n", cursor_offset);
    printf("log_line_length[3]: %d\n", log_line_lengths[3]);
    
    // Simulate reflow
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
    
    printf("new_cursor_h: %d\n", new_cursor_h);
    printf("new_cursor_w: %d\n", new_cursor_w);
    printf("new_line_count: %d\n", new_line_count);
}
