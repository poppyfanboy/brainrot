#include "gui.h"

#include <assert.h> // assert
#include <stdlib.h> // abort

#define sizeof(expr) (isize)sizeof(expr)

#define ARENA_ALIGNMENT 16

void *gui_arena_alloc(GuiArena *arena, isize size) {
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

    struct {
        struct timespec created_time;
        struct timespec last_update_time;
        f64 last_frame_delta;
    } timer;
};

GuiWindow *gui_window_create(
    isize width,
    isize height,
    char const *title,
    GuiArena *arena
) {
    assert(width < GUI_MAX_WINDOW_WIDTH && height < GUI_MAX_WINDOW_HEIGHT);

    GuiWindow *window = gui_arena_alloc(arena, sizeof(GuiWindow));
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
            .event_mask = StructureNotifyMask,
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
    window->timer.last_frame_delta = 0.0;

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
    if (event->type == ConfigureNotify) {
        if (
            window->width != event->xconfigure.width ||
            window->height != event->xconfigure.height
        ) {
            window->width = event->xconfigure.width;
            window->height = event->xconfigure.height;
            window->resized = true;
        }
    } else if (event->type == ClientMessage) {
        if ((Atom)event->xclient.data.l[0] == window->atom.delete_window) {
            window->should_close = true;
        }
    } else if (event->type == window->event.shm_completion) {
        window->bitmap.available = true;
    }
}

bool gui_window_resized(GuiWindow const *window) {
    return window->resized;
}

void gui_window_size(GuiWindow const *window, isize *width, isize *height) {
    *width = window->width;
    *height = window->height;
}

bool gui_window_should_close(GuiWindow *window) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    window->timer.last_frame_delta =
        (f64)(current_time.tv_sec - window->timer.last_update_time.tv_sec) +
        (f64)(current_time.tv_nsec - window->timer.last_update_time.tv_nsec) / 1e9;
    window->timer.last_update_time = current_time;

    window->resized = false;

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

u32 *gui_bitmap_data(GuiBitmap const *bitmap) {
    assert(bitmap->available);

    return (u32 *)bitmap->shared_segment.shmaddr;
}

void gui_bitmap_size(GuiBitmap const *bitmap, isize *width, isize *height) {
    *width = bitmap->width;
    *height = bitmap->height;
}

bool gui_bitmap_resize(GuiBitmap *bitmap, isize width, isize height) {
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

f64 gui_window_time(GuiWindow const *window) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    return
        (f64)(current_time.tv_sec - window->timer.created_time.tv_sec) +
        (f64)(current_time.tv_nsec - window->timer.created_time.tv_nsec) / 1e9;
}

f64 gui_window_frame_time(GuiWindow const *window) {
    return window->timer.last_frame_delta;
}

void gui_window_destroy(GuiWindow *window) {
    gui_bitmap_destroy(window->display, &window->bitmap);
    XDestroyWindow(window->display, window->handle);
    XCloseDisplay(window->display);
}

#endif // __linux__

#ifdef _WIN32

#include <stddef.h> // NULL
#include <string.h> // memset

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#define GUI_WINDOW_CLASS_NAME L"GUI_WINDOW_CLASS_NAME"

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

    isize width;
    isize height;
    bool resized;
    GuiBitmap bitmap;
    bool should_close;

    struct {
        LARGE_INTEGER frequency;
        LARGE_INTEGER created_time;
        LARGE_INTEGER last_update_time;
        f64 last_frame_delta;
    } timer;
};

static LRESULT CALLBACK gui_window_procedure(
    HWND window_handle,
    UINT message_type,
    WPARAM w_param,
    LPARAM l_param
) {
    GuiWindow *window = (GuiWindow *)GetWindowLongPtrW(window_handle, GWLP_USERDATA);

    if (window != NULL) {
        switch (message_type) {
        case WM_DESTROY: {
            window->should_close = true;
        } break;

        case WM_SIZE: {
            u32 new_width = LOWORD(l_param);
            u32 new_height = HIWORD(l_param);
            if (window->width != new_width || window->height != new_height) {
                window->width = new_width;
                window->height = new_height;
                window->resized = true;
            }
        } break;
        }
    }

    return DefWindowProcW(window_handle, message_type, w_param, l_param);
}

