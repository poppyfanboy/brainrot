#include <assert.h> // assert
#include <stdlib.h> // malloc
#include <stddef.h> // NULL
#include <time.h>   // time
#include <math.h>   // sinf, cosf, M_PI, floorf

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

    typedef size_t   usize; typedef ptrdiff_t isize;
    typedef uintptr_t uptr;

    typedef float f32; typedef double f64;
#endif

#include "../res/font8x8.c"
u32 utf8_chop_char(char const **string_iter);
u32 *font8x8_glyph_get(u32 unicode_char);

#define BACKGROUND_COLOR 0x192739
#define ACTIVE_COLOR 0xffec62
#define SECONDARY_COLOR 0x908f88
#define DISABLED_COLOR 0x45454c

#define FIELD_ASPECT_RATIO (4.0F / 3.0F)
#define FIELD_MARGIN 48

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

inline f64 f64_random(PCG32 *rng) {
    return (f64)pcg32_random(rng) * 0x1p-32;
}

inline f32 f32_max(f32 left, f32 right) {
    return left > right ? left : right;
}

inline f32 f32_min(f32 left, f32 right) {
    return left < right ? left : right;
}

inline f32 f32_abs(f32 value) {
    if (value >= 0.0F) {
        return value;
    } else {
        return -value;
    }
}

typedef struct {
    f32 x;
    f32 y;
} f32x2;

inline f32x2 f32x2_add(f32x2 left, f32x2 right) {
    return (f32x2){left.x + right.x, left.y + right.y};
}

inline f32x2 f32x2_sub(f32x2 left, f32x2 right) {
    return (f32x2){left.x - right.x, left.y - right.y};
}

inline f32x2 f32x2_scale(f32x2 vector, f32 scale) {
    return (f32x2){vector.x * scale, vector.y * scale};
}

inline f32x2 f32x2_clamp(f32x2 value, f32x2 min, f32x2 max) {
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

inline f32x2 f32x2_floor(f32x2 vector) {
    vector.x = floorf(vector.x);
    vector.y = floorf(vector.y);
    return vector;
}

typedef struct {
    f32x2 min;
    f32x2 max;
} f32box2;

inline f32box2 f32box2_clamp(f32box2 target, f32box2 clamper) {
    target.min = f32x2_clamp(target.min, clamper.min, clamper.max);
    target.max = f32x2_clamp(target.max, clamper.min, clamper.max);
    return target;
}

inline bool f32box2_vs_f32box2(f32box2 this, f32box2 other) {
    return
        this.min.x < other.max.x && this.max.x > other.min.x &&
        this.min.y < other.max.y && this.max.y > other.min.y;
}

inline bool f32box2_contains(f32box2 box, f32x2 point) {
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
            *normal = (f32x2){-1.0F, 0.0F};
        } else {
            *normal = (f32x2){1.0F, 0.0F};
        }
    } else {
        // Ray collided with the horizontal side first.
        // Get collision normal based on if the ray comes from top to bottom or from bottom to top.
        if (direction.y > 0) {
            *normal = (f32x2){0.0F, -1.0F};
        } else {
            *normal = (f32x2){0.0F, 1.0F};
        }
    }

    return true;
}

typedef struct {
    u32 *pixels;
    int width, height;
    int stride;
} Bitmap;

inline f32box2 bitmap_box(Bitmap const *bitmap) {
    f32box2 box = {
        .min = {0, 0},
        .max = {bitmap->width - 1.0F, bitmap->height - 1.0F},
    };
    return box;
}

