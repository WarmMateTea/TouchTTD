/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

package io.github.warmmatetea.touchttd;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
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
        Log.i(TAG, "applySystemBars(" + showNavigation + ")");
        showNavigationBar = showNavigation;
        runOnUiThread(this::applySystemBarsNow);
        /* SDL may still be applying its own window style; apply ours again after it. */
        View decor = getWindow() != null ? getWindow().getDecorView() : null;
        if (decor != null) decor.postDelayed(this::applySystemBarsNow, 300);
    }

    private void applySystemBarsNow() {
        Window window = getWindow();
        if (window == null) return;
        /* SDL treats every window as full screen and re-hides any system bar that appears; the bars are ours to manage. */
        mFullscreenModeActive = false;
        Log.i(TAG, "applying system bars, navigation bar shown: " + showNavigationBar);
        /* The game's dock and toolbars reach the screen edge under the navigation bar; let them show through it
         * instead of the black background (MIUI) or contrast scrim some systems give three-button navigation. */
        window.setNavigationBarColor(Color.TRANSPARENT);
        if (Build.VERSION.SDK_INT >= 29) window.setNavigationBarContrastEnforced(false);
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = window.getInsetsController();
            if (controller == null) return;
            /* Not BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE: bars swiped in that way overlay the app without telling it,
             * so the game could not move out of their way. With the default behaviour a swipe brings the bars back
             * for real; the app is told, and hides them again after a moment (see #onSystemBarsChanged). */
            controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_DEFAULT);
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
    public void onSystemUiVisibilityChange(int visibility) {
        /* Keep SDL from hiding the bars again two seconds after they appear. */
        mFullscreenModeActive = false;
        super.onSystemUiVisibilityChange(visibility);
    }

    /** How long bars brought back by a swipe stay before they are hidden again. */
    private static final long REHIDE_DELAY_MS = 3000;
    private final Runnable rehideBars = this::applySystemBarsNow;

    /**
     * Called by {@link OpenTTDSurface} when the window insets change: when a swipe brought back bars that should be
     * hidden, hide them again after a moment, as Android does for its own transient bars.
     * @param insets The new insets.
     */
    void onSystemBarsChanged(WindowInsets insets) {
        if (Build.VERSION.SDK_INT < 30 || getWindow() == null) return;
        boolean unwanted = insets.isVisible(WindowInsets.Type.statusBars())
                || (!showNavigationBar && insets.isVisible(WindowInsets.Type.navigationBars()));
        View decor = getWindow().getDecorView();
        decor.removeCallbacks(rehideBars);
        if (unwanted) decor.postDelayed(rehideBars, REHIDE_DELAY_MS);
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

    /** Preferences: which save resumes the game on a fresh start, and whether notifications were asked for. */
    private static final String PREFS = "touchttd";
    private static final String PREF_RESUME_SAVE = "resume_save";
    private static final String PREF_ASKED_NOTIFICATIONS = "asked_notifications";

    /**
     * Called by the game (os/android/android_bridge.cpp) when a game starts or ends: run the "game in progress"
     * foreground service while one does.
     * @param running Whether a game is running.
     */
    public void setGameRunning(boolean running) {
        Log.i(TAG, "setGameRunning(" + running + ")");
        runOnUiThread(() -> {
            Intent service = new Intent(this, GameService.class);
            if (!running) {
                stopService(service);
                return;
            }
            askForNotificationsOnce();
            try {
                startForegroundService(service);
            } catch (RuntimeException e) {
                /* E.g. not allowed while the app is in the background; the next game start tries again. */
                Log.w(TAG, "Could not start the game service", e);
            }
        });
    }

    /** Android 13+ hides a foreground service's notification without this permission; ask the first time a game starts. */
    private void askForNotificationsOnce() {
        if (Build.VERSION.SDK_INT < 33) return;
        if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) return;
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        if (prefs.getBoolean(PREF_ASKED_NOTIFICATIONS, false)) return;
        prefs.edit().putBoolean(PREF_ASKED_NOTIFICATIONS, true).apply();
        requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 1);
    }

    /**
     * Called by the game when it saved itself on leaving the app (path set), or when the player left the game on
     * purpose (empty). On the next fresh start, a set save is loaded instead of showing the main menu.
     * @param path Full path of the save, or empty.
     */
    public void setResumeSave(String path) {
        Log.i(TAG, "setResumeSave(" + path + ")");
        /* commit(), not apply(): the process may be killed soon after. */
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(PREF_RESUME_SAVE, path).commit();
    }

    @Override
    protected void onDestroy() {
        stopService(new Intent(this, GameService.class));
        super.onDestroy();
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }

    @Override
    protected String[] getArguments() {
        File config = new File(getFilesDir(), "openttd.cfg");
        /* Android killed the app during a game: continue from the save made when the player left the app. Used once;
         * the game sets it again the next time it's saved on leaving. */
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        String resume = prefs.getString(PREF_RESUME_SAVE, "");
        prefs.edit().remove(PREF_RESUME_SAVE).commit();
        if (!resume.isEmpty() && new File(resume).isFile()) {
            Log.i(TAG, "Resuming the game from " + resume);
            return new String[] {"-c", config.getAbsolutePath(), "-g", resume};
        }
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