GuiWindow *gui_window_create(
    isize width,
    isize height,
    char const *title,
    GuiArena *arena
) {
    assert(width < GUI_MAX_WINDOW_WIDTH && height < GUI_MAX_WINDOW_HEIGHT);

    GuiWindow *window = gui_arena_alloc(arena, sizeof(GuiWindow));
    memset(window, 0, (size_t)sizeof(GuiWindow));
    window->width = width;
    window->height = height;
    window->should_close = false;
    window->resized = false;

    // Create window class and window.
    {
        WNDCLASSW window_class_data = {
            .style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
            .lpfnWndProc = gui_window_procedure,
            .hInstance = GetModuleHandleW(NULL),
            .hCursor = LoadCursor(NULL, IDC_ARROW),
            .lpszClassName = GUI_WINDOW_CLASS_NAME,
        };
        ATOM window_class = RegisterClassW(&window_class_data);
        if (window_class == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            goto fail;
        }

        int wide_title_size = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
        if (wide_title_size == 0) {
            goto fail;
        }
        WCHAR *wide_title = gui_arena_alloc(arena, wide_title_size);
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wide_title, wide_title_size);

        HWND handle = CreateWindowExW(
            0,
            GUI_WINDOW_CLASS_NAME,
            wide_title,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            (int)width,
            (int)height,
            NULL,
            NULL,
            GetModuleHandleW(NULL),
            NULL
        );
        if (handle == NULL) {
            goto fail;
        }
        SetWindowLongPtrW(handle, GWLP_USERDATA, (LONG_PTR)window);
        HDC device_context = GetDC(handle);

        window->handle = handle;
        window->device_context = device_context;
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
    window->timer.last_frame_delta = 0.0;

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
    return window->resized;
}

void gui_window_size(GuiWindow const *window, isize *width, isize *height) {
    *width = window->width;
    *height = window->height;
}

bool gui_window_should_close(GuiWindow *window) {
    LARGE_INTEGER current_time;
    QueryPerformanceCounter(&current_time);
    LONGLONG elapsed_ticks = current_time.QuadPart - window->timer.last_update_time.QuadPart;
    LONGLONG elapsed_us = elapsed_ticks * 1000000 / window->timer.frequency.QuadPart;
    window->timer.last_frame_delta = (f64)elapsed_us / 1e6;
    window->timer.last_update_time = current_time;

    window->resized = false;

    MSG message;
    while (!window->should_close && PeekMessageW(&message, window->handle, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            window->should_close = true;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return window->should_close;
}

f64 gui_window_time(GuiWindow const *window) {
    LARGE_INTEGER current_time;
    QueryPerformanceCounter(&current_time);
    LONGLONG elapsed_ticks = current_time.QuadPart - window->timer.created_time.QuadPart;
    LONGLONG elapsed_us = elapsed_ticks * 1000000 / window->timer.frequency.QuadPart;
    return (f64)elapsed_us / 1e6;
}

f64 gui_window_frame_time(GuiWindow const *window) {
    return window->timer.last_frame_delta;
}

GuiBitmap *gui_window_bitmap(GuiWindow *window) {
    return &window->bitmap;
}

u32 *gui_bitmap_data(GuiBitmap const *bitmap) {
    return bitmap->data;
}

bool gui_bitmap_resize(GuiBitmap *bitmap, isize width, isize height) {
    if (bitmap->width != width || bitmap->height != height) {
        u32 *bitmap_data;
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

        bitmap->device_context = new_device_context;
        bitmap->handle = new_handle;
        bitmap->data = bitmap_data;
        bitmap->width = width;
        bitmap->height = height;
    }

    return true;
}

void gui_bitmap_size(GuiBitmap const *bitmap, isize *width, isize *height) {
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
