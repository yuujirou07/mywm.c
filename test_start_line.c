#include <stdio.h>

int main() {
    int old_cursor_y = 19;
    int new_cursor_h = 19;
    int term_size_h = 10;
    
    int start_line = new_cursor_h - old_cursor_y;
    printf("Without min(): start_line=%d, rendering src_y %d to %d\n", start_line, start_line, start_line + term_size_h - 1);
    
    int target_cursor_y = old_cursor_y < term_size_h ? old_cursor_y : term_size_h - 1;
    int better_start_line = new_cursor_h - target_cursor_y;
    printf("With min(): start_line=%d, rendering src_y %d to %d\n", better_start_line, better_start_line, better_start_line + term_size_h - 1);
    
    return 0;
}
