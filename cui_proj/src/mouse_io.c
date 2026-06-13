#include <GLFW/glfw3.h>
#include <string.h>
#include "vulkan_mywrap.h"
#include "mouse_io.h"
#include"pty_make.h"


void init_mouse(struct windata *wd)
{
    glfwSetMouseButtonCallback(wd->window, mouse_button_callback);
    glfwSetCursorPosCallback(wd->window,cursor_position_callback);

}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    struct windata *wd = (struct windata *)glfwGetWindowUserPointer(window);

    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        if(action == GLFW_PRESS)
        {
            if(wd->copy_data.start_copy)
            {
                for(int i = 0;i<wd->copy_data.copy_cell_counter;i++)
                {
                    wd->copy_data.copy_cell[i]->bg_color = wd->copy_data.copy_cell_orig_bg[i];
                    wd->copy_data.copy_cell[i]->fg_color = wd->copy_data.copy_cell_orig_fg[i];
                }

                wd->copy_data.copy_cell_counter = 0;
                *wd->dirty = true;
                memset(wd->copy_data.copy_cell,0,wd->ctx->total_cells * sizeof(struct term_cell*));
                wd->copy_data.start_copy = false;
            }
            else
            {
                wd->copy_data.start_copy = true;
            }

            wd->mouce_data.mouce_button_left_down = true;

        /////マウス左クリック長押しでコピー時の白背景にする機能を実装したが、
        // 白背景にした状態で再度右クリックを押すと白背景が解除される機能は未実装なので実装する
        }
        else
        {
            wd->mouce_data.mouce_button_left_down = false;
            wd->copy_data.start_copy = false;
        }
    }
   
    
}

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos)
{    

    struct windata *wd = (struct windata *)glfwGetWindowUserPointer(window);

    if(wd->copy_data.start_copy)
    {
        struct pos on_cell_mouse_pos;
        double scale = wd->ctx->display_scale;

        on_cell_mouse_pos.h = (ypos * scale)/wd->ctx->cell_h;
        on_cell_mouse_pos.w = (xpos * scale)/wd->ctx->cell_w;

        if(!(on_cell_mouse_pos.h >= 0 && on_cell_mouse_pos.h < wd->ctx->term_size.h &&
           on_cell_mouse_pos.w >= 0 && on_cell_mouse_pos.w < wd->ctx->term_size.w))
            return;
        
        int cell_idx = (on_cell_mouse_pos.h * wd->ctx->term_size.w) + on_cell_mouse_pos.w;
        struct term_cell *cell = &wd->ctx->term_cell[cell_idx];

        if(wd->copy_data.start_idx == 0 && wd->copy_data.start_idx == 0)
        {
            wd->copy_data.start_idx = cell_idx;
        }

        //まだ選択(白塗り)されていないセルだけを記録してから白くする
        Color bg_color = WHITE;
        Color fg_color = BLACK;

        if(memcmp(&cell->bg_color, &bg_color, sizeof(Color)) != 0)
        {
            wd->copy_data.copy_cell_orig_bg[wd->copy_data.copy_cell_counter] = cell->bg_color;
            wd->copy_data.copy_cell_orig_fg[wd->copy_data.copy_cell_counter] = cell->fg_color;
            wd->copy_data.copy_cell[wd->copy_data.copy_cell_counter++] = cell;
            cell->bg_color = bg_color;
            cell->fg_color = fg_color;

            *wd->dirty = true;
        }
    }







}