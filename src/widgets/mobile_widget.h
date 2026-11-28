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
	WID_MD_GAP_LEFT, ///< Spacer filling the screen edge's inset on the left (landscape).
	WID_MD_GAP_TOP,  ///< Spacer filling the screen edge's inset at the top (landscape).
	WID_MD_GAP_BOTTOM, ///< Spacer filling the screen edge's inset at the bottom.
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
	WID_MM_QUIT_GAME,    ///< Quit the game.
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

/** Widgets of the mobile depot sheet (#MobileDepotWindow). */
enum MobileDepotWidgets : WidgetID {
	WID_MDEP_TITLE, ///< Title: the depot's name.
	WID_MDEP_CLOSE, ///< Close.
	WID_MDEP_LIST, ///< The vehicle chains in the depot.
	WID_MDEP_SCROLL, ///< Scrollbar of the list.
	WID_MDEP_NEW, ///< Build a new train.
	WID_MDEP_CLONE, ///< Clone a train.
};

/** Widgets of the mobile New train view (#MobileNewTrainWindow). */
enum MobileNewTrainWidgets : WidgetID {
	WID_MNT_BACK, ///< Back to the depot.
	WID_MNT_TITLE, ///< Title.
	WID_MNT_CLOSE, ///< Close.
	WID_MNT_STRIP, ///< The train being built, with a sell button per vehicle.
	WID_MNT_SUMMARY, ///< Summary line of the train being built.
	WID_MNT_TAB_ENGINES, ///< Tab: engines.
	WID_MNT_TAB_WAGONS, ///< Tab: wagons.
	WID_MNT_LIST, ///< List of engines or wagons to buy.
	WID_MNT_SCROLL, ///< Scrollbar of the list.
	WID_MNT_SPENT, ///< How much was spent.
	WID_MNT_NEXT, ///< Next: orders.
};

/** Widgets of the mobile train window (#MobileTrainWindow). */
enum MobileTrainWidgets : WidgetID {
	WID_MTR_TITLE, ///< Title: the train's name.
	WID_MTR_RENAME, ///< Rename.
	WID_MTR_CLOSE, ///< Close.
	WID_MTR_STATUS, ///< State and detail lines.
	WID_MTR_SEL_START, ///< Selection between Start and Stop.
	WID_MTR_START, ///< Start the train.
	WID_MTR_STOP, ///< Stop the train.
	WID_MTR_TAB_ORDERS, ///< Tab: orders.
	WID_MTR_TAB_DETAILS, ///< Tab: details.
	WID_MTR_SEL_CONTENT, ///< Selection between the orders and the details.
	WID_MTR_ORDERS, ///< List of orders.
	WID_MTR_ORDERS_SCROLL, ///< Scrollbar of the orders.
	WID_MTR_SEL_CHIPS, ///< Selection showing the suggested stations while adding stops.
	WID_MTR_CHIP_FIRST, ///< First suggested station; the others follow.
	WID_MTR_CHIP_LAST = WID_MTR_CHIP_FIRST + 2, ///< Last suggested station.
	WID_MTR_ADD_STOPS, ///< Start adding stops.
	WID_MTR_STRIP, ///< Details: the train, with a sell button per vehicle.
	WID_MTR_ADD_VEHICLES, ///< Details: add vehicles.
	WID_MTR_DETAILS, ///< Details: text.
	WID_MTR_SHOW_ON_MAP, ///< Show the train on the map.
	WID_MTR_DEPOT, ///< Send to depot.
	WID_MTR_CLONE, ///< Clone the train.
};

/** Widgets of the mobile train list (#MobileTrainListWindow). */
enum MobileTrainListWidgets : WidgetID {
	WID_MTL_SEL_TITLE, ///< Selection showing the title when the list is filtered to a station.
	WID_MTL_TITLE, ///< Title: "Trains calling at ...".
	WID_MTL_CLOSE, ///< Close (with the title).
	WID_MTL_SORT, ///< Sort order dropdown.
	WID_MTL_LIST, ///< The trains.
	WID_MTL_SCROLL, ///< Scrollbar of the list.
	WID_MTL_TOTAL, ///< Total profit this year.
};

