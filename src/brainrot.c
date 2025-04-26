#include <stdlib.h> // malloc, free
#include <stddef.h> // size_t, NULL

#include "gui.h"

typedef GuiArena Arena;
#define arena_alloc gui_arena_alloc

static inline isize isize_clamp(isize value, isize min, isize max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

void fill_rect(
    u32 *bitmap,
    isize bitmap_width,
    isize bitmap_height,

    isize pos_x,
    isize pos_y,

    isize width,
    isize height,

    u32 color
) {
    isize from_x = isize_clamp(pos_x, 0, bitmap_width);
    isize from_y = isize_clamp(pos_y, 0, bitmap_height);
    isize to_x = isize_clamp(pos_x + width, 0, bitmap_width);
    isize to_y = isize_clamp(pos_y + height, 0, bitmap_height);

    u32 *line_start = &bitmap[from_y * bitmap_width + from_x];
    for (isize y = from_y; y < to_y; y += 1) {
        u32 *line_iter = line_start;
        for (isize x = from_x; x < to_x; x += 1) {
            *line_iter = color;
            line_iter += 1;
        }

        line_start += bitmap_width;
    }
}

typedef struct {
    f32 x;
    f32 y;
} f32x2;

int main(void) {
    int exit_code = 0;

    u8 *arena_memory = NULL;
    GuiWindow *window = NULL;

    isize arena_capacity = 64 * 1024 * 1024;
    arena_memory = malloc((size_t)arena_capacity);
    if (arena_memory == NULL) {
        goto fail;
    }
    Arena arena = {arena_memory, arena_memory + arena_capacity};

    window = gui_window_create(1280, 720, "brainrot", &arena);
    if (window == NULL) {
        goto fail;
    }

    f32x2 rect_pos = {0.2F, 0.1F};
    f32x2 rect_size = {0.1F, 0.1F};
    f32x2 rect_dir = {1.0F, 1.0F};
    f32 rect_speed = 0.5F;

    while (!gui_window_should_close(window)) {
        GuiBitmap *gui_bitmap = gui_window_bitmap(window);
        if (gui_window_resized(window)) {
            isize new_width;
            isize new_height;
            gui_window_size(window, &new_width, &new_height);
            gui_bitmap_resize(gui_bitmap, new_width, new_height);
        }

        isize width;
        isize height;
        gui_bitmap_size(gui_bitmap, &width, &height);
        u32 *bitmap = gui_bitmap_data(gui_bitmap);

        u32 background_color = 0x333333;
        for (isize i = 0; i < width * height; i += 1) {
            bitmap[i] = background_color;
        }

        f32 aspect_ratio = (f32)width / (f32)height;
        f32x2 screen = {aspect_ratio, 1.0F};

        f32 dt = (f32)gui_window_frame_time(window);

        rect_pos.x += rect_speed * rect_dir.x * dt;
        rect_pos.y += rect_speed * rect_dir.y * dt;
        if (rect_pos.x < 0.0F * screen.x) {
            rect_dir.x = 1.0F;
        }
        if (rect_pos.x + rect_size.x > 1.0F * screen.x) {
            rect_dir.x = -1.0F;
        }
        if (rect_pos.y < 0.0F * screen.y) {
            rect_dir.y = 1.0F;
        }
        if (rect_pos.y + rect_size.y > 1.0F * screen.y) {
            rect_dir.y = -1.0F;
        }

        fill_rect(
            bitmap,
            width,
            height,
            (isize)(rect_pos.x / screen.x * (f32)width),
            (isize)(rect_pos.y / screen.y * (f32)height),
            (isize)(rect_size.x / screen.x * (f32)width),
            (isize)(rect_size.y / screen.y * (f32)height),
            0xffff00
        );

        gui_bitmap_render(gui_bitmap);
    }

    goto clean_up;

fail:
    exit_code = 1;
clean_up:
    if (window != NULL) {
        gui_window_destroy(window);
    }
    if (arena_memory != NULL) {
        free(arena_memory);
    }
    return exit_code;
}
