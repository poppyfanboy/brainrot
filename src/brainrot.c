// TODO: Make it so that randomly generated rectangles never overlap initially on the first frame.
// TODO: Add swept AABB collision.

#include <assert.h> // assert
#include <stdlib.h> // malloc
#include <stddef.h> // NULL
#include <time.h>   // time
#include <math.h>   // fabsf, sinf, cosf, M_PI
#include <stdio.h>  // snprintf

#include "gui.h"

#include "../res/font8x8.c"
u32 utf8_chop_char(char const **string_iter);
u32 *font8x8_glyph_get(u32 unicode_char);

#define FIELD_ASPECT_RATIO (4.0F / 3.0F)
#define FIELD_MARGIN 64

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

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

f64 f64_random(PCG32 *rng) {
    return (f64)pcg32_random(rng) * 0x1p-32;
}

static inline f32 f32_max(f32 left, f32 right) {
    return left > right ? left : right;
}

static inline f32 f32_min(f32 left, f32 right) {
    return left < right ? left : right;
}

typedef struct {
    f32 x;
    f32 y;
} f32x2;

static inline f32x2 f32x2_add(f32x2 left, f32x2 right) {
    return (f32x2){left.x + right.x, left.y + right.y};
}

static inline f32x2 f32x2_scale(f32x2 vector, f32 scale) {
    return (f32x2){vector.x * scale, vector.y * scale};
}

static inline f32x2 f32x2_clamp(f32x2 value, f32x2 min, f32x2 max) {
    if (value.x < min.x) {
        value.x = min.x;
    }
    if (value.x > max.x) {
        value.x = max.x;
    }

    if (value.y < min.y) {
        value.y = min.y;
    }
    if (value.y > max.y) {
        value.y = max.y;
    }

    return value;
}

typedef struct {
    f32x2 min;
    f32x2 max;
} AABB;

AABB aabb_clamp(AABB clamped, AABB outer) {
    clamped.min = f32x2_clamp(clamped.min, outer.min, outer.max);
    clamped.max = f32x2_clamp(clamped.max, outer.min, outer.max);
    return clamped;
}

bool aabb_vs_aabb(AABB first, AABB second) {
    return
        first.min.x < second.max.x && first.max.x > second.min.x &&
        first.min.y < second.max.y && first.max.y > second.min.y;
}

typedef struct {
    u32 *pixels;
    isize width, height;
    isize stride;
} Bitmap;

AABB bitmap_aabb(Bitmap const *bitmap) {
    AABB aabb = {
        .min = {0, 0},
        .max = {bitmap->width, bitmap->height},
    };
    return aabb;
}

Bitmap sub_bitmap(Bitmap const *bitmap, AABB aabb) {
    aabb = aabb_clamp(aabb, bitmap_aabb(bitmap));
    isize from_x = aabb.min.x, from_y = aabb.min.y;
    isize to_x = aabb.max.x, to_y = aabb.max.y;

    Bitmap sub_bitmap = {
        .pixels = &bitmap->pixels[from_y * bitmap->stride + from_x],
        .width = to_x - from_x,
        .height = to_y - from_y,
        .stride = bitmap->stride,
    };

    return sub_bitmap;
}

void bitmap_clear(Bitmap *bitmap, u32 color) {
    for (isize y = 0; y < bitmap->height; y += 1) {
        for (isize x = 0; x < bitmap->width; x += 1) {
            bitmap->pixels[y * bitmap->stride + x] = color;
        }
    }
}

void fill_rectangle(Bitmap *bitmap, AABB rectangle, u32 color) {
    rectangle = aabb_clamp(rectangle, bitmap_aabb(bitmap));

    for (isize y = rectangle.min.y; y < rectangle.max.y; y += 1) {
        u32 *line = &bitmap->pixels[y * bitmap->stride];
        for (isize x = rectangle.min.x; x < rectangle.max.x; x += 1) {
            line[x] = color;
        }
    }
}

void draw_rectangle(Bitmap *bitmap, AABB rectangle, u32 color) {
    rectangle = aabb_clamp(rectangle, bitmap_aabb(bitmap));

    for (isize x = rectangle.min.x; x < rectangle.max.x; x += 1) {
        bitmap->pixels[(isize)rectangle.min.y * bitmap->stride + x] = color;
        bitmap->pixels[(isize)rectangle.max.y * bitmap->stride + x] = color;
    }
    for (isize y = rectangle.min.y; y < rectangle.max.y; y += 1) {
        bitmap->pixels[y * bitmap->stride + (isize)rectangle.min.x] = color;
        bitmap->pixels[y * bitmap->stride + (isize)rectangle.max.x] = color;
    }
}