/** Widgets of the mobile save and load list (#MobileSaveLoadWindow). */
enum MobileSaveLoadWidgets : WidgetID {
	WID_MSL_TITLE, ///< Title.
	WID_MSL_CLOSE, ///< Close (or back from the autosaves).
	WID_MSL_SEL_SAVE, ///< Selection showing the name field when saving.
	WID_MSL_NAME, ///< Name to save under.
	WID_MSL_SAVE, ///< Save.
	WID_MSL_LIST, ///< The saves.
	WID_MSL_SCROLL, ///< Scrollbar of the list.
	WID_MSL_SORT, ///< Sort order.
};

/** Widgets of the mobile New game screen (#MobileNewGameWindow). */
enum MobileNewGameWidgets : WidgetID {
	WID_MNG_BACK, ///< Back to the main menu.
	WID_MNG_TITLE, ///< Title.
	WID_MNG_START, ///< Start the game.
	WID_MNG_PRESET_FIRST, ///< First rules preset; Classic, Relaxed, Sandbox follow.
	WID_MNG_CLIMATE_FIRST = WID_MNG_PRESET_FIRST + 3, ///< First climate; four follow.
	WID_MNG_SIZE_FIRST = WID_MNG_CLIMATE_FIRST + 4, ///< First map size; three follow.
	WID_MNG_TOWNS_FIRST = WID_MNG_SIZE_FIRST + 3, ///< First number of towns; three follow.
	WID_MNG_INDUSTRIES_FIRST = WID_MNG_TOWNS_FIRST + 3, ///< First number of industries; three follow.
	WID_MNG_TERRAIN_FIRST = WID_MNG_INDUSTRIES_FIRST + 3, ///< First terrain; three follow.
	WID_MNG_YEAR_DOWN = WID_MNG_TERRAIN_FIRST + 3, ///< Start year earlier.
	WID_MNG_YEAR, ///< Start year.
	WID_MNG_YEAR_UP, ///< Start year later.
	WID_MNG_ADVANCED, ///< The desktop world generation window.
};

/** Widgets of the mobile settings (#MobileSettingsWindow). */
enum MobileSettingsWidgets : WidgetID {
	WID_MSET_BACK, ///< Back (or close).
	WID_MSET_TITLE, ///< Title.
	WID_MSET_SEARCH, ///< Search all settings.
	WID_MSET_SECTIONS, ///< The sections.
	WID_MSET_SEL_PANE, ///< Portrait: sections or settings.
	WID_MSET_LIST, ///< The settings.
	WID_MSET_SCROLL, ///< Scrollbar of the settings.
};

/** Widgets of the bar shown while adding stops (#MobileStopsBarWindow). */
enum MobileStopsBarWidgets : WidgetID {
	WID_MSB_STATUS, ///< Status line.
	WID_MSB_DONE, ///< Stop adding stops.
	WID_MSB_GAP_BOTTOM, ///< Spacer filling the screen edge's inset at the bottom.
};

/** Widgets of the mobile local authority (round 5 design, 10.1). */
enum MobileTownAuthorityWidgets : WidgetID {
	WID_MTA_SEL_BACK, ///< Selection showing Back when opened over the town window.
	WID_MTA_BACK, ///< Back to the town.
	WID_MTA_TITLE, ///< "Local authority - <town>".
	WID_MTA_CLOSE, ///< Close.
	WID_MTA_LIST, ///< Ratings, then the actions.
	WID_MTA_SCROLL, ///< Scrollbar of the list.
};

/** Widgets of the mobile subsidies and goals lists (round 5 design, 10.2 and 10.4). */
enum MobileWorldListWidgets : WidgetID {
	WID_MWL_LIST, ///< The rows.
	WID_MWL_SCROLL, ///< Scrollbar of the list.
};

