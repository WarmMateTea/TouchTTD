/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

package io.github.warmmatetea.touchttd;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/**
 * Starts OpenTTD through SDL. Before SDL starts the native code, the game data bundled in the APK's
 * assets (language files, base sets, script compatibility files) is copied to the app's internal
 * storage, which is where the native side runs from (see os/unix/unix_main.cpp).
 */
public class OpenTTDActivity extends SDLActivity {
    private static final String TAG = "TouchTTD";
    /** Asset directories copied to internal storage. */
    private static final String[] DATA_DIRS = {"lang", "baseset", "ai", "game"};
    /** Marker storing the app version whose data was extracted last. */
    private static final String MARKER = ".extracted-version";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        try {
            extractDataIfNeeded();
            writeDefaultConfigIfMissing();
        } catch (IOException e) {
            Log.e(TAG, "Could not prepare game data", e);
        }
        super.onCreate(savedInstanceState);
    }

    /** Whether the navigation bar stays on screen; set by the game through #applySystemBars. */
    private volatile boolean showNavigationBar = true;

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new OpenTTDSurface(context);
    }

    /**
     * Called by the game (os/android/android_bridge.cpp): hide the status bar, and show or hide the navigation bar.
     * Hidden bars can still be swiped in for a moment.
     * @param showNavigation Whether the navigation bar stays on screen.
     */
    public void applySystemBars(boolean showNavigation) {
        showNavigationBar = showNavigation;
        runOnUiThread(this::applySystemBarsNow);
        /* SDL may still be applying its own window style; apply ours again after it. */
        View decor = getWindow() != null ? getWindow().getDecorView() : null;
        if (decor != null) decor.postDelayed(this::applySystemBarsNow, 300);
    }

    private void applySystemBarsNow() {
        Window window = getWindow();
        if (window == null) return;
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = window.getInsetsController();
            if (controller == null) return;
            controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            controller.hide(WindowInsets.Type.statusBars());
            if (showNavigationBar) {
                controller.show(WindowInsets.Type.navigationBars());
            } else {
                controller.hide(WindowInsets.Type.navigationBars());
            }
        } else {
            int flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
            if (!showNavigationBar) flags |= View.SYSTEM_UI_FLAG_HIDE_NAVIGATION;
            window.getDecorView().setSystemUiVisibility(flags);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        /* SDL resets the bars when the window regains focus; put ours back. */
        if (hasFocus) {
            applySystemBarsNow();
            getWindow().getDecorView().postDelayed(this::applySystemBarsNow, 300);
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }

    @Override
    protected String[] getArguments() {
        File config = new File(getFilesDir(), "openttd.cfg");
        return new String[] {"-c", config.getAbsolutePath()};
    }

    /** Copy the bundled data when the app was installed or updated since the last extraction. */
    private void extractDataIfNeeded() throws IOException {
        File root = getFilesDir();
        File marker = new File(root, MARKER);
        String version = currentVersion();
        if (marker.exists() && version.equals(readFile(marker))) return;

        long start = System.currentTimeMillis();
        AssetManager assets = getAssets();
        for (String dir : DATA_DIRS) copyAssetDir(assets, dir, new File(root, dir));
        writeFile(marker, version);
        Log.i(TAG, "Extracted game data in " + (System.currentTimeMillis() - start) + " ms");
    }

    /**
     * First start: turn on the mobile layout, a phone-sized interface and dragging the map with one finger.
     * Afterwards the file belongs to the game, which saves the player's own settings in it.
     */
    private void writeDefaultConfigIfMissing() throws IOException {
        File config = new File(getFilesDir(), "openttd.cfg");
        if (config.exists()) return;
        writeFile(config,
            "[misc]\n" +
            "gui_scale = 300\n" +
            "[gui]\n" +
            "mobile_layout = true\n" +
            "scroll_mode = 3\n" +
            "[network]\n" +
            "participate_survey = no\n");
    }

    private String currentVersion() {
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            return info.getLongVersionCode() + "-" + info.lastUpdateTime;
        } catch (Exception e) {
            return "unknown";
        }
    }

    private static void copyAssetDir(AssetManager assets, String path, File target) throws IOException {
        String[] children = assets.list(path);
        if (children == null || children.length == 0) {
            /* A file (list() returns nothing for files). */
            target.getParentFile().mkdirs();
            try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
                byte[] buffer = new byte[64 * 1024];
                int n;
                while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
            }
            return;
        }
        target.mkdirs();
        for (String child : children) copyAssetDir(assets, path + "/" + child, new File(target, child));
    }

    private static String readFile(File file) throws IOException {
        try (InputStream in = new java.io.FileInputStream(file)) {
            return new String(in.readAllBytes(), StandardCharsets.UTF_8);
        }
    }

    private static void writeFile(File file, String text) throws IOException {
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }
}
