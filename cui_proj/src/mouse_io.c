#include <GLFW/glfw3.h>
#include <string.h>
#include "vulkan_mywrap.h"
#include "mouse_io.h"
#include"pty_make.h"

// restore_copy_cells(): 選択中に白黒反転したセルを、保存しておいた元の色へ戻す。
static void restore_copy_cells(struct windata *wd)
{
    for(int i = 0; i < wd->copy_data.copy_cell_counter; i++)
    {
        wd->copy_data.copy_cell[i]->bg_color = wd->copy_data.copy_cell_orig_bg[i];
        wd->copy_data.copy_cell[i]->fg_color = wd->copy_data.copy_cell_orig_fg[i];
    }

    wd->copy_data.copy_cell_counter = 0;
    memset(wd->copy_data.copy_cell, 0, wd->ctx->total_cells * sizeof(struct term_cell *));
}


// update_copy_selection(): マウス座標をセル位置へ変換し、ドラッグ開始セルから
// 現在セルまでの範囲をコピー選択として白黒反転する。
static void update_copy_selection(struct windata *wd, double xpos, double ypos)
{
    struct pos on_cell_mouse_pos;
    double scale = wd->ctx->display_scale;

    on_cell_mouse_pos.h = (ypos * scale)/wd->ctx->cell_h;
    on_cell_mouse_pos.w = (xpos * scale)/wd->ctx->cell_w;

    if(!(on_cell_mouse_pos.h >= 0 && on_cell_mouse_pos.h < wd->ctx->term_size.h &&
       on_cell_mouse_pos.w >= 0 && on_cell_mouse_pos.w < wd->ctx->term_size.w))
        return;

    int cell_idx = (on_cell_mouse_pos.h * wd->ctx->term_size.w) + on_cell_mouse_pos.w;

    if(wd->copy_data.copy_cell_idx_data.start_idx_block == false)
    {
        wd->copy_data.copy_cell_idx_data.start_idx = cell_idx;
        wd->copy_data.copy_cell_idx_data.start_idx_block = true;
    }

    wd->copy_data.copy_cell_idx_data.end_idx = cell_idx;

    restore_copy_cells(wd);

    int start_idx = wd->copy_data.copy_cell_idx_data.start_idx;
    int end_idx = wd->copy_data.copy_cell_idx_data.end_idx;
    if(start_idx > end_idx)
    {
        int temp = start_idx;
        start_idx = end_idx;
        end_idx = temp;
    }

    Color bg_color = WHITE;
    Color fg_color = BLACK;

    for(int i = start_idx; i <= end_idx; i++)
    {
        struct term_cell *cell = &wd->ctx->term_cell[i];
        if(memcmp(&cell->bg_color, &bg_color, sizeof(Color)) != 0 ||
           memcmp(&cell->fg_color, &fg_color, sizeof(Color)) != 0)
        {
            wd->copy_data.copy_cell_orig_bg[wd->copy_data.copy_cell_counter] = cell->bg_color;
            wd->copy_data.copy_cell_orig_fg[wd->copy_data.copy_cell_counter] = cell->fg_color;
            wd->copy_data.copy_cell[wd->copy_data.copy_cell_counter++] = cell;
            cell->bg_color = bg_color;
            cell->fg_color = fg_color;
        }
    }

    *wd->dirty = true;
}

// init_mouse(): GLFWにマウスボタンとカーソル移動のコールバックを登録する。
void init_mouse(struct windata *wd)
{
    glfwSetMouseButtonCallback(wd->window, mouse_button_callback);
    glfwSetCursorPosCallback(wd->window,cursor_position_callback);

}

// mouse_button_callback(): 左クリックでコピー選択の開始/解除を切り替える。
// 押下時は現在位置のセルを選択範囲の起点として初期化する。
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    struct windata *wd = (struct windata *)glfwGetWindowUserPointer(window);

    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        if(action == GLFW_PRESS)
        {
            if(wd->copy_data.start_copy)
            {
                restore_copy_cells(wd);
                *wd->dirty = true;
                wd->copy_data.start_copy = false;
                wd->copy_data.copy_cell_idx_data.start_idx_block = false;
            }
            else
            {
                wd->copy_data.start_copy = true;
                wd->copy_data.copy_cell_idx_data.start_idx = 0;
                wd->copy_data.copy_cell_idx_data.end_idx = 0;
                wd->copy_data.copy_cell_idx_data.start_idx_block = false;
            }
            wd->mouce_data.mouce_button_left_down = true;

            if(wd->copy_data.start_copy)
            {
                double xpos;
                double ypos;
                glfwGetCursorPos(window, &xpos, &ypos);
                update_copy_selection(wd, xpos, ypos);
            }
        }
        else
        {
            wd->mouce_data.mouce_button_left_down = false;
        }
    }
}

// cursor_position_callback(): 左ドラッグ中だけ選択範囲を現在のマウス位置まで更新する。
static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos)
{    

    struct windata *wd = (struct windata *)glfwGetWindowUserPointer(window);

    if(wd->copy_data.start_copy && wd->mouce_data.mouce_button_left_down)
    {
        update_copy_selection(wd, xpos, ypos);
    }
}
