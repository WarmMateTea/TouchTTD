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
            for (int type : new int[] {WindowInsets.Type.statusBars(), WindowInsets.Type.navigationBars(), WindowInsets.Type.captionBar()}) {
                if (insets.isVisible(type)) edges = Insets.max(edges, insets.getInsetsIgnoringVisibility(type));
            }
            SDLActivity.onNativeInsetsChanged(edges.left, edges.right, edges.top, edges.bottom);
            if (getContext() instanceof OpenTTDActivity) ((OpenTTDActivity) getContext()).onSystemBarsChanged(insets);
        }
        return result;
    }
}
