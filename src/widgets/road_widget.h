/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file road_widget.h Types related to the road widgets. */

#ifndef WIDGETS_ROAD_WIDGET_H
#define WIDGETS_ROAD_WIDGET_H

/** Widgets of the #BuildRoadToolbarWindow class. */
enum RoadToolbarWidgets : WidgetID {
	/* Name starts with RO instead of R, because of collision with RailToolbarWidgets */
	WID_ROT_CAPTION,        ///< Caption of the window
	WID_ROT_ROAD_X,         ///< Build road in x-direction.
	WID_ROT_ROAD_Y,         ///< Build road in y-direction.
	WID_ROT_AUTOROAD,       ///< Autorail.
	WID_ROT_DEMOLISH,       ///< Demolish.
	WID_ROT_DEPOT,          ///< Build depot.
	WID_ROT_BUILD_WAYPOINT, ///< Build waypoint.
	WID_ROT_BUS_STATION,    ///< Build bus station.
	WID_ROT_TRUCK_STATION,  ///< Build truck station.
	WID_ROT_ONE_WAY,        ///< Build one-way road.
	WID_ROT_BUILD_BRIDGE,   ///< Build bridge.
	WID_ROT_BUILD_TUNNEL,   ///< Build tunnel.
	WID_ROT_REMOVE,         ///< Remove road.
	WID_ROT_CONVERT_ROAD,   ///< Convert road.
	WID_ROT_CATEGORY,       ///< Mobile layout: road (or tram) type and construction category flyout.
	WID_ROT_MORE,           ///< Mobile layout: flyout with the less used tools.
	WID_ROT_STATUS,         ///< Mobile layout: status line.
	WID_ROT_CANCEL,         ///< Mobile layout: cancel the selection in review.
	WID_ROT_CONFIRM,        ///< Mobile layout: build (or remove, demolish, convert, change) the selection in review.
	WID_ROT_UNDO,           ///< Mobile layout: undo the last construction.
	WID_ROT_EXIT,           ///< Mobile layout: leave build mode (portrait, where the dock is hidden).
	WID_ROT_SEL_TOOLS,      ///< Mobile layout: selection hiding the tools while a picker fills the sheet.
	WID_ROT_SEL_CONFIRM,    ///< Mobile layout: selection between confirm, undo and Show tools.
	WID_ROT_SEL_HIDDEN,     ///< Mobile layout: never shown; holds the tools that are in the More flyout.
	WID_ROT_SEL_ROTATE,     ///< Mobile layout: selection showing the Rotate button while a depot is in review.
	WID_ROT_ROTATE,         ///< Mobile layout: turn the entrance of the depot in review.
	WID_ROT_GAP_BOTTOM,     ///< Mobile layout: spacer filling the screen edge's inset at the bottom.
	WID_ROT_SEL_CANCEL,     ///< Mobile layout: selection hiding Cancel while the tools are hidden by a sheet.
	WID_ROT_SHOW_TOOLS,     ///< Mobile layout: closes the sheet that hides the tools.
};

/** Widgets of the #BuildRoadDepotWindow class. */
enum BuildRoadDepotWidgets : WidgetID {
	/* Name starts with BRO instead of BR, because of collision with BuildRailDepotWidgets */
	WID_BROD_CAPTION,   ///< Caption of the window.
	WID_BROD_DEPOT_NE,  ///< Depot with NE entry.
	WID_BROD_DEPOT_SE,  ///< Depot with SE entry.
	WID_BROD_DEPOT_SW,  ///< Depot with SW entry.
	WID_BROD_DEPOT_NW,  ///< Depot with NW entry.
};

/** Widgets of the #BuildRoadStationWindow class. */
enum BuildRoadStationWidgets : WidgetID {
	/* Name starts with BRO instead of BR, because of collision with BuildRailStationWidgets */
	WID_BROS_CAPTION,                ///< Caption of the window.
	WID_BROS_STATION_NE,             ///< Terminal station with NE entry.
	WID_BROS_STATION_SE,             ///< Terminal station with SE entry.
	WID_BROS_STATION_SW,             ///< Terminal station with SW entry.
	WID_BROS_STATION_NW,             ///< Terminal station with NW entry.
	WID_BROS_STATION_X,              ///< Drive-through station in x-direction.
	WID_BROS_STATION_Y,              ///< Drive-through station in y-direction.
	WID_BROS_LT_OFF,                 ///< Turn off area highlight.
	WID_BROS_LT_ON,                  ///< Turn on area highlight.
	WID_BROS_ACCEPTANCE,             ///< Station acceptance info.
	WID_BROS_AVAILABLE_ORIENTATIONS, ///< Selection for selecting 6 or 2 orientations.
	WID_BROS_TITLE,                  ///< Mobile layout: title of the picker.
	WID_BROS_CLOSE,                  ///< Mobile layout: close, which drops the stop tool.
	WID_BROS_SEL_KIND,               ///< Mobile layout: selection hiding the drive-through/bay choice (trams).
	WID_BROS_DRIVE_THROUGH,          ///< Mobile layout: drive-through stops.
	WID_BROS_BAY,                    ///< Mobile layout: bays.
	WID_BROS_SEL_FACING,             ///< Mobile layout: the facing of a bay, or "follows the road".
	WID_BROS_COVERAGE_TOGGLE,        ///< Mobile layout: show the catchment area, on or off.
	WID_BROS_SEL_JOIN,               ///< Mobile layout: selection hiding the join dropdown when there is nothing to join.
	WID_BROS_JOIN,                   ///< Mobile layout: which station the stop joins.
};

#endif /* WIDGETS_ROAD_WIDGET_H */
