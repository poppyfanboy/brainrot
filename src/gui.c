#include "gui.h"

#include <assert.h> // assert
#include <stdlib.h> // abort
#include <string.h> // memset

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

#define GUI_MAX_WINDOW_WIDTH 32767
#define GUI_MAX_WINDOW_HEIGHT 32767

#define countof(array) ((isize)sizeof(array) / (isize)sizeof((array)[0]))

typedef struct {
    u8 *begin;
    u8 *end;
} Arena;

#define ARENA_ALIGNMENT 16

void *arena_alloc(Arena *arena, isize size) {
    assert(size > 0);

    isize padding = (~(uptr)arena->begin + 1) & (ARENA_ALIGNMENT - 1);
    isize memory_left = arena->end - arena->begin - padding;
    if (memory_left < 0 || memory_left < size) {
        abort();
    }

    void *ptr = arena->begin + padding;
    arena->begin += padding + size;
    return ptr;
}

#define FPS_SAMPLE_COUNT 5
#define FPS_SAMPLE_PERIOD 0.1

// Calculates average FPS based on the amount of frames rendered within the last
// (FPS_SAMPLE_COUNT * FPS_SAMPLE_PERIOD) seconds.
typedef struct {
    // How many frames we rendered...
    isize samples[FPS_SAMPLE_COUNT];
    isize samples_sum;

    // ...in this amount of time (in nanoseconds).
    i64 durations[FPS_SAMPLE_COUNT];
    i64 total_duration;

    isize next_sample;
    isize next_sample_duration;
    isize next_sample_index;
} FPSCounter;

static void fps_counter_init(FPSCounter *fps_counter) {
    memset(fps_counter->samples, 0, sizeof(fps_counter->samples));
    fps_counter->samples_sum = 0;

    memset(fps_counter->durations, 0, sizeof(fps_counter->durations));
    // Initialize to 1 to avoid dividing by 0 in fps_counter_average until we get our first sample.
    fps_counter->durations[0] = 1;
    fps_counter->total_duration = 1;

    fps_counter->next_sample = 0;
    fps_counter->next_sample_duration = 0;
    fps_counter->next_sample_index = 0;
}

static void fps_counter_add_frame(FPSCounter *fps_counter, i64 frame_time) {
    fps_counter->next_sample_duration += frame_time;
    fps_counter->next_sample += 1;

    if (fps_counter->next_sample_duration >= (i64)(FPS_SAMPLE_PERIOD * 1e9)) {
        fps_counter->samples_sum -= fps_counter->samples[fps_counter->next_sample_index];
        fps_counter->samples_sum += fps_counter->next_sample;
        fps_counter->samples[fps_counter->next_sample_index] = fps_counter->next_sample;

        fps_counter->total_duration -= fps_counter->durations[fps_counter->next_sample_index];
        fps_counter->total_duration += fps_counter->next_sample_duration;
        fps_counter->durations[fps_counter->next_sample_index] = fps_counter->next_sample_duration;

        fps_counter->next_sample = 0;
        fps_counter->next_sample_duration = 0;

        fps_counter->next_sample_index = (fps_counter->next_sample_index + 1) % FPS_SAMPLE_COUNT;
    }
}

static f64 fps_counter_average(FPSCounter const *fps_counter) {
    return (f64)fps_counter->samples_sum / ((f64)fps_counter->total_duration * 1e-9);
}

#ifdef __linux__

#include <stddef.h> // NULL, size_t
#include <string.h> // memset

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

#include <time.h>

#include <sys/shm.h>
#include <sys/ipc.h>

struct GuiBitmap {
    GuiWindow *window;
    XImage *image;
    isize width;
    isize height;
    XShmSegmentInfo shared_segment;
    bool available;
};

static void gui_bitmap_destroy(Display *display, GuiBitmap *bitmap) {
    if (bitmap->shared_segment.shmaddr != NULL) {
        XShmDetach(display, &bitmap->shared_segment);
        XSync(display, False);

        shmdt(bitmap->shared_segment.shmaddr);
        shmctl(bitmap->shared_segment.shmid, IPC_RMID, 0);

        bitmap->shared_segment.shmaddr = NULL;
    }

    if (bitmap->image != NULL) {
        XDestroyImage(bitmap->image);

        bitmap->image = NULL;
    }
}

