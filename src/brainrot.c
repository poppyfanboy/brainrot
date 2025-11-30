#include <assert.h> // assert
#include <stdlib.h> // malloc, abort
#include <stddef.h> // NULL
#include <time.h>   // time
#include <math.h>   // sinf, cosf, M_PI, roundf, sqrtf, fabsf, floorf
#include <string.h> // memset

#include "gui.h"

// Redefinition of typedefs is a C11 feature.
// This is the official™ guard, which is used across different headers to protect u8 and friends.
// (Or just add a #define before including this header, if you already have short names defined.)
#ifndef SHORT_NAMES_FOR_PRIMITIVE_TYPES_WERE_DEFINED
    #define SHORT_NAMES_FOR_PRIMITIVE_TYPES_WERE_DEFINED
    #include <stdint.h>
    #include <stddef.h>

    typedef uint8_t   u8; typedef int8_t   i8;
    typedef uint16_t u16; typedef int16_t i16;
    typedef uint32_t u32; typedef int32_t i32;
    typedef uint64_t u64; typedef int64_t i64;

    typedef size_t    usize;
    typedef ptrdiff_t isize;
    typedef uintptr_t uptr;

    typedef float  f32;
    typedef double f64;
#endif

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
    u32 xorshifted = (u32)(((oldstate >> 18U) ^ oldstate) >> 27U);
    u32 rot = oldstate >> 59U;
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

void pcg32_init(PCG32 *rng, u64 init_state) {
    rng->state = 0U;
    rng->inc = 1U;
    pcg32_random(rng);
    rng->state += init_state;
    pcg32_random(rng);
}

#include "../res/font8x8.c"
u32 *font8x8_glyph_get(u32 unicode_char);

u32 utf8_chop_char(char const **string_iter);
isize utf8_char_count(char const *string);

#define BACKGROUND_COLOR    0xff192739
#define ACTIVE_COLOR        0xffffec62
#define SECONDARY_COLOR     0xff908f88
#define DISABLED_COLOR      0xff45454c

#define FIELD_ASPECT_RATIO (4.0F / 3.0F)
#define FIELD_MARGIN 48

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

#define countof(array) (sizeof(array) / sizeof((array)[0]))

#define ARENA_ALIGNMENT 16

typedef struct {
    u8 *begin;
    u8 *end;
} Arena;

void *arena_alloc(Arena *arena, isize size) {
    if (size == 0) {
        return NULL;
    }

    isize padding = (~(uptr)arena->begin + 1) & (ARENA_ALIGNMENT - 1);

    isize memory_left = arena->end - arena->begin - padding;
    if (memory_left < 0 || memory_left < size) {
        abort();
    }

    void *ptr = arena->begin + padding;
    arena->begin += padding + size;
    return ptr;
}

static inline f64 f64_random(PCG32 *rng) {
    return (f64)pcg32_random(rng) * 0x1P-32;
}

static inline isize isize_min(isize left, isize right) {
    return left < right ? left : right;
}

static inline isize isize_max(isize left, isize right) {
    return left > right ? left : right;
}

