/*
 * Hammerhead/Sailfish Waydroid GPU-blit Proof A.
 *
 * Create a separate fullscreen Wayland wl_egl_window, then clear it to a
 * solid color through EGL/GLES.  If this becomes visible, HWC can create a
 * normal EGL Wayland output surface and the next step can import the Android
 * framebuffer target as a texture.
 */

#include "egl-window-proof.h"
#include "wayland-hwc.h"

#include <cutils/properties.h>
#include <log/log.h>

#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>

#include <GLES2/gl2.h>

#include <wayland-client.h>
#include <wayland-egl.h>

namespace {

struct proof_state {
    bool attempted = false;
    bool shown = false;

    wl_surface *surface = nullptr;
    wl_shell_surface *shell_surface = nullptr;
    wl_egl_window *egl_window = nullptr;

    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLContext egl_context = EGL_NO_CONTEXT;
    EGLSurface egl_surface = EGL_NO_SURFACE;
};

proof_state g_proof;

const char *egl_error_name(EGLint err) {
    switch (err) {
        case EGL_SUCCESS: return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
        case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
        case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
        case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
        case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
        case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
        case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
        default: return "EGL_UNKNOWN_ERROR";
    }
}

void proof_shell_ping(void *, wl_shell_surface *shell_surface, uint32_t serial) {
    wl_shell_surface_pong(shell_surface, serial);
}

void proof_shell_configure(void *, wl_shell_surface *, uint32_t, int32_t, int32_t) {
}

void proof_shell_popup_done(void *, wl_shell_surface *) {
}

const wl_shell_surface_listener proof_shell_listener = {
    proof_shell_ping,
    proof_shell_configure,
    proof_shell_popup_done,
};

bool choose_window_config(EGLDisplay egl_display, EGLConfig *out_config) {
    const EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLint num = 0;
    if (!eglChooseConfig(egl_display, attrs, out_config, 1, &num) || num < 1) {
        ALOGE("Waydroid GPU blit Proof A: eglChooseConfig failed: %s",
              egl_error_name(eglGetError()));
        return false;
    }

    return true;
}

EGLDisplay init_egl_display(display *dpy) {
    const EGLNativeDisplayType candidates[] = {
        reinterpret_cast<EGLNativeDisplayType>(dpy->display),
        EGL_DEFAULT_DISPLAY,
    };

    const char *names[] = {
        "wl_display",
        "EGL_DEFAULT_DISPLAY",
    };

    for (size_t i = 0; i < 2; ++i) {
        EGLDisplay egl_display = eglGetDisplay(candidates[i]);
        if (egl_display == EGL_NO_DISPLAY) {
            ALOGE("Waydroid GPU blit Proof A: eglGetDisplay(%s) returned no display: %s",
                  names[i], egl_error_name(eglGetError()));
            continue;
        }

        EGLint major = 0;
        EGLint minor = 0;
        if (eglInitialize(egl_display, &major, &minor)) {
            ALOGE("Waydroid GPU blit Proof A: eglInitialize(%s) OK, version=%d.%d",
                  names[i], major, minor);
            return egl_display;
        }

        ALOGE("Waydroid GPU blit Proof A: eglInitialize(%s) failed: %s",
              names[i], egl_error_name(eglGetError()));
    }

    return EGL_NO_DISPLAY;
}

int positive_or_fallback(int value, int fallback) {
    return value > 0 ? value : fallback;
}

} // namespace

