#ifndef GUI_H
#define GUI_H

#define GUI_MAX_WINDOW_WIDTH 32767
#define GUI_MAX_WINDOW_HEIGHT 32767

#include <stdbool.h>

// Redefinition of typedefs is a C11 feature.
// This is the official™ guard, which is used across different headers to protect u8 and friends.
// (Or just add a #define before including this header, if you already have short names defined.)
#ifndef SHORT_NAMES_FOR_PRIMITIVE_TYPES_WERE_DEFINED
    #define SHORT_NAMES_FOR_PRIMITIVE_TYPES_WERE_DEFINED

    #include <stdint.h>
    #include <stddef.h>

    typedef int8_t i8;
    typedef uint8_t u8;
    typedef uint16_t u16;
    typedef int16_t i16;
    typedef uint32_t u32;
    typedef int32_t i32;
    typedef uint64_t u64;
    typedef int64_t i64;

    typedef uintptr_t uptr;
    typedef size_t usize;
    typedef ptrdiff_t isize;

    typedef float f32;
    typedef double f64;
#endif

typedef struct {
    u8 *begin;
    u8 *end;
} GuiArena;

// Always returns a non-null value.
// Aborts in case we run out of memory.
// Allocating 0 bytes is not allowed.
void *gui_arena_alloc(GuiArena *arena, isize size);

typedef struct GuiWindow GuiWindow;
typedef struct GuiBitmap GuiBitmap;

GuiWindow *gui_window_create(
    isize width,
    isize height,
    char const *title,
    GuiArena *arena
);
void gui_window_destroy(GuiWindow *window);

bool gui_window_resized(GuiWindow const *window);
void gui_window_size(GuiWindow const *window, isize *width, isize *height);

// Polls events, updates the timer and tells, if you should quit your game/render loop.
bool gui_window_should_close(GuiWindow *window);

f64 gui_window_time(GuiWindow const *window);
f64 gui_window_frame_time(GuiWindow const *window);

GuiBitmap *gui_window_bitmap(GuiWindow *window);
u32 *gui_bitmap_data(GuiBitmap const *bitmap);
bool gui_bitmap_resize(GuiBitmap *bitmap, isize width, isize height);
void gui_bitmap_size(GuiBitmap const *bitmap, isize *width, isize *height);
void gui_bitmap_render(GuiBitmap *bitmap);

#endif // GUI_H