static bool gui_bitmap_create(
    Display *display,
    XVisualInfo *visual_info,
    isize width,
    isize height,
    GuiBitmap *bitmap
) {
    XImage *image = XShmCreateImage(
        display,
        visual_info->visual,
        (unsigned int)visual_info->depth,
        ZPixmap,
        NULL,
        &bitmap->shared_segment,
        (unsigned int)width,
        (unsigned int)height
    );
    if (image == NULL || image->bits_per_pixel != 32) {
        goto fail;
    }

    isize buffer_size = image->bytes_per_line * image->height;
    int shmid = shmget(IPC_PRIVATE, (size_t)buffer_size, IPC_CREAT | 0666);
    if (shmid == -1) {
        goto fail;
    }

    bitmap->available = true;

    bitmap->shared_segment.shmid = shmid;
    bitmap->shared_segment.shmaddr = shmat(shmid, 0, 0);
    bitmap->shared_segment.readOnly = false;

    image->data = bitmap->shared_segment.shmaddr;
    XShmAttach(display, &bitmap->shared_segment);

    bitmap->image = image;
    bitmap->width = width;
    bitmap->height = height;

    return true;

fail:
    gui_bitmap_destroy(display, bitmap);
    return false;
}

struct GuiWindow {
    Display *display;
    Window handle;
    XVisualInfo visual_info;

    struct {
        Atom delete_window;
    } atom;

    struct {
        int shm_completion;
    } event;

    isize width;
    isize height;
    bool resized;
    GuiBitmap bitmap;
    bool should_close;

    int mouse_x;
    int mouse_y;

    struct {
        bool was_down;
        bool is_down;
        bool currently_down;
    } mouse_buttons[2];

    struct {
        struct timespec created_time;
        struct timespec last_update_time;
        f64 last_frame_time;
    } timer;

    f64 target_fps;
    FPSCounter fps_counter;
};