void draw_text(Bitmap *bitmap, f32x2 text_pos, char const *text, u32 color) {
    f32x2 current_pos = text_pos;

    char const *text_iter = text;
    while (*text_iter != '\0') {
        u32 unicode_char = utf8_chop_char(&text_iter);

        if (unicode_char == '\n') {
            current_pos.x = text_pos.x;
            current_pos.y += font8x8_glyph_height;
            continue;
        }

        f32x2 glyph_size = {font8x8_glyph_width, font8x8_glyph_height};
        u32 *glyph_bitmap = font8x8_glyph_get(unicode_char);
        if (glyph_bitmap == NULL) {
            glyph_bitmap = font8x8_glyph_get(0xfffd);
            assert(glyph_bitmap != NULL);
        }

        AABB glyph_rect = {current_pos, f32x2_add(current_pos, glyph_size)};
        glyph_rect = aabb_clamp(glyph_rect, bitmap_aabb(bitmap));

        for (isize y = glyph_rect.min.y; y < glyph_rect.max.y; y += 1) {
            for (isize x = glyph_rect.min.x; x < glyph_rect.max.x; x += 1) {
                isize local_x = x - current_pos.x;
                isize local_y = y - current_pos.y;

                if ((glyph_bitmap[local_y * font8x8_glyph_width + local_x] & 0xffffffff) != 0) {
                    bitmap->pixels[y * bitmap->stride + x] = color;
                }
            }
        }

        current_pos.x += font8x8_glyph_width;
    }
}

typedef struct {
    f32x2 pos;
    f32x2 size;
    f32x2 velocity;
    bool visible;
} Rectangle;

void draw_field(Bitmap *bitmap, Rectangle *rectangles, isize rectangle_count) {
    draw_rectangle(bitmap, bitmap_aabb(bitmap), 0xffffff);

    for (isize i = 0; i < rectangle_count; i += 1) {
        Rectangle rectangle = rectangles[i];

        if (!rectangle.visible) {
            continue;
        }

        AABB aabb = {
            .min = rectangle.pos,
            .max = f32x2_add(rectangle.pos, rectangle.size),
        };
        aabb.min = f32x2_scale(aabb.min, bitmap->height);
        aabb.max = f32x2_scale(aabb.max, bitmap->height);

        fill_rectangle(bitmap, aabb, 0xffff00);
    }
}

typedef struct {
    u8 *begin;
    u8 *end;
} Arena;

