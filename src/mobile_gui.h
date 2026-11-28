/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file mobile_gui.h Functions of the touch-first mobile layout. */

#ifndef MOBILE_GUI_H
#define MOBILE_GUI_H

#include "window_type.h"
#include "core/geometry_type.hpp"
#include "gfx_type.h"
#include "strings_type.h"

/** Left and right inset of titles and list rows in sheets, in GUI pixels at 1x (round 3 design, section 9.1). */
static constexpr int MOBILE_TEXT_INSET = 8;

bool IsMobileLayout();
bool IsMobileMenus();
bool IsMobileShellWindow(WindowClass cls);
void ShowMobileVitalWindows();
void RelocateMobileWindows();
bool MobileConsumeOutsideTap(const Window *w);
bool MobileHandleBack();
void MobileDockOverlayInSheet(Window *w);
Point GetMobileToolbarAlignedWindowPosition(int window_width);
void ApplyMobileLayoutSetting();
void ApplyMobileSystemBars();
Rect GetMobileVisibleMapRect();
Rect GetMobileEdgeGaps();
Point GetMobileMapCentreOffset();
void MobileShowInSheet(Window *w);
void MobileShowInfoWindow(Window *w);
bool IsMobileSheetOpen();
void CloseMobileSheetPublic();
void ShowMobileResumeBanner();
void RelocateMobileMainMenu();
void ShowMobileBuildFlyoutForToolbar(Window *toolbar, WidgetID widget);
bool ShowMobileError(const std::string &summary, const std::string &detail);
void DrawMobileIconButton(const Rect &r, SpriteID sprite, StringID label, bool lowered, bool disabled, FontSize font_size = FontSize::Small);
void DrawMobileTitle(const Rect &r, std::string_view text);

void ShowMobileNewsBanner();
void CloseMobileNewsBanner();
bool IsMobileNewsBannerShown();

#endif /* MOBILE_GUI_H */