inline Bitmap sub_bitmap(Bitmap const *bitmap, f32box2 box) {
    box = f32box2_clamp(box, bitmap_box(bitmap));
    isize from_x = box.min.x, from_y = box.min.y;
    isize to_x = box.max.x, to_y = box.max.y;

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

void fill_rectangle(Bitmap *bitmap, f32box2 rectangle, u32 color) {
    rectangle = f32box2_clamp(rectangle, bitmap_box(bitmap));

    for (isize y = rectangle.min.y; y <= rectangle.max.y; y += 1) {
        u32 *line = &bitmap->pixels[y * bitmap->stride];
        for (isize x = rectangle.min.x; x <= rectangle.max.x; x += 1) {
            line[x] = color;
        }
    }
}

void draw_rectangle(Bitmap *bitmap, f32box2 rectangle, u32 color) {
    f32box2 bounds = bitmap_box(bitmap);
    f32box2 clamped_rectangle = f32box2_clamp(rectangle, bounds);

    // Check if rectangle is completely clamped out
    if (clamped_rectangle.max.y - clamped_rectangle.min.y < 1) {
        return;
    }
    if (clamped_rectangle.max.x - clamped_rectangle.min.x < 1) {
        return;
    }

    // Top horizontal line
    if (bounds.min.y <= rectangle.min.y) {
        for (isize x = clamped_rectangle.min.x; x <= (isize)clamped_rectangle.max.x; x += 1) {
            bitmap->pixels[(isize)rectangle.min.y * bitmap->stride + x] = color;
        }
    }

    // Bottom horizontal line
    if (rectangle.max.y <= bounds.max.y) {
        for (isize x = clamped_rectangle.min.x; x <= (isize)clamped_rectangle.max.x; x += 1) {
            bitmap->pixels[(isize)rectangle.max.y * bitmap->stride + x] = color;
        }
    }

    // Left vertical line
    if (bounds.min.x <= rectangle.min.x) {
        for (isize y = clamped_rectangle.min.y; y <= (isize)clamped_rectangle.max.y; y += 1) {
            bitmap->pixels[y * bitmap->stride + (isize)rectangle.min.x] = color;
        }
    }

    // Right vertical line
    if (rectangle.max.x <= bounds.max.x) {
        for (isize y = clamped_rectangle.min.y; y <= (isize)clamped_rectangle.max.y; y += 1) {
            bitmap->pixels[y * bitmap->stride + (isize)rectangle.max.x] = color;
        }
    }
}

void draw_line(Bitmap *bitmap, f32x2 from, f32x2 to, u32 color) {
    from = f32x2_floor(from);
    to = f32x2_floor(to);

    f32box2 bounds = bitmap_box(bitmap);

    // Line equation: f(x, y) = Ax + By + C
    // (A, B) is a perpendicular vector. C is then derived from f(x, y) = 0.
    f32 A = to.y - from.y;
    f32 B = from.x - to.x;
    f32 C = -A * from.x - B * from.y;

    // Single pixel special case:
    if (A == 0.0F && B == 0.0F) {
        f32 x = from.x, y = from.y;
        if (f32box2_contains(bounds, (f32x2){x + 0.5F, y + 0.5F})) {
            bitmap->pixels[(isize)(y + 0.5F) * bitmap->stride + (isize)(x + 0.5F)] = color;
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
        for (isize x = from.x; x <= to.x; x += 1) {
            f32 y = (-A * x - C) / B;

            if (f32box2_contains(bounds, (f32x2){x + 0.5F, y + 0.5F})) {
                bitmap->pixels[(isize)(y + 0.5F) * bitmap->stride + (isize)(x + 0.5F)] = color;
            }
        }
    } else {
        for (isize y = from.y; y <= to.y; y += 1) {
            f32 x = (-B * y - C) / A;

            if (f32box2_contains(bounds, (f32x2){x + 0.5F, y + 0.5F})) {
                bitmap->pixels[(isize)(y + 0.5F) * bitmap->stride + (isize)(x + 0.5F)] = color;
            }
        }
    }
}

void draw_text(Bitmap *bitmap, f32x2 text_pos, char const *text) {
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
        shadow_box = f32box2_clamp(shadow_box, bitmap_box(bitmap));

        for (isize y = shadow_box.min.y; y < shadow_box.max.y; y += 1) {
            for (isize x = shadow_box.min.x; x < shadow_box.max.x; x += 1) {
                isize local_x = x - shadow_pos.x;
                isize local_y = y - shadow_pos.y;

                if ((glyph_bitmap[local_y * font8x8_glyph_width + local_x] & 0xffffffff) != 0) {
                    bitmap->pixels[y * bitmap->stride + x] = 0x000000;
                }
            }
        }

        f32box2 glyph_box = {current_pos, f32x2_add(current_pos, glyph_size)};
        glyph_box = f32box2_clamp(glyph_box, bitmap_box(bitmap));

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
    f32box2 box;
    f32x2 velocity;
    bool visible;
} Rectangle;

void draw_field(Bitmap *bitmap, Rectangle *rectangles, isize rectangle_count) {
    for (isize i = 0; i < rectangle_count; i += 1) {
        Rectangle rectangle = rectangles[i];

        if (!rectangle.visible) {
            continue;
        }

        f32box2 box = rectangle.box;
        box.min = f32x2_scale(box.min, bitmap->height);
        box.max = f32x2_scale(box.max, bitmap->height);
        fill_rectangle(bitmap, box, ACTIVE_COLOR);
    }

    draw_rectangle(bitmap, bitmap_box(bitmap), SECONDARY_COLOR);
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
    gui_window_set_target_fps(window, 60.0);

    PCG32 rng;
    pcg32_init(&rng, (u64)time(NULL));

    isize rectangle_count = 0;
    Rectangle rectangles[12];

    assert(countof(rectangles) >= 4);
    rectangles[rectangle_count++] = (Rectangle){
        // left boundary
        .box = {{-FIELD_ASPECT_RATIO, 0.0F}, {0.0F, 1.0F}},
        .velocity = {0},
        .visible = true,
    };
    rectangles[rectangle_count++] = (Rectangle){
        // right boundary
        .box = {{FIELD_ASPECT_RATIO, 0.0F}, {2.0F * FIELD_ASPECT_RATIO, 1.0F}},
        .velocity = {0},
        .visible = true,
    };
    rectangles[rectangle_count++] = (Rectangle){
        // top boundary
        .box = {{0.0F, -1.0F}, {FIELD_ASPECT_RATIO, 0.0F}},
        .velocity = {0},
        .visible = true,
    };
    rectangles[rectangle_count++] = (Rectangle){
        // bottom boundary
        .box = {{0.0F, 1.0F}, {FIELD_ASPECT_RATIO, 2.0F}},
        .velocity = {0},
        .visible = true,
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
            if (f32box2_contains(rectangles[i].box, min_position)) {
                goto retry_rectangle_generation;
            }
        }

        // Find out what's the largest rectangle we can fit in the chosen position:
        f32x2 max_position = {FIELD_ASPECT_RATIO, 1.0F};
        for (isize i = 0; i < rectangle_count; i += 1) {
            if (min_position.x < rectangles[i].box.min.x) {
                max_position.x = f32_min(max_position.x, rectangles[i].box.min.x);
            }
            if (min_position.y < rectangles[i].box.min.y) {
                max_position.y = f32_min(max_position.y, rectangles[i].box.min.y);
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

        f32 angle = f64_random(&rng) * 2.0F * (f32)M_PI;
        f32x2 velocity = f32x2_scale((f32x2){cosf(angle), sinf(angle)}, 0.5F);

        rectangles[rectangle_count++] = (Rectangle){box, velocity, .visible = true};
    }

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

        int mouse_x, mouse_y;
        gui_mouse_position(window, &mouse_x, &mouse_y);
        f32x2 mouse_position = {mouse_x, mouse_y};

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

            f32box2 field_box = {
                .min = {(bitmap.width - field_width) / 2.0F, (bitmap.height - field_height) / 2.0F},
                .max = {(bitmap.width + field_width) / 2.0F, (bitmap.height + field_height) / 2.0F},
            };

            Bitmap field_bitmap = sub_bitmap(&bitmap, field_box);
            draw_field(&field_bitmap, rectangles, rectangle_count);
        }

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

                if (rectangle.velocity.x == 0.0F && rectangle.velocity.y == 0.0F) {
                    continue;
                }

                f32x2 ray_origin = f32x2_scale(
                    f32x2_add(rectangle.box.min, rectangle.box.max),
                    0.5F
                );

                f32 this_collision_time = INFINITY;
                Rectangle *this_collision_rectangle;
                f32x2 this_collision_normal;

                for (isize other = 0; other < rectangle_count; other += 1) {
                    if (this == other) {
                        continue;
                    }

                    f32x2 ray_direction = f32x2_sub(rectangle.velocity, rectangles[other].velocity);

                    f32box2 fat_box;
                    fat_box.min = f32x2_sub(
                        rectangles[other].box.min,
                        f32x2_scale(f32x2_sub(rectangle.box.max, rectangle.box.min), 0.5F)
                    );
                    fat_box.max = f32x2_add(
                        rectangles[other].box.max,
                        f32x2_scale(f32x2_sub(rectangle.box.max, rectangle.box.min), 0.5F)
                    );

                    f32 near, far;
                    f32x2 normal;
                    if (ray_vs_f32box2(ray_origin, ray_direction, fat_box, &near, &far, &normal)) {
                        if (near < 0.0F) {
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
                rectangles[i].box.min = f32x2_add(rectangles[i].box.min, distance);
                rectangles[i].box.max = f32x2_add(rectangles[i].box.max, distance);
            }

            // Then update rectangle velocities:
            if (closest_collision_time <= time_left) {
                // The target rectangle bounces off in the direction of the collision normal.
                // The rectangle it collided with bounces off in the opposite direction.
                if (collision_normal.x != 0.0F) {
                    if (collision_normal.x > 0.0F) {
                        this_rectangle->velocity.x = f32_abs(this_rectangle->velocity.x);
                        other_rectangle->velocity.x = -f32_abs(other_rectangle->velocity.x);
                    } else {
                        this_rectangle->velocity.x = -f32_abs(this_rectangle->velocity.x);
                        other_rectangle->velocity.x = f32_abs(other_rectangle->velocity.x);
                    }
                } else {
                    if (collision_normal.y > 0.0F) {
                        this_rectangle->velocity.y = f32_abs(this_rectangle->velocity.y);
                        other_rectangle->velocity.y = -f32_abs(other_rectangle->velocity.y);
                    } else {
                        this_rectangle->velocity.y = -f32_abs(this_rectangle->velocity.y);
                        other_rectangle->velocity.y = f32_abs(other_rectangle->velocity.y);
                    }
                }
            }

            time_left -= time_passed;
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