void waydroid_run_egl_window_proof_if_requested(display *dpy) {
    if (!property_get_bool("persist.waydroid.egl_window_proof", false)) {
        return;
    }

    if (g_proof.attempted) {
        return;
    }
    g_proof.attempted = true;

    if (!dpy) {
        ALOGE("Waydroid GPU blit Proof A: no display");
        return;
    }
    if (!dpy->compositor) {
        ALOGE("Waydroid GPU blit Proof A: no wl_compositor");
        return;
    }
    if (!dpy->shell) {
        ALOGE("Waydroid GPU blit Proof A: no wl_shell");
        return;
    }

    const int width = positive_or_fallback(dpy->outputWidth,
                      positive_or_fallback(dpy->full_width,
                      positive_or_fallback(dpy->req_width, 1080)));
    const int height = positive_or_fallback(dpy->outputHeight,
                       positive_or_fallback(dpy->full_height,
                       positive_or_fallback(dpy->req_height, 1920)));

    ALOGE("Waydroid GPU blit Proof A: creating wl_egl_window %dx%d", width, height);

    g_proof.surface = wl_compositor_create_surface(dpy->compositor);
    if (!g_proof.surface) {
        ALOGE("Waydroid GPU blit Proof A: wl_compositor_create_surface failed");
        return;
    }

    g_proof.shell_surface = wl_shell_get_shell_surface(dpy->shell, g_proof.surface);
    if (!g_proof.shell_surface) {
        ALOGE("Waydroid GPU blit Proof A: wl_shell_get_shell_surface failed");
        return;
    }

    wl_shell_surface_add_listener(g_proof.shell_surface, &proof_shell_listener, nullptr);
    wl_shell_surface_set_fullscreen(g_proof.shell_surface,
                                    WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT,
                                    0,
                                    dpy->output);
    wl_surface_commit(g_proof.surface);
    wl_display_flush(dpy->display);

    g_proof.egl_window = wl_egl_window_create(g_proof.surface, width, height);
    if (!g_proof.egl_window) {
        ALOGE("Waydroid GPU blit Proof A: wl_egl_window_create failed");
        return;
    }

    g_proof.egl_display = init_egl_display(dpy);
    if (g_proof.egl_display == EGL_NO_DISPLAY) {
        ALOGE("Waydroid GPU blit Proof A: no usable EGLDisplay");
        return;
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        ALOGE("Waydroid GPU blit Proof A: eglBindAPI failed: %s",
              egl_error_name(eglGetError()));
        return;
    }

    EGLConfig config = nullptr;
    if (!choose_window_config(g_proof.egl_display, &config)) {
        return;
    }

    const EGLint ctx_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    g_proof.egl_context = eglCreateContext(g_proof.egl_display,
                                           config,
                                           EGL_NO_CONTEXT,
                                           ctx_attrs);
    if (g_proof.egl_context == EGL_NO_CONTEXT) {
        ALOGE("Waydroid GPU blit Proof A: eglCreateContext failed: %s",
              egl_error_name(eglGetError()));
        return;
    }

    g_proof.egl_surface = eglCreateWindowSurface(
            g_proof.egl_display,
            config,
            reinterpret_cast<EGLNativeWindowType>(g_proof.egl_window),
            nullptr);

    if (g_proof.egl_surface == EGL_NO_SURFACE) {
        ALOGE("Waydroid GPU blit Proof A: eglCreateWindowSurface failed: %s",
              egl_error_name(eglGetError()));
        return;
    }

    if (!eglMakeCurrent(g_proof.egl_display,
                        g_proof.egl_surface,
                        g_proof.egl_surface,
                        g_proof.egl_context)) {
        ALOGE("Waydroid GPU blit Proof A: eglMakeCurrent failed: %s",
              egl_error_name(eglGetError()));
        return;
    }

    glViewport(0, 0, width, height);

    /*
     * Loud green. If this appears, Proof A passed and a real GPU-blit output
     * surface is viable.
     */
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    if (!eglSwapBuffers(g_proof.egl_display, g_proof.egl_surface)) {
        ALOGE("Waydroid GPU blit Proof A: eglSwapBuffers failed: %s",
              egl_error_name(eglGetError()));
        return;
    }

    wl_display_flush(dpy->display);

    g_proof.shown = true;
    ALOGE("Waydroid GPU blit Proof A: solid-color EGL window submitted");
}
