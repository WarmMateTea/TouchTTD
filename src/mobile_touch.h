/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file mobile_touch.h Touch screen gestures for the mobile layout. */

#ifndef MOBILE_TOUCH_H
#define MOBILE_TOUCH_H

bool IsTouchInputActive();
void TouchDown(int64_t finger, int x, int y);
void TouchMove(int64_t finger, int x, int y);
void TouchUp(int64_t finger, int x, int y);
void TouchCancel();
void TouchTick();

#endif /* MOBILE_TOUCH_H */