int main(void) {
    isize arena_capacity = 64 * 1024 * 1024;
    u8 *arena_memory = malloc(arena_capacity);
    Arena arena = {arena_memory, arena_memory + arena_capacity};
    if (arena.begin == NULL) {
        return 1;
    }

    GuiWindow *window = gui_window_create(1280, 720, "brainrot", &arena);
    if (window == NULL) {
        return 1;
    }

    PCG32 rng;
    pcg32_init(&rng, (u64)time(NULL));

    isize rectangle_count = 4;
    Rectangle rectangles[12] = {
        // left boundary
        {
            .pos = {-FIELD_ASPECT_RATIO, 0.0F},
            .size = {FIELD_ASPECT_RATIO, 1.0F},
            .velocity = {0},
            .visible = false,
        },

        // right boundary
        {
            .pos = {FIELD_ASPECT_RATIO, 0.0F},
            .size = {FIELD_ASPECT_RATIO, 1.0F},
            .velocity = {0},
            .visible = false,
        },

        // top boundary
        {
            .pos = {0.0F, -1.0F},
            .size = {FIELD_ASPECT_RATIO, 1.0F},
            .velocity = {0},
            .visible = false,
        },

        // bottom boundary
        {
            .pos = {0.0F, 1.0F},
            .size = {FIELD_ASPECT_RATIO, 1.0F},
            .velocity = {0},
            .visible = false,
        },
    };

    for (isize i = rectangle_count; i < countof(rectangles); i += 1) {
        f32 size_x = 0.05F + f64_random(&rng) * 0.2F;
        f32 aspect_ratio = 0.75F + f64_random(&rng) * 0.5F;
        f32x2 size = {
            size_x,
            size_x * aspect_ratio,
        };

        f32x2 pos = {
            f64_random(&rng) * (FIELD_ASPECT_RATIO - size.x),
            f64_random(&rng) * (1.0F - size.y),
        };

        f32 angle = f64_random(&rng) * 2.0F * (f32)M_PI;

        rectangles[i] = (Rectangle){
            .pos = pos,
            .size = size,
            .velocity = f32x2_scale((f32x2){cosf(angle), sinf(angle)}, 0.5F),
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

        Bitmap bitmap = {gui_bitmap_data(gui_bitmap)};
        gui_bitmap_size(gui_bitmap, &bitmap.width, &bitmap.height);
        bitmap.stride = bitmap.width;

        f32 dt = (f32)gui_window_frame_time(window);

        bitmap_clear(&bitmap, 0x333333);

        char text_buffer[128];
        snprintf(text_buffer, sizeof(text_buffer), "FPS: %.1f", gui_window_fps(window));
        draw_text(&bitmap, (f32x2){16, 16}, text_buffer, 0xffffff);

        f32x2 interior_size = {
            bitmap.width - 1 - 2 * FIELD_MARGIN,
            bitmap.height - 1 - 2 * FIELD_MARGIN,
        };
        if (interior_size.x > 0 && interior_size.y > 0) {
            f32 field_height = interior_size.x / FIELD_ASPECT_RATIO;
            if (field_height > interior_size.y) {
                field_height = interior_size.y;
            }
            f32 field_width = field_height * FIELD_ASPECT_RATIO;

            AABB field_aabb = {
                .min = {(bitmap.width - field_width) / 2.0F, (bitmap.height - field_height) / 2.0F},
                .max = {(bitmap.width + field_width) / 2.0F, (bitmap.height + field_height) / 2.0F},
            };

            Bitmap field_bitmap = sub_bitmap(&bitmap, field_aabb);
            draw_field(&field_bitmap, rectangles, countof(rectangles));
        }

        gui_bitmap_render(gui_bitmap);

        for (isize i = 0; i < countof(rectangles); i += 1) {
            rectangles[i].pos = f32x2_add(
                rectangles[i].pos,
                f32x2_scale(rectangles[i].velocity, dt)
            );
        }

        for (isize to_update = 0; to_update < countof(rectangles); to_update += 1) {
            if (
                rectangles[to_update].velocity.x == 0.0F &&
                rectangles[to_update].velocity.y == 0.0F
            ) {
                continue;
            }

            for (isize to_collide = 0; to_collide < countof(rectangles); to_collide += 1) {
                if (to_update == to_collide) {
                    continue;
                }

                AABB aabb_to_update = {
                    rectangles[to_update].pos,
                    f32x2_add(rectangles[to_update].pos, rectangles[to_update].size),
                };
                AABB aabb_to_collide = {
                    rectangles[to_collide].pos,
                    f32x2_add(rectangles[to_collide].pos, rectangles[to_collide].size),
                };
                if (!aabb_vs_aabb(aabb_to_update, aabb_to_collide)) {
                    continue;
                }

                f32 horizontal_overlap =
                    f32_min(aabb_to_update.max.x, aabb_to_collide.max.x) -
                    f32_max(aabb_to_update.min.x, aabb_to_collide.min.x);
                f32 vertical_overlap =
                    f32_min(aabb_to_update.max.y, aabb_to_collide.max.y) -
                    f32_max(aabb_to_update.min.y, aabb_to_collide.min.y);

                if (vertical_overlap > horizontal_overlap) {
                    if (aabb_to_update.min.x < aabb_to_collide.min.x) {
                        rectangles[to_update].velocity.x = -fabsf(rectangles[to_update].velocity.x);
                    } else {
                        rectangles[to_update].velocity.x = fabsf(rectangles[to_update].velocity.x);
                    }
                } else {
                    if (aabb_to_update.min.y < aabb_to_collide.min.y) {
                        rectangles[to_update].velocity.y = -fabsf(rectangles[to_update].velocity.y);
                    } else {
                        rectangles[to_update].velocity.y = fabsf(rectangles[to_update].velocity.y);
                    }
                }
            }
        }
    }

    return 0;
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

u32 utf8_chop_char(char const **string_iter) {
    char const *string = *string_iter;
    isize char_size = utf8_char_size[(u8)string[0]];
    *string_iter += char_size;

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

    case 4: {
        return
            (u32)(string[0] & 0x07) << 18 |
            (u32)(string[1] & 0x3f) << 12 |
            (u32)(string[2] & 0x3f) << 6 |
            (u32)(string[3] & 0x3f);
    } break;
    }

    assert(false);
    return 0;
}

u32 *font8x8_glyph_get(u32 unicode_char) {
    isize left = 0;
    isize right = font8x8_glyph_count;

    while (left < right) {
        isize middle = (left + right) / 2;
        if (font8x8_glyphs[middle].char_code < unicode_char) {
            left = middle + 1;
        } else {
            right = middle;
        }
    }

    if (font8x8_glyphs[left].char_code == unicode_char) {
        return font8x8_glyphs[left].bitmap;
    } else {
        return NULL;
    }
}