GuiWindow *gui_window_create(int width, int height, char const *title, void *arena) {
    assert(width < GUI_MAX_WINDOW_WIDTH && height < GUI_MAX_WINDOW_HEIGHT);

    GuiWindow *window = arena_alloc(arena, sizeof(GuiWindow));
    memset(window, 0, (size_t)sizeof(GuiWindow));
    window->width = width;
    window->height = height;
    window->should_close = false;
    window->resized = false;

    // Open a connection to X server.
    {
        Display *display = XOpenDisplay(NULL);
        if (display == NULL) {
            goto fail;
        }
        window->display = display;

        window->event.shm_completion = XShmGetEventBase(window->display) + ShmCompletion;
    }

    // Get visual info.
    {
        bool visual_info_found = XMatchVisualInfo(
            window->display,
            DefaultScreen(window->display),
            24,
            TrueColor,
            &window->visual_info
        );
        if (!visual_info_found) {
            goto fail;
        }

        // On a little-endian machine this means that the byte order is B-G-R-X (X for padding).
        if (
            window->visual_info.red_mask != 0x00ff0000 ||
            window->visual_info.green_mask != 0x0000ff00 ||
            window->visual_info.blue_mask != 0x000000ff
        ) {
            goto fail;
        }
    }

    // Create a window.
    {
        Window root_window = DefaultRootWindow(window->display);
        Colormap colormap = XCreateColormap(
            window->display,
            root_window,
            window->visual_info.visual,
            AllocNone
        );
        XSetWindowAttributes window_attributes = {
            .background_pixel = BlackPixel(window->display, DefaultScreen(window->display)),
            .colormap = colormap,
            .bit_gravity = StaticGravity,
            .event_mask =
                StructureNotifyMask |
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
        };
        Window handle = XCreateWindow(
            window->display,
            root_window,
            0,
            0,
            (unsigned int)width,
            (unsigned int)height,
            0,
            window->visual_info.depth,
            InputOutput,
            window->visual_info.visual,
            CWBackPixel | CWColormap | CWBitGravity | CWEventMask,
            &window_attributes
        );
        if (handle == None) {
            goto fail;
        }
        window->handle = handle;

        // VLC allocates XClassHint on stack instead of using XAllocClassHint and so do I.
        // https://github.com/videolan/vlc/blob/6ac95c00183ff8f7a8172f216d306a0d56d14b2b/modules/gui/skins2/x11/x11_window.cpp#L179
        XSetClassHint(window->display, window->handle, &(XClassHint){(char *)title, (char *)title});
        XStoreName(window->display, window->handle, title);

        // https://handmade.network/forums/articles/t/2834-tutorial_a_tour_through_xlib_and_related_technologies#part_4_-_even_better_shutdown
        //
        // Ask for a ClientMessage event to get sent when the window is getting closed, so that we
        // could handle destroying the window ourselves.
        window->atom.delete_window = XInternAtom(window->display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(window->display, window->handle, &window->atom.delete_window, 1);
    }

    // Create a bitmap.
    gui_bitmap_create(window->display, &window->visual_info, width, height, &window->bitmap);
    window->bitmap.window = window;

    // Show the window only once everything is initialized.
    XMapWindow(window->display, window->handle);
    XFlush(window->display);

    // Start the timer.
    struct timespec created_time;
    clock_gettime(CLOCK_MONOTONIC, &created_time);
    window->timer.created_time = created_time;
    window->timer.last_update_time = created_time;
    window->timer.last_frame_time = 0.0;

    fps_counter_init(&window->fps_counter);

    return window;

fail:
    gui_bitmap_destroy(window->display, &window->bitmap);

    if (window->handle != None) {
        XDestroyWindow(window->display, window->handle);
    }

    if (window->display != NULL) {
        XCloseDisplay(window->display);
    }

    return NULL;
}

static void gui_window_handle_event(GuiWindow *window, XEvent *event) {
    if (event->type == window->event.shm_completion) {
        window->bitmap.available = true;
    }

    switch (event->type) {
    case ConfigureNotify: {
        if (
            window->width != event->xconfigure.width ||
            window->height != event->xconfigure.height
        ) {
            window->width = event->xconfigure.width;
            window->height = event->xconfigure.height;
            window->resized = true;
        }
    } break;

    case ClientMessage: {
        if ((Atom)event->xclient.data.l[0] == window->atom.delete_window) {
            window->should_close = true;
        }
    } break;

    case MotionNotify: {
        window->mouse_x = event->xmotion.x;
        window->mouse_y = event->xmotion.y;
    } break;

    case ButtonPress: {
        if (event->xbutton.button == Button1) {
            window->mouse_buttons[GUI_MOUSE_BUTTON_LEFT].currently_down = true;
        } else if (event->xbutton.button == Button3) {
            window->mouse_buttons[GUI_MOUSE_BUTTON_RIGHT].currently_down = true;
        }
    } break;

    case ButtonRelease: {
        if (event->xbutton.button == Button1) {
            window->mouse_buttons[GUI_MOUSE_BUTTON_LEFT].currently_down = false;
        } else if (event->xbutton.button == Button3) {
            window->mouse_buttons[GUI_MOUSE_BUTTON_RIGHT].currently_down = false;
        }
    } break;
    }
}

bool gui_window_resized(GuiWindow const *window) {
    return window->resized;
}

void gui_mouse_position(GuiWindow const *window, int *mouse_x, int *mouse_y) {
    *mouse_x = window->mouse_x;
    *mouse_y = window->mouse_y;
}

bool gui_mouse_button_down(GuiWindow const *window, int mouse_button) {
    assert(0 <= mouse_button && mouse_button < countof(window->mouse_buttons));

    return window->mouse_buttons[mouse_button].is_down == 1;
}

bool gui_mouse_button_was_pressed(GuiWindow const *window, int mouse_button) {
    assert(0 <= mouse_button && mouse_button < countof(window->mouse_buttons));

    return
        window->mouse_buttons[mouse_button].was_down == 0 &&
        window->mouse_buttons[mouse_button].is_down == 1;
}

bool gui_mouse_button_was_released(GuiWindow const *window, int mouse_button) {
    assert(0 <= mouse_button && mouse_button < countof(window->mouse_buttons));

    return
        window->mouse_buttons[mouse_button].was_down == 1 &&
        window->mouse_buttons[mouse_button].is_down == 0;
}

void gui_window_size(GuiWindow const *window, int *width, int *height) {
    *width = window->width;
    *height = window->height;
}

bool gui_window_should_close(GuiWindow *window) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    if (window->target_fps != 0.0) {
        i64 elapsed_seconds = current_time.tv_sec - window->timer.last_update_time.tv_sec;
        i64 elapsed_nanos = current_time.tv_nsec - window->timer.last_update_time.tv_nsec;

        f64 elapsed_nanos_total = elapsed_seconds * 1e9 + elapsed_nanos;
        f64 target_nanos_total = 1e9 / window->target_fps;

        if (elapsed_nanos_total < target_nanos_total) {
            struct timespec sleep_time;
            sleep_time.tv_sec =
                (target_nanos_total - elapsed_nanos_total) * 1e-9;
            sleep_time.tv_nsec =
                (target_nanos_total - elapsed_nanos_total) - sleep_time.tv_sec * 1e9;

            nanosleep(&sleep_time, NULL);

            clock_gettime(CLOCK_MONOTONIC, &current_time);
        }
    }

    i64 elapsed_seconds = current_time.tv_sec - window->timer.last_update_time.tv_sec;
    i64 elapsed_nanos = current_time.tv_nsec - window->timer.last_update_time.tv_nsec;
    window->timer.last_update_time = current_time;

    window->timer.last_frame_time = (f64)elapsed_seconds + (f64)elapsed_nanos / 1e9;

    i64 elapsed_nanos_total = elapsed_seconds * 1000000000 + elapsed_nanos;
    fps_counter_add_frame(&window->fps_counter, elapsed_nanos_total);

    window->resized = false;
    for (isize i = 0; i < countof(window->mouse_buttons); i += 1) {
        window->mouse_buttons[i].was_down = window->mouse_buttons[i].is_down;
        window->mouse_buttons[i].is_down = window->mouse_buttons[i].currently_down;
    }

    while (!window->should_close && XPending(window->display) > 0) {
        XEvent event;
        XNextEvent(window->display, &event);
        gui_window_handle_event(window, &event);
    }

    return window->should_close;
}

