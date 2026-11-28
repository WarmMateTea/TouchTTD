/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

package io.github.warmmatetea.touchttd;

import android.content.Context;
import android.graphics.Insets;
import android.os.Build;
import android.view.View;
import android.view.WindowInsets;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/**
 * SDL's surface, except for what counts as the unsafe edge of the screen. SDL also counts the areas
 * reserved for system gestures, which stay reserved even while the bars are hidden; the game's layout
 * only needs to keep clear of bars that are actually showing (also when swiped in for a moment) and of
 * the camera cutout. So the layout follows the bars as they appear and disappear.
 */
public class OpenTTDSurface extends SDLSurface {
    /** Implemented in the game (src/os/android/android_bridge.cpp). */
    private static native void nativeSetTopInsets(int statusBar, int[] cutoutRects);

    public OpenTTDSurface(Context context) {
        super(context);
    }

    @Override
    public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
        /* Keep SDL's handling of the on-screen keyboard. */
        WindowInsets result = super.onApplyWindowInsets(v, insets);
        if (Build.VERSION.SDK_INT >= 30) {
            /* A bar swiped in while hidden ("transient") overlays the app and takes no space, so getInsets()
             * reports nothing for it. The layout should still move out of its way, so count every bar that
             * is visible at its full size. */
            Insets edges = insets.getInsets(WindowInsets.Type.displayCutout());
            for (int type : new int[] {WindowInsets.Type.statusBars(), WindowInsets.Type.captionBar()}) {
                if (insets.isVisible(type)) edges = Insets.max(edges, insets.getInsetsIgnoringVisibility(type));
            }
            if (insets.isVisible(WindowInsets.Type.navigationBars())) {
                /* Only the part of the navigation bar that takes taps: the buttons of three-button navigation, but
                 * not the gesture handle, where a tap still reaches the app (only the edge swipe is the system's).
                 * The game's chrome reaches the screen edge anyway, under the bar; only its buttons keep clear.
                 * The navigation bar is never at the top, so the top of the tappable inset is the status bar's. */
                Insets tappable = insets.getInsetsIgnoringVisibility(WindowInsets.Type.tappableElement());
                edges = Insets.max(edges, Insets.of(tappable.left, 0, tappable.right, tappable.bottom));
            }
            /* The floating chrome at the top (status pill, map button) goes against the cutout's actual shape, not the
             * whole inset band: tell the game where the cutout is, and how tall a visible status bar is. */
            int statusBar = insets.isVisible(WindowInsets.Type.statusBars()) ? insets.getInsetsIgnoringVisibility(WindowInsets.Type.statusBars()).top : 0;
            android.view.DisplayCutout cutout = insets.getDisplayCutout();
            java.util.List<android.graphics.Rect> rects = cutout != null ? cutout.getBoundingRects() : java.util.Collections.emptyList();
            int[] flat = new int[rects.size() * 4];
            for (int i = 0; i < rects.size(); i++) {
                android.graphics.Rect r = rects.get(i);
                flat[i * 4] = r.left;
                flat[i * 4 + 1] = r.top;
                flat[i * 4 + 2] = r.right;
                flat[i * 4 + 3] = r.bottom;
            }
            try {
                nativeSetTopInsets(statusBar, flat);
            } catch (UnsatisfiedLinkError e) {
                /* The game library isn't loaded yet; the next insets change brings it. */
            }
            SDLActivity.onNativeInsetsChanged(edges.left, edges.right, edges.top, edges.bottom);
            if (getContext() instanceof OpenTTDActivity) ((OpenTTDActivity) getContext()).onSystemBarsChanged(insets);
        }
        return result;
    }
}
