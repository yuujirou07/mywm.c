#ifndef MOUCE_IO_H
#define MOUCE_IO_H
#include <GLFW/glfw3.h>
#include"vulkan_mywrap.h"

void init_mouse(struct windata *wd);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);



#endif