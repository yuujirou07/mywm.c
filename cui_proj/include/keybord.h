#ifndef KEY_BORD_H
#define KEY_BORD_H
#include"vulkan_mywrap.h"

int set_kbd_callback(struct windata* wd);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void character_callback(GLFWwindow* window, unsigned int codepoint);
#endif