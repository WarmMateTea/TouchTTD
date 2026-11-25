/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file mobile_widget.h Types related to the widgets of the touch-first mobile layout. */

#ifndef WIDGETS_MOBILE_WIDGET_H
#define WIDGETS_MOBILE_WIDGET_H

/** Widgets of the #MobileStatusWindow class. */
enum MobileStatusWidgets : WidgetID {
	WID_MS_PAUSE, ///< Pause toggle.
	WID_MS_FFWD,  ///< Fast forward toggle.
	WID_MS_INFO,  ///< Date and company money; opens the finances.
};

/** Widgets of the #MobileDockWindow class. */
enum MobileDockWidgets : WidgetID {
	WID_MD_MENU,     ///< Opens the menu flyout.
	WID_MD_COMPANY,  ///< Opens the company hub.
	WID_MD_WORLD,    ///< Opens the world hub.
	WID_MD_VEHICLES, ///< Opens the vehicles hub.
	WID_MD_BUILD,    ///< Reopens the last construction toolbar or opens the build flyout.
};

/** Widgets of the #MobileBuildMenuWindow class. */
enum MobileBuildMenuWidgets : WidgetID {
	WID_MB_RAIL,      ///< Rail construction.
	WID_MB_ROAD,      ///< Road construction.
	WID_MB_TRAM,      ///< Tram construction.
	WID_MB_WATER,     ///< Waterways construction.
	WID_MB_AIR,       ///< Airport construction.
	WID_MB_LANDSCAPE, ///< Landscaping.
	WID_MB_SEL_RAILTYPES, ///< Selection showing the rail types only when opened from the rail toolbar.
	WID_MB_RAILTYPES, ///< List of rail types.
};

/** Widgets of the #MobileErrorWindow class. */
enum MobileErrorWidgets : WidgetID {
	WID_MERR_TEXT,    ///< The error; tapping it dismisses it.
	WID_MERR_DISMISS, ///< Dismiss the error.
};

/** Widgets of the #MobileToolCancelWindow class. */
enum MobileToolCancelWidgets : WidgetID {
	WID_MTC_CANCEL, ///< Stop placing with the selected tool.
};

/** Widgets of the #MobileMenuWindow class. */
enum MobileMenuWidgets : WidgetID {
	WID_MM_MESSAGES,     ///< Message history.
	WID_MM_GAME_OPTIONS, ///< Game options.
	WID_MM_SETTINGS,     ///< Advanced settings.
	WID_MM_SCRIPTS,      ///< AI and game script settings.
	WID_MM_NEWGRF,       ///< NewGRF settings.
	WID_MM_TRANSPARENCY, ///< Transparency options.
	WID_MM_MAP_TOOLS,    ///< Dropdown with the extra map tools.
	WID_MM_SAVE,         ///< Save game.
	WID_MM_LOAD,         ///< Load game.
	WID_MM_MUSIC,        ///< Music and sound.
	WID_MM_HELP,         ///< Help and manuals.
	WID_MM_ABOUT,        ///< About OpenTTD.
	WID_MM_QUIT,         ///< Quit to the main menu.
};

/** Widgets of the #MobileHubTabsWindow class. */
enum MobileHubTabsWidgets : WidgetID {
	WID_MHUB_BACKGROUND, ///< Background panel, coloured like the content.
	WID_MHUB_CAPTION,    ///< Name of the hub.
	WID_MHUB_SEL_EXPAND, ///< Selection showing the expand button in portrait only.
	WID_MHUB_EXPAND,     ///< Toggle between half and full height sheet.
	WID_MHUB_CLOSE,      ///< Close the hub.
	WID_MHUB_SEL_TAB5,   ///< Selection hiding the fifth tab for hubs with four tabs.
	WID_MHUB_TAB_FIRST,  ///< First tab button; the other tabs follow.
	WID_MHUB_TAB_LAST = WID_MHUB_TAB_FIRST + 4, ///< Last possible tab button.
};

/** Widgets of the #MobileNewsWindow class. */
enum MobileNewsWidgets : WidgetID {
	WID_MN_TEXT,    ///< Scrolling news text; opens the news item.
	WID_MN_DISMISS, ///< Hide the banner.
};

/** Widgets of the #MobileMinimapWindow class. */
enum MobileMinimapWidgets : WidgetID {
	WID_MMB_BUTTON, ///< Opens the map of the world.
};

#endif /* WIDGETS_MOBILE_WIDGET_H */
