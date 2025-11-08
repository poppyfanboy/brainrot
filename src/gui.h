#ifndef GUI_H
#define GUI_H

#include <stdbool.h>
#include <stdint.h>

typedef struct GuiWindow GuiWindow;

// Arena is a pair of pointers: struct { unsigned char *begin; unsigned char *end; }
GuiWindow *gui_window_create(int width, int height, char const *title, void *arena);
void gui_window_set_target_fps(GuiWindow *window, double target_fps);
void gui_window_destroy(GuiWindow *window);

// Polls events, updates timer and FPS counter, sleeps to match the target FPS.
bool gui_window_should_close(GuiWindow *window);

bool gui_window_resized(GuiWindow const *window);
void gui_window_size(GuiWindow const *window, int *width, int *height);

void gui_mouse_position(GuiWindow const *window, int *mouse_x, int *mouse_y);

#define GUI_MOUSE_BUTTON_LEFT 0
#define GUI_MOUSE_BUTTON_RIGHT 1
bool gui_mouse_button_down(GuiWindow const *window, int mouse_button);
bool gui_mouse_button_was_pressed(GuiWindow const *window, int mouse_button);
bool gui_mouse_button_was_released(GuiWindow const *window, int mouse_button);

double gui_window_time(GuiWindow const *window);
double gui_window_frame_time(GuiWindow const *window);
double gui_window_fps(GuiWindow const *window);

typedef struct GuiBitmap GuiBitmap;

GuiBitmap *gui_window_bitmap(GuiWindow *window);
uint32_t *gui_bitmap_data(GuiBitmap const *bitmap);
bool gui_bitmap_resize(GuiBitmap *bitmap, int width, int height);
void gui_bitmap_size(GuiBitmap const *bitmap, int *width, int *height);
void gui_bitmap_render(GuiBitmap *bitmap);

#endif
