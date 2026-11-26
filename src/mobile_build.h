/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file mobile_build.h Review-before-building for construction tools in the mobile layout. */

#ifndef MOBILE_BUILD_H
#define MOBILE_BUILD_H

#include "window_type.h"
#include "tile_type.h"
#include "economy_type.h"
#include "gfx_type.h"
#include "strings_type.h"
#include "station_type.h"
#include "direction_type.h"

struct Viewport;

/** States of a construction in the mobile layout; see the round 2 design, section 5. */
enum class MobileBuildState : uint8_t {
	Idle, ///< A tool may be selected, nothing is pending.
	Dragging, ///< A selection is being dragged (or one end of a pending one adjusted).
	Pending, ///< The selection is frozen on the map, waiting for Build or Cancel.
	Done, ///< The construction succeeded; Undo is offered for a while.
	BridgePicking, ///< A bridge span is selected and the bridge list is open.
};

/** What confirming the pending selection does. */
enum class MobileBuildKind : uint8_t {
	Build, ///< Build something.
	Remove, ///< Remove track (or another piece of infrastructure).
	Demolish, ///< Clear an area.
	Convert, ///< Convert rail to another type.
};

/** What to do with a click on the main viewport. */
enum class MobileViewportClick : uint8_t {
	NotHandled, ///< Handle the click as usual.
	Consumed, ///< The click was used.
	Pan, ///< Don't place anything; let the click start panning the map.
};

bool MobileBuildReviewWanted(const Window *w);
void MobileBuildOnRelease(Window *w);
void MobileBuildOnSizingUpdate(Window *w);
MobileViewportClick MobileBuildOnViewportClick(const Viewport &vp, int x, int y);
void MobileBuildOnPlacementReset();
bool MobileBuildTunnelTap(Window *w, TileIndex tile);
bool MobileBuildDepotTap(Window *w, TileIndex tile);
void MobileBuildRotateDepot();
TileIndex GetMobileBuildPendingTile();
bool GetMobileBuildDepotArrow(TileIndex tile, DiagDirection &dir);
bool GetMobilePendingStationSize(uint &x, uint &y);
bool GetMobilePendingStationSize();
void MobileBuildStationResize(uint x, uint y);
void MobileBuildStationResize();
const std::vector<StationID> &GetMobileStationJoinCandidates();
StationID GetMobileStationJoinShown();
void MobileBuildSetStationJoin(StationID station);
bool GetMobileStationJoin(StationID &join, bool &adjacent);
void MobileBuildBridgeResult(bool success, Money cost, TileIndex start, const std::string &error);
void MobileBuildBridgeListClosed();
bool MobileBuildMeasurementTooltipsAllowed();

void MobileBuildConfirm();
void MobileBuildCancel();
void MobileBuildUndo();
bool MobileBuildBack();
void MobileBuildSetToolHint(StringID hint);
void MobileBuildReestimate();

MobileBuildState GetMobileBuildState();
MobileBuildKind GetMobileBuildKind();
std::string GetMobileBuildStatus(TextColour &colour);
bool MobileBuildCanConfirm();
bool MobileBuildCanCancel();
bool MobileBuildHasUndo();
Money GetMobileBuildUndoCost();
bool IsMobileBuildHandleTile(TileIndex tile);
bool MobileBuildHandleError(const std::string &message);
void MobileBuildRevealPending();

#endif /* MOBILE_BUILD_H */
