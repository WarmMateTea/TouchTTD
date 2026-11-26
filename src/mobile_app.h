/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file mobile_app.h App life cycle on phones: going to the background, coming back, being killed. */

#ifndef MOBILE_APP_H
#define MOBILE_APP_H

void MobileAppWillEnterBackground();
void MobileAppDidEnterForeground();
void MobileAppTick();

#endif /* MOBILE_APP_H */