GuiBitmap *gui_window_bitmap(GuiWindow *window) {
    if (!window->bitmap.available) {
        // Block at least until the next event, so that we don't busy-loop.
        do {
            XEvent event;
            XNextEvent(window->display, &event);
            gui_window_handle_event(window, &event);
        } while(!window->bitmap.available);
    }

    return &window->bitmap;
}

uint32_t *gui_bitmap_data(GuiBitmap const *bitmap) {
    assert(bitmap->available);

    return (u32 *)bitmap->shared_segment.shmaddr;
}

void gui_bitmap_size(GuiBitmap const *bitmap, int *width, int *height) {
    *width = bitmap->width;
    *height = bitmap->height;
}

bool gui_bitmap_resize(GuiBitmap *bitmap, int width, int height) {
    assert(bitmap->available);

    isize buffer_old_size = bitmap->width * bitmap->height * 4;
    isize buffer_new_size = width * height * 4;

    GuiWindow *window = bitmap->window;

    if (buffer_old_size >= buffer_new_size) {
        XImage *new_image = XShmCreateImage(
            window->display,
            window->visual_info.visual,
            (unsigned int)window->visual_info.depth,
            ZPixmap,
            NULL,
            &bitmap->shared_segment,
            (unsigned int)width,
            (unsigned int)height
        );
        if (new_image == NULL || new_image->bits_per_pixel != 32) {
            return false;
        }

        XDestroyImage(bitmap->image);

        new_image->data = bitmap->shared_segment.shmaddr;
        bitmap->image = new_image;
        bitmap->width = width;
        bitmap->height = height;

        return true;
    } else {
        gui_bitmap_destroy(window->display, bitmap);
        memset(bitmap, 0, (size_t)sizeof(GuiBitmap));

        if (!gui_bitmap_create(window->display, &window->visual_info, width, height, bitmap)) {
            bitmap->width = 0;
            bitmap->height = 0;
            return false;
        }
        bitmap->window = window;

        return true;
    }
}

void gui_bitmap_render(GuiBitmap *bitmap) {
    assert(bitmap->available);

    bitmap->available = false;

    XShmPutImage(
        bitmap->window->display,
        bitmap->window->handle,
        DefaultGC(bitmap->window->display, bitmap->window->visual_info.screen),
        bitmap->image,
        0,
        0,
        0,
        0,
        (unsigned int)bitmap->width,
        (unsigned int)bitmap->height,
        true
    );
    XFlush(bitmap->window->display);
}

double gui_window_time(GuiWindow const *window) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    return
        (f64)(current_time.tv_sec - window->timer.created_time.tv_sec) +
        (f64)(current_time.tv_nsec - window->timer.created_time.tv_nsec) / 1e9;
}

