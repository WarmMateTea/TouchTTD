/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rail_gui.h Functions/types etc. related to the rail GUI. */

#ifndef RAIL_GUI_H
#define RAIL_GUI_H

#include "rail_type.h"
#include "dropdown_type.h"
#include "direction_type.h"
#include "tile_type.h"

struct Window *ShowBuildRailToolbar(RailType railtype);
void ReinitGuiAfterToggleElrail(bool disable);
void ResetSignalVariant(int32_t = 0);
void InitializeSignalGui();
DropDownList GetRailTypeDropDownList(bool for_replacement = false, bool all_option = false);
void SetDefaultRailGui();

RailType GetCurrentRailType();
bool IsRailRemoveModeActive();
void SetRailRemoveMode(bool remove);
void PostRailTunnel(TileIndex tile);
void GetRailStationFootprintSize(uint &x, uint &y);
void GetRailStationTracksAndLength(uint x, uint y, uint &tracks, uint &length);
void SetRailStationSizeFromFootprint(uint x, uint y);
DiagDirection GetRailDepotDirection();
void PostRailDepot(TileIndex tile, DiagDirection dir);
struct Window *GetMobileRailToolbar();
bool IsMobileRailToolbarLandscape(const struct Window *w);
void RecreateMobileRailToolbar();

/** Settings for which signals are shown by the signal GUI. */
enum SignalGUISettings : uint8_t {
	SIGNAL_GUI_PATH = 0, ///< Show path signals only.
	SIGNAL_GUI_ALL = 1,  ///< Show all signals, including block and presignals.
};

/** Settings for which signals are cycled through by control-clicking on the signal with the signal tool. */
enum SignalCycleSettings : uint8_t {
	SIGNAL_CYCLE_GROUP = 0, ///< Cycle through current signal group (block or path) only.
	SIGNAL_CYCLE_ALL = 1,   ///< Cycle through all signals visible to the player.
};

#endif /* RAIL_GUI_H */
