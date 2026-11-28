/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file android_bridge.cpp Calls from the game into the Android activity (os/android/project). */

#include "../../stdafx.h"
#include "android_bridge.h"

#include <jni.h>
#include <string>
#include <android/log.h>
#include <SDL3/SDL_system.h>

#include "../../core/geometry_type.hpp"
#include <mutex>
#include <vector>

#include "../../safeguards.h"

static std::mutex _top_insets_mutex; ///< Guards the top insets, written by the UI thread.
static int _status_bar_height = 0; ///< Height of the status bar when it is visible, else 0.
static std::vector<Rect> _cutout_rects; ///< The display cutouts, in screen pixels.
static bool _top_insets_reported = false; ///< The activity reported its insets.

/**
 * Called by the activity's surface (OpenTTDSurface.java) when the insets change: the visible status bar and the
 * display cutout's rectangles, for placing the floating chrome at the top of the screen.
 */
extern "C" JNIEXPORT void JNICALL Java_io_github_warmmatetea_touchttd_OpenTTDSurface_nativeSetTopInsets(JNIEnv *env, jclass, jint status_bar, jintArray rects)
{
	std::vector<Rect> cutouts;
	jsize n = env->GetArrayLength(rects);
	std::vector<jint> values(n);
	if (n > 0) env->GetIntArrayRegion(rects, 0, n, values.data());
	for (jsize i = 0; i + 3 < n; i += 4) cutouts.push_back({values[i], values[i + 1], values[i + 2] - 1, values[i + 3] - 1});
	std::lock_guard<std::mutex> lock(_top_insets_mutex);
	_status_bar_height = status_bar;
	_cutout_rects = std::move(cutouts);
	_top_insets_reported = true;
}

/**
 * Where the floating chrome at the top may start, for a horizontal span of the screen.
 * @param left Left edge of the span.
 * @param right Right edge of the span.
 * @param gap Gap to keep from the top edge, a visible status bar and the cutout.
 * @return The top, in screen pixels; -1 when the activity hasn't reported its insets.
 */
int AndroidGetTopChromeY(int left, int right, int gap)
{
	std::lock_guard<std::mutex> lock(_top_insets_mutex);
	if (!_top_insets_reported) return -1;
	int y = std::max(gap, _status_bar_height + (_status_bar_height > 0 ? gap : 0));
	for (const Rect &r : _cutout_rects) {
		if (r.right < left || r.left > right || r.top > y + 200) continue;
		y = std::max(y, r.bottom + 1 + gap);
	}
	return y;
}

/**
 * Tell the activity which system bars to show: the status bar is always hidden, the navigation bar
 * (back, home and recent apps) is shown or hidden. Hidden bars can be swiped in for a moment.
 * @param show_navigation_bar Whether the navigation bar stays on screen.
 */
void AndroidApplySystemBars(bool show_navigation_bar)
{
	JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
	__android_log_print(ANDROID_LOG_INFO, "TouchTTD", "AndroidApplySystemBars(%d) env=%p activity=%p", show_navigation_bar, (void *)env, (void *)activity);
	if (env == nullptr || activity == nullptr) return;

	jclass cls = env->GetObjectClass(activity);
	jmethodID method = env->GetMethodID(cls, "applySystemBars", "(Z)V");
	if (method != nullptr) {
		env->CallVoidMethod(activity, method, static_cast<jboolean>(show_navigation_bar));
	} else {
		__android_log_print(ANDROID_LOG_WARN, "TouchTTD", "applySystemBars not found on the activity");
		env->ExceptionClear();
	}
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(activity);
}

/**
 * Tell the activity whether a game is running. While one is, it keeps a foreground service with a notification, which
 * makes Android much less likely to kill the app in the background.
 * @param running Whether a game is running.
 */
void AndroidSetGameRunning(bool running)
{
	JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
	__android_log_print(ANDROID_LOG_INFO, "TouchTTD", "AndroidSetGameRunning(%d)", running);
	if (env == nullptr || activity == nullptr) return;

	jclass cls = env->GetObjectClass(activity);
	jmethodID method = env->GetMethodID(cls, "setGameRunning", "(Z)V");
	if (method != nullptr) {
		env->CallVoidMethod(activity, method, static_cast<jboolean>(running));
	} else {
		env->ExceptionClear();
	}
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(activity);
}

/**
 * Tell the activity which save resumes the game when the app is started afresh (after Android killed it).
 * @param path Full path of the save, or empty to start at the main menu.
 */
void AndroidSetResumeSave(const std::string &path)
{
	JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
	__android_log_print(ANDROID_LOG_INFO, "TouchTTD", "AndroidSetResumeSave(%s)", path.c_str());
	if (env == nullptr || activity == nullptr) return;

	jclass cls = env->GetObjectClass(activity);
	jmethodID method = env->GetMethodID(cls, "setResumeSave", "(Ljava/lang/String;)V");
	if (method != nullptr) {
		jstring jpath = env->NewStringUTF(path.c_str());
		env->CallVoidMethod(activity, method, jpath);
		env->DeleteLocalRef(jpath);
	} else {
		env->ExceptionClear();
	}
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(activity);
}
