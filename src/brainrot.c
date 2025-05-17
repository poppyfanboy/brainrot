// TODO: Extract rendering into functions which are given a region where they are supposed to draw.
// TODO: Add swept AABB collision.

#include <stdlib.h> // malloc, free
#include <stddef.h> // size_t, NULL
#include <time.h>   // time
#include <math.h>   // fabsf, sinf, cosf, M_PI
#include <stdio.h>  // snprintf

#include "gui.h"

#include "../res/font8x8.c"

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

#define sizeof(expr) (isize)sizeof(expr)
#define countof(array) (sizeof(array) / sizeof((array)[0]))

// *Really* minimal PCG32 code / (c) 2014 M.E. O'Neill / pcg-random.org
// Licensed under Apache License 2.0 (NO WARRANTY, etc. see website)

typedef struct {
    u64 state;
    u64 inc;
} PCG32;

u32 pcg32_random(PCG32 *rng) {
    u64 oldstate = rng->state;
    // Advance internal state
    rng->state = oldstate * 6364136223846793005ULL + (rng->inc | 1);
    // Calculate output function (XSH RR), uses old state for max ILP
    u32 xorshifted = (u32)(((oldstate >> 18u) ^ oldstate) >> 27u);
    u32 rot = oldstate >> 59u;
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

void pcg32_init(PCG32 *rng, u64 init_state) {
    rng->state = 0U;
    rng->inc = 1u;
    pcg32_random(rng);
    rng->state += init_state;
    pcg32_random(rng);
}

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

static inline isize isize_min(isize left, isize right) {
    return left < right ? left : right;
}

static inline isize isize_max(isize left, isize right) {
    return left > right ? left : right;
}

static inline f32 f32_max(f32 left, f32 right) {
    return left > right ? left : right;
}

static inline f32 f32_min(f32 left, f32 right) {
    return left < right ? left : right;
}

void fill_rect(
    u32 *bitmap, isize bitmap_width, isize bitmap_height,
    isize pos_x, isize pos_y,
    isize width, isize height,
    u32 color
) {
    if (bitmap_width == 0 || bitmap_height == 0) {
        return;
    }
    if (width == 0 || height == 0) {
        return;
    }

    isize from_x = isize_clamp(pos_x, 0, bitmap_width - 1);
    isize from_y = isize_clamp(pos_y, 0, bitmap_height - 1);
    isize to_x = isize_clamp(pos_x + width - 1, 0, bitmap_width - 1);
    isize to_y = isize_clamp(pos_y + height - 1, 0, bitmap_height - 1);

    u32 *line_start = &bitmap[from_y * bitmap_width + from_x];
    for (isize y = from_y; y <= to_y; y += 1) {
        u32 *line_iter = line_start;
        for (isize x = from_x; x <= to_x; x += 1) {
            *line_iter = color;
            line_iter += 1;
        }

        line_start += bitmap_width;
    }
}

void draw_rect(
    u32 *bitmap, isize bitmap_width, isize bitmap_height,
    isize pos_x, isize pos_y,
    isize width, isize height,
    u32 color
) {
    if (bitmap_width == 0 || bitmap_height == 0) {
        return;
    }
    if (width == 0 || height == 0) {
        return;
    }

    isize from_x = isize_clamp(pos_x, 0, bitmap_width - 1);
    isize from_y = isize_clamp(pos_y, 0, bitmap_height - 1);
    isize to_x = isize_clamp(pos_x + width - 1, 0, bitmap_width - 1);
    isize to_y = isize_clamp(pos_y + height - 1, 0, bitmap_height - 1);

    for (isize x = from_x; x <= to_x; x += 1) {
        bitmap[from_y * bitmap_width + x] = color;
        bitmap[to_y * bitmap_width + x] = color;
    }

    for (isize y = from_y; y <= to_y; y += 1) {
        bitmap[y * bitmap_width + from_x] = color;
        bitmap[y * bitmap_width + to_x] = color;
    }
}

u8 utf8_char_size[] = {
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 1
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 2
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 3
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 5
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 7
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // B
    0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, // C
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, // D
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, // E
    4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // F
};

u32 utf8_chop_char(char const **string_out) {
    char const *string = *string_out;
    isize char_size = utf8_char_size[(u8)string[0]];
    *string_out += char_size;

    switch (char_size) {
    case 1: {
        return
            (u32)(string[0]);
    } break;

    case 2: {
        return
            (u32)(string[0] & 0x1f) << 6 |
            (u32)(string[1] & 0x3f);
    } break;

    case 3: {
        return
            (u32)(string[0] & 0x0f) << 12 |
            (u32)(string[1] & 0x3f) << 6 |
            (u32)(string[2] & 0x3f);
    } break;

    case 4:
    default: {
        return
            (u32)(string[0] & 0x07) << 18 |
            (u32)(string[1] & 0x3f) << 12 |
            (u32)(string[2] & 0x3f) << 6 |
            (u32)(string[3] & 0x3f);
    } break;
    }
}

void draw_debug_text(
    u32 *bitmap, isize bitmap_width, isize bitmap_height,
    isize pos_x, isize pos_y,
    char const *text,
    u32 color
) {
    isize current_pos_x = pos_x;
    isize current_pos_y = pos_y;

    char const *text_iter = text;
    while (*text_iter != 0) {
        u32 next_char = utf8_chop_char(&text_iter);

        if (next_char == '\n') {
            current_pos_x = pos_x;
            current_pos_y += font8x8_glyph_height;
        } else {
            u32 *glyph_bitmap = NULL;
            {
                isize left = 0;
                isize right = font8x8_glyph_count;

                while (left < right) {
                    isize middle = (left + right) / 2;
                    if (font8x8_glyphs[middle].char_code < next_char) {
                        left = middle + 1;
                    } else {
                        right = middle;
                    }
                }
                if (font8x8_glyphs[left].char_code == next_char) {
                    glyph_bitmap = font8x8_glyphs[left].bitmap;
                }
            }

            if (
                glyph_bitmap != NULL &&
                current_pos_x + font8x8_glyph_width >= 0 && current_pos_x < bitmap_width &&
                current_pos_y + font8x8_glyph_height >= 0 && current_pos_y < bitmap_height
            ) {
                for (
                    isize glyph_y = isize_max(0, -current_pos_y);
                    glyph_y < isize_min(font8x8_glyph_height, bitmap_height - current_pos_y);
                    glyph_y += 1
                ) {
                    for (
                        isize glyph_x = isize_max(0, -current_pos_x);
                        glyph_x < isize_min(font8x8_glyph_width, bitmap_width - current_pos_x);
                        glyph_x += 1
                    ) {
                        isize bitmap_index =
                            (current_pos_y + glyph_y) * bitmap_width +
                            (current_pos_x + glyph_x);

                        isize glyph_index = glyph_y * font8x8_glyph_width + glyph_x;

                        if ((glyph_bitmap[glyph_index] & 0xff000000) != 0) {
                            bitmap[bitmap_index] = color;
                        }
                    }
                }
            }

            current_pos_x += font8x8_glyph_width;
        }
    }
}

void bitmap_clear(
    u32 *bitmap, isize bitmap_width, isize bitmap_height,
    u32 color
) {
    for (isize i = 0; i < bitmap_width * bitmap_height; i += 1) {
        bitmap[i] = color;
    }
}

typedef struct {
    f32 x;
    f32 y;
} f32x2;

typedef struct {
    f32x2 pos;
    f32x2 size;
    f32x2 direction;
    f32 speed;
    bool visible;
} Rectangle;

typedef struct {
    f32 top;
    f32 right;
    f32 bottom;
    f32 left;
} Margin;

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

    PCG32 rng;
    pcg32_init(&rng, (u64)time(NULL));

    Margin field_margin = {0.05F, 0.05F, 0.05F, 0.05F};
    f32 const field_aspect_ratio = 4.0F / 3.0F;

    isize rectangle_count = 4;
    Rectangle rectangles[12] = {
        // left boundary
        {
            .pos = {-field_aspect_ratio, 0.0F},
            .size = {field_aspect_ratio, 1.0F},
            .direction = {0.0F, 0.0F},
            .speed = 0.0F,
            .visible = false,
        },
        // right boundary
        {
            .pos = {field_aspect_ratio, 0.0F},
            .size = {field_aspect_ratio, 1.0F},
            .direction = {0.0F, 0.0F},
            .speed = 0.0F,
            .visible = false,
        },
        // top boundary
        {
            .pos = {0.0F, -1.0F},
            .size = {field_aspect_ratio, 1.0F},
            .direction = {0.0F, 0.0F},
            .speed = 0.0F,
            .visible = false,
        },
        // bottom boundary
        {
            .pos = {0.0F, 1.0F},
            .size = {field_aspect_ratio, 1.0F},
            .direction = {0.0F, 0.0F},
            .speed = 0.0F,
            .visible = false,
        },
    };

    for (isize i = rectangle_count; i < countof(rectangles); i += 1) {
        f32 size_x = 0.05F + (f32)((f64)pcg32_random(&rng) * 0x1p-32) * 0.2F;
        f32 aspect_ratio = 0.75F + (f32)((f64)pcg32_random(&rng) * 0x1p-32) * 0.5F;
        f32x2 size = {
            size_x,
            size_x * aspect_ratio,
        };

        f32x2 pos = {
            (f32)((f64)pcg32_random(&rng) * 0x1p-32) * (field_aspect_ratio - size.x),
            (f32)((f64)pcg32_random(&rng) * 0x1p-32) * (1.0F - size.y),
        };

        f32 angle = (f32)((f64)pcg32_random(&rng) * 0x1p-32) * 2.0F * (f32)M_PI;

        rectangles[i] = (Rectangle){
            .pos = pos,
            .size = size,
            .direction = {cosf(angle), sinf(angle)},
            .speed = 0.5F,
            .visible = true,
        };
    }

    while (!gui_window_should_close(window)) {
        GuiBitmap *gui_bitmap = gui_window_bitmap(window);
        if (gui_window_resized(window)) {
            isize new_width;
            isize new_height;
            gui_window_size(window, &new_width, &new_height);
            gui_bitmap_resize(gui_bitmap, new_width, new_height);
        }

        u32 *bitmap = gui_bitmap_data(gui_bitmap);
        isize width;
        isize height;
        gui_bitmap_size(gui_bitmap, &width, &height);

        f32 dt = (f32)gui_window_frame_time(window);

        bitmap_clear(bitmap, width, height, 0x333333);

        char text_buffer[128];
        snprintf(text_buffer, sizeof(text_buffer), "FPS: %.1f", gui_window_fps(window));
        draw_debug_text(bitmap, width, height, 16, 16, text_buffer, 0xffffff);

        f32 screen_aspect_ratio = (f32)width / (f32)height;

        f32x2 field_pos;
        f32x2 field_size;
        {
            f32x2 bounding_box_size = {
                f32_max(screen_aspect_ratio - (field_margin.left + field_margin.right), 0.0F),
                f32_max(1.0F - (field_margin.top + field_margin.bottom), 0.0F),
            };

            if (bounding_box_size.x / bounding_box_size.y > field_aspect_ratio) {
                field_size.y = bounding_box_size.y;
                field_pos.y = field_margin.top;
                field_size.x = field_size.y * field_aspect_ratio;
                field_pos.x = (bounding_box_size.x - field_size.x) / 2.0F + field_margin.left;
            } else {
                field_size.x = bounding_box_size.x;
                field_pos.x = field_margin.left;
                field_size.y = field_size.x / field_aspect_ratio;
                field_pos.y = (bounding_box_size.y - field_size.y) / 2.0F + field_margin.top;
            }
        }
        draw_rect(
            bitmap,
            width,
            height,
            (isize)(field_pos.x / screen_aspect_ratio * (f32)width),
            (isize)(field_pos.y * (f32)height),
            (isize)(field_size.x / screen_aspect_ratio * (f32)width),
            (isize)(field_size.y * (f32)height),
            0xffffff
        );

        for (isize i = 0; i < countof(rectangles); i += 1) {
            rectangles[i].pos.x += rectangles[i].speed * rectangles[i].direction.x * dt;
            rectangles[i].pos.y += rectangles[i].speed * rectangles[i].direction.y * dt;
        }

        // i-th rectangle is the one we are updating.
        for (isize i = 0; i < countof(rectangles); i += 1) {
            if (rectangles[i].speed == 0.0F) {
                continue;
            }

            for (isize j = 0; j < countof(rectangles); j += 1) {
                if (i == j) {
                    continue;
                }

                f32x2 i_min = {
                    rectangles[i].pos.x,
                    rectangles[i].pos.y,
                };
                f32x2 i_max = {
                    rectangles[i].pos.x + rectangles[i].size.x,
                    rectangles[i].pos.y + rectangles[i].size.y,
                };

                f32x2 j_min = {
                    rectangles[j].pos.x,
                    rectangles[j].pos.y,
                };
                f32x2 j_max = {
                    rectangles[j].pos.x + rectangles[j].size.x,
                    rectangles[j].pos.y + rectangles[j].size.y,
                };

                if (
                    i_max.x >= j_min.x && i_min.x <= j_max.x &&
                    i_max.y >= j_min.y && i_min.y <= j_max.y
                ) {
                    f32 horizontal_intersection =
                        (f32_min(i_max.x, j_max.x) - f32_max(i_min.x, j_min.x)) /
                        (i_max.x - i_min.x);

                    f32 vertical_intersection =
                        (f32_min(i_max.y, j_max.y) - f32_max(i_min.y, j_min.y)) /
                        (i_max.y - i_min.y);

                    if (horizontal_intersection > vertical_intersection) {
                        if (i_min.y < j_min.y) {
                            rectangles[i].direction.y = -fabsf(rectangles[i].direction.y);
                        } else {
                            rectangles[i].direction.y = fabsf(rectangles[i].direction.y);
                        }
                    } else {
                        if (i_min.x < j_min.x) {
                            rectangles[i].direction.x = -fabsf(rectangles[i].direction.x);
                        } else {
                            rectangles[i].direction.x = fabsf(rectangles[i].direction.x);
                        }
                    }
                }

            }
        }

        for (isize i = 0; i < countof(rectangles); i += 1) {
            if (rectangles[i].visible) {
                f32x2 rect_size = {
                    rectangles[i].size.x / field_aspect_ratio * field_size.x,
                    rectangles[i].size.y * field_size.y,
                };

                f32x2 rect_pos = {
                    field_pos.x + (rectangles[i].pos.x / field_aspect_ratio) * field_size.x,
                    field_pos.y + rectangles[i].pos.y * field_size.y,
                };

                fill_rect(
                    bitmap,
                    width,
                    height,
                    (isize)(rect_pos.x / screen_aspect_ratio * (f32)width),
                    (isize)(rect_pos.y * (f32)height),
                    (isize)(rect_size.x / screen_aspect_ratio * (f32)width),
                    (isize)(rect_size.y * (f32)height),
                    0xffff00
                );
            }
        }

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