double gui_window_frame_time(GuiWindow const *window) {
    return window->timer.last_frame_time;
}

double gui_window_fps(GuiWindow const *window) {
    return fps_counter_average(&window->fps_counter);
}

void gui_window_set_target_fps(GuiWindow *window, double target_fps) {
    window->target_fps = target_fps;
}

void gui_window_destroy(GuiWindow *window) {
    gui_bitmap_destroy(window->display, &window->bitmap);
    XDestroyWindow(window->display, window->handle);
    XCloseDisplay(window->display);
}

#endif // __linux__

#ifdef _WIN32

#include <stddef.h> // size_t, NULL
#include <string.h> // memset

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>

// I took these from here:
// https://github.com/f1nalspace/final_game_tech/blob/master/final_platform_layer.h

#if defined(__GNUC__) || defined(__clang__)

static i32 i32_atomic_load(volatile i32 *source) {
    return __sync_add_and_fetch(source, 0);
}

static void i32_atomic_store(volatile i32 *dest, i32 value) {
    __sync_synchronize();
    __sync_lock_test_and_set(dest, value);
}

static i32 i32_atomic_exchange(volatile i32 *target, i32 value) {
    __sync_synchronize();
    return __sync_lock_test_and_set(target, value);
}

#elif defined(_MSC_VER)

static i32 i32_atomic_load(volatile i32 *source) {
    return InterlockedCompareExchange((volatile LONG *)source, 0, 0);
}

static void i32_atomic_store(volatile i32 *dest, i32 value) {
    InterlockedExchange((volatile LONG *)dest, value);
}

static i32 i32_atomic_exchange(volatile i32 *target, i32 value) {
    return InterlockedExchange((volatile LONG *)target, value);
}

#endif

#define WINDOW_CLASS_NAME L"WINDOW_CLASS_NAME"

struct GuiBitmap {
    GuiWindow *window;
    HBITMAP handle;
    HDC device_context;

    isize width;
    isize height;
    u32 *data;
};

struct GuiWindow {
    HWND handle;
    HDC device_context;
    GuiBitmap bitmap;
    volatile i32 should_close;

    volatile i32 width;
    volatile i32 height;
    volatile i32 resized;

    volatile i32 mouse_x;
    volatile i32 mouse_y;

    struct {
        i32 was_down;
        i32 is_down;
        volatile i32 currently_down;
    } mouse_buttons[2];

    struct {
        LARGE_INTEGER frequency;
        LARGE_INTEGER created_time;
        LARGE_INTEGER last_update_time;
        f64 last_frame_time;
    } timer;

    f64 target_fps;
    FPSCounter fps_counter;
};

static LRESULT CALLBACK window_procedure(
    HWND window_handle,
    UINT message_type,
    WPARAM w_param,
    LPARAM l_param
) {
    GuiWindow *window = (GuiWindow *)GetWindowLongPtrW(window_handle, GWLP_USERDATA);

    if (window != NULL) {
        switch (message_type) {
        case WM_DESTROY: {
            i32_atomic_store(&window->should_close, true);
        } break;

        case WM_SIZE: {
            i32 new_width = LOWORD(l_param);
            i32 new_height = HIWORD(l_param);
            if (window->width != new_width || window->height != new_height) {
                i32_atomic_store(&window->width, new_width);
                i32_atomic_store(&window->height, new_height);
                i32_atomic_store(&window->resized, true);
            }
        } break;

        case WM_MOUSEMOVE: {
            i32 mouse_x = LOWORD(l_param);
            i32 mouse_y = HIWORD(l_param);

            i32_atomic_store(&window->mouse_x, mouse_x);
            i32_atomic_store(&window->mouse_y, mouse_y);
        } break;

        case WM_LBUTTONDOWN: {
            i32_atomic_store(&window->mouse_buttons[GUI_MOUSE_BUTTON_LEFT].currently_down, 1);
        } break;

        case WM_LBUTTONUP: {
            i32_atomic_store(&window->mouse_buttons[GUI_MOUSE_BUTTON_LEFT].currently_down, 0);
        } break;

        case WM_RBUTTONDOWN: {
            i32_atomic_store(&window->mouse_buttons[GUI_MOUSE_BUTTON_RIGHT].currently_down, 1);
        } break;

        case WM_RBUTTONUP: {
            i32_atomic_store(&window->mouse_buttons[GUI_MOUSE_BUTTON_RIGHT].currently_down, 0);
        } break;
        }
    }

    return DefWindowProcW(window_handle, message_type, w_param, l_param);
}

