/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file android_bridge.h Calls from the game into the Android activity (os/android/project). */

#ifndef ANDROID_BRIDGE_H
#define ANDROID_BRIDGE_H

void AndroidApplySystemBars(bool show_navigation_bar);
void AndroidSetGameRunning(bool running);
void AndroidSetResumeSave(const std::string &path);
int AndroidGetTopChromeY(int left, int right, int gap);

#endif /* ANDROID_BRIDGE_H */