static inline isize isize_clamp(isize value, isize min, isize max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

// https://easings.net/#easeOutQuad
f32 ease_out_quadratic(f32 x) {
    return 1 - (1 - x) * (1 - x);
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

static inline f32x2 f32x2_sub(f32x2 left, f32x2 right) {
    return (f32x2){left.x - right.x, left.y - right.y};
}

static inline f32x2 f32x2_scale(f32x2 vector, f32 scale) {
    return (f32x2){vector.x * scale, vector.y * scale};
}

static inline f32x2 f32x2_max(f32x2 left, f32x2 right) {
    return (f32x2){
        left.x > right.x ? left.x : right.x,
        left.y > right.y ? left.y : right.y,
    };
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

static inline f32x2 f32x2_round(f32x2 vector) {
    return (f32x2){roundf(vector.x), roundf(vector.y)};
}

static inline f32 f32x2_dot(f32x2 left, f32x2 right) {
    return left.x * right.x + left.y * right.y;
}

static inline f32 f32x2_length(f32x2 vector) {
    return sqrtf(vector.x * vector.x + vector.y * vector.y);
}

static inline f32x2 f32x2_normalize(f32x2 vector) {
    f32 length = f32x2_length(vector);
    return (f32x2){vector.x / length, vector.y / length};
}

// When colliding the "max" end is exclusive.
// When rendering the "max" end is inclusive.
typedef struct {
    f32x2 min;
    f32x2 max;
} f32box2;

static inline f32box2 f32box2_clamp(f32box2 target, f32box2 clamper) {
    return (f32box2){
        .min = f32x2_clamp(target.min, clamper.min, clamper.max),
        .max = f32x2_clamp(target.max, clamper.min, clamper.max),
    };
}

static inline bool f32box2_vs_f32box2(f32box2 this, f32box2 other) {
    return
        this.min.x < other.max.x && this.max.x > other.min.x &&
        this.min.y < other.max.y && this.max.y > other.min.y;
}

static inline bool f32box2_contains(f32box2 box, f32x2 point) {
    return
        box.min.x <= point.x && point.x < box.max.x &&
        box.min.y <= point.y && point.y < box.max.y;
}

// "normal" is the collision normal at the "near_time" time.
bool ray_vs_f32box2(
    f32x2 origin, f32x2 direction,
    f32box2 box,
    f32 *near_time, f32 *far_time, f32x2 *normal
) {
    // near.x and far.x - collision time with the vertical lines of the box.
    // near.y and far.y - collision time with the horizontal lines of the box.
    f32x2 near = {
        (box.min.x - origin.x) / direction.x,
        (box.min.y - origin.y) / direction.y,
    };
    f32x2 far = {
        (box.max.x - origin.x) / direction.x,
        (box.max.y - origin.y) / direction.y,
    };

    if (box.min.x == origin.x && direction.x == 0 || box.min.y == origin.y && direction.y == 0) {
        return false;
    }
    if (box.max.y == origin.x && direction.x == 0 || box.max.y == origin.y && direction.y == 0) {
        return false;
    }

    // Account for rays coming from directions other than "from top-left to bottom-right":
    if (near.x > far.x) {
        f32 swap = near.x;
        near.x = far.x;
        far.x = swap;
    }
    if (near.y > far.y) {
        f32 swap = near.y;
        near.y = far.y;
        far.y = swap;
    }

    // We intersected with both horizontal (vertical) lines of the box before intersecting with any
    // of the vertical (horizontal) ones.
    if (far.y < near.x || far.x < near.y) {
        return false;
    }

    *near_time = f32_max(near.x, near.y);
    *far_time = f32_min(far.x, far.y);

    if (near.x > near.y) {
        // Ray collided with the vertical side first.
        // Get collision normal based on if the ray comes from left to right or from right to left.
        if (direction.x > 0) {
            *normal = (f32x2){-1, 0};
        } else {
            *normal = (f32x2){1, 0};
        }
    } else {
        // Ray collided with the horizontal side first.
        // Get collision normal based on if the ray comes from top to bottom or from bottom to top.
        if (direction.y > 0) {
            *normal = (f32x2){0, -1};
        } else {
            *normal = (f32x2){0, 1};
        }
    }

    return true;
}

typedef struct {
    f32 x;
    f32 y;
    f32 z;
} f32x3;

static inline u32 color_blend(u32 background_color, u32 foreground_color) {
    f32x3 foreground = {
        ((foreground_color >> 16) & 0xff) / 255.0F,
        ((foreground_color >>  8) & 0xff) / 255.0F,
        ((foreground_color >>  0) & 0xff) / 255.0F,
    };

    f32x3 background = {
        ((background_color >> 16) & 0xff) / 255.0F,
        ((background_color >>  8) & 0xff) / 255.0F,
        ((background_color >>  0) & 0xff) / 255.0F,
    };

    f32 alpha = ((foreground_color >> 24) & 0xff) / 255.0F;
    f32x3 blended = {
        foreground.x * alpha + background.x * (1 - alpha),
        foreground.y * alpha + background.y * (1 - alpha),
        foreground.z * alpha + background.z * (1 - alpha),
    };

    return
        (background_color & 0xff000000) |
        (u32)(blended.x * 255.0F) << 16 |
        (u32)(blended.y * 255.0F) <<  8 |
        (u32)(blended.z * 255.0F);
}

typedef struct {
    u32 *pixels;
    int width, height;
    int stride;
} Bitmap;

static inline Bitmap sub_bitmap(Bitmap const *bitmap, f32box2 box) {
    assert(box.min.x >= 0 && box.min.y >= 0);
    isize from_x = (isize)box.min.x;
    isize from_y = (isize)box.min.y;

    assert(box.max.x < bitmap->width && box.max.y < bitmap->height);
    isize to_x = (isize)box.max.x;
    isize to_y = (isize)box.max.y;

    return (Bitmap){
        .pixels = &bitmap->pixels[from_y * bitmap->stride + from_x],
        .width = to_x - from_x + 1,
        .height = to_y - from_y + 1,
        .stride = bitmap->stride,
    };
}

void bitmap_clear(Bitmap *bitmap, u32 color) {
    for (isize y = 0; y < bitmap->height; y += 1) {
        for (isize x = 0; x < bitmap->width; x += 1) {
            bitmap->pixels[y * bitmap->stride + x] = color;
        }
    }
}

static inline void bitmap_set_pixel(
    Bitmap *bitmap,
    isize x, isize y,
    u32 color
) {
    if (x < 0 || x >= bitmap->width) {
        return;
    }
    if (y < 0 || y >= bitmap->height) {
        return;
    }

    u32 *background = &bitmap->pixels[y * bitmap->stride + x];
    *background = color_blend(*background, color);
}

static inline void bitmap_set_row_pixels(
    Bitmap *bitmap,
    isize from_x, isize to_x,
    isize y,
    u32 color
) {
    assert(from_x <= to_x);

    if (to_x < 0 || from_x >= bitmap->width) {
        return;
    }
    if (y < 0 || y >= bitmap->height) {
        return;
    }

    from_x = isize_max(from_x, 0);
    to_x = isize_min(to_x, bitmap->width - 1);

    u32 *row_iter = &bitmap->pixels[y * bitmap->stride + from_x];
    for (isize i = 0; i < to_x - from_x + 1; i += 1) {
        *row_iter = color_blend(*row_iter, color);
        row_iter += 1;
    }
}

static inline void bitmap_set_column_pixels(
    Bitmap *bitmap,
    isize x,
    isize from_y, isize to_y,
    u32 color
) {
    assert(from_y <= to_y);

    if (to_y < 0 || from_y >= bitmap->height) {
        return;
    }
    if (x < 0 || x >= bitmap->width) {
        return;
    }

    from_y = isize_max(from_y, 0);
    to_y = isize_min(to_y, bitmap->height - 1);

    u32 *column_iter = &bitmap->pixels[from_y * bitmap->stride + x];
    for (isize i = 0; i < to_y - from_y + 1; i += 1) {
        *column_iter = color_blend(*column_iter, color);
        column_iter += bitmap->stride;
    }
}

void fill_rectangle(Bitmap *bitmap, f32box2 rectangle, u32 color) {
    // Check if rectangle is completely clamped out:
    if (rectangle.max.x < 0 && rectangle.max.y < 0) {
        return;
    }
    if (rectangle.min.x >= bitmap->width && rectangle.min.y >= bitmap->height) {
        return;
    }

    isize from_y = f32_max(0, rectangle.min.y);
    isize to_y = f32_min(bitmap->height - 1, rectangle.max.y);

    for (isize y = from_y; y <= to_y; y += 1) {
        u32 *line = &bitmap->pixels[y * bitmap->stride];

        isize from_x = f32_max(0, rectangle.min.x);
        isize to_x = f32_min(bitmap->width - 1, rectangle.max.x);

        for (isize x = from_x; x <= to_x; x += 1) {
            line[x] = color_blend(line[x], color);
        }
    }
}

void draw_rectangle(Bitmap *bitmap, f32box2 rectangle, u32 color) {
    // Top and bottom horizontal lines
    bitmap_set_row_pixels(bitmap, rectangle.min.x, rectangle.max.x, rectangle.min.y, color);
    bitmap_set_row_pixels(bitmap, rectangle.min.x, rectangle.max.x, rectangle.max.y, color);

    // Left and right vertical lines
    bitmap_set_column_pixels(bitmap, rectangle.min.x, rectangle.min.y, rectangle.max.y, color);
    bitmap_set_column_pixels(bitmap, rectangle.max.x, rectangle.min.y, rectangle.max.y, color);
}

void draw_line(Bitmap *bitmap, f32x2 from, f32x2 to, u32 color) {
    // Line equation: f(x, y) = Ax + By + C
    // (A, B) is a perpendicular vector. C is then derived from f(x, y) = 0.
    f32 A = to.y - from.y;
    f32 B = from.x - to.x;
    f32 C = -A * from.x - B * from.y;

    // Single pixel special case:
    if (A == 0 && B == 0) {
        isize x = from.x, y = from.y;

        if ((0 <= x && x < bitmap->width) && (0 <= y && y < bitmap->height)) {
            u32 *background = &bitmap->pixels[y * bitmap->stride + x];
            *background = color_blend(*background, color);
        }

        return;
    }

    if (from.x > to.x) {
        f32 swap = from.x;
        from.x = to.x;
        to.x = swap;
    }

    if (from.y > to.y) {
        f32 swap = from.y;
        from.y = to.y;
        to.y = swap;
    }

    if (to.x - from.x > to.y - from.y) {
        isize from_x = f32_max(0, from.x);
        isize to_x = f32_min(bitmap->width - 1, to.x);
        for (isize x = from_x; x <= to_x; x += 1) {
            isize y = (-A * (x + 0.5F) - C) / B + 0.5F;

            if (y >= 0 && y < bitmap->height) {
                u32 *background = &bitmap->pixels[y * bitmap->stride + x];
                *background = color_blend(*background, color);
            }
        }
    } else {
        isize from_y = f32_max(0, from.y);
        isize to_y = f32_min(bitmap->height - 1, to.y);
        for (isize y = from_y; y <= to_y; y += 1) {
            isize x = (-B * (y + 0.5F) - C) / A + 0.5F;

            if (x >= 0 && x < bitmap->width) {
                u32 *background = &bitmap->pixels[y * bitmap->stride + x];
                *background = color_blend(*background, color);
            }
        }
    }
}

void draw_circle(Bitmap *bitmap, f32x2 center, f32 radius, u32 color) {
    struct { isize x, y; } center_floored = {center.x, center.y};
    isize x = 0;
    isize y = radius;

    // Using a slightly larger circle seems to produce nicer looking results.
    // https://www.redblobgames.com/grids/circle-drawing/#aesthetics
    f32 const radius_squared = (floorf(radius) + 0.5F) * (floorf(radius) + 0.5F);

    while (x <= y) {
        // Top half

        bitmap_set_pixel(bitmap, center_floored.x + x, center_floored.y - y, color);
        bitmap_set_pixel(bitmap, center_floored.x - x, center_floored.y - y, color);

        bitmap_set_pixel(bitmap, center_floored.x + y, center_floored.y - x, color);
        bitmap_set_pixel(bitmap, center_floored.x - y, center_floored.y - x, color);

        // Bottom half

        bitmap_set_pixel(bitmap, center_floored.x + y, center_floored.y + x, color);
        bitmap_set_pixel(bitmap, center_floored.x - y, center_floored.y + x, color);

        bitmap_set_pixel(bitmap, center_floored.x + x, center_floored.y + y, color);
        bitmap_set_pixel(bitmap, center_floored.x - x, center_floored.y + y, color);

        x += 1;

        // Do the calculations as if we're drawing a slightly larger circle.
        f32 go_straight_distance = fabsf(x * x + (y + 0.5F) * (y + 0.5F) - radius_squared);
        f32 turn_distance = fabsf(x * x + (y - 0.5F) * (y - 0.5F) - radius_squared);
        if (turn_distance < go_straight_distance) {
            y -= 1;
        }
    }
}

void fill_circle(Bitmap *bitmap, f32x2 center, f32 radius, u32 color, bool blend) {
    isize x = 0;
    isize y = radius;

    f32 const radius_squared = (floorf(radius) + 0.5F) * (floorf(radius) + 0.5F);

    goto loop_start;
    while (x < y) {
        // if (x != 0)
        bitmap_set_row_pixels(bitmap, center.x - y, center.x + y, center.y - x, color);

        loop_start:
        bitmap_set_row_pixels(bitmap, center.x - y, center.x + y, center.y + x, color);

        f32 go_straight_distance = fabsf(x * x + (y + 0.5F) * (y + 0.5F) - radius_squared);
        f32 turn_distance = fabsf(x * x + (y - 0.5F) * (y - 0.5F) - radius_squared);

        if (turn_distance < go_straight_distance) {
            // if (x != y)
            bitmap_set_row_pixels(bitmap, center.x - x, center.x + x, center.y - y, color);
            bitmap_set_row_pixels(bitmap, center.x - x, center.x + x, center.y + y, color);

            y -= 1;
        }

        x += 1;
    }

    if (x == y) {
        if (x != 0) {
            bitmap_set_row_pixels(bitmap, center.x - y, center.x + y, center.y - x, color);
        }
        bitmap_set_row_pixels(bitmap, center.x - y, center.x + y, center.y + x, color);
    }
}

void draw_debug_text(Bitmap *bitmap, f32x2 text_pos, char const *text) {
    f32x2 current_pos = text_pos;

    char const *text_iter = text;
    while (*text_iter != '\0') {
        u32 unicode_char = utf8_chop_char(&text_iter);

        if (unicode_char == '\n') {
            int line_height = font8x8_glyph_height * 5 / 4;

            current_pos.x = text_pos.x;
            current_pos.y += line_height;

            continue;
        }

        f32x2 glyph_size = {font8x8_glyph_width, font8x8_glyph_height};
        u32 *glyph_bitmap = font8x8_glyph_get(unicode_char);
        if (glyph_bitmap == NULL) {
            glyph_bitmap = font8x8_glyph_get(0xfffd);
            assert(glyph_bitmap != NULL);
        }

        f32x2 shadow_pos = current_pos;
        shadow_pos.y += 2;

        f32box2 shadow_box = {shadow_pos, f32x2_add(shadow_pos, glyph_size)};
        shadow_box = f32box2_clamp(shadow_box, (f32box2){.max = {bitmap->width, bitmap->height}});

        for (isize y = shadow_box.min.y; y < shadow_box.max.y; y += 1) {
            for (isize x = shadow_box.min.x; x < shadow_box.max.x; x += 1) {
                isize local_x = x - shadow_pos.x;
                isize local_y = y - shadow_pos.y;

                if ((glyph_bitmap[local_y * font8x8_glyph_width + local_x] & 0xffffffff) != 0) {
                    bitmap->pixels[y * bitmap->stride + x] = 0xff000000;
                }
            }
        }

        f32box2 glyph_box = {current_pos, f32x2_add(current_pos, glyph_size)};
        glyph_box = f32box2_clamp(glyph_box, (f32box2){.max = {bitmap->width, bitmap->height}});

        for (isize y = glyph_box.min.y; y < glyph_box.max.y; y += 1) {
            for (isize x = glyph_box.min.x; x < glyph_box.max.x; x += 1) {
                isize local_x = x - current_pos.x;
                isize local_y = y - current_pos.y;

                if ((glyph_bitmap[local_y * font8x8_glyph_width + local_x] & 0xffffffff) != 0) {
                    f32 local_y_norm = (f32)(font8x8_glyph_height - local_y) / font8x8_glyph_height;

                    u8 shade = 192 + 64 * local_y_norm;
                    u32 color = (u32)shade << 16 | (u32)shade << 8 | shade;

                    bitmap->pixels[y * bitmap->stride + x] = color;
                }
            }
        }

        current_pos.x += font8x8_glyph_width;
    }
}

typedef struct {
    f32x2 center;
    f32x2 size;
    f32x2 velocity;

    f32x2 render_size;

    struct {
        bool top;
        bool right;
        bool bottom;
        bool left;
    } damaging_side;

    bool hidden;
    bool dynamic;
    bool disabled;
} Rectangle;

static inline f32box2 rectangle_box(Rectangle const *rectangle) {
    f32box2 box = {
        .min = f32x2_sub(rectangle->center, f32x2_scale(rectangle->size, 0.5F)),
        .max = f32x2_add(rectangle->center, f32x2_scale(rectangle->size, 0.5F)),
    };

    return box;
}

void draw_rectangle_entity(Bitmap *bitmap, Rectangle const *rectangle) {
    // Round when scaling or doing computations to tolerate floating point errors.
    // Floor later when drawing for simplicity.
    f32box2 box = {
        .min = f32x2_sub(rectangle->center, f32x2_scale(rectangle->render_size, 0.5F)),
        .max = f32x2_add(rectangle->center, f32x2_scale(rectangle->render_size, 0.5F)),
    };
    box.min = f32x2_round(f32x2_scale(box.min, bitmap->height));
    box.max = f32x2_round(f32x2_scale(box.max, bitmap->height));

    fill_rectangle(bitmap, box, ACTIVE_COLOR);

    isize border_size = isize_clamp(bitmap->width * 0.01F, 4, 16);
    for (isize i = 0; i < border_size; i += 1) {
        f32box2 frame_box = box;
        frame_box.min = f32x2_add(frame_box.min, (f32x2){i, i});
        frame_box.max = f32x2_sub(frame_box.max, (f32x2){i, i});
        if (frame_box.min.x > frame_box.max.x || frame_box.min.y > frame_box.max.y) {
            break;
        }

        draw_rectangle(bitmap, frame_box, 0xfff1b46c);

        u32 const RED_COLOR = 0xffc3604a;
        if (rectangle->damaging_side.top) {
            bitmap_set_row_pixels(
                bitmap, frame_box.min.x, frame_box.max.x, frame_box.min.y, RED_COLOR
            );
        }
        if (rectangle->damaging_side.right) {
            bitmap_set_column_pixels(
                bitmap, frame_box.max.x, frame_box.min.y, frame_box.max.y, RED_COLOR
            );
        }
        if (rectangle->damaging_side.bottom) {
            bitmap_set_row_pixels(
                bitmap, frame_box.min.x, frame_box.max.x, frame_box.max.y, RED_COLOR
            );
        }
        if (rectangle->damaging_side.left) {
            bitmap_set_column_pixels(
                bitmap, frame_box.min.x, frame_box.min.y, frame_box.max.y, RED_COLOR
            );
        }
    }
}

typedef struct Particle Particle;

struct Particle {
    Particle *next;

    f32x2 position;
    f32x2 velocity;
    f32 size;
    u32 color;

    f32 time;
    f32 lifetime;
};

#define PARTICLE_POOL_CAPACITY 128

typedef struct {
    Particle *particles;
    isize capacity;

    Particle *active_list;
    Particle *free_list;
} ParticlePool;

void particle_pool_create(Arena *arena, ParticlePool *pool) {
    pool->capacity = PARTICLE_POOL_CAPACITY;
    pool->particles = arena_alloc(arena, pool->capacity * sizeof(Particle));
    memset(pool->particles, 0, pool->capacity * sizeof(Particle));

    pool->active_list = NULL;

    pool->free_list = NULL;
    for (isize i = 0; i < pool->capacity; i += 1) {
        pool->particles[i].next = pool->free_list;
        pool->free_list = &pool->particles[i];
    }
}

Particle *particle_pool_get(ParticlePool *pool) {
    if (pool->free_list == NULL) {
        return NULL;
    }

    Particle *particle = pool->free_list;
    pool->free_list = pool->free_list->next;

    particle->next = pool->active_list;
    pool->active_list = particle;

    return particle;
}

void particle_explosion_spawn(f32x2 position, PCG32 *rng, ParticlePool *pool) {
    isize particle_count = 20 + 20 * f64_random(rng);
    for (isize i = 0; i < particle_count; i += 1) {
        Particle *particle = particle_pool_get(pool);
        if (particle == NULL) {
            break;
        }

        particle->position = position;

        f32 velocity_angle = 2 * M_PI * f64_random(rng);
        particle->velocity = f32x2_scale(
            (f32x2){cosf(velocity_angle), sinf(velocity_angle)},
            0.25F + 0.35F * f64_random(rng)
        );

        particle->size = 0.01F + 0.05F * f64_random(rng);

        particle->color = ACTIVE_COLOR;

        particle->time = 0.0F;
        particle->lifetime = 0.25F + 0.5F * f64_random(rng);
    }
}

void draw_field(
    Bitmap *bitmap,
    Rectangle const *rectangles, isize rectangle_count,
    Particle *particles
) {
    for (isize i = 0; i < rectangle_count; i += 1) {
        Rectangle rectangle = rectangles[i];

        if (!rectangle.hidden) {
            draw_rectangle_entity(bitmap, &rectangle);
        }
    }

    Particle *particle_iter = particles;
    while (particle_iter != NULL) {
        fill_circle(
            bitmap,
            f32x2_scale(particle_iter->position, bitmap->height),
            particle_iter->size * bitmap->height,
            particle_iter->color,
            false
        );
        particle_iter = particle_iter->next;
    }

    draw_rectangle(
        bitmap,
        (f32box2){{0, 0}, {bitmap->width - 1, bitmap->height - 1}},
        SECONDARY_COLOR
    );
}

#ifdef _WIN32
int WinMain(void) {
#else
int main(void) {
#endif
    isize arena_capacity = 64 * 1024;
    u8 *arena_memory = malloc(arena_capacity);
    Arena arena = {arena_memory, arena_memory + arena_capacity};
    if (arena.begin == NULL) {
        return 1;
    }

    GuiWindow *window = gui_window_create(1280, 720, "brainrot", &arena);
    if (window == NULL) {
        return 1;
    }
    gui_window_set_target_fps(window, 60.0);

    PCG32 rng;
    pcg32_init(&rng, (u64)time(NULL));

    isize rectangle_count = 0;
    Rectangle rectangles[12];

    // At least 4 rectangles for the field boundaries.
    assert(countof(rectangles) >= 4);

    f32box2 left_boundary_box = {{-FIELD_ASPECT_RATIO, 0}, {0, 1}};
    rectangles[rectangle_count++] = (Rectangle){
        .center = f32x2_scale(f32x2_add(left_boundary_box.min, left_boundary_box.max), 0.5F),
        .size = f32x2_sub(left_boundary_box.max, left_boundary_box.min),
        .hidden = true,
    };

    f32box2 right_boundary_box = {{FIELD_ASPECT_RATIO, 0}, {2 * FIELD_ASPECT_RATIO, 1}};
    rectangles[rectangle_count++] = (Rectangle){
        .center = f32x2_scale(f32x2_add(right_boundary_box.min, right_boundary_box.max), 0.5F),
        .size = f32x2_sub(right_boundary_box.max, right_boundary_box.min),
        .hidden = true,
    };

    f32box2 top_boundary_box = {{0, -1}, {FIELD_ASPECT_RATIO, 0}};
    rectangles[rectangle_count++] = (Rectangle){
        .center = f32x2_scale(f32x2_add(top_boundary_box.min, top_boundary_box.max), 0.5F),
        .size = f32x2_sub(top_boundary_box.max, top_boundary_box.min),
        .hidden = true,
    };

    f32box2 bottom_boundary_box = {{0, 1}, {FIELD_ASPECT_RATIO, 2}};
    rectangles[rectangle_count++] = (Rectangle){
        .center = f32x2_scale(f32x2_add(bottom_boundary_box.min, bottom_boundary_box.max), 0.5F),
        .size = f32x2_sub(bottom_boundary_box.max, bottom_boundary_box.min),
        .hidden = true,
    };

    isize give_up_counter = 0;
    while (rectangle_count < countof(rectangles)) {
        retry_rectangle_generation:
        give_up_counter += 1;
        if (give_up_counter > countof(rectangles) * 4) {
            break;
        }

        // Try to find a top-left corner position which is not yet occupied by any rectangle:
        f32x2 min_position = {f64_random(&rng) * (FIELD_ASPECT_RATIO), f64_random(&rng)};
        for (isize i = 0; i < rectangle_count; i += 1) {
            if (f32box2_contains(rectangle_box(&rectangles[i]), min_position)) {
                goto retry_rectangle_generation;
            }
        }

        // Find out what's the largest rectangle we can fit in the chosen position:
        f32x2 max_position = {FIELD_ASPECT_RATIO, 1};
        for (isize i = 0; i < rectangle_count; i += 1) {
            f32box2 box = rectangle_box(&rectangles[i]);

            if (min_position.x < box.min.x) {
                max_position.x = f32_min(max_position.x, box.min.x);
            }
            if (min_position.y < box.min.y) {
                max_position.y = f32_min(max_position.y, box.min.y);
            }
        }

        f32 const MIN_SIZE = 0.05F;
        if (
            max_position.x - min_position.x < MIN_SIZE ||
            max_position.y - min_position.y < MIN_SIZE
        ) {
            goto retry_rectangle_generation;
        }

        f32 const MIN_ASPECT_RATIO = 0.75F;
        f32 const MAX_ASPECT_RATIO = 1.25F;
        f32 aspect_ratio =
            MIN_ASPECT_RATIO + f64_random(&rng) * (MAX_ASPECT_RATIO - MIN_ASPECT_RATIO);

        f32 size_x = MIN_SIZE + f64_random(&rng) * (max_position.x - min_position.x);
        f32x2 size = { size_x, size_x * aspect_ratio };
        f32box2 box = {min_position, f32x2_add(min_position, size)};
        if (box.max.x > max_position.x || box.max.y > max_position.y) {
            goto retry_rectangle_generation;
        }

        Rectangle rectangle = {
            .center = f32x2_scale(f32x2_add(box.min, box.max), 0.5F),
            .size = size,
            .dynamic = true,
        };

        f32x2 velocity_direction;
        {
            f64 random = f64_random(&rng);

            if (random < 0.25F) {
                velocity_direction = (f32x2){1, 1};
            } else if (random < 0.5F) {
                velocity_direction = (f32x2){1, -1};
            } else if (random < 0.75F) {
                velocity_direction = (f32x2){-1, 1};
            } else {
                velocity_direction = (f32x2){-1, -1};
            }

            velocity_direction = f32x2_normalize(velocity_direction);
        }
        rectangle.velocity = f32x2_scale(velocity_direction, 0.5F);

        if (f64_random(&rng) < 0.5) {
            rectangle.damaging_side.top = true;
            rectangle.damaging_side.bottom = true;
        } else {
            rectangle.damaging_side.right = true;
            rectangle.damaging_side.left = true;
        }

        rectangles[rectangle_count++] = rectangle;
    }

    for (isize i = 0; i < countof(rectangles); i += 1) {
        rectangles[i].render_size = rectangles[i].size;
    }

    ParticlePool particle_pool;
    particle_pool_create(&arena, &particle_pool);

    while (!gui_window_should_close(window)) {
        GuiBitmap *gui_bitmap = gui_window_bitmap(window);
        if (gui_window_resized(window)) {
            int new_width, new_height;
            gui_window_size(window, &new_width, &new_height);

            gui_bitmap_resize(gui_bitmap, new_width, new_height);
        }

        Bitmap bitmap = {gui_bitmap_data(gui_bitmap)};
        gui_bitmap_size(gui_bitmap, &bitmap.width, &bitmap.height);
        bitmap.stride = bitmap.width;

        f64 dt = gui_window_frame_time(window);

        bitmap_clear(&bitmap, BACKGROUND_COLOR);

        f32x2 interior_size = {
            bitmap.width - 2 * FIELD_MARGIN,
            bitmap.height - 2 * FIELD_MARGIN,
        };

        f32 field_height = interior_size.x / FIELD_ASPECT_RATIO;
        if (field_height > interior_size.y) {
            field_height = interior_size.y;
        }
        f32 field_width = field_height * FIELD_ASPECT_RATIO;

        if (field_width >= 1 && field_height >= 1) {
            f32box2 field_box;
            field_box.min = (f32x2){
                (bitmap.width - field_width) * 0.5F,
                (bitmap.height - field_height) * 0.5F,
            };
            field_box.max = f32x2_add(field_box.min, (f32x2){field_width - 1, field_height - 1});

            if (
                field_box.min.x >= 0 && field_box.min.y >= 0 &&
                field_box.max.x < bitmap.width && field_box.max.y < bitmap.height
            ) {
                Bitmap field_bitmap = sub_bitmap(&bitmap, field_box);
                draw_field(&field_bitmap, rectangles, rectangle_count, particle_pool.active_list);
            }
        }

        char rules_text[] = "Красные стороны наносят урон";
        isize rules_text_width = utf8_char_count(rules_text) * font8x8_glyph_width;
        f32x2 rules_text_position = {
            (bitmap.width - rules_text_width) / 2.0F,
            font8x8_glyph_height,
        };
        draw_debug_text(&bitmap, rules_text_position, rules_text);

        gui_bitmap_render(gui_bitmap);

        f32 const TIME_EPSILON = 1e-6;
        isize iterations_without_progress[countof(rectangles)] = {0};

        f64 time_left = dt;
        while (time_left > 0.0) {
            f32 closest_collision_time = INFINITY;

            Rectangle *this_rectangle = NULL;
            Rectangle *other_rectangle = NULL;
            f32x2 collision_normal;

            for (isize this = 0; this < rectangle_count; this += 1) {
                Rectangle rectangle = rectangles[this];

                if (rectangle.velocity.x == 0 && rectangle.velocity.y == 0) {
                    continue;
                }
                if (rectangle.disabled) {
                    continue;
                }

                f32x2 ray_origin = rectangle.center;

                f32 this_collision_time = INFINITY;
                Rectangle *this_collision_rectangle;
                f32x2 this_collision_normal;

                for (isize other = 0; other < rectangle_count; other += 1) {
                    if (this == other) {
                        continue;
                    }
                    if (rectangles[other].disabled) {
                        continue;
                    }

                    f32x2 ray_direction = f32x2_sub(rectangle.velocity, rectangles[other].velocity);

                    f32box2 fat_box;
                    fat_box.min = f32x2_sub(
                        rectangle_box(&rectangles[other]).min,
                        f32x2_scale(rectangle.size, 0.5F)
                    );
                    fat_box.max = f32x2_add(
                        rectangle_box(&rectangles[other]).max,
                        f32x2_scale(rectangle.size, 0.5F)
                    );

                    f32 near, far;
                    f32x2 normal;
                    if (ray_vs_f32box2(ray_origin, ray_direction, fat_box, &near, &far, &normal)) {
                        if (near < 0) {
                            continue;
                        }

                        if (near < this_collision_time) {
                            this_collision_time = near;
                            this_collision_rectangle = &rectangles[other];
                            this_collision_normal = normal;
                        }
                    }
                }

                // If the rectangle has "bounced" 4 times without moving, this probably means that
                // its velocity vector has come back to the original direction which we've already
                // tried.
                if (iterations_without_progress[this] < 4 || this_collision_time >= TIME_EPSILON) {
                    if (this_collision_time < TIME_EPSILON) {
                        iterations_without_progress[this] += 1;
                    } else {
                        iterations_without_progress[this] = 0;
                    }

                    if (this_collision_time < closest_collision_time) {
                        closest_collision_time = this_collision_time;

                        this_rectangle = &rectangles[this];
                        other_rectangle = this_collision_rectangle;
                        collision_normal = this_collision_normal;
                    }
                }
            }

            // Update rectangle positions first:
            f32 time_passed = f32_min(closest_collision_time, time_left);
            for (isize i = 0; i < rectangle_count; i += 1) {
                // Don't update rectangles which got stuck.
                if (iterations_without_progress[i] > 0) {
                    continue;
                }

                f32x2 distance = f32x2_scale(rectangles[i].velocity, time_passed);
                rectangles[i].center = f32x2_add(rectangles[i].center, distance);
            }

            // Collision happened within the current time left:
            if (closest_collision_time <= time_left) {
                if (this_rectangle->dynamic && other_rectangle->dynamic) {
                    f32 const DECREMENT = 0.01F;
                    f32 const MIN_SIZE = 0.05F;

                    if (
                        collision_normal.x < 0 && this_rectangle->damaging_side.right ||
                        collision_normal.x > 0 && this_rectangle->damaging_side.left ||
                        collision_normal.y < 0 && this_rectangle->damaging_side.bottom ||
                        collision_normal.y > 0 && this_rectangle->damaging_side.top
                    ) {
                        f32 aspect_ratio = other_rectangle->size.y / other_rectangle->size.x;
                        other_rectangle->size.x -= DECREMENT;
                        other_rectangle->size.y = other_rectangle->size.x * aspect_ratio;

                        if (other_rectangle->size.x < MIN_SIZE) {
                            other_rectangle->hidden = true;
                            other_rectangle->disabled = true;

                            particle_explosion_spawn(other_rectangle->center, &rng, &particle_pool);
                        }
                    }

                    if (
                        collision_normal.x < 0 && other_rectangle->damaging_side.left ||
                        collision_normal.x > 0 && other_rectangle->damaging_side.right ||
                        collision_normal.y < 0 && other_rectangle->damaging_side.top ||
                        collision_normal.y > 0 && other_rectangle->damaging_side.bottom
                    ) {
                        f32 aspect_ratio = this_rectangle->size.y / this_rectangle->size.x;
                        this_rectangle->size.x -= DECREMENT;
                        this_rectangle->size.y = this_rectangle->size.x * aspect_ratio;

                        if (this_rectangle->size.x < MIN_SIZE) {
                            this_rectangle->hidden = true;
                            this_rectangle->disabled = true;

                            particle_explosion_spawn(this_rectangle->center, &rng, &particle_pool);
                        }
                    }
                }

                if (!other_rectangle->dynamic) {
                    f32 normal_velocity = f32x2_dot(this_rectangle->velocity, collision_normal);

                    f32x2 force = {0};
                    force.x = 2 * normal_velocity * collision_normal.x;
                    force.y = 2 * normal_velocity * collision_normal.y;

                    this_rectangle->velocity = f32x2_sub(this_rectangle->velocity, force);
                } else {
                    f32x2 collision_tangent = {collision_normal.y, -collision_normal.x};

                    f32x2 this_original_velocity = this_rectangle->velocity;
                    f32x2 other_original_velocity = other_rectangle->velocity;

                    // Elastic collision in 1D.
                    // v1 and v2 are velocities of two balls moving towards each other.
                    // v1' and v2' are velocities after collision.
                    //
                    // Conservation of momentum:
                    // m1*v1 + m2*v2 = m1*v1' + m2*v2'
                    //
                    // Kinetic energy is conserved for a perfectly elastic collision:
                    // v1 + v1' = v2 + v2'
                    //
                    // Solving for m1=1 and m2=1 we get:
                    // v1' = v2
                    // v2' = v1

                    f32 tangent_velocity = f32x2_dot(this_original_velocity, collision_tangent);
                    this_rectangle->velocity = f32x2_add(
                        f32x2_scale(
                            collision_normal,
                            fabsf(f32x2_dot(other_original_velocity, collision_normal))
                        ),
                        f32x2_scale(collision_tangent, tangent_velocity)
                    );
                    if (this_rectangle->velocity.x != 0 || this_rectangle->velocity.y != 0) {
                        // Keep the original velocity magnitude.
                        this_rectangle->velocity = f32x2_scale(
                            this_rectangle->velocity,
                            f32x2_length(this_original_velocity) /
                                f32x2_length(this_rectangle->velocity)
                        );
                    }

                    tangent_velocity = f32x2_dot(other_original_velocity, collision_tangent);
                    other_rectangle->velocity = f32x2_add(
                        f32x2_scale(
                            collision_normal,
                            -fabsf(f32x2_dot(this_original_velocity, collision_normal))
                        ),
                        f32x2_scale(collision_tangent, tangent_velocity)
                    );
                    if (other_rectangle->velocity.x != 0 || other_rectangle->velocity.y != 0) {
                        // Keep the original velocity magnitude.
                        other_rectangle->velocity = f32x2_scale(
                            other_rectangle->velocity,
                            f32x2_length(other_original_velocity) /
                                f32x2_length(other_rectangle->velocity)
                        );
                    }
                }
            }

            time_left -= time_passed;
        }

        for (isize i = 0; i < countof(rectangles); i += 1) {
            if (rectangles[i].hidden || rectangles[i].disabled) {
                continue;
            }

            rectangles[i].render_size = f32x2_max(
                rectangles[i].size,
                f32x2_sub(rectangles[i].render_size, (f32x2){7.5e-2F * dt, 7.5e-2F * dt})
            );
        }

        Particle *previous_particle = NULL;
        Particle *particle_iter = particle_pool.active_list;
        while (particle_iter != NULL) {
            particle_iter->time += dt;

            if (particle_iter->time >= particle_iter->lifetime) {
                // Skip over with an iterator:
                Particle *particle = particle_iter;
                particle_iter = particle_iter->next;

                // Remove from the active list:
                if (previous_particle == NULL) {
                    particle_pool.active_list = particle_pool.active_list->next;
                } else {
                    previous_particle->next = particle->next;
                }

                // Attach to the free list:
                particle->next = particle_pool.free_list;
                particle_pool.free_list = particle;

                continue;
            }

            particle_iter->position = f32x2_add(
                particle_iter->position,
                f32x2_scale(particle_iter->velocity, dt)
            );
            particle_iter->velocity = f32x2_add(
                particle_iter->velocity,
                f32x2_scale((f32x2){0, 0.5F}, dt)
            );

            particle_iter->color &= 0x00ffffff;
            particle_iter->color |= (u32)(
                ease_out_quadratic(1 - particle_iter->time / particle_iter->lifetime) * 255.0F
            ) << 24;

            previous_particle = particle_iter;
            particle_iter = particle_iter->next;
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

isize utf8_char_count(char const *string) {
    isize char_count = 0;
    while (*string != '\0') {
        utf8_chop_char(&string);
        char_count += 1;
    }

    return char_count;
}