#define WINDOW_CREATE_MESSAGE WM_USER

typedef struct {
    GuiWindow *window;
    WCHAR *title;
    isize width;
    isize height;
    HANDLE window_created_event;
} WindowCreateData;

static DWORD event_loop_procedure(LPVOID param) {
    // Call a function from user32 (PeekMessageW in this case) to set up a message queue.
    // https://stackoverflow.com/questions/22428066/postthreadmessage-doesnt-work#comment34127566_22433275
    {
        MSG message;
        PeekMessageW(&message, NULL, 0, 0, PM_NOREMOVE);

        HANDLE event_loop_created_event = (HANDLE)param;
        SetEvent(event_loop_created_event);
    }

    MSG message;
    while (GetMessageW(&message, NULL, 0, 0)) {
        UINT message_type = message.message;

        switch (message_type) {
        case WINDOW_CREATE_MESSAGE: {
            WindowCreateData *message_data = (WindowCreateData *)message.lParam;

            RECT window_rect = {0, 0, (int)message_data->width, (int)message_data->height};
            AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);
            HWND window_handle = CreateWindowExW(
                0,
                WINDOW_CLASS_NAME,
                message_data->title,
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                window_rect.right - window_rect.left,
                window_rect.bottom - window_rect.top,
                NULL,
                NULL,
                GetModuleHandleW(NULL),
                NULL
            );
            if (window_handle != NULL) {
                SetWindowLongPtrW(window_handle, GWLP_USERDATA, (LONG_PTR)message_data->window);
            }
            message_data->window->handle = window_handle;

            SetEvent(message_data->window_created_event);
        } break;

        default: {
            TranslateMessage(&message);

            // Don't dispatch WM_PAINT messages. For some reason dispatching them causes a visible
            // rendering slowdown on Linux under Wine.
            //
            // (For the record, we can't get rid of the window procedure and just handle all events
            // right here, because some of the events (e.g. WM_SIZE) are sent directly to the window
            // procedure.)
            if (message.message != WM_PAINT) {
                DispatchMessage(&message);
            }
        } break;
        }
    }

    return 0;
}