/** Widgets of the mobile newspaper (round 5 design, 10.5). */
enum MobileNewspaperWidgets : WidgetID {
	WID_MNP_SEL_BACK, ///< Selection showing Back when opened from the history.
	WID_MNP_BACK, ///< Back to the history.
	WID_MNP_TITLE, ///< The date.
	WID_MNP_CLOSE, ///< Close.
	WID_MNP_TEXT, ///< The headline and text.
	WID_MNP_SEL_LOCATION, ///< Selection showing Show on map when the item has a place.
	WID_MNP_SHOW_ON_MAP, ///< Scroll the map there.
};

/** Widgets of the mobile message history (round 5 design, 10.5). */
enum MobileMessageHistoryWidgets : WidgetID {
	WID_MMH_TITLE, ///< Title.
	WID_MMH_CLOSE, ///< Close.
	WID_MMH_LIST, ///< The messages, newest first.
	WID_MMH_SCROLL, ///< Scrollbar of the list.
};

/** Widgets of the mobile industry window (round 5 design, 9.2). */
enum MobileIndustryWidgets : WidgetID {
	WID_MIV_SEL_BACK, ///< Selection showing Back when opened over the directory.
	WID_MIV_BACK, ///< Back to the directory.
	WID_MIV_TITLE, ///< The industry's name.
	WID_MIV_CLOSE, ///< Close.
	WID_MIV_INFO, ///< Produces, accepts, hint.
	WID_MIV_SHOW_ON_MAP, ///< Scroll the map there.
	WID_MIV_CHAIN, ///< The cargo chain (desktop window).
	WID_MIV_GRAPH, ///< The production graph (desktop window).
};

/** Widgets of the mobile industry directory (round 5 design, 9.1). */
enum MobileIndustryDirectoryWidgets : WidgetID {
	WID_MIND_PRODUCES, ///< Filter by produced cargo.
	WID_MIND_ACCEPTS, ///< Filter by accepted cargo.
	WID_MIND_SORT, ///< Sort order.
	WID_MIND_LIST, ///< The industries.
	WID_MIND_SCROLL, ///< Scrollbar of the list.
	WID_MIND_FUND, ///< Fund new industry.
};

/** Widgets of the mobile Fund new industry (round 5 design, 9.3). */
enum MobileBuildIndustryWidgets : WidgetID {
	WID_MBI_TITLE, ///< Title.
	WID_MBI_CLOSE, ///< Close.
	WID_MBI_LIST, ///< The industry types.
	WID_MBI_SCROLL, ///< Scrollbar of the list.
	WID_MBI_SEL_FOOTER, ///< Selection showing the review footer while placing.
	WID_MBI_STATUS, ///< What to do, the cost, or the error.
	WID_MBI_CANCEL, ///< Cancel the industry in review, or stop placing.
	WID_MBI_FUND, ///< Fund the industry in review.
};

/** Widgets of the mobile finances (round 5 design, batch 6). */
enum MobileFinancesWidgets : WidgetID {
	WID_MFIN_YEAR, ///< "<year> so far".
	WID_MFIN_COMPARE, ///< The year compared with.
	WID_MFIN_TABLE, ///< The groups, their categories and the profit.
	WID_MFIN_SCROLL, ///< Scrollbar of the table.
	WID_MFIN_LOAN, ///< Loan and bank balance.
	WID_MFIN_BORROW, ///< Borrow dropdown.
	WID_MFIN_REPAY, ///< Repay dropdown.
};

/** Widgets of the mobile refit window (round 5 design, 7.3). */
enum MobileRefitWidgets : WidgetID {
	WID_MRF_BACK, ///< Back to the window it was opened from.
	WID_MRF_TITLE, ///< "Refit <vehicle>".
	WID_MRF_CLOSE, ///< Close.
	WID_MRF_SEL_SCOPE, ///< Selection hiding the whole train / only this wagon choice (not a train, or a single vehicle).
	WID_MRF_WHOLE, ///< Refit the whole train.
	WID_MRF_ONLY_THIS, ///< Refit only this wagon.
	WID_MRF_LIST, ///< The cargoes.
	WID_MRF_SCROLL, ///< Scrollbar of the list.
	WID_MRF_COST, ///< Cost, or why it can't refit.
	WID_MRF_REFIT, ///< Refit.
};

#endif /* WIDGETS_MOBILE_WIDGET_H */
