/*
 * Hammerhead/Sailfish Waydroid GPU-blit Proof A.
 *
 * This is intentionally only a visible EGL-window proof. It does not import
 * Android framebuffer buffers yet.
 */
#pragma once

struct display;

void waydroid_run_egl_window_proof_if_requested(struct display *display);