GuiWindow *gui_window_create(int width, int height, char const *title, void *arena) {
    assert(width < GUI_MAX_WINDOW_WIDTH && height < GUI_MAX_WINDOW_HEIGHT);

    GuiWindow *window = arena_alloc(arena, sizeof(GuiWindow));
    memset(window, 0, (size_t)sizeof(GuiWindow));
    window->width = (i32)width;
    window->height = (i32)height;
    window->should_close = false;
    window->resized = false;

    // Create a separate "event loop" thread responsible for creating windows and handling their
    // events. If we were to handle messages on the same thread where we render frames, message
    // processing during resizing and moving the window would interrupt rendering new frames until
    // resizing/moving is finished.
    //
    // Similar to this:
    // https://github.com/cmuratori/dtc
    // except that I'm not using Windows message queue to communicate between the threads.

    static struct {
        HANDLE handle;
        DWORD id;
    } event_loop = {0};

    if (event_loop.handle == NULL) {
        HANDLE event_loop_created_event = CreateEventW(NULL, true, false, NULL);

        event_loop.handle = CreateThread(
            NULL,
            0,
            event_loop_procedure,
            event_loop_created_event,
            0,
            &event_loop.id
        );
        if (event_loop.handle == NULL) {
            goto fail;
        }

        if (WaitForSingleObject(event_loop_created_event, 5000) != WAIT_OBJECT_0) {
            goto fail;
        }
    }

    // Create a window class, if it does not exist yet.
    {
        WNDCLASSW window_class_data = {
            .style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
            .lpfnWndProc = window_procedure,
            .hInstance = GetModuleHandleW(NULL),
            .hCursor = LoadCursor(NULL, IDC_ARROW),
            .lpszClassName = WINDOW_CLASS_NAME,
        };
        ATOM window_class = RegisterClassW(&window_class_data);
        if (window_class == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            goto fail;
        }
    }

    // Create a window by sending WINDOW_CREATE_MESSAGE to the event loop thread.
    {
        int wide_title_size = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
        if (wide_title_size == 0) {
            goto fail;
        }
        WCHAR *wide_title = arena_alloc(arena, wide_title_size);
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wide_title, wide_title_size);

        WindowCreateData window_data = {
            .window = window,
            .title = wide_title,
            .width = width,
            .height = height,
            .window_created_event = CreateEventW(NULL, true, false, NULL),
        };
        if (!PostThreadMessageW(event_loop.id, WINDOW_CREATE_MESSAGE, 0, (LPARAM)&window_data)) {
            goto fail;
        }
        if (WaitForSingleObject(window_data.window_created_event, 5000) != WAIT_OBJECT_0) {
            goto fail;
        }
        if (window->handle == NULL) {
            goto fail;
        }

        SetWindowLongPtrW(window->handle, GWLP_USERDATA, (LONG_PTR)window);
        window->device_context = GetDC(window->handle);
    }

    // Create a bitmap.
    {
        u32 *bitmap_data = NULL;

        BITMAPINFO bitmap_info = {
            .bmiHeader = {
                .biSize = sizeof(BITMAPINFOHEADER),
                .biWidth = (int)width,
                .biHeight = (int)-height,
                .biPlanes = 1,
                .biBitCount = 32,
                .biCompression = BI_RGB,
            },
        };
        HBITMAP handle = CreateDIBSection(
            window->device_context,
            &bitmap_info,
            DIB_RGB_COLORS,
            (void **)&bitmap_data,
            NULL,
            0
        );

        HDC device_context = CreateCompatibleDC(window->device_context);
        HBITMAP default_bitmap = (HBITMAP)SelectObject(device_context, handle);
        DeleteObject(default_bitmap);

        window->bitmap.window = window;
        window->bitmap.handle = handle;
        window->bitmap.device_context = device_context;
        window->bitmap.width = width;
        window->bitmap.height = height;
        window->bitmap.data = bitmap_data;
    }

    // Show the window only once everything is initialized.
    ShowWindow(window->handle, SW_SHOWNORMAL);

    // Start the timer.
    QueryPerformanceFrequency(&window->timer.frequency);
    LARGE_INTEGER created_time;
    QueryPerformanceCounter(&created_time);
    window->timer.created_time = created_time;
    window->timer.last_update_time = created_time;
    window->timer.last_frame_time = 0.0;

    fps_counter_init(&window->fps_counter);
    window->target_fps = 0.0;

    return window;

fail:
    if (window->handle != NULL) {
        DestroyWindow(window->handle);
    }
    return NULL;
}

void gui_window_destroy(GuiWindow *window) {
    DeleteDC(window->bitmap.device_context);
    DeleteObject(window->bitmap.data);
    DeleteObject(window->bitmap.handle);

    DestroyWindow(window->handle);
}

bool gui_window_resized(GuiWindow const *window) {
    return i32_atomic_exchange((volatile i32 *)&window->resized, false);
}

void gui_window_size(GuiWindow const *window, int *width, int *height) {
    *width = i32_atomic_load((volatile i32 *)&window->width);
    *height = i32_atomic_load((volatile i32 *)&window->height);
}

void gui_mouse_position(GuiWindow const *window, int *mouse_x, int *mouse_y) {
    *mouse_x = i32_atomic_load((volatile i32 *)&window->mouse_x);
    *mouse_y = i32_atomic_load((volatile i32 *)&window->mouse_y);
}

bool gui_mouse_button_down(GuiWindow const *window, int mouse_button) {
    assert(0 <= mouse_button && mouse_button < countof(window->mouse_buttons));

    return window->mouse_buttons[mouse_button].is_down == 1;
}

bool gui_mouse_button_was_pressed(GuiWindow const *window, int mouse_button) {
    assert(0 <= mouse_button && mouse_button < countof(window->mouse_buttons));

    return
        window->mouse_buttons[mouse_button].was_down == 0 &&
        window->mouse_buttons[mouse_button].is_down == 1;
}

bool gui_mouse_button_was_released(GuiWindow const *window, int mouse_button) {
    assert(0 <= mouse_button && mouse_button < countof(window->mouse_buttons));

    return
        window->mouse_buttons[mouse_button].was_down == 1 &&
        window->mouse_buttons[mouse_button].is_down == 0;
}

