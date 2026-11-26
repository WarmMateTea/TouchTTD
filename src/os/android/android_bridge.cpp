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
#include <android/log.h>
#include <SDL3/SDL_system.h>

#include "../../safeguards.h"

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