bool gui_window_should_close(GuiWindow *window) {
    LARGE_INTEGER current_time;
    QueryPerformanceCounter(&current_time);
    LONGLONG elapsed_ticks = current_time.QuadPart - window->timer.last_update_time.QuadPart;

    if (window->target_fps != 0.0) {
        f64 millis_per_tick = 1e3 / (f64)window->timer.frequency.QuadPart;
        f64 elapsed_millis = (f64)elapsed_ticks * millis_per_tick;
        f64 target_millis = 1e3 / window->target_fps;

        if (elapsed_millis < target_millis) {
            DWORD sleep_millis = (DWORD)(target_millis - elapsed_millis);
            Sleep(sleep_millis);
            QueryPerformanceCounter(&current_time);
            elapsed_ticks = current_time.QuadPart - window->timer.last_update_time.QuadPart;
        }
    }

    window->timer.last_update_time = current_time;

    f64 micros_per_tick = 1e6 / (f64)window->timer.frequency.QuadPart;
    f64 elapsed_micros = (f64)elapsed_ticks * micros_per_tick;
    window->timer.last_frame_time = elapsed_micros / 1e6;

    f64 nanos_per_tick = 1e9 / (f64)window->timer.frequency.QuadPart;
    f64 elapsed_nanos = (f64)elapsed_ticks * nanos_per_tick;
    fps_counter_add_frame(&window->fps_counter, (i64)elapsed_nanos);

    for (isize i = 0; i < countof(window->mouse_buttons); i += 1) {
        window->mouse_buttons[i].was_down = window->mouse_buttons[i].is_down;
        window->mouse_buttons[i].is_down = i32_atomic_load(
            &window->mouse_buttons[i].currently_down
        );
    }

    return i32_atomic_load(&window->should_close);
}

double gui_window_time(GuiWindow const *window) {
    LARGE_INTEGER current_time;
    QueryPerformanceCounter(&current_time);
    LONGLONG elapsed_ticks = current_time.QuadPart - window->timer.created_time.QuadPart;
    LONGLONG elapsed_us = elapsed_ticks * 1000000 / window->timer.frequency.QuadPart;
    return (f64)elapsed_us / 1e6;
}

double gui_window_frame_time(GuiWindow const *window) {
    return window->timer.last_frame_time;
}

double gui_window_fps(GuiWindow const *window) {
    return fps_counter_average(&window->fps_counter);
}

void gui_window_set_target_fps(GuiWindow *window, double target_fps) {
    // Set Windows scheduler granularity to 1ms.
    if (timeBeginPeriod(1) == TIMERR_NOERROR) {
        window->target_fps = target_fps;
    }
}

GuiBitmap *gui_window_bitmap(GuiWindow *window) {
    return &window->bitmap;
}

uint32_t *gui_bitmap_data(GuiBitmap const *bitmap) {
    return bitmap->data;
}

bool gui_bitmap_resize(GuiBitmap *bitmap, int width, int height) {
    if (bitmap->width != width || bitmap->height != height) {
        u32 *bitmap_data;
        BITMAPINFO bitmap_info = {
            .bmiHeader = {
                .biSize = sizeof(BITMAPINFOHEADER),
                .biWidth = width,
                .biHeight = -height,
                .biPlanes = 1,
                .biBitCount = 32,
                .biCompression = BI_RGB,
            },
        };
        HBITMAP new_handle = CreateDIBSection(
            bitmap->window->device_context,
            &bitmap_info,
            DIB_RGB_COLORS,
            (void **)&bitmap_data,
            NULL,
            0
        );
        HDC new_device_context = CreateCompatibleDC(bitmap->window->device_context);

        HBITMAP default_bitmap = (HBITMAP)SelectObject(new_device_context, new_handle);
        DeleteObject(default_bitmap);
        DeleteDC(bitmap->device_context);
        DeleteObject(bitmap->data);
        DeleteObject(bitmap->handle);

        bitmap->device_context = new_device_context;
        bitmap->handle = new_handle;
        bitmap->data = bitmap_data;
        bitmap->width = width;
        bitmap->height = height;
    }

    return true;
}

void gui_bitmap_size(GuiBitmap const *bitmap, int *width, int *height) {
    *width = bitmap->width;
    *height = bitmap->height;
}

void gui_bitmap_render(GuiBitmap *bitmap) {
    BitBlt(
        bitmap->window->device_context,
        0,
        0,
        (int)bitmap->width,
        (int)bitmap->height,
        bitmap->device_context,
        0,
        0,
        SRCCOPY
    );
}

#endif // _WIN32
