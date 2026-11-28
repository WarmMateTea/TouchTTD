/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file road_gui.cpp GUI for building roads. */

#include "stdafx.h"
#include "gui.h"
#include "window_gui.h"
#include "station_gui.h"
#include "terraform_gui.h"
#include "viewport_func.h"
#include "command_func.h"
#include "road_cmd.h"
#include "station_func.h"
#include "waypoint_func.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "company_func.h"
#include "tunnelbridge.h"
#include "tunnelbridge_map.h"
#include "tilehighlight_func.h"
#include "company_base.h"
#include "hotkeys.h"
#include "road_gui.h"
#include "toolbar_gui.h"
#include "zoom_func.h"
#include "dropdown_type.h"
#include "dropdown_func.h"
#include "station_base.h"
#include "station_func.h"
#include "waypoint_base.h"
#include "strings_func.h"
#include "core/geometry_func.hpp"
#include "station_cmd.h"
#include "tunnelbridge_cmd.h"
#include "waypoint_cmd.h"
#include "road_cmd.h"
#include "newgrf_badge_gui.h"
#include "newgrf_roadstop.h"
#include "picker_gui.h"
#include "timer/timer_game_calendar.h"

#include "widgets/road_widget.h"
#include "widgets/rail_widget.h"
#include "mobile_gui.h"
#include "mobile_build.h"

#include "table/strings.h"

#include "safeguards.h"

static void ShowRVStationPicker(Window *parent, RoadStopType rs);
static void ShowRoadDepotPicker(Window *parent);
static void ShowBuildRoadWaypointPicker(Window *parent);

static bool _remove_button_clicked;
static bool _one_way_button_clicked;

static Axis _place_road_dir;
static bool _place_road_start_half_x;
static bool _place_road_start_half_y;
static bool _place_road_end_half;

static RoadType _cur_roadtype;

static DiagDirection _road_depot_orientation;

struct RoadWaypointPickerSelection {
	RoadStopClassID sel_class; ///< Selected road waypoint class.
	uint16_t sel_type; ///< Selected road waypoint type within the class.
};
static RoadWaypointPickerSelection _waypoint_gui; ///< Settings of the road waypoint picker.

struct RoadStopPickerSelection {
	RoadStopClassID sel_class; ///< Selected road stop class.
	uint16_t sel_type; ///< Selected road stop type within the class.
	DiagDirection orientation; ///< Selected orientation of the road stop.
};
static RoadStopPickerSelection _roadstop_gui;

static bool IsRoadStopEverAvailable(const RoadStopSpec *spec, StationType type)
{
	if (spec == nullptr) return true;

	if (spec->flags.Test(RoadStopSpecFlag::RoadOnly) && !RoadTypeIsRoad(_cur_roadtype)) return false;
	if (spec->flags.Test(RoadStopSpecFlag::TramOnly) && !RoadTypeIsTram(_cur_roadtype)) return false;

	switch (spec->stop_type) {
		case ROADSTOPTYPE_ALL: return true;
		case ROADSTOPTYPE_PASSENGER: return type == StationType::Bus;
		case ROADSTOPTYPE_FREIGHT: return type == StationType::Truck;
		default: NOT_REACHED();
	}
}

/**
 * Check whether a road stop type can be built.
 * @param spec The specification, or \c nullptr.
 * @param type The type of road stop that is being considered.
 * @return true if building is allowed.
 */
static bool IsRoadStopAvailable(const RoadStopSpec *spec, StationType type)
{
	if (spec == nullptr) return true;
	if (!IsRoadStopEverAvailable(spec, type)) return false;

	if (!spec->callback_mask.Test(RoadStopCallbackMask::Avail)) return true;

	uint16_t cb_res = GetRoadStopCallback(CBID_STATION_AVAILABILITY, 0, 0, spec, nullptr, INVALID_TILE, _cur_roadtype, type, 0);
	if (cb_res == CALLBACK_FAILED) return true;

	return Convert8bitBooleanCallback(spec->grf_prop.grffile, CBID_STATION_AVAILABILITY, cb_res);
}

void CcPlaySound_CONSTRUCTION_OTHER(Commands, const CommandCost &result, TileIndex tile)
{
	if (result.Succeeded() && _settings_client.sound.confirm) SndPlayTileFx(SND_1F_CONSTRUCTION_OTHER, tile);
}

/**
 * Callback to start placing a bridge.
 * @param tile Start tile of the bridge.
 * @param w The window to call back to when overbuilding a bridge.
 */
static void PlaceRoad_Bridge(TileIndex tile, Window *w)
{
	if (IsBridgeTile(tile)) {
		TileIndex other_tile = GetOtherTunnelBridgeEnd(tile);
		Point pt = {0, 0};
		w->OnPlaceMouseUp(VPM_X_OR_Y, DDSP_BUILD_BRIDGE, pt, other_tile, tile);
	} else {
		VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_BUILD_BRIDGE);
	}
}

/**
 * Callback executed after a build road tunnel command has been called.
 *
 * @param result Whether the build succeeded.
 * @param start_tile Starting tile of the tunnel.
 */
void CcBuildRoadTunnel(Commands, const CommandCost &result, TileIndex start_tile)
{
	if (result.Succeeded()) {
		if (_settings_client.sound.confirm) SndPlayTileFx(SND_1F_CONSTRUCTION_OTHER, start_tile);
		if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();

		DiagDirection start_direction = ReverseDiagDir(GetTunnelBridgeDirection(start_tile));
		ConnectRoadToStructure(start_tile, start_direction);

		TileIndex end_tile = GetOtherTunnelBridgeEnd(start_tile);
		DiagDirection end_direction = ReverseDiagDir(GetTunnelBridgeDirection(end_tile));
		ConnectRoadToStructure(end_tile, end_direction);
	} else {
		SetRedErrorSquare(_build_tunnel_endtile);
	}
}

/**
 * If required, connects a new structure to an existing road or tram by building the missing roadbit.
 * @param tile Tile containing the structure to connect.
 * @param direction Direction to check.
 */
void ConnectRoadToStructure(TileIndex tile, DiagDirection direction)
{
	tile += TileOffsByDiagDir(direction);
	/* if there is a roadpiece just outside of the station entrance, build a connecting route */
	if (IsNormalRoadTile(tile)) {
		if (GetRoadBits(tile, GetRoadTramType(_cur_roadtype)).Any()) {
			Command<Commands::BuildRoad>::Post(tile, DiagDirToRoadBits(ReverseDiagDir(direction)), _cur_roadtype, {}, TownID::Invalid());
		}
	}
}

void CcRoadDepot(Commands, const CommandCost &result, TileIndex tile, RoadType, DiagDirection dir)
{
	if (result.Failed()) return;

	if (_settings_client.sound.confirm) SndPlayTileFx(SND_1F_CONSTRUCTION_OTHER, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
	ConnectRoadToStructure(tile, dir);
}

/**
 * Command callback for building road stops.
 * @param result Result of the build road stop command.
 * @param tile Start tile.
 * @param width Width of the road stop.
 * @param length Length of the road stop.
 * @param is_drive_through False for normal stops, true for drive-through.
 * @param dir Entrance direction (#DiagDirection) for normal stops. Converted to the axis for drive-through stops.
 * @param spec_class Road stop spec class.
 * @param spec_index Road stop spec index.
 * @see CmdBuildRoadStop
 */
void CcRoadStop(Commands, const CommandCost &result, TileIndex tile, uint8_t width, uint8_t length, RoadStopType, bool is_drive_through,
		DiagDirection dir, RoadType, RoadStopClassID spec_class, uint16_t spec_index, StationID, bool)
{
	if (result.Failed()) return;

	if (_settings_client.sound.confirm) SndPlayTileFx(SND_1F_CONSTRUCTION_OTHER, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();

	bool connect_to_road = true;
	if (spec_class.base() < RoadStopClass::GetClassCount() && spec_index < RoadStopClass::Get(spec_class)->GetSpecCount()) {
		const RoadStopSpec *roadstopspec = RoadStopClass::Get(spec_class)->GetSpec(spec_index);
		if (roadstopspec != nullptr && roadstopspec->flags.Test(RoadStopSpecFlag::NoAutoRoadConnection)) connect_to_road = false;
	}

	if (connect_to_road) {
		TileArea roadstop_area(tile, width, length);
		for (TileIndex cur_tile : roadstop_area) {
			ConnectRoadToStructure(cur_tile, dir);
			/* For a drive-through road stop build connecting road for other entrance. */
			if (is_drive_through) ConnectRoadToStructure(cur_tile, ReverseDiagDir(dir));
		}
	}
}

/**
 * Place a new road stop.
 * @param start_tile First tile of the area.
 * @param end_tile Last tile of the area.
 * @param stop_type Type of stop (bus/truck).
 * @param adjacent Allow stations directly adjacent to other stations.
 * @param rt The roadtypes.
 * @param err_msg Error message to show.
 * @see CcRoadStop()
 */
/**
 * Mobile layout: which way a stop on a tapped tile goes. A drive-through stop takes the axis of the road on the tile;
 * a bay faces the only adjacent road, when there is one (round 5 design, 6.2).
 * @param tile The tile.
 * @param drive_through Whether it's a drive-through stop.
 * @param rt The road type.
 * @param[in,out] ddir The picker's direction (for a drive-through stop: the axis as a direction), changed when the road tells.
 */
static void GetMobileRoadStopDirection(TileIndex tile, bool drive_through, RoadType rt, DiagDirection &ddir)
{
	RoadTramType rtt = GetRoadTramType(rt);
	if (drive_through) {
		/* Any road on the tile counts: a tram stop on a road, a bus stop on tram track. */
		if (!IsNormalRoadTile(tile)) return;
		RoadBits bits = GetAllRoadBits(tile);
		if (bits.Any(ROAD_X) && !bits.Any(ROAD_Y)) ddir = static_cast<DiagDirection>(to_underlying(Axis::X));
		if (bits.Any(ROAD_Y) && !bits.Any(ROAD_X)) ddir = static_cast<DiagDirection>(to_underlying(Axis::Y));
		return;
	}
	DiagDirection found = DiagDirection::Invalid;
	int count = 0;
	for (DiagDirection dir : EnumRange(DiagDirection::End)) {
		TileIndex neighbour = TileAddByDiagDir(tile, dir);
		if (!IsValidTile(neighbour) || !IsNormalRoadTile(neighbour) || !HasTileRoadType(neighbour, rtt)) continue;
		if (GetRoadBits(neighbour, rtt).Any(DiagDirToRoadBits(ReverseDiagDir(dir)))) {
			found = dir;
			count++;
		}
	}
	if (count == 1) ddir = found;
}

static void PlaceRoadStop(TileIndex start_tile, TileIndex end_tile, RoadStopType stop_type, bool adjacent, RoadType rt, StringID err_msg)
{
	TileArea ta(start_tile, end_tile);
	DiagDirection ddir = _roadstop_gui.orientation;
	bool drive_through = ddir >= DiagDirection::End;
	if (drive_through) ddir = ddir - DiagDirection::End; // Adjust picker result to actual direction.
	RoadStopClassID spec_class = _roadstop_gui.sel_class;
	uint16_t spec_index = _roadstop_gui.sel_type;

	if (IsMobileLayout()) {
		if (ta.w == 1 && ta.h == 1) GetMobileRoadStopDirection(ta.tile, drive_through, rt, ddir);
		/* The mobile picker chooses the station to join before Build; the desktop's join window never shows. */
		StationID mobile_join = StationID::Invalid();
		bool mobile_adjacent = adjacent;
		if (GetMobileStationJoin(mobile_join, mobile_adjacent)) {
			Command<Commands::BuildRoadStop>::Post(err_msg, CcRoadStop, ta.tile, ta.w, ta.h, stop_type, drive_through,
					ddir, rt, spec_class, spec_index, mobile_join, mobile_adjacent);
			return;
		}
	}

	auto proc = [=](bool test, StationID to_join) -> bool {
		if (test) {
			return Command<Commands::BuildRoadStop>::Do(CommandFlagsToDCFlags(GetCommandFlags<Commands::BuildRoadStop>()), ta.tile, ta.w, ta.h, stop_type, drive_through,
					ddir, rt, spec_class, spec_index, StationID::Invalid(), adjacent).Succeeded();
		} else {
			return Command<Commands::BuildRoadStop>::Post(err_msg, CcRoadStop, ta.tile, ta.w, ta.h, stop_type, drive_through,
					ddir, rt, spec_class, spec_index, to_join, adjacent);
		}
	};

	ShowSelectStationIfNeeded(ta, proc);
}

/**
 * Place a road waypoint.
 * @param tile Position to start dragging a waypoint.
 */
static void PlaceRoad_Waypoint(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_REMOVE_ROAD_WAYPOINT);
		return;
	}

	Axis axis = GetAxisForNewRoadWaypoint(tile);
	if (IsValidAxis(axis)) {
		/* Valid tile for waypoints */
		VpStartPlaceSizing(tile, axis == Axis::X ? VPM_X_LIMITED : VPM_Y_LIMITED, DDSP_BUILD_ROAD_WAYPOINT);
		VpSetPlaceSizingLimit(_settings_game.station.station_spread);
	} else {
		/* Tile where we can't build road waypoints. This is always going to fail,
		 * but provides the user with a proper error message. */
		Command<Commands::BuildRoadWaypoint>::Post(STR_ERROR_CAN_T_BUILD_ROAD_WAYPOINT, tile, Axis::X, 1, 1, ROADSTOP_CLASS_WAYP, 0, StationID::Invalid(), false);
	}
}

/**
 * Callback for placing a bus station.
 * @param tile Position to place the station.
 */
static void PlaceRoad_BusStation(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_REMOVE_BUSSTOP);
	} else {
		if (_roadstop_gui.orientation < DiagDirection::End) { // Not a drive-through stop.
			VpStartPlaceSizing(tile, (DiagDirToAxis(_roadstop_gui.orientation) == Axis::X) ? VPM_X_LIMITED : VPM_Y_LIMITED, DDSP_BUILD_BUSSTOP);
		} else {
			VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED, DDSP_BUILD_BUSSTOP);
		}
		VpSetPlaceSizingLimit(_settings_game.station.station_spread);
	}
}

/**
 * Callback for placing a truck station.
 * @param tile Position to place the station.
 */
static void PlaceRoad_TruckStation(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_REMOVE_TRUCKSTOP);
	} else {
		if (_roadstop_gui.orientation < DiagDirection::End) { // Not a drive-through stop.
			VpStartPlaceSizing(tile, (DiagDirToAxis(_roadstop_gui.orientation) == Axis::X) ? VPM_X_LIMITED : VPM_Y_LIMITED, DDSP_BUILD_TRUCKSTOP);
		} else {
			VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED, DDSP_BUILD_TRUCKSTOP);
		}
		VpSetPlaceSizingLimit(_settings_game.station.station_spread);
	}
}

typedef void OnButtonClick(Window *w);

/**
 * Toggles state of the Remove button of Build road toolbar
 * @param w window the button belongs to
 */
/**
 * Get the road (or tram) type of the road toolbar.
 * @return The road type being built.
 */
RoadType GetCurrentRoadType()
{
	return _cur_roadtype;
}

/**
 * Is the road toolbar in remove mode?
 * @return \c true when building tools remove instead.
 */
bool IsRoadRemoveModeActive()
{
	return _remove_button_clicked;
}

/**
 * Switch the road toolbar's remove mode without touching its buttons; used to replay a selection
 * in review in the mode it was made in.
 * @param remove Whether building tools remove instead.
 */
void SetRoadRemoveMode(bool remove)
{
	_remove_button_clicked = remove;
}

/**
 * Does the road tool build (or toggle) one-way road?
 * @return \c true for the one-way tool.
 */
bool IsRoadOneWayModeActive()
{
	return _one_way_button_clicked;
}

/**
 * Switch the one-way mode without touching the buttons; used to replay a selection in review.
 * @param one_way Whether road is built, or toggled, one-way.
 */
void SetRoadOneWayMode(bool one_way)
{
	_one_way_button_clicked = one_way;
}

/** @return The direction new road depots face. */
DiagDirection GetRoadDepotDirection()
{
	return _road_depot_orientation;
}

/**
 * Build a road depot (or, under an estimating #CommandResultCapture, test it).
 * @param tile The tile.
 * @param dir The direction its entrance faces.
 */
void PostRoadDepot(TileIndex tile, DiagDirection dir)
{
	_road_depot_orientation = dir;
	Command<Commands::BuildRoadDepot>::Post(GetRoadTypeInfo(_cur_roadtype)->strings.err_depot, CcRoadDepot, tile, _cur_roadtype, dir);
}

/**
 * Build a road tunnel from a tile, as the tunnel tool does.
 * @param tile The tunnel entrance.
 */
void PostRoadTunnel(TileIndex tile)
{
	Command<Commands::BuildTunnel>::Post(STR_ERROR_CAN_T_BUILD_TUNNEL_HERE, CcBuildRoadTunnel, tile, TransportType::Road, INVALID_RAILTYPE, _cur_roadtype);
}

static void ToggleRoadButton_Remove(Window *w)
{
	w->ToggleWidgetLoweredState(WID_ROT_REMOVE);
	w->SetWidgetDirty(WID_ROT_REMOVE);
	_remove_button_clicked = w->IsWidgetLowered(WID_ROT_REMOVE);
	SetSelectionRed(_remove_button_clicked);
}

/**
 * Updates the Remove button because of Ctrl state change
 * @param w window the button belongs to
 * @return true iff the remove button was changed
 */
static bool RoadToolbar_CtrlChanged(Window *w)
{
	if (w->IsWidgetDisabled(WID_ROT_REMOVE)) return false;

	/* allow ctrl to switch remove mode only for these widgets */
	for (WidgetID i = WID_ROT_ROAD_X; i <= WID_ROT_AUTOROAD; i++) {
		if (w->IsWidgetLowered(i)) {
			ToggleRoadButton_Remove(w);
			return true;
		}
	}

	return false;
}

/** Road toolbar window handler. */
static void ShowMobileRoadMoreFlyout(struct BuildRoadToolbarWindow *toolbar);

struct BuildRoadToolbarWindow : Window {
	RoadType roadtype = INVALID_ROADTYPE; ///< Road type to build.
	WidgetID last_started_action = INVALID_WIDGET; ///< Last started user action.
	const bool mobile; ///< Whether this is the mobile layout of the toolbar.
	const bool landscape; ///< For the mobile layout: whether this is the landscape (one row) layout.
	bool tools_hidden = false; ///< For the mobile layout: a sheet hides the tools and nothing is in progress.

	/** The tools that the mobile layout keeps in its More flyout, in that flyout's order (round 5 design, 6.1). */
	static inline const WidgetID more_road_landscape[] = {WID_ROT_ONE_WAY, WID_ROT_CONVERT_ROAD, WID_ROT_ROAD_X, WID_ROT_ROAD_Y};
	static inline const WidgetID more_road_portrait[] = {WID_ROT_BUILD_BRIDGE, WID_ROT_BUILD_TUNNEL, WID_ROT_ONE_WAY, WID_ROT_CONVERT_ROAD, WID_ROT_ROAD_X, WID_ROT_ROAD_Y};
	static inline const WidgetID more_tram_landscape[] = {WID_ROT_CONVERT_ROAD, WID_ROT_ROAD_X, WID_ROT_ROAD_Y};
	static inline const WidgetID more_tram_portrait[] = {WID_ROT_BUILD_BRIDGE, WID_ROT_BUILD_TUNNEL, WID_ROT_CONVERT_ROAD, WID_ROT_ROAD_X, WID_ROT_ROAD_Y};

	BuildRoadToolbarWindow(WindowDesc &desc, WindowNumber window_number, bool mobile = false, bool landscape = false) : Window(desc), roadtype(_cur_roadtype), mobile(mobile), landscape(landscape)
	{
		this->CreateNestedTree();
		if (mobile) this->GetWidget<NWidgetStacked>(WID_ROT_SEL_HIDDEN)->SetDisplayedPlane(SZSP_NONE);
		this->FinishInitNested(window_number);
		this->SetWidgetDisabledState(WID_ROT_REMOVE, true);

		/* The mobile layout's one-way button is a tool of its own, never disabled. */
		if (RoadTypeIsRoad(this->roadtype) && !mobile) {
			this->SetWidgetDisabledState(WID_ROT_ONE_WAY, true);
		}

		this->OnInvalidateData();

		if (mobile) {
			this->UpdateMobileHint();
		} else if (_settings_client.gui.link_terraform_toolbar) {
			ShowTerraformToolbar(this);
		}
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		if (_game_mode == GameMode::Normal && (this->IsWidgetLowered(WID_ROT_BUS_STATION) || this->IsWidgetLowered(WID_ROT_TRUCK_STATION))) SetViewportCatchmentStation(nullptr, true);
		if (_settings_client.gui.link_terraform_toolbar) CloseWindowById(WindowClass::ScenarioGenerateLandscape, 0, false);
		CloseWindowById(WindowClass::MobileRailMore, 0);
		this->Window::Close();
		/* Give the dock its place back. */
		if (this->mobile) RelocateMobileWindows();
	}

	/**
	 * The tools in the mobile layout's More flyout.
	 * @return The tools.
	 */
	std::span<const WidgetID> GetMoreTools() const
	{
		if (RoadTypeIsRoad(this->roadtype)) {
			if (this->landscape) return more_road_landscape;
			return more_road_portrait;
		}
		if (this->landscape) return more_tram_landscape;
		return more_tram_portrait;
	}

	/**
	 * The icon of a tool, as its toolbar button shows it.
	 * @param tool The tool.
	 * @return Its sprite.
	 */
	SpriteID GetToolSprite(WidgetID tool) const
	{
		const RoadTypeInfo *rti = GetRoadTypeInfo(this->roadtype);
		switch (tool) {
			case WID_ROT_ROAD_X: return rti->gui_sprites.build_x_road;
			case WID_ROT_ROAD_Y: return rti->gui_sprites.build_y_road;
			case WID_ROT_CONVERT_ROAD: return rti->gui_sprites.convert_road;
			case WID_ROT_BUILD_TUNNEL: return rti->gui_sprites.build_tunnel;
			case WID_ROT_ONE_WAY: return SPR_IMG_ROAD_ONE_WAY;
			case WID_ROT_BUILD_BRIDGE: return SPR_IMG_BRIDGE;
			default: return rti->gui_sprites.auto_road;
		}
	}

	/**
	 * The label of a tool in the More flyout.
	 * @param tool The tool.
	 * @return Its label.
	 */
	StringID GetToolLabel(WidgetID tool) const
	{
		bool road = RoadTypeIsRoad(this->roadtype);
		switch (tool) {
			case WID_ROT_ONE_WAY: return STR_MOBILE_ROAD_ONE_WAY;
			case WID_ROT_CONVERT_ROAD: return STR_MOBILE_RAIL_CONVERT;
			case WID_ROT_ROAD_X: return road ? STR_MOBILE_ROAD_X : STR_MOBILE_RAIL_X;
			case WID_ROT_ROAD_Y: return road ? STR_MOBILE_ROAD_Y : STR_MOBILE_RAIL_Y;
			case WID_ROT_BUILD_BRIDGE: return STR_MOBILE_RAIL_BRIDGE;
			case WID_ROT_BUILD_TUNNEL: return STR_MOBILE_RAIL_TUNNEL;
			default: NOT_REACHED();
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		/* The mobile toolbar reaches the bottom edge of the screen; this spacer fills the inset there. */
		if (widget == WID_ROT_GAP_BOTTOM) size = {0, static_cast<uint>(GetMobileEdgeGaps().bottom)};
	}

	/** Show the hint of the selected tool in the status line. */
	void UpdateMobileHint()
	{
		if (!this->mobile) return;
		StringID hint = STR_MOBILE_BUILD_HINT_PICK_TOOL;
		bool road = RoadTypeIsRoad(this->roadtype);
		if (_thd.GetCallbackWnd() == this) {
			switch (this->last_started_action) {
				case WID_ROT_ROAD_X:
				case WID_ROT_ROAD_Y:
				case WID_ROT_AUTOROAD:
					if (_remove_button_clicked) {
						hint = road ? STR_MOBILE_BUILD_HINT_REMOVE_ROAD : STR_MOBILE_BUILD_HINT_REMOVE_TRACK;
					} else {
						hint = road ? STR_MOBILE_BUILD_HINT_ROAD : STR_MOBILE_BUILD_HINT_TRACK;
					}
					break;
				case WID_ROT_ONE_WAY: hint = STR_MOBILE_BUILD_HINT_ONE_WAY; break;
				case WID_ROT_DEMOLISH: hint = STR_MOBILE_BUILD_HINT_DEMOLISH; break;
				case WID_ROT_BUILD_BRIDGE: hint = STR_MOBILE_BUILD_HINT_BRIDGE; break;
				case WID_ROT_BUILD_TUNNEL: hint = STR_MOBILE_BUILD_HINT_TUNNEL; break;
				case WID_ROT_CONVERT_ROAD: hint = road ? STR_MOBILE_BUILD_HINT_CONVERT_ROAD : STR_MOBILE_BUILD_HINT_CONVERT_TRAM; break;
				case WID_ROT_DEPOT: hint = STR_MOBILE_BUILD_HINT_DEPOT; break;
				case WID_ROT_BUS_STATION:
				case WID_ROT_TRUCK_STATION: hint = _remove_button_clicked ? STR_MOBILE_BUILD_HINT_REMOVE_STOP : STR_MOBILE_BUILD_HINT_STOP; break;
				default: break;
			}
		}
		MobileBuildSetToolHint(hint);
	}

	/**
	 * What confirming does with the selected tool, for the confirm button while nothing is in review.
	 * @return The kind of construction.
	 */
	MobileBuildKind GetToolKind() const
	{
		if (GetMobileBuildState() == MobileBuildState::Pending) return GetMobileBuildKind();
		if (_thd.GetCallbackWnd() != this) return MobileBuildKind::Build;
		switch (this->last_started_action) {
			case WID_ROT_DEMOLISH: return MobileBuildKind::Demolish;
			case WID_ROT_CONVERT_ROAD: return MobileBuildKind::Convert;
			case WID_ROT_ONE_WAY: return MobileBuildKind::Change;
			default: return _remove_button_clicked ? MobileBuildKind::Remove : MobileBuildKind::Build;
		}
	}

	/** @return Whether a picker of this toolbar fills the sheet. */
	static bool IsPickerOpen()
	{
		return FindWindowById(WindowClass::BuildBusStation, TransportType::Road) != nullptr || FindWindowById(WindowClass::BuildTruckStation, TransportType::Road) != nullptr;
	}

	/** Update the mobile layout's status line, Cancel, Build/Undo and More buttons to the state of the review. */
	void UpdateMobileWidgets()
	{
		MobileBuildState state = GetMobileBuildState();
		bool picker_open = IsPickerOpen();
		/* While a picker fills the sheet slot, the toolbar shows only its status line, Cancel and Build. */
		bool picking = state == MobileBuildState::BridgePicking || picker_open;
		/* In landscape the tools don't fit beside an open sheet either (an info window, say). */
		if (this->landscape && IsMobileSheetOpen()) picking = true;
		/* ...and with no tool or selection either, offer to bring the tools back rather than a dead Cancel and Build. */
		this->tools_hidden = picking && state == MobileBuildState::Idle && _thd.GetCallbackWnd() != this && !picker_open;
		DiagDirection depot_dir;
		bool depot_pending = state == MobileBuildState::Pending && GetMobileBuildDepotArrow(GetMobileBuildPendingTile(), depot_dir);

		bool reinit = false;
		auto set_plane = [&](WidgetID widget, int plane) {
			NWidgetStacked *sel = this->GetWidget<NWidgetStacked>(widget);
			if (sel->shown_plane == plane) return;
			sel->SetDisplayedPlane(plane);
			reinit = true;
		};
		/* Portrait stacks the tool row under row A: hidden, it must still resize horizontally (SZSP_HORIZONTAL). */
		set_plane(WID_ROT_SEL_TOOLS, picking ? (this->landscape ? SZSP_NONE : SZSP_HORIZONTAL) : 0);
		set_plane(WID_ROT_SEL_CONFIRM, this->tools_hidden ? 2 : (MobileBuildHasUndo() ? 1 : (state == MobileBuildState::BridgePicking ? SZSP_NONE : 0)));
		set_plane(WID_ROT_SEL_CANCEL, this->tools_hidden ? SZSP_NONE : 0);
		set_plane(WID_ROT_SEL_ROTATE, depot_pending ? 0 : SZSP_NONE);

		MobileBuildKind kind = this->GetToolKind();
		NWidgetCore *confirm = this->GetWidget<NWidgetCore>(WID_ROT_CONFIRM);
		/* Disabled, it's grey like the disabled Cancel: dark green read as nearly active. */
		confirm->colour = !MobileBuildCanConfirm() ? Colours::Grey : ((kind == MobileBuildKind::Build || kind == MobileBuildKind::Change) ? Colours::Green : Colours::Red);
		static const StringID labels[] = {STR_MOBILE_BUILD_CONFIRM_BUILD, STR_MOBILE_BUILD_CONFIRM_REMOVE, STR_MOBILE_BUILD_CONFIRM_DEMOLISH, STR_MOBILE_BUILD_CONFIRM_CONVERT, STR_MOBILE_BUILD_CONFIRM_CHANGE};
		confirm->SetString(labels[to_underlying(kind)]);
		this->SetWidgetDisabledState(WID_ROT_CONFIRM, !MobileBuildCanConfirm());
		/* Cancel drops the selection in review, or else the selected tool. */
		this->SetWidgetDisabledState(WID_ROT_CANCEL, !MobileBuildCanCancel() && _thd.GetCallbackWnd() != this);

		auto more = this->GetMoreTools();
		this->SetWidgetLoweredState(WID_ROT_MORE, std::ranges::find(more, this->last_started_action) != more.end() && this->IsWidgetLowered(this->last_started_action));

		if (reinit) {
			this->ReInit();
			RelocateMobileWindows();
		}
		this->SetDirty();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_ROT_STATUS: {
				TextColour colour;
				std::string status = GetMobileBuildStatus(colour);
				if (this->tools_hidden) {
					colour = TextColour::Black;
					status = GetString(STR_MOBILE_BUILD_TOOLS_HIDDEN);
				}
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
				if (colour == TextColour::Red) {
					/* Errors start with a warning sign. */
					Dimension d = GetSpriteSize(SPR_WARNING_SIGN);
					bool rtl = _current_text_dir == TD_RTL;
					DrawSprite(SPR_WARNING_SIGN, PAL_NONE, rtl ? tr.right - d.width + 1 : tr.left, CentreBounds(r.top, r.bottom, d.height));
					tr = tr.Indent(d.width + WidgetDimensions::scaled.hsep_normal, rtl);
				}
				int lines_height = std::min<int>(tr.Height(), GetCharacterHeight(FontSize::Normal) * 2);
				tr.top = CentreBounds(r.top, r.bottom, std::min(lines_height, GetStringHeight(status, tr.Width())));
				DrawStringMultiLineWithClipping(tr.WithHeight(lines_height), status, colour);
				break;
			}

			case WID_ROT_MORE: {
				/* The tool chosen from the flyout, or "..." while none is. */
				bool lowered = this->IsWidgetLowered(WID_ROT_MORE);
				int offset = lowered ? ScaleGUITrad(1) : 0;
				if (lowered) {
					SpriteID sprite = this->GetToolSprite(this->last_started_action);
					Dimension d = GetSpriteSize(sprite);
					DrawSprite(sprite, PAL_NONE, CentreBounds(r.left, r.right, d.width) + offset, CentreBounds(r.top, r.bottom, d.height) + offset);
				} else {
					DrawString(r.left, r.right, CentreBounds(r.top, r.bottom, GetCharacterHeight(FontSize::Normal)), STR_MOBILE_RAIL_MORE, TextColour::White, AlignmentH::Centre);
				}
				break;
			}
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;

		if (this->mobile) this->UpdateMobileWidgets();

		if (_cur_roadtype != this->roadtype) {
			bool close_build_station = this->ModifyRoadType(_cur_roadtype);

			/* Update cursor and all sub windows. */
			if (_thd.GetCallbackWnd() == this) SetCursor(this->GetCursorForWidget(this->last_started_action), PAL_NONE);
			for (WindowClass cls : {WindowClass::BuildBusStation, WindowClass::BuildTruckStation, WindowClass::BuildWaypoint, WindowClass::BuildDepot}) {
				SetWindowDirty(cls, TransportType::Road);
			}

			if (close_build_station) {
				CloseWindowById(WindowClass::BuildBusStation, TransportType::Road);
				CloseWindowById(WindowClass::BuildTruckStation, TransportType::Road);
			}
		}

		if (!ValParamRoadType(this->roadtype)) {
			/* Close toolbar if road type is not available. */
			this->Close();
			return;
		}

		RoadTramType rtt = GetRoadTramType(this->roadtype);

		bool can_build = CanBuildVehicleInfrastructure(VehicleType::Road, rtt);
		this->SetWidgetsDisabledState(!can_build,
			WID_ROT_DEPOT,
			WID_ROT_BUILD_WAYPOINT,
			WID_ROT_BUS_STATION,
			WID_ROT_TRUCK_STATION);
		if (!can_build) {
			CloseWindowById(WindowClass::BuildBusStation, TransportType::Road);
			CloseWindowById(WindowClass::BuildTruckStation, TransportType::Road);
			CloseWindowById(WindowClass::BuildDepot, TransportType::Road);
			CloseWindowById(WindowClass::BuildWaypoint, TransportType::Road);
		}

		if (_game_mode != GameMode::Editor) {
			if (!can_build) {
				/* Show in the tooltip why this button is disabled. */
				this->GetWidget<NWidgetCore>(WID_ROT_DEPOT)->SetToolTip(STR_TOOLBAR_DISABLED_NO_VEHICLE_AVAILABLE);
				this->GetWidget<NWidgetCore>(WID_ROT_BUILD_WAYPOINT)->SetToolTip(STR_TOOLBAR_DISABLED_NO_VEHICLE_AVAILABLE);
				this->GetWidget<NWidgetCore>(WID_ROT_BUS_STATION)->SetToolTip(STR_TOOLBAR_DISABLED_NO_VEHICLE_AVAILABLE);
				this->GetWidget<NWidgetCore>(WID_ROT_TRUCK_STATION)->SetToolTip(STR_TOOLBAR_DISABLED_NO_VEHICLE_AVAILABLE);
			} else {
				this->GetWidget<NWidgetCore>(WID_ROT_DEPOT)->SetToolTip(rtt == RoadTramType::Road ? STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_VEHICLE_DEPOT : STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAM_VEHICLE_DEPOT);
				this->GetWidget<NWidgetCore>(WID_ROT_BUILD_WAYPOINT)->SetToolTip(rtt == RoadTramType::Road ? STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD_TO_WAYPOINT : STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_TRAM_TO_WAYPOINT);
				this->GetWidget<NWidgetCore>(WID_ROT_BUS_STATION)->SetToolTip(rtt == RoadTramType::Road ? STR_ROAD_TOOLBAR_TOOLTIP_BUILD_BUS_STATION : STR_ROAD_TOOLBAR_TOOLTIP_BUILD_PASSENGER_TRAM_STATION);
				this->GetWidget<NWidgetCore>(WID_ROT_TRUCK_STATION)->SetToolTip(rtt == RoadTramType::Road ? STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRUCK_LOADING_BAY : STR_ROAD_TOOLBAR_TOOLTIP_BUILD_CARGO_TRAM_STATION);
			}
		}
	}

	void OnInit() override
	{
		/* Configure the road toolbar for the roadtype. */
		const RoadTypeInfo *rti = GetRoadTypeInfo(this->roadtype);
		this->GetWidget<NWidgetCore>(WID_ROT_ROAD_X)->SetSprite(rti->gui_sprites.build_x_road);
		this->GetWidget<NWidgetCore>(WID_ROT_ROAD_Y)->SetSprite(rti->gui_sprites.build_y_road);
		this->GetWidget<NWidgetCore>(WID_ROT_AUTOROAD)->SetSprite(rti->gui_sprites.auto_road);
		if (_game_mode != GameMode::Editor) {
			this->GetWidget<NWidgetCore>(WID_ROT_DEPOT)->SetSprite(rti->gui_sprites.build_depot);
		}
		this->GetWidget<NWidgetCore>(WID_ROT_CONVERT_ROAD)->SetSprite(rti->gui_sprites.convert_road);
		this->GetWidget<NWidgetCore>(WID_ROT_BUILD_TUNNEL)->SetSprite(rti->gui_sprites.build_tunnel);
		if (this->mobile) {
			/* One layout serves road and tram: the category shows the type, the tooltips say which. */
			bool road = RoadTypeIsRoad(this->roadtype);
			this->GetWidget<NWidgetCore>(WID_ROT_CATEGORY)->SetSprite(rti->gui_sprites.build_x_road);
			this->GetWidget<NWidgetCore>(WID_ROT_AUTOROAD)->SetToolTip(road ? STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOROAD : STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOTRAM);
			this->GetWidget<NWidgetCore>(WID_ROT_REMOVE)->SetToolTip(road ? STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_ROAD : STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_TRAMWAYS);
			this->GetWidget<NWidgetCore>(WID_ROT_BUILD_BRIDGE)->SetToolTip(road ? STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_BRIDGE : STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_BRIDGE);
			this->GetWidget<NWidgetCore>(WID_ROT_BUILD_TUNNEL)->SetToolTip(road ? STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_TUNNEL : STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_TUNNEL);
			this->GetWidget<NWidgetCore>(WID_ROT_CONVERT_ROAD)->SetToolTip(road ? STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD : STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_TRAM);
		}
	}

	/**
	 * Switch to another road type.
	 * @param roadtype New road type.
	 * @return true if RoadTramType has changed.
	 */
	bool ModifyRoadType(RoadType roadtype)
	{
		bool result = RoadTypeIsRoad(this->roadtype) != RoadTypeIsRoad(roadtype);

		this->roadtype = roadtype;
		this->ReInit();
		MobileBuildReestimate();
		return result;
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_ROT_UNDO) {
			/* Removing road refunds part of its cost; show that as a gain rather than a negative cost. */
			Money cost = GetMobileBuildUndoCost();
			return cost < 0 ? GetString(STR_MOBILE_BUILD_UNDO_REFUND, -cost) : GetString(STR_MOBILE_BUILD_UNDO, cost);
		}
		if (widget == WID_ROT_CAPTION) {
			const RoadTypeInfo *rti = GetRoadTypeInfo(this->roadtype);
			if (rti->max_speed > 0) {
				return GetString(STR_TOOLBAR_ROADTYPE_VELOCITY, rti->strings.toolbar_caption, PackVelocity(rti->max_speed / 2, VehicleType::Road));
			}
			return GetString(rti->strings.toolbar_caption);

		}

		return this->Window::GetWidgetString(widget, stringid);
	}

	/**
	 * Update the remove button lowered state of the road toolbar
	 *
	 * @param clicked_widget The widget which the client clicked just now
	 */
	void UpdateOptionWidgetStatus(RoadToolbarWidgets clicked_widget)
	{
		/* The remove and the one way button state is driven
		 * by the other buttons so they don't act on themselves.
		 * Both are only valid if they are able to apply as options. */
		switch (clicked_widget) {
			case WID_ROT_REMOVE:
				if (RoadTypeIsRoad(this->roadtype)) {
					this->RaiseWidget(WID_ROT_ONE_WAY);
					this->SetWidgetDirty(WID_ROT_ONE_WAY);
				}

				break;

			case WID_ROT_ONE_WAY:
				this->RaiseWidget(WID_ROT_REMOVE);
				this->SetWidgetDirty(WID_ROT_REMOVE);
				break;

			case WID_ROT_BUS_STATION:
			case WID_ROT_TRUCK_STATION:
			case WID_ROT_BUILD_WAYPOINT:
				if (RoadTypeIsRoad(this->roadtype)) this->DisableWidget(WID_ROT_ONE_WAY);
				this->SetWidgetDisabledState(WID_ROT_REMOVE, !this->IsWidgetLowered(clicked_widget));
				break;

			case WID_ROT_ROAD_X:
			case WID_ROT_ROAD_Y:
			case WID_ROT_AUTOROAD:
				this->SetWidgetDisabledState(WID_ROT_REMOVE, !this->IsWidgetLowered(clicked_widget));
				if (RoadTypeIsRoad(this->roadtype)) {
					this->SetWidgetDisabledState(WID_ROT_ONE_WAY, !this->IsWidgetLowered(clicked_widget));
				}
				break;

			default:
				/* When any other buttons than road/station, raise and
				 * disable the removal button */
				this->SetWidgetDisabledState(WID_ROT_REMOVE, true);
				this->SetWidgetLoweredState(WID_ROT_REMOVE, false);

				if (RoadTypeIsRoad(this->roadtype)) {
					this->SetWidgetDisabledState(WID_ROT_ONE_WAY, true);
					this->SetWidgetLoweredState(WID_ROT_ONE_WAY, false);
				}

				break;
		}
	}

	/**
	 * Returns corresponding cursor for provided button.
	 * @param widget Widget ID of the button.
	 * @return Corresponding cursor ID.
	 */
	CursorID GetCursorForWidget(WidgetID widget)
	{
		switch (widget) {
			case WID_ROT_ROAD_X: return GetRoadTypeInfo(this->roadtype)->cursor.road_nwse;
			case WID_ROT_ROAD_Y: return GetRoadTypeInfo(this->roadtype)->cursor.road_swne;
			case WID_ROT_AUTOROAD: return GetRoadTypeInfo(this->roadtype)->cursor.autoroad;
			case WID_ROT_DEMOLISH: return ANIMCURSOR_DEMOLISH;
			case WID_ROT_DEPOT: return GetRoadTypeInfo(this->roadtype)->cursor.depot;
			case WID_ROT_BUILD_WAYPOINT: return SPR_CURSOR_WAYPOINT;
			case WID_ROT_BUS_STATION: return SPR_CURSOR_BUS_STATION;
			case WID_ROT_TRUCK_STATION: return SPR_CURSOR_TRUCK_STATION;
			case WID_ROT_BUILD_BRIDGE: return SPR_CURSOR_BRIDGE;
			case WID_ROT_BUILD_TUNNEL: return GetRoadTypeInfo(this->roadtype)->cursor.tunnel;
			case WID_ROT_CONVERT_ROAD: return GetRoadTypeInfo(this->roadtype)->cursor.convert_road;
			case WID_ROT_ONE_WAY: return GetRoadTypeInfo(this->roadtype)->cursor.autoroad; // The mobile layout's one-way tool.
			default: NOT_REACHED();
		}
	}

	/**
	 * Returns corresponding high light style for provided button.
	 * @param widget Widget ID of the button.
	 * @return Corresponding high light style.
	 */
	HighLightStyle GetHighLightStyleForWidget(WidgetID widget)
	{
		switch (widget) {
			case WID_ROT_ROAD_X: return HT_RECT;
			case WID_ROT_ROAD_Y: return HT_RECT;
			case WID_ROT_AUTOROAD: return HT_RECT;
			case WID_ROT_DEMOLISH: return HT_RECT | HT_DIAGONAL;
			case WID_ROT_DEPOT: return HT_RECT;
			case WID_ROT_BUILD_WAYPOINT: return HT_RECT;
			case WID_ROT_BUS_STATION: return HT_RECT;
			case WID_ROT_TRUCK_STATION: return HT_RECT;
			case WID_ROT_BUILD_BRIDGE: return HT_RECT;
			case WID_ROT_BUILD_TUNNEL: return HT_SPECIAL;
			case WID_ROT_CONVERT_ROAD: return HT_RECT | HT_DIAGONAL;
			case WID_ROT_ONE_WAY: return HT_RECT; // The mobile layout's one-way tool.
			default: NOT_REACHED();
		}
	}

	/**
	 * Mobile layout: a click on one of its own buttons, or on Remove (a toggle that keeps the tool).
	 * @param widget The widget.
	 * @return \c true when handled.
	 */
	bool OnMobileClick(WidgetID widget)
	{
		switch (widget) {
			case WID_ROT_CATEGORY: ShowMobileBuildFlyoutForToolbar(this, widget); return true;
			case WID_ROT_MORE: ShowMobileRoadMoreFlyout(this); return true;
			case WID_ROT_CANCEL:
				if (MobileBuildCanCancel()) {
					MobileBuildCancel();
				} else if (_thd.GetCallbackWnd() == this) {
					ResetObjectToPlace();
				}
				return true;
			case WID_ROT_CONFIRM: MobileBuildConfirm(); return true;
			case WID_ROT_UNDO: MobileBuildUndo(); return true;
			case WID_ROT_ROTATE: MobileBuildRotateDepot(); return true;
			case WID_ROT_SHOW_TOOLS: CloseMobileSheetPublic(); return true;
			case WID_ROT_EXIT: this->Close(); return true;
			case WID_ROT_STATUS: return true;
			case WID_ROT_REMOVE:
				if (this->IsWidgetDisabled(WID_ROT_REMOVE)) return true;
				CloseWindowById(WindowClass::JoinStation, 0);
				ToggleRoadButton_Remove(this);
				SndClickBeep();
				MobileBuildReestimate();
				this->UpdateMobileHint();
				this->UpdateMobileWidgets();
				return true;
			default: return false;
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (this->mobile && this->OnMobileClick(widget)) return;

		bool started;
		_remove_button_clicked = false;
		_one_way_button_clicked = false;

		/* In the mobile layout one-way is a tool of its own (desktop: a toggle for the road tools). */
		if ((widget != WID_ROT_ONE_WAY || this->mobile) && widget != WID_ROT_REMOVE) {
			started = HandlePlacePushButton(this, widget, this->GetCursorForWidget(widget), this->GetHighLightStyleForWidget(widget));
			this->last_started_action = widget;
		}

		switch (widget) {
			case WID_ROT_DEPOT:
				/* The mobile layout has no depot picker: the depot faces the road, and Turn rotates it. */
				if (started && !this->mobile) {
					ShowRoadDepotPicker(this);
				}
				break;

			case WID_ROT_BUILD_WAYPOINT:
				if (started) {
					ShowBuildRoadWaypointPicker(this);
				}
				break;

			case WID_ROT_BUS_STATION:
				if (started) {
					ShowRVStationPicker(this, RoadStopType::Bus);
				}
				break;

			case WID_ROT_TRUCK_STATION:
				if (started) {
					ShowRVStationPicker(this, RoadStopType::Truck);
				}
				break;

			case WID_ROT_ONE_WAY:
				if (this->mobile) break;
				if (this->IsWidgetDisabled(WID_ROT_ONE_WAY)) return;
				this->SetDirty();
				this->ToggleWidgetLoweredState(WID_ROT_ONE_WAY);
				SetSelectionRed(false);
				break;

			case WID_ROT_REMOVE:
				if (this->IsWidgetDisabled(WID_ROT_REMOVE)) return;

				CloseWindowById(WindowClass::JoinStation, 0);
				ToggleRoadButton_Remove(this);
				SndClickBeep();
				break;
		}
		if (this->mobile && widget == WID_ROT_ONE_WAY) {
			/* Remove doesn't apply to the one-way tool. */
			this->SetWidgetDisabledState(WID_ROT_REMOVE, true);
			this->SetWidgetLoweredState(WID_ROT_REMOVE, false);
		} else {
			this->UpdateOptionWidgetStatus(static_cast<RoadToolbarWidgets>(widget));
			/* The desktop's one-way toggle follows the road tools; the mobile layout's one-way tool is always there. */
			if (this->mobile) this->SetWidgetDisabledState(WID_ROT_ONE_WAY, false);
		}
		if (IsCtrlPressed()) RoadToolbar_CtrlChanged(this);
		if (this->mobile) {
			this->UpdateMobileHint();
			this->UpdateMobileWidgets();
		}
	}

	/**
	* Selects new RoadType based on SpecialHotkeys and order defined in _sorted_roadtypes.
	* @param hotkey Defines what action to perform.
	* @return EventState::Handled if hotkey was accepted.
	*/
	EventState ChangeRoadTypeOnHotkey(int hotkey)
	{
		auto [index, step] = GetListIndexStep(SpecialListHotkeys(hotkey), _sorted_roadtypes, this->roadtype);

		while (RoadTypeIsRoad(_sorted_roadtypes[index]) != RoadTypeIsRoad(this->roadtype) || !HasRoadTypeAvail(_local_company, _sorted_roadtypes[index])) {
			index = (index + step) % _sorted_roadtypes.size();
		}

		_cur_roadtype = _sorted_roadtypes[index];
		if (RoadTypeIsRoad(_cur_roadtype)) {
			_last_built_roadtype = _cur_roadtype;
		} else {
			_last_built_tramtype = _cur_roadtype;
		}
		this->ModifyRoadType(_cur_roadtype);

		if (_thd.GetCallbackWnd() == this) SetCursor(this->GetCursorForWidget(this->last_started_action), PAL_NONE);
		for (WindowClass cls : {WindowClass::BuildBusStation, WindowClass::BuildTruckStation, WindowClass::BuildWaypoint, WindowClass::BuildDepot}) {
			SetWindowDirty(cls, TransportType::Road);
		}

		return EventState::Handled;
	}

	EventState OnHotkey(int hotkey) override
	{
		if (IsSpecialHotkey(hotkey)) return this->ChangeRoadTypeOnHotkey(hotkey);
		MarkTileDirtyByTile(TileVirtXY(_thd.pos.x, _thd.pos.y)); // redraw tile selection
		return Window::OnHotkey(hotkey);
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		_remove_button_clicked = this->IsWidgetLowered(WID_ROT_REMOVE);
		_one_way_button_clicked = RoadTypeIsRoad(this->roadtype) ? this->IsWidgetLowered(WID_ROT_ONE_WAY) : false;
		switch (this->last_started_action) {
			case WID_ROT_ROAD_X:
				_place_road_dir = Axis::X;
				_place_road_start_half_x = _tile_fract_coords.x >= 8;
				VpStartPlaceSizing(tile, VPM_FIX_Y, DDSP_PLACE_ROAD_X_DIR);
				break;

			case WID_ROT_ROAD_Y:
				_place_road_dir = Axis::Y;
				_place_road_start_half_y = _tile_fract_coords.y >= 8;
				VpStartPlaceSizing(tile, VPM_FIX_X, DDSP_PLACE_ROAD_Y_DIR);
				break;

			case WID_ROT_ONE_WAY: // The mobile layout's one-way tool: autoroad over existing road toggles its direction.
			case WID_ROT_AUTOROAD:
				_place_road_dir = Axis::Invalid;
				_place_road_start_half_x = _tile_fract_coords.x >= 8;
				_place_road_start_half_y = _tile_fract_coords.y >= 8;
				VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_PLACE_AUTOROAD);
				break;

			case WID_ROT_DEMOLISH:
				PlaceProc_DemolishArea(tile);
				break;

			case WID_ROT_DEPOT:
				if (MobileBuildDepotTap(this, tile)) break;
				Command<Commands::BuildRoadDepot>::Post(GetRoadTypeInfo(this->roadtype)->strings.err_depot, CcRoadDepot,
						tile, _cur_roadtype, _road_depot_orientation);
				break;

			case WID_ROT_BUILD_WAYPOINT:
				PlaceRoad_Waypoint(tile);
				break;

			case WID_ROT_BUS_STATION:
				PlaceRoad_BusStation(tile);
				break;

			case WID_ROT_TRUCK_STATION:
				PlaceRoad_TruckStation(tile);
				break;

			case WID_ROT_BUILD_BRIDGE:
				PlaceRoad_Bridge(tile, this);
				break;

			case WID_ROT_BUILD_TUNNEL:
				if (MobileBuildTunnelTap(this, tile)) break;
				PostRoadTunnel(tile);
				break;

			case WID_ROT_CONVERT_ROAD:
				VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_CONVERT_ROAD);
				break;

			default: NOT_REACHED();
		}
	}

	void OnPlaceObjectAbort() override
	{
		if (_game_mode != GameMode::Editor && (this->IsWidgetLowered(WID_ROT_BUS_STATION) || this->IsWidgetLowered(WID_ROT_TRUCK_STATION))) SetViewportCatchmentStation(nullptr, true);

		this->RaiseButtons();
		this->SetWidgetDisabledState(WID_ROT_REMOVE, true);
		this->SetWidgetDirty(WID_ROT_REMOVE);

		if (RoadTypeIsRoad(this->roadtype) && !this->mobile) {
			this->SetWidgetDisabledState(WID_ROT_ONE_WAY, true);
			this->SetWidgetDirty(WID_ROT_ONE_WAY);
		}

		CloseWindowById(WindowClass::BuildBusStation, TransportType::Road);
		CloseWindowById(WindowClass::BuildTruckStation, TransportType::Road);
		CloseWindowById(WindowClass::BuildDepot, TransportType::Road);
		CloseWindowById(WindowClass::BuildWaypoint, TransportType::Road);
		CloseWindowById(WindowClass::JoinStation, 0);
		CloseWindowByClass(WindowClass::BuildBridge);
		if (this->mobile) {
			this->UpdateMobileHint();
			this->UpdateMobileWidgets();
		}
	}

	void OnPlaceDrag(ViewportPlaceMethod select_method, [[maybe_unused]] ViewportDragDropSelectionProcess select_proc, [[maybe_unused]] Point pt) override
	{
		/* Here we update the end tile flags
		 * of the road placement actions.
		 * At first we reset the end halfroad
		 * bits and if needed we set them again. */
		switch (select_proc) {
			case DDSP_PLACE_ROAD_X_DIR:
				_place_road_end_half = pt.x & 8;
				break;

			case DDSP_PLACE_ROAD_Y_DIR:
				_place_road_end_half = pt.y & 8;
				break;

			case DDSP_PLACE_AUTOROAD:
				/* For autoroad we need to update the
				 * direction of the road */
				if (_thd.size.x > _thd.size.y || (_thd.size.x == _thd.size.y &&
						((_tile_fract_coords.x < _tile_fract_coords.y && (_tile_fract_coords.x + _tile_fract_coords.y) <= 15) ||
						(_tile_fract_coords.x > _tile_fract_coords.y && (_tile_fract_coords.x + _tile_fract_coords.y) >= 15)))) {
					/* Set dir = X */
					_place_road_dir = Axis::X;
					_place_road_end_half = pt.x & 8;
				} else {
					/* Set dir = Y */
					_place_road_dir = Axis::Y;
					_place_road_end_half = pt.y & 8;
				}

				break;

			default:
				break;
		}

		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	Point OnInitialPosition(int16_t sm_width, [[maybe_unused]] int16_t sm_height, [[maybe_unused]] int window_number) override
	{
		/* The mobile layout pass docks the mobile toolbar. */
		if (this->mobile) return {0, _screen.height - sm_height};
		return AlignInitialConstructionToolbar(sm_width);
	}

	void OnPlaceMouseUp([[maybe_unused]] ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, [[maybe_unused]] Point pt, TileIndex start_tile, TileIndex end_tile) override
	{
		if (pt.x != -1) {
			switch (select_proc) {
				default: NOT_REACHED();
				case DDSP_BUILD_BRIDGE:
					/* The mobile layout keeps the tool, and the span highlighted, while the bridge list is open. */
					if (!_settings_client.gui.persistent_buildingtools && !this->mobile) ResetObjectToPlace();
					ShowBuildBridgeWindow(start_tile, end_tile, TransportType::Road, INVALID_RAILTYPE, _cur_roadtype);
					break;

				case DDSP_DEMOLISH_AREA:
					GUIPlaceProcDragXY(select_proc, start_tile, end_tile);
					break;

				case DDSP_PLACE_ROAD_X_DIR:
				case DDSP_PLACE_ROAD_Y_DIR:
				case DDSP_PLACE_AUTOROAD: {
					bool start_half = _place_road_dir == Axis::Y ? _place_road_start_half_y : _place_road_start_half_x;

					if (_remove_button_clicked) {
						Command<Commands::RemoveRoadLong>::Post(GetRoadTypeInfo(this->roadtype)->strings.err_remove_road, CcPlaySound_CONSTRUCTION_OTHER,
								end_tile, start_tile, _cur_roadtype, _place_road_dir, start_half, _place_road_end_half);
					} else {
						Command<Commands::BuildRoadLong>::Post(GetRoadTypeInfo(this->roadtype)->strings.err_build_road, CcPlaySound_CONSTRUCTION_OTHER,
								end_tile, start_tile, _cur_roadtype, _place_road_dir, _one_way_button_clicked ? DisallowedRoadDirection::Northbound : DisallowedRoadDirections{}, start_half, _place_road_end_half, false);
					}
					break;
				}

				case DDSP_BUILD_ROAD_WAYPOINT:
				case DDSP_REMOVE_ROAD_WAYPOINT:
					if (this->IsWidgetLowered(WID_ROT_BUILD_WAYPOINT)) {
						if (_remove_button_clicked) {
							Command<Commands::RemoveFromRoadWaypoint>::Post(STR_ERROR_CAN_T_REMOVE_ROAD_WAYPOINT, CcPlaySound_CONSTRUCTION_OTHER, end_tile, start_tile);
						} else {
							TileArea ta(start_tile, end_tile);
							Axis axis = select_method == VPM_X_LIMITED ? Axis::X : Axis::Y;
							bool adjacent = IsCtrlPressed();

							auto proc = [=](bool test, StationID to_join) -> bool {
								if (test) {
									return Command<Commands::BuildRoadWaypoint>::Do(CommandFlagsToDCFlags(GetCommandFlags<Commands::BuildRoadWaypoint>()), ta.tile, axis, ta.w, ta.h, _waypoint_gui.sel_class, _waypoint_gui.sel_type, StationID::Invalid(), adjacent).Succeeded();
								} else {
									return Command<Commands::BuildRoadWaypoint>::Post(STR_ERROR_CAN_T_BUILD_ROAD_WAYPOINT, CcPlaySound_CONSTRUCTION_OTHER, ta.tile, axis, ta.w, ta.h, _waypoint_gui.sel_class, _waypoint_gui.sel_type, to_join, adjacent);
								}
							};

							ShowSelectRoadWaypointIfNeeded(ta, proc);
						}
					}
					break;

				case DDSP_BUILD_BUSSTOP:
				case DDSP_REMOVE_BUSSTOP:
					if (this->IsWidgetLowered(WID_ROT_BUS_STATION) && GetIfClassHasNewStopsByType(RoadStopClass::Get(_roadstop_gui.sel_class), RoadStopType::Bus, _cur_roadtype)) {
						if (_remove_button_clicked) {
							TileArea ta(start_tile, end_tile);
							StringID str = GetRoadTypeInfo(this->roadtype)->strings.err_remove_station[RoadStopType::Bus];
							Command<Commands::RemoveRoadStop>::Post(str, CcPlaySound_CONSTRUCTION_OTHER, ta.tile, ta.w, ta.h, RoadStopType::Bus, IsCtrlPressed());
						} else {
							StringID str = GetRoadTypeInfo(this->roadtype)->strings.err_build_station[RoadStopType::Bus];
							PlaceRoadStop(start_tile, end_tile, RoadStopType::Bus, IsCtrlPressed(), _cur_roadtype, str);
						}
					}
					break;

				case DDSP_BUILD_TRUCKSTOP:
				case DDSP_REMOVE_TRUCKSTOP:
					if (this->IsWidgetLowered(WID_ROT_TRUCK_STATION) && GetIfClassHasNewStopsByType(RoadStopClass::Get(_roadstop_gui.sel_class), RoadStopType::Truck, _cur_roadtype)) {
						if (_remove_button_clicked) {
							TileArea ta(start_tile, end_tile);
							StringID str = GetRoadTypeInfo(this->roadtype)->strings.err_remove_station[RoadStopType::Truck];
							Command<Commands::RemoveRoadStop>::Post(str, CcPlaySound_CONSTRUCTION_OTHER, ta.tile, ta.w, ta.h, RoadStopType::Truck, IsCtrlPressed());
						} else {
							StringID str = GetRoadTypeInfo(this->roadtype)->strings.err_build_station[RoadStopType::Truck];
							PlaceRoadStop(start_tile, end_tile, RoadStopType::Truck, IsCtrlPressed(), _cur_roadtype, str);
						}
					}
					break;

				case DDSP_CONVERT_ROAD:
					Command<Commands::ConvertRoad>::Post(GetRoadTypeInfo(this->roadtype)->strings.err_convert_road, CcPlaySound_CONSTRUCTION_OTHER, end_tile, start_tile, _cur_roadtype, IsCtrlPressed());
					break;
			}
		}
	}

	void OnPlacePresize([[maybe_unused]] Point pt, TileIndex tile) override
	{
		Command<Commands::BuildTunnel>::Do(DoCommandFlag::Auto, tile, TransportType::Road, INVALID_RAILTYPE, _cur_roadtype);
		VpSetPresizeRange(tile, _build_tunnel_endtile == INVALID_TILE ? tile : _build_tunnel_endtile);
	}

	EventState OnCTRLStateChange() override
	{
		if (RoadToolbar_CtrlChanged(this)) return EventState::Handled;
		return EventState::NotHandled;
	}

	void OnRealtimeTick([[maybe_unused]] uint delta_ms) override
	{
		if (_game_mode == GameMode::Normal && this->IsWidgetLowered(WID_ROT_BUILD_WAYPOINT)) CheckRedrawRoadWaypointCoverage(this);
	}

	/**
	 * Handler for global hotkeys of the BuildRoadToolbarWindow.
	 * @param hotkey Hotkey
	 * @param last_build Last build road type
	 * @param rtt The road/tram type.
	 * @return EventState::Handled if hotkey was accepted.
	 */
	static EventState RoadTramToolbarGlobalHotkeys(int hotkey, RoadType last_build, RoadTramType rtt)
	{
		Window *w = nullptr;
		switch (_game_mode) {
			case GameMode::Normal:
				w = ShowBuildRoadToolbar(last_build);
				break;

			case GameMode::Editor:
				if (!GetRoadTypes(true).Any(GetMaskForRoadTramType(rtt))) return EventState::NotHandled;
				w = ShowBuildRoadScenToolbar(last_build);
				break;

			default:
				break;
		}

		if (w == nullptr) return EventState::NotHandled;
		return w->OnHotkey(hotkey);
	}

	static EventState RoadToolbarGlobalHotkeys(int hotkey)
	{
		return RoadTramToolbarGlobalHotkeys(hotkey, _last_built_roadtype, RoadTramType::Road);
	}

	static EventState TramToolbarGlobalHotkeys(int hotkey)
	{
		return RoadTramToolbarGlobalHotkeys(hotkey, _last_built_tramtype, RoadTramType::Tram);
	}

	static inline HotkeyList road_hotkeys{"roadtoolbar", {
		Hotkey('1', "build_x", WID_ROT_ROAD_X),
		Hotkey('2', "build_y", WID_ROT_ROAD_Y),
		Hotkey('3', "autoroad", WID_ROT_AUTOROAD),
		Hotkey('4', "demolish", WID_ROT_DEMOLISH),
		Hotkey('5', "depot", WID_ROT_DEPOT),
		Hotkey('6', "bus_station", WID_ROT_BUS_STATION),
		Hotkey('7', "truck_station", WID_ROT_TRUCK_STATION),
		Hotkey('8', "oneway", WID_ROT_ONE_WAY),
		Hotkey('9', "waypoint", WID_ROT_BUILD_WAYPOINT),
		Hotkey('B', "bridge", WID_ROT_BUILD_BRIDGE),
		Hotkey('T', "tunnel", WID_ROT_BUILD_TUNNEL),
		Hotkey('R', "remove", WID_ROT_REMOVE),
		Hotkey('C', "convert", WID_ROT_CONVERT_ROAD),
		Hotkey(WKC_L_BRACKET, "prev_roadtype", to_underlying(SpecialListHotkeys::PreviousItem)),
		Hotkey(WKC_R_BRACKET, "next_roadtype", to_underlying(SpecialListHotkeys::NextItem)),
		Hotkey(WKC_L_BRACKET | WKC_CTRL, "first_roadtype", to_underlying(SpecialListHotkeys::FirstItem)),
		Hotkey(WKC_R_BRACKET | WKC_CTRL, "last_roadtype", to_underlying(SpecialListHotkeys::LastItem)),
	}, RoadToolbarGlobalHotkeys};

	static inline HotkeyList tram_hotkeys{"tramtoolbar", {
		Hotkey('1', "build_x", WID_ROT_ROAD_X),
		Hotkey('2', "build_y", WID_ROT_ROAD_Y),
		Hotkey('3', "autoroad", WID_ROT_AUTOROAD),
		Hotkey('4', "demolish", WID_ROT_DEMOLISH),
		Hotkey('5', "depot", WID_ROT_DEPOT),
		Hotkey('6', "bus_station", WID_ROT_BUS_STATION),
		Hotkey('7', "truck_station", WID_ROT_TRUCK_STATION),
		Hotkey('9', "waypoint", WID_ROT_BUILD_WAYPOINT),
		Hotkey('B', "bridge", WID_ROT_BUILD_BRIDGE),
		Hotkey('T', "tunnel", WID_ROT_BUILD_TUNNEL),
		Hotkey('R', "remove", WID_ROT_REMOVE),
		Hotkey('C', "convert", WID_ROT_CONVERT_ROAD),
		Hotkey(WKC_L_BRACKET, "prev_tramtype", to_underlying(SpecialListHotkeys::PreviousItem)),
		Hotkey(WKC_R_BRACKET, "next_tramtype", to_underlying(SpecialListHotkeys::NextItem)),
		Hotkey(WKC_L_BRACKET | WKC_CTRL, "first_tramtype", to_underlying(SpecialListHotkeys::FirstItem)),
		Hotkey(WKC_R_BRACKET | WKC_CTRL, "last_tramtype", to_underlying(SpecialListHotkeys::LastItem)),
	}, TramToolbarGlobalHotkeys};
};

/** One tool button of the mobile road toolbar. */
#define MOBILE_ROAD_TOOL(widget, sprite, tip) NWidget(WWT_IMGBTN, Colours::Grey, widget), SetMinimalSize(40, 40), SetSpriteTip(sprite, tip)

/** The tools that the mobile layout keeps in its More flyout; never shown in the toolbar itself. */
#define MOBILE_ROAD_HIDDEN_TOOLS \
	MOBILE_ROAD_TOOL(WID_ROT_ROAD_X, SPR_IMG_ROAD_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION), \
	MOBILE_ROAD_TOOL(WID_ROT_ROAD_Y, SPR_IMG_ROAD_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION), \
	MOBILE_ROAD_TOOL(WID_ROT_ONE_WAY, SPR_IMG_ROAD_ONE_WAY, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_ONE_WAY_ROAD), \
	MOBILE_ROAD_TOOL(WID_ROT_BUILD_WAYPOINT, SPR_IMG_WAYPOINT, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD_TO_WAYPOINT), \
	MOBILE_ROAD_TOOL(WID_ROT_CONVERT_ROAD, SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD)

/** The status line, Rotate, Cancel and Build of the mobile road toolbar. */
#define MOBILE_ROAD_CONFIRM_PARTS \
	NWidget(WWT_INSET, Colours::Grey, WID_ROT_STATUS), SetMinimalSize(120, 40), SetFill(1, 0), SetResize(1, 0), EndContainer(), \
	NWidget(NWID_SELECTION, Colours::Invalid, WID_ROT_SEL_ROTATE), \
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_ROT_ROTATE), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_BUILD_ROTATE, STR_MOBILE_BUILD_ROTATE_TOOLTIP), \
	EndContainer(), \
	NWidget(NWID_SELECTION, Colours::Invalid, WID_ROT_SEL_CANCEL), \
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_ROT_CANCEL), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_CROSS, STR_MOBILE_BUILD_CANCEL_TOOLTIP), \
	EndContainer(), \
	NWidget(NWID_SELECTION, Colours::Invalid, WID_ROT_SEL_CONFIRM), \
		NWidget(WWT_PUSHTXTBTN, Colours::Green, WID_ROT_CONFIRM), SetMinimalSize(56, 40), SetStringTip(STR_MOBILE_BUILD_CONFIRM_BUILD, STR_MOBILE_BUILD_CONFIRM_TOOLTIP), \
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_ROT_UNDO), SetMinimalSize(100, 40), SetStringTip(STR_MOBILE_BUILD_UNDO, STR_MOBILE_BUILD_UNDO_TOOLTIP), \
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_ROT_SHOW_TOOLS), SetMinimalSize(100, 40), SetStringTip(STR_MOBILE_BUILD_SHOW_TOOLS, STR_MOBILE_BUILD_SHOW_TOOLS_TOOLTIP), \
	EndContainer()

/** Road (and tram) toolbar of the mobile layout in landscape: one bar along the bottom of the map (round 5 design, 6.1). */
static constexpr std::initializer_list<NWidgetPart> _nested_mobile_road_landscape_widgets = {
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_HORIZONTAL), SetPadding(3, 4, 3, 4), SetPIP(0, 4, 0),
			NWidget(NWID_SELECTION, Colours::Invalid, WID_ROT_SEL_TOOLS),
				NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
					MOBILE_ROAD_TOOL(WID_ROT_CATEGORY, SPR_IMG_BUILDROAD, STR_MOBILE_ROAD_CATEGORY_TOOLTIP),
					NWidget(NWID_SPACER), SetMinimalSize(0, 0),
					MOBILE_ROAD_TOOL(WID_ROT_AUTOROAD, SPR_IMG_AUTOROAD, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOROAD),
					MOBILE_ROAD_TOOL(WID_ROT_REMOVE, SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_ROAD),
					MOBILE_ROAD_TOOL(WID_ROT_DEMOLISH, SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
					NWidget(NWID_SPACER), SetMinimalSize(0, 0),
					MOBILE_ROAD_TOOL(WID_ROT_BUS_STATION, SPR_IMG_BUS_STATION, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_BUS_STATION),
					MOBILE_ROAD_TOOL(WID_ROT_TRUCK_STATION, SPR_IMG_TRUCK_BAY, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRUCK_LOADING_BAY),
					MOBILE_ROAD_TOOL(WID_ROT_DEPOT, SPR_IMG_ROAD_DEPOT, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_VEHICLE_DEPOT),
					MOBILE_ROAD_TOOL(WID_ROT_BUILD_BRIDGE, SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_BRIDGE),
					MOBILE_ROAD_TOOL(WID_ROT_BUILD_TUNNEL, SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_TUNNEL),
					NWidget(NWID_SPACER), SetMinimalSize(0, 0),
					NWidget(WWT_PANEL, Colours::Grey, WID_ROT_MORE), SetMinimalSize(40, 40), SetToolTip(STR_MOBILE_ROAD_MORE_TOOLTIP), EndContainer(),
				EndContainer(),
			EndContainer(),
			MOBILE_ROAD_CONFIRM_PARTS,
			NWidget(NWID_SELECTION, Colours::Invalid, WID_ROT_SEL_HIDDEN),
				NWidget(NWID_HORIZONTAL),
					MOBILE_ROAD_HIDDEN_TOOLS,
					NWidget(WWT_PUSHTXTBTN, Colours::Green, WID_ROT_EXIT), SetMinimalSize(56, 40), SetStringTip(STR_MOBILE_BUILD_EXIT, STR_MOBILE_BUILD_EXIT_TOOLTIP),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		/* The toolbar reaches the screen edge; its buttons stay inside the safe area. */
		NWidget(WWT_EMPTY, Colours::Invalid, WID_ROT_GAP_BOTTOM), SetFill(1, 0), SetResize(1, 0),
	EndContainer(),
};

/** Road (and tram) toolbar of the mobile layout in portrait: two rows replacing the dock; row B holds 8 buttons. */
static constexpr std::initializer_list<NWidgetPart> _nested_mobile_road_portrait_widgets = {
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_VERTICAL), SetPadding(4), SetPIP(0, 4, 0),
			NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
				NWidget(WWT_PUSHTXTBTN, Colours::Green, WID_ROT_EXIT), SetMinimalSize(56, 40), SetStringTip(STR_MOBILE_BUILD_EXIT, STR_MOBILE_BUILD_EXIT_TOOLTIP),
				MOBILE_ROAD_CONFIRM_PARTS,
				/* Inside this row rather than below it: in a vertical container, a child that can't resize
				 * (like this always hidden selection) stops the whole container from resizing horizontally. */
				NWidget(NWID_SELECTION, Colours::Invalid, WID_ROT_SEL_HIDDEN),
					NWidget(NWID_HORIZONTAL),
						MOBILE_ROAD_HIDDEN_TOOLS,
						MOBILE_ROAD_TOOL(WID_ROT_BUILD_BRIDGE, SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_BRIDGE),
						MOBILE_ROAD_TOOL(WID_ROT_BUILD_TUNNEL, SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_TUNNEL),
					EndContainer(),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SELECTION, Colours::Invalid, WID_ROT_SEL_TOOLS),
				NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
					MOBILE_ROAD_TOOL(WID_ROT_CATEGORY, SPR_IMG_BUILDROAD, STR_MOBILE_ROAD_CATEGORY_TOOLTIP), SetFill(1, 0), SetResize(1, 0),
					MOBILE_ROAD_TOOL(WID_ROT_AUTOROAD, SPR_IMG_AUTOROAD, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOROAD), SetFill(1, 0), SetResize(1, 0),
					MOBILE_ROAD_TOOL(WID_ROT_REMOVE, SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_ROAD), SetFill(1, 0), SetResize(1, 0),
					MOBILE_ROAD_TOOL(WID_ROT_DEMOLISH, SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC), SetFill(1, 0), SetResize(1, 0),
					MOBILE_ROAD_TOOL(WID_ROT_BUS_STATION, SPR_IMG_BUS_STATION, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_BUS_STATION), SetFill(1, 0), SetResize(1, 0),
					MOBILE_ROAD_TOOL(WID_ROT_TRUCK_STATION, SPR_IMG_TRUCK_BAY, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRUCK_LOADING_BAY), SetFill(1, 0), SetResize(1, 0),
					MOBILE_ROAD_TOOL(WID_ROT_DEPOT, SPR_IMG_ROAD_DEPOT, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_VEHICLE_DEPOT), SetFill(1, 0), SetResize(1, 0),
					NWidget(WWT_PANEL, Colours::Grey, WID_ROT_MORE), SetMinimalSize(40, 40), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_ROAD_MORE_TOOLTIP), EndContainer(),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		/* The toolbar reaches the screen edge; its buttons stay inside the safe area. */
		NWidget(WWT_EMPTY, Colours::Invalid, WID_ROT_GAP_BOTTOM), SetFill(1, 0), SetResize(1, 0),
	EndContainer(),
};

#undef MOBILE_ROAD_CONFIRM_PARTS
#undef MOBILE_ROAD_HIDDEN_TOOLS
#undef MOBILE_ROAD_TOOL

static WindowDesc _mobile_road_landscape_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::BuildToolbar, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_mobile_road_landscape_widgets,
	&BuildRoadToolbarWindow::road_hotkeys
);

static WindowDesc _mobile_road_portrait_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::BuildToolbar, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_mobile_road_portrait_widgets,
	&BuildRoadToolbarWindow::road_hotkeys
);

/** Flyout of the mobile road toolbar with the tools that don't fit in the toolbar (as the rail toolbar's). */
struct MobileRoadMoreWindow : Window {
	BuildRoadToolbarWindow *toolbar; ///< The toolbar the tools belong to.

	MobileRoadMoreWindow(WindowDesc &desc, BuildRoadToolbarWindow *toolbar) : Window(desc), toolbar(toolbar)
	{
		this->CreateNestedTree();
		size_t tools = toolbar->GetMoreTools().size();
		this->GetWidget<NWidgetStacked>(WID_RMORE_SEL_ROW2)->SetDisplayedPlane(tools > 4 ? 0 : SZSP_HORIZONTAL);
		this->GetWidget<NWidgetStacked>(WID_RMORE_SEL_EXTRA)->SetDisplayedPlane(tools > 6 ? 0 : SZSP_NONE);
		this->FinishInitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
	}

	Point OnInitialPosition(int16_t sm_width, int16_t sm_height, [[maybe_unused]] int window_number) override
	{
		/* Above the whole toolbar (so the status line, Cancel and Build stay visible), right edge aligned with More. */
		const NWidgetBase *more = this->toolbar->GetWidget<NWidgetBase>(WID_ROT_MORE);
		int right = this->toolbar->left + more->pos_x + more->current_x;
		return {Clamp(right - sm_width, 0, std::max(0, _screen.width - sm_width)), std::max(0, this->toolbar->top - sm_height)};
	}

	/**
	 * The toolbar tool behind a button.
	 * @param widget The button.
	 * @return The tool, or \c INVALID_WIDGET.
	 */
	WidgetID GetTool(WidgetID widget) const
	{
		auto tools = this->toolbar->GetMoreTools();
		size_t index = widget - WID_RMORE_FIRST;
		return index < tools.size() ? tools[index] : INVALID_WIDGET;
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		WidgetID tool = this->GetTool(widget);
		if (tool == INVALID_WIDGET) return;
		bool active = this->toolbar->last_started_action == tool && this->toolbar->IsWidgetLowered(tool);
		DrawMobileIconButton(r, this->toolbar->GetToolSprite(tool), this->toolbar->GetToolLabel(tool), active, this->toolbar->IsWidgetDisabled(tool));
	}

	void OnPaint() override
	{
		for (WidgetID widget = WID_RMORE_FIRST; widget <= WID_RMORE_LAST; ++widget) {
			WidgetID tool = this->GetTool(widget);
			this->SetWidgetLoweredState(widget, tool != INVALID_WIDGET && this->toolbar->last_started_action == tool && this->toolbar->IsWidgetLowered(tool));
		}
		this->DrawWidgets();
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		WidgetID tool = this->GetTool(widget);
		if (tool == INVALID_WIDGET || this->toolbar->IsWidgetDisabled(tool)) return;
		BuildRoadToolbarWindow *toolbar = this->toolbar;
		this->Close();
		toolbar->OnClick({0, 0}, tool, 1);
	}
};

/** One button of the More flyout: every cell the same 56x48, left-aligned. */
#define MOBILE_ROAD_MORE_BUTTON(index) NWidget(WWT_PANEL, Colours::Grey, WID_RMORE_FIRST + (index)), SetMinimalSize(56, 48), SetFill(0, 0), EndContainer()

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_road_more_widgets = {
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_VERTICAL), SetPadding(4), SetPIP(0, 4, 0),
			NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
				MOBILE_ROAD_MORE_BUTTON(0), MOBILE_ROAD_MORE_BUTTON(1), MOBILE_ROAD_MORE_BUTTON(2), MOBILE_ROAD_MORE_BUTTON(3),
			EndContainer(),
			NWidget(NWID_SELECTION, Colours::Invalid, WID_RMORE_SEL_ROW2),
				NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
					MOBILE_ROAD_MORE_BUTTON(4), MOBILE_ROAD_MORE_BUTTON(5),
					NWidget(NWID_SELECTION, Colours::Invalid, WID_RMORE_SEL_EXTRA),
						NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
							MOBILE_ROAD_MORE_BUTTON(6), MOBILE_ROAD_MORE_BUTTON(7),
						EndContainer(),
					EndContainer(),
					NWidget(NWID_SPACER), SetFill(1, 0),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

#undef MOBILE_ROAD_MORE_BUTTON

static WindowDesc _mobile_road_more_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileRailMore, WindowClass::None,
	{},
	_nested_mobile_road_more_widgets
);

/**
 * Open the More flyout of the mobile road toolbar, or close it when it is open.
 * @param toolbar The toolbar.
 */
static void ShowMobileRoadMoreFlyout(BuildRoadToolbarWindow *toolbar)
{
	if (FindWindowById(WindowClass::MobileRailMore, 0) != nullptr) {
		CloseWindowById(WindowClass::MobileRailMore, 0);
		return;
	}
	new MobileRoadMoreWindow(_mobile_road_more_desc, toolbar);
}

/**
 * Get the road (or tram) toolbar of the mobile layout.
 * @return The toolbar, or \c nullptr when it is not open.
 */
Window *GetMobileRoadToolbar()
{
	BuildRoadToolbarWindow *w = dynamic_cast<BuildRoadToolbarWindow *>(FindWindowById(WindowClass::BuildToolbar, TransportType::Road));
	return (w != nullptr && w->mobile) ? w : nullptr;
}

/**
 * Is a mobile road toolbar the landscape layout?
 * @param w The toolbar.
 * @return \c true for the landscape layout.
 */
bool IsMobileRoadToolbarLandscape(const Window *w)
{
	const BuildRoadToolbarWindow *t = dynamic_cast<const BuildRoadToolbarWindow *>(w);
	return t != nullptr && t->landscape;
}

/**
 * Let the mobile road toolbar re-plan which of its parts show, e.g. after a sheet opened beside it.
 * @param w The mobile road toolbar.
 */
void UpdateMobileRoadToolbarPlanes(Window *w)
{
	if (BuildRoadToolbarWindow *toolbar = dynamic_cast<BuildRoadToolbarWindow *>(w); toolbar != nullptr && toolbar->mobile) toolbar->UpdateMobileWidgets();
}

/** After a rotation: open the mobile road toolbar in the other layout, with the same tool. */
void RecreateMobileRoadToolbar()
{
	BuildRoadToolbarWindow *w = dynamic_cast<BuildRoadToolbarWindow *>(GetMobileRoadToolbar());
	if (w == nullptr) return;
	WidgetID tool = (_thd.GetCallbackWnd() == w && w->last_started_action != INVALID_WIDGET && w->IsWidgetLowered(w->last_started_action)) ? w->last_started_action : INVALID_WIDGET;
	w->Close();
	Window *nw = ShowBuildRoadToolbar(_cur_roadtype);
	if (nw != nullptr && tool != INVALID_WIDGET) nw->OnClick({0, 0}, tool, 1);
}

static constexpr std::initializer_list<NWidgetPart> _nested_build_road_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen, WID_ROT_CAPTION), SetTextStyle(TextColour::White),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_ROAD_X),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ROAD_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ROAD_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_AUTOROAD, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOROAD),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_DEPOT),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ROAD_DEPOT, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_VEHICLE_DEPOT),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_BUILD_WAYPOINT),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_WAYPOINT, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD_TO_WAYPOINT),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_BUS_STATION),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_BUS_STATION, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_BUS_STATION),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_TRUCK_STATION),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_TRUCK_BAY, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRUCK_LOADING_BAY),

		NWidget(WWT_PANEL, Colours::DarkGreen), SetFill(1, 1), EndContainer(),

		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_ONE_WAY),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ROAD_ONE_WAY, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_ONE_WAY_ROAD),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetToolbarMinimalSize(2), SetSpriteTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_BRIDGE),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_TUNNEL),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_REMOVE),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_ROAD),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD),
	EndContainer(),
};

/** Window definition for the road toolbar. */
static WindowDesc _build_road_desc(
	WindowPosition::Manual, "toolbar_road", 0, 0,
	WindowClass::BuildToolbar, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_build_road_widgets,
	&BuildRoadToolbarWindow::road_hotkeys
);

static constexpr std::initializer_list<NWidgetPart> _nested_build_tramway_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen, WID_ROT_CAPTION), SetTextStyle(TextColour::White),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_ROAD_X),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_TRAMWAY_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_TRAMWAY_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_AUTOTRAM, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOTRAM),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_DEPOT),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ROAD_DEPOT, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAM_VEHICLE_DEPOT),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_BUILD_WAYPOINT),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_WAYPOINT, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_TRAM_TO_WAYPOINT),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_BUS_STATION),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_BUS_STATION, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_PASSENGER_TRAM_STATION),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_TRUCK_STATION),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_TRUCK_BAY, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_CARGO_TRAM_STATION),

		NWidget(WWT_PANEL, Colours::DarkGreen), SetFill(1, 1), EndContainer(),

		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetToolbarMinimalSize(2), SetSpriteTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_BRIDGE),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_TUNNEL),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_REMOVE),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_TRAMWAYS),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_TRAM),
	EndContainer(),
};

/** Window definition for the tram toolbar. */
static WindowDesc _build_tramway_desc(
	WindowPosition::Manual, "toolbar_tramway", 0, 0,
	WindowClass::BuildToolbar, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_build_tramway_widgets,
	&BuildRoadToolbarWindow::tram_hotkeys
);

/**
 * Open the build road toolbar window
 *
 * If the terraform toolbar is linked to the toolbar, that window is also opened.
 *
 * @param roadtype The road type for the toolbar.
 * @return newly opened road toolbar, or nullptr if the toolbar could not be opened.
 */
Window *ShowBuildRoadToolbar(RoadType roadtype)
{
	if (IsMobileLayout() && Company::IsValidID(_local_company) && ValParamRoadType(roadtype)) {
		_cur_roadtype = roadtype;
		/* The same toolbar for another road type of the same kind keeps its tool; road and tram swap toolbars. */
		if (BuildRoadToolbarWindow *w = dynamic_cast<BuildRoadToolbarWindow *>(FindWindowById(WindowClass::BuildToolbar, TransportType::Road)); w != nullptr && w->mobile) {
			if (RoadTypeIsRoad(w->roadtype) == RoadTypeIsRoad(roadtype)) {
				w->OnInvalidateData();
				return w;
			}
		}
		CloseWindowByClass(WindowClass::BuildToolbar);
		_remove_button_clicked = false;
		_one_way_button_clicked = false;
		bool landscape = _screen.width > _screen.height;
		return new BuildRoadToolbarWindow(landscape ? _mobile_road_landscape_desc : _mobile_road_portrait_desc, TransportType::Road, true, landscape);
	}

	if (!Company::IsValidID(_local_company)) return nullptr;
	if (!ValParamRoadType(roadtype)) return nullptr;

	if (GetRoadTramType(roadtype) == GetRoadTramType(_cur_roadtype)) {
		Window *w = BringWindowToFrontById(WindowClass::BuildToolbar, TransportType::Road);

		if (w != nullptr) {
			_cur_roadtype = roadtype;
			w->OnInvalidateData();
			return w;
		}
	}

	_cur_roadtype = roadtype;
	CloseWindowByClass(WindowClass::BuildToolbar);
	return AllocateWindowDescFront<BuildRoadToolbarWindow>(RoadTypeIsRoad(_cur_roadtype) ? _build_road_desc : _build_tramway_desc, TransportType::Road);
}

static constexpr std::initializer_list<NWidgetPart> _nested_build_road_scen_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen, WID_ROT_CAPTION), SetTextStyle(TextColour::White),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_ROAD_X),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ROAD_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ROAD_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_AUTOROAD, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOROAD),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),

		NWidget(WWT_PANEL, Colours::DarkGreen), SetFill(1, 1), EndContainer(),

		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_ONE_WAY),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ROAD_ONE_WAY, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_ONE_WAY_ROAD),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetToolbarMinimalSize(2), SetSpriteTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_BRIDGE),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_TUNNEL),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_REMOVE),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_ROAD),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD),
	EndContainer(),
};

/** Window definition for the road toolbar of the scenario editor. */
static WindowDesc _build_road_scen_desc(
	WindowPosition::Automatic, "toolbar_road_scen", 0, 0,
	WindowClass::ScenarioBuildToolbar, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_build_road_scen_widgets,
	&BuildRoadToolbarWindow::road_hotkeys
);

static constexpr std::initializer_list<NWidgetPart> _nested_build_tramway_scen_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen, WID_ROT_CAPTION), SetTextStyle(TextColour::White),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_ROAD_X),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_TRAMWAY_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_TRAMWAY_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_AUTOTRAM, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOTRAM),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),

		NWidget(WWT_PANEL, Colours::DarkGreen), SetFill(1, 1), EndContainer(),

		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetToolbarMinimalSize(2), SetSpriteTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_BRIDGE),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_TUNNEL),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_REMOVE),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_TRAMWAYS),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_TRAM),
	EndContainer(),
};

/** Window definition for the tram toolbar of the scenario editor. */
static WindowDesc _build_tramway_scen_desc(
	WindowPosition::Automatic, "toolbar_tram_scen", 0, 0,
	WindowClass::ScenarioBuildToolbar, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_build_tramway_scen_widgets,
	&BuildRoadToolbarWindow::tram_hotkeys
);

/**
 * Show the road building toolbar in the scenario editor.
 * @param roadtype The road type for the toolbar.
 * @return The just opened toolbar, or \c nullptr if the toolbar was already open.
 */
Window *ShowBuildRoadScenToolbar(RoadType roadtype)
{
	CloseWindowById(WindowClass::ScenarioBuildToolbar, TransportType::Road);
	_cur_roadtype = roadtype;

	return AllocateWindowDescFront<BuildRoadToolbarWindow>(RoadTypeIsRoad(_cur_roadtype) ? _build_road_scen_desc : _build_tramway_scen_desc, TransportType::Road);
}

struct BuildRoadDepotWindow : public PickerWindowBase {
	BuildRoadDepotWindow(WindowDesc &desc, Window *parent) : PickerWindowBase(desc, parent)
	{
		this->CreateNestedTree();

		this->LowerWidget(WID_BROD_DEPOT_NE + _road_depot_orientation);
		if (RoadTypeIsTram(_cur_roadtype)) {
			this->GetWidget<NWidgetCore>(WID_BROD_CAPTION)->SetString(STR_BUILD_DEPOT_TRAM_ORIENTATION_CAPTION);
			for (WidgetID i = WID_BROD_DEPOT_NE; i <= WID_BROD_DEPOT_NW; i++) {
				this->GetWidget<NWidgetCore>(i)->SetToolTip(STR_BUILD_DEPOT_TRAM_ORIENTATION_SELECT_TOOLTIP);
			}
		}

		this->FinishInitNested(TransportType::Road);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (!IsInsideMM(widget, WID_BROD_DEPOT_NE, WID_BROD_DEPOT_NW + 1)) return;

		size.width  = ScaleGUITrad(64) + WidgetDimensions::scaled.fullbevel.Horizontal();
		size.height = ScaleGUITrad(48) + WidgetDimensions::scaled.fullbevel.Vertical();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (!IsInsideMM(widget, WID_BROD_DEPOT_NE, WID_BROD_DEPOT_NW + 1)) return;

		DrawPixelInfo tmp_dpi;
		Rect ir = r.Shrink(WidgetDimensions::scaled.bevel);
		if (FillDrawPixelInfo(&tmp_dpi, ir)) {
			AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);
			int x = (ir.Width()  - ScaleSpriteTrad(64)) / 2 + ScaleSpriteTrad(31);
			int y = (ir.Height() + ScaleSpriteTrad(48)) / 2 - ScaleSpriteTrad(31);
			DrawRoadDepotSprite(x, y, static_cast<DiagDirection>(widget - WID_BROD_DEPOT_NE + to_underlying(DiagDirection::NE)), _cur_roadtype);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_BROD_DEPOT_NW:
			case WID_BROD_DEPOT_NE:
			case WID_BROD_DEPOT_SW:
			case WID_BROD_DEPOT_SE:
				this->RaiseWidget(WID_BROD_DEPOT_NE + _road_depot_orientation);
				_road_depot_orientation = (DiagDirection)(widget - WID_BROD_DEPOT_NE);
				this->LowerWidget(WID_BROD_DEPOT_NE + _road_depot_orientation);
				SndClickBeep();
				this->SetDirty();
				break;

			default:
				break;
		}
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_build_road_depot_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen, WID_BROD_CAPTION), SetStringTip(STR_BUILD_DEPOT_ROAD_ORIENTATION_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::DarkGreen),
		NWidget(NWID_HORIZONTAL_LTR), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0), SetPIPRatio(1, 0, 1), SetPadding(WidgetDimensions::unscaled.picker),
			NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0),
				NWidget(WWT_TEXTBTN, Colours::Grey, WID_BROD_DEPOT_NW), SetFill(0, 0), SetToolTip(STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				NWidget(WWT_TEXTBTN, Colours::Grey, WID_BROD_DEPOT_SW), SetFill(0, 0), SetToolTip(STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0),
				NWidget(WWT_TEXTBTN, Colours::Grey, WID_BROD_DEPOT_NE), SetFill(0, 0), SetToolTip(STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				NWidget(WWT_TEXTBTN, Colours::Grey, WID_BROD_DEPOT_SE), SetFill(0, 0), SetToolTip(STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

/** Window definition for the build road depot window. */
static WindowDesc _build_road_depot_desc(
	WindowPosition::Automatic, {}, 0, 0,
	WindowClass::BuildDepot, WindowClass::BuildToolbar,
	WindowDefaultFlag::Construction,
	_nested_build_road_depot_widgets
);

static void ShowRoadDepotPicker(Window *parent)
{
	new BuildRoadDepotWindow(_build_road_depot_desc, parent);
}

template <RoadStopType roadstoptype>
class RoadStopPickerCallbacks : public PickerCallbacksNewGRFClass<RoadStopClass> {
public:
	RoadStopPickerCallbacks(const std::string &ini_group) : PickerCallbacksNewGRFClass<RoadStopClass>(ini_group) {}

	GrfSpecFeature GetFeature() const override { return GrfSpecFeature::RoadStops; }

	StringID GetClassTooltip() const override;
	StringID GetTypeTooltip() const override;
	StringID GetCollectionTooltip() const override;

	bool IsActive() const override
	{
		for (const auto &cls : RoadStopClass::Classes()) {
			if (IsWaypointClass(cls)) continue;
			for (const auto *spec : cls.Specs()) {
				if (spec == nullptr) continue;
				if (roadstoptype == RoadStopType::Truck && spec->stop_type != ROADSTOPTYPE_FREIGHT && spec->stop_type != ROADSTOPTYPE_ALL) continue;
				if (roadstoptype == RoadStopType::Bus && spec->stop_type != ROADSTOPTYPE_PASSENGER && spec->stop_type != ROADSTOPTYPE_ALL) continue;
				return true;
			}
		}
		return false;
	}

	static bool IsClassChoice(const RoadStopClass &cls)
	{
		return !IsWaypointClass(cls) && GetIfClassHasNewStopsByType(&cls, roadstoptype, _cur_roadtype);
	}

	bool HasClassChoice() const override
	{
		return std::ranges::count_if(RoadStopClass::Classes(), IsClassChoice);
	}

	int GetSelectedClass() const override { return _roadstop_gui.sel_class.base(); }
	void SetSelectedClass(int id) const override { _roadstop_gui.sel_class = this->GetClassIndex(id); }

	StringID GetClassName(int id) const override
	{
		const auto *rsc = this->GetClass(id);
		if (!IsClassChoice(*rsc)) return INVALID_STRING_ID;
		return rsc->name;
	}

	int GetSelectedType() const override { return _roadstop_gui.sel_type; }
	void SetSelectedType(int id) const override { _roadstop_gui.sel_type = id; }

	StringID GetTypeName(int cls_id, int id) const override
	{
		const auto *spec = this->GetSpec(cls_id, id);
		if (!IsRoadStopEverAvailable(spec, roadstoptype == RoadStopType::Bus ? StationType::Bus : StationType::Truck)) return INVALID_STRING_ID;
		return (spec == nullptr) ? STR_STATION_CLASS_DFLT_ROADSTOP : spec->name;
	}

	std::span<const BadgeID> GetTypeBadges(int cls_id, int id) const override
	{
		const auto *spec = this->GetSpec(cls_id, id);
		if (!IsRoadStopEverAvailable(spec, roadstoptype == RoadStopType::Bus ? StationType::Bus : StationType::Truck)) return {};
		if (spec == nullptr) return {};
		return spec->badges;
	}

	bool IsTypeAvailable(int cls_id, int id) const override
	{
		const auto *spec = this->GetSpec(cls_id, id);
		return IsRoadStopAvailable(spec, roadstoptype == RoadStopType::Bus ? StationType::Bus : StationType::Truck);
	}

	void DrawType(int x, int y, int cls_id, int id) const override
	{
		const auto *spec = this->GetSpec(cls_id, id);
		if (spec == nullptr) {
			StationPickerDrawSprite(x, y, roadstoptype == RoadStopType::Bus ? StationType::Bus : StationType::Truck, INVALID_RAILTYPE, _cur_roadtype, to_underlying(_roadstop_gui.orientation));
		} else {
			DiagDirection orientation = _roadstop_gui.orientation;
			if (orientation < DiagDirection::End && spec->flags.Test(RoadStopSpecFlag::DriveThroughOnly)) orientation = DiagDirection::End;
			DrawRoadStopTile(x, y, _cur_roadtype, spec, roadstoptype == RoadStopType::Bus ? StationType::Bus : StationType::Truck, to_underlying(orientation));
		}
	}

	void FillUsedItems(std::set<PickerItem> &items) override
	{
		for (const Station *st : Station::Iterate()) {
			if (st->owner != _local_company) continue;
			if (roadstoptype == RoadStopType::Truck && !st->facilities.Test(StationFacility::TruckStop)) continue;
			if (roadstoptype == RoadStopType::Bus && !st->facilities.Test(StationFacility::BusStop)) continue;
			items.insert({GrfID{}, 0, ROADSTOP_CLASS_DFLT.base(), 0}); // We would need to scan the map to find out if default is used.
			for (const auto &sm : st->roadstop_speclist) {
				if (sm.spec == nullptr) continue;
				if (roadstoptype == RoadStopType::Truck && sm.spec->stop_type != ROADSTOPTYPE_FREIGHT && sm.spec->stop_type != ROADSTOPTYPE_ALL) continue;
				if (roadstoptype == RoadStopType::Bus && sm.spec->stop_type != ROADSTOPTYPE_PASSENGER && sm.spec->stop_type != ROADSTOPTYPE_ALL) continue;
				items.insert({sm.grfid, sm.localidx, sm.spec->class_index.base(), sm.spec->index});
			}
		}
	}
};

template <> StringID RoadStopPickerCallbacks<RoadStopType::Bus>::GetClassTooltip() const { return STR_PICKER_ROADSTOP_BUS_CLASS_TOOLTIP; }
template <> StringID RoadStopPickerCallbacks<RoadStopType::Bus>::GetTypeTooltip() const { return STR_PICKER_ROADSTOP_BUS_TYPE_TOOLTIP; }
template <> StringID RoadStopPickerCallbacks<RoadStopType::Bus>::GetCollectionTooltip() const { return STR_PICKER_ROADSTOP_BUS_COLLECTION_TOOLTIP; }

template <> StringID RoadStopPickerCallbacks<RoadStopType::Truck>::GetClassTooltip() const { return STR_PICKER_ROADSTOP_TRUCK_CLASS_TOOLTIP; }
template <> StringID RoadStopPickerCallbacks<RoadStopType::Truck>::GetTypeTooltip() const { return STR_PICKER_ROADSTOP_TRUCK_TYPE_TOOLTIP; }
template <> StringID RoadStopPickerCallbacks<RoadStopType::Truck>::GetCollectionTooltip() const { return STR_PICKER_ROADSTOP_TRUCK_COLLECTION_TOOLTIP; }

static RoadStopPickerCallbacks<RoadStopType::Bus> _bus_callback_instance("fav_passenger_roadstops");
static RoadStopPickerCallbacks<RoadStopType::Truck> _truck_callback_instance("fav_freight_roadstops");

static PickerCallbacks &GetRoadStopPickerCallbacks(RoadStopType rs)
{
	return rs == RoadStopType::Bus ? static_cast<PickerCallbacks &>(_bus_callback_instance) : static_cast<PickerCallbacks &>(_truck_callback_instance);
}

struct BuildRoadStationWindow : public PickerWindow {
private:
	uint coverage_height = 0; ///< Height of the coverage texts.
	bool mobile = false; ///< Uses the mobile layout, in the sheet slot (round 5 design, 6.2).

	/** @return Whether the picker builds drive-through stops. */
	static bool IsDriveThrough()
	{
		return _roadstop_gui.orientation >= DiagDirection::End;
	}

	/**
	 * Mobile layout: a click on the picker.
	 * @param widget The widget.
	 * @return \c true when handled.
	 */
	bool OnMobileClick(WidgetID widget)
	{
		const RoadStopSpec *spec = RoadStopClass::Get(_roadstop_gui.sel_class)->GetSpec(_roadstop_gui.sel_type);
		bool drive_through_only = RoadTypeIsTram(_cur_roadtype) || (spec != nullptr && spec->flags.Test(RoadStopSpecFlag::DriveThroughOnly));
		switch (widget) {
			case WID_BROS_CLOSE:
				/* Drops the stop tool, which closes this picker. */
				ResetObjectToPlace();
				return true;

			case WID_BROS_DRIVE_THROUGH:
				/* The axis comes from the road on the tapped tile; the picker keeps one for dragged areas. */
				if (!IsDriveThrough()) _roadstop_gui.orientation = static_cast<DiagDirection>(to_underlying(DiagDirection::End) + to_underlying(Axis::X));
				break;

			case WID_BROS_BAY:
				if (drive_through_only) return true;
				if (IsDriveThrough()) _roadstop_gui.orientation = DiagDirection::NE;
				break;

			case WID_BROS_STATION_NE:
			case WID_BROS_STATION_SE:
			case WID_BROS_STATION_SW:
			case WID_BROS_STATION_NW:
				if (drive_through_only) return true;
				_roadstop_gui.orientation = static_cast<DiagDirection>(widget - WID_BROS_STATION_NE);
				break;

			case WID_BROS_COVERAGE_TOGGLE:
				_settings_client.gui.station_show_coverage = !_settings_client.gui.station_show_coverage;
				SetViewportCatchmentStation(nullptr, true);
				MobileBuildStationResize(); // Shows or hides the catchment of a pending stop.
				break;

			case WID_BROS_JOIN: {
				DropDownList list;
				const std::vector<StationID> &candidates = GetMobileStationJoinCandidates();
				for (size_t i = 0; i < candidates.size(); i++) {
					list.push_back(MakeDropDownListStringItem(GetString(STR_MOBILE_STATION_JOIN, candidates[i]), static_cast<int>(i)));
				}
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_STATION_NEW, -1));
				StationID shown = GetMobileStationJoinShown();
				auto it = std::ranges::find(candidates, shown);
				ShowDropDownList(this, std::move(list), it == candidates.end() ? -1 : static_cast<int>(it - candidates.begin()), widget);
				return true;
			}

			default:
				return false;
		}
		SndClickBeep();
		CloseWindowById(WindowClass::JoinStation, 0);
		this->UpdateMobilePlanes();
		/* A pending stop follows the picker. */
		MobileBuildReestimate();
		this->SetDirty();
		return true;
	}

	/** Mobile layout: show the parts that apply (bay facing or "follows the road"; the join dropdown when it can join). */
	void UpdateMobilePlanes()
	{
		const RoadStopSpec *spec = RoadStopClass::Get(_roadstop_gui.sel_class)->GetSpec(_roadstop_gui.sel_type);
		bool drive_through_only = RoadTypeIsTram(_cur_roadtype) || (spec != nullptr && spec->flags.Test(RoadStopSpecFlag::DriveThroughOnly));
		if (drive_through_only && !IsDriveThrough()) _roadstop_gui.orientation = static_cast<DiagDirection>(to_underlying(DiagDirection::End) + to_underlying(Axis::X));

		bool reinit = false;
		auto set_plane = [&](WidgetID widget, int plane) {
			NWidgetStacked *sel = this->GetWidget<NWidgetStacked>(widget);
			if (sel->shown_plane == plane) return;
			sel->SetDisplayedPlane(plane);
			reinit = true;
		};
		/* Hidden as SZSP_HORIZONTAL: in a vertical container the picker must still resize to the sheet's width. */
		set_plane(WID_BROS_SEL_KIND, drive_through_only ? SZSP_HORIZONTAL : 0);
		set_plane(WID_BROS_SEL_FACING, IsDriveThrough() ? 1 : 0);
		set_plane(WID_BROS_SEL_JOIN, GetMobileStationJoinCandidates().empty() ? SZSP_HORIZONTAL : 0);
		this->SetWidgetLoweredState(WID_BROS_DRIVE_THROUGH, IsDriveThrough());
		this->SetWidgetLoweredState(WID_BROS_BAY, !IsDriveThrough());
		for (WidgetID w = WID_BROS_STATION_NE; w <= WID_BROS_STATION_NW; w++) {
			this->SetWidgetLoweredState(w, _roadstop_gui.orientation == static_cast<DiagDirection>(w - WID_BROS_STATION_NE));
		}
		this->SetWidgetLoweredState(WID_BROS_COVERAGE_TOGGLE, _settings_client.gui.station_show_coverage);
		if (reinit) {
			this->ReInit();
			RelocateMobileWindows();
		}
	}

	/**
	 * Mobile layout: draw a stop preview in part of a button.
	 * @param r Where.
	 * @param image The orientation's image (0-3 bays, 4-5 drive-through).
	 */
	void DrawMobileStopPreview(const Rect &r, int image) const
	{
		StationType st = this->window_class == WindowClass::BuildBusStation ? StationType::Bus : StationType::Truck;
		const RoadStopSpec *spec = RoadStopClass::Get(_roadstop_gui.sel_class)->GetSpec(_roadstop_gui.sel_type);
		DrawPixelInfo tmp_dpi;
		Rect ir = r.Shrink(WidgetDimensions::scaled.bevel);
		if (!FillDrawPixelInfo(&tmp_dpi, ir)) return;
		AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);
		int x = (ir.Width() - ScaleSpriteTrad(PREVIEW_WIDTH)) / 2 + ScaleSpriteTrad(PREVIEW_LEFT);
		int y = (ir.Height() + ScaleSpriteTrad(PREVIEW_HEIGHT)) / 2 - ScaleSpriteTrad(PREVIEW_BOTTOM);
		if (spec == nullptr) {
			StationPickerDrawSprite(x, y, st, INVALID_RAILTYPE, _cur_roadtype, image);
		} else {
			DrawRoadStopTile(x, y, _cur_roadtype, spec, st, image);
		}
	}

	void CheckOrientationValid()
	{
		if (this->mobile) return;

		const RoadStopSpec *spec = RoadStopClass::Get(_roadstop_gui.sel_class)->GetSpec(_roadstop_gui.sel_type);

		/* Raise and lower to ensure the correct widget is lowered after changing displayed orientation plane. */
		if (RoadTypeIsRoad(_cur_roadtype)) {
			this->RaiseWidget(WID_BROS_STATION_NE + _roadstop_gui.orientation);
			this->GetWidget<NWidgetStacked>(WID_BROS_AVAILABLE_ORIENTATIONS)->SetDisplayedPlane((spec != nullptr && spec->flags.Test(RoadStopSpecFlag::DriveThroughOnly)) ? 1 : 0);
			this->LowerWidget(WID_BROS_STATION_NE + _roadstop_gui.orientation);
		}

		if (_roadstop_gui.orientation >= DiagDirection::End) return;

		if (spec != nullptr && spec->flags.Test(RoadStopSpecFlag::DriveThroughOnly)) {
			this->RaiseWidget(WID_BROS_STATION_NE + _roadstop_gui.orientation);
			_roadstop_gui.orientation = DiagDirection::End;
			this->LowerWidget(WID_BROS_STATION_NE + _roadstop_gui.orientation);
			this->SetDirty();
			CloseWindowById(WindowClass::JoinStation, 0);
		}
	}

public:
	BuildRoadStationWindow(WindowDesc &desc, Window *parent, RoadStopType rs, bool mobile = false) : PickerWindow(desc, parent, TransportType::Road, GetRoadStopPickerCallbacks(rs)), mobile(mobile)
	{
		this->coverage_height = 2 * GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.vsep_normal;

		/* Trams don't have non-drivethrough stations */
		if (RoadTypeIsTram(_cur_roadtype) && _roadstop_gui.orientation < DiagDirection::End) {
			_roadstop_gui.orientation = DiagDirection::End;
		}
		this->ConstructWindow();

		if (mobile) {
			this->window_class = (rs == RoadStopType::Bus) ? WindowClass::BuildBusStation : WindowClass::BuildTruckStation;
			this->UpdateMobilePlanes();
			return;
		}

		const RoadTypeInfo *rti = GetRoadTypeInfo(_cur_roadtype);
		this->GetWidget<NWidgetCore>(WID_BROS_CAPTION)->SetString(rti->strings.picker_title[rs]);

		for (WidgetID i = RoadTypeIsTram(_cur_roadtype) ? WID_BROS_STATION_X : WID_BROS_STATION_NE; i < WID_BROS_LT_OFF; i++) {
			this->GetWidget<NWidgetCore>(i)->SetToolTip(rti->strings.picker_tooltip[rs]);
		}

		this->LowerWidget(WID_BROS_STATION_NE + _roadstop_gui.orientation);
		this->LowerWidget(WID_BROS_LT_OFF + _settings_client.gui.station_show_coverage);

		this->window_class = (rs == RoadStopType::Bus) ? WindowClass::BuildBusStation : WindowClass::BuildTruckStation;
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		CloseWindowById(WindowClass::JoinStation, 0);
		this->PickerWindow::Close();
	}

	void OnInit() override
	{
		/* The mobile layout has no class or type lists, so no badge filters either (see the rail station picker). */
		if (this->mobile) return;
		this->PickerWindow::OnInit();
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		this->PickerWindow::OnInvalidateData(data, gui_scope);

		if (gui_scope) {
			this->CheckOrientationValid();
			if (this->mobile) this->UpdateMobilePlanes();
		}
	}

	void OnPaint() override
	{
		const RoadStopSpec *spec = RoadStopClass::Get(_roadstop_gui.sel_class)->GetSpec(_roadstop_gui.sel_type);

		this->DrawWidgets();

		int rad = _settings_game.station.modified_catchment ? ((this->window_class == WindowClass::BuildBusStation) ? CA_BUS : CA_TRUCK) : CA_UNMODIFIED;
		if (this->mobile) {
			/* A touch doesn't hover; the highlight starts with the tap. */
			SetTileSelectSize(1, 1);
		} else if (_settings_client.gui.station_show_coverage) {
			SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);
		} else {
			SetTileSelectSize(1, 1);
		}

		if (this->IsShaded()) return;

		/* 'Accepts' and 'Supplies' texts. */
		StationCoverageType sct = (this->window_class == WindowClass::BuildBusStation) ? SCT_PASSENGERS_ONLY : SCT_NON_PASSENGERS_ONLY;
		Rect r = this->GetWidget<NWidgetBase>(WID_BROS_ACCEPTANCE)->GetCurrentRect();
		const int bottom = r.bottom;
		r.bottom = INT_MAX; // Allow overflow as we want to know the required height.
		if (this->mobile) r = r.Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
		if (spec != nullptr) r.top = DrawBadgeNameList(r, spec->badges, GrfSpecFeature::RoadStops);
		if (this->mobile && !GetMobilePendingStationSize()) {
			/* Without hovering there is nothing to describe until the stop is placed. */
			r.top = DrawStringMultiLine(r, STR_MOBILE_STOP_COVERAGE_LATER);
		} else {
			r.top = DrawStationCoverageAreaText(r, sct, rad, false) + WidgetDimensions::scaled.vsep_normal;
			r.top = DrawStationCoverageAreaText(r, sct, rad, true);
		}
		/* Resize background if the window is too small.
		 * Never make the window smaller to avoid oscillating if the size change affects the acceptance.
		 * (This is the case, if making the window bigger moves the mouse into the window.) */
		if (r.top > bottom) {
			this->coverage_height += r.top - bottom;
			this->ReInit();
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_BROS_STATION_NE:
			case WID_BROS_STATION_SE:
			case WID_BROS_STATION_SW:
			case WID_BROS_STATION_NW:
			case WID_BROS_STATION_X:
			case WID_BROS_STATION_Y:
				if (this->mobile) break; // As the layout makes them.
				size.width  = ScaleGUITrad(PREVIEW_WIDTH) + WidgetDimensions::scaled.fullbevel.Horizontal();
				size.height = ScaleGUITrad(PREVIEW_HEIGHT) + WidgetDimensions::scaled.fullbevel.Vertical();
				break;

			case WID_BROS_TITLE:
				size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal));
				break;

			case WID_BROS_ACCEPTANCE:
				size.height = this->coverage_height;
				break;

			default:
				this->PickerWindow::UpdateWidgetSize(widget, size, padding, fill, resize);
				break;
		}
	}

	/**
	 * Simply to have a easier way to get the StationType for bus, truck and trams from the WindowClass.
	 * @param window_class The window class to get the type for.
	 * @return The associated station type.
	 */
	StationType GetRoadStationTypeByWindowClass(WindowClass window_class) const
	{
		switch (window_class) {
			case WindowClass::BuildBusStation: return StationType::Bus;
			case WindowClass::BuildTruckStation: return StationType::Truck;
			default: NOT_REACHED();
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (this->mobile) {
			Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
			int y = CentreBounds(r.top, r.bottom, GetCharacterHeight(FontSize::Normal));
			bool rtl = _current_text_dir == TD_RTL;
			switch (widget) {
				case WID_BROS_TITLE: {
					StringID title;
					if (RoadTypeIsTram(_cur_roadtype)) {
						title = this->window_class == WindowClass::BuildBusStation ? STR_MOBILE_STOP_TITLE_TRAM : STR_MOBILE_STOP_TITLE_TRAM_FREIGHT;
					} else {
						title = this->window_class == WindowClass::BuildBusStation ? STR_MOBILE_STOP_TITLE_BUS : STR_MOBILE_STOP_TITLE_LORRY;
					}
					DrawMobileTitle(r, GetString(title));
					return;
				}
				case WID_BROS_DRIVE_THROUGH:
				case WID_BROS_BAY: {
					/* A preview of the stop beside the label. */
					Rect preview = r.WithWidth(ScaleSpriteTrad(PREVIEW_WIDTH) + WidgetDimensions::scaled.fullbevel.Horizontal(), rtl);
					this->DrawMobileStopPreview(preview, widget == WID_BROS_DRIVE_THROUGH ? 4 : 2);
					Rect text = tr.Indent(preview.Width(), rtl);
					DrawString(text.left, text.right, y, widget == WID_BROS_DRIVE_THROUGH ? STR_MOBILE_STOP_DRIVE_THROUGH : STR_MOBILE_STOP_BAY, TextColour::Black);
					return;
				}
				case WID_BROS_STATION_NE:
				case WID_BROS_STATION_SE:
				case WID_BROS_STATION_SW:
				case WID_BROS_STATION_NW:
					this->DrawMobileStopPreview(r, widget - WID_BROS_STATION_NE);
					return;
				case WID_BROS_COVERAGE_TOGGLE:
					DrawString(tr.left, tr.right, y, STR_MOBILE_STATION_SHOW_CATCHMENT, TextColour::Black);
					DrawString(tr.left, tr.right, y, _settings_client.gui.station_show_coverage ? STR_MOBILE_ON : STR_MOBILE_OFF, TextColour::Black, AlignmentH::End);
					return;
				default:
					break;
			}
		}
		switch (widget) {
			case WID_BROS_STATION_NE:
			case WID_BROS_STATION_SE:
			case WID_BROS_STATION_SW:
			case WID_BROS_STATION_NW:
			case WID_BROS_STATION_X:
			case WID_BROS_STATION_Y: {
				StationType st = GetRoadStationTypeByWindowClass(this->window_class);
				const RoadStopSpec *spec = RoadStopClass::Get(_roadstop_gui.sel_class)->GetSpec(_roadstop_gui.sel_type);
				DrawPixelInfo tmp_dpi;
				Rect ir = r.Shrink(WidgetDimensions::scaled.bevel);
				if (FillDrawPixelInfo(&tmp_dpi, ir)) {
					AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);
					int x = (ir.Width()  - ScaleSpriteTrad(PREVIEW_WIDTH)) / 2 + ScaleSpriteTrad(PREVIEW_LEFT);
					int y = (ir.Height() + ScaleSpriteTrad(PREVIEW_HEIGHT)) / 2 - ScaleSpriteTrad(PREVIEW_BOTTOM);
					if (spec == nullptr) {
						StationPickerDrawSprite(x, y, st, INVALID_RAILTYPE, _cur_roadtype, widget - WID_BROS_STATION_NE);
					} else {
						DrawRoadStopTile(x, y, _cur_roadtype, spec, st, widget - WID_BROS_STATION_NE);
					}
				}
				break;
			}

			default:
				this->PickerWindow::DrawWidget(r, widget);
				break;
		}
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (this->mobile && widget == WID_BROS_JOIN) {
			StationID shown = GetMobileStationJoinShown();
			return shown == NEW_STATION ? GetString(STR_MOBILE_STATION_NEW) : GetString(STR_MOBILE_STATION_JOIN, shown);
		}
		return this->PickerWindow::GetWidgetString(widget, stringid);
	}

	void OnDropdownSelect(WidgetID widget, int index, int click_result) override
	{
		if (this->mobile && widget == WID_BROS_JOIN) {
			const std::vector<StationID> &candidates = GetMobileStationJoinCandidates();
			MobileBuildSetStationJoin(index >= 0 && static_cast<size_t>(index) < candidates.size() ? candidates[index] : NEW_STATION);
			this->SetDirty();
			return;
		}
		this->PickerWindow::OnDropdownSelect(widget, index, click_result);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (this->mobile && this->OnMobileClick(widget)) return;
		switch (widget) {
			case WID_BROS_STATION_NE:
			case WID_BROS_STATION_SE:
			case WID_BROS_STATION_SW:
			case WID_BROS_STATION_NW:
			case WID_BROS_STATION_X:
			case WID_BROS_STATION_Y:
				if (widget < WID_BROS_STATION_X) {
					const RoadStopSpec *spec = RoadStopClass::Get(_roadstop_gui.sel_class)->GetSpec(_roadstop_gui.sel_type);
					if (spec != nullptr && spec->flags.Test(RoadStopSpecFlag::DriveThroughOnly)) return;
				}
				this->RaiseWidget(WID_BROS_STATION_NE + _roadstop_gui.orientation);
				_roadstop_gui.orientation = (DiagDirection)(widget - WID_BROS_STATION_NE);
				this->LowerWidget(WID_BROS_STATION_NE + _roadstop_gui.orientation);
				SndClickBeep();
				this->SetDirty();
				CloseWindowById(WindowClass::JoinStation, 0);
				break;

			case WID_BROS_LT_OFF:
			case WID_BROS_LT_ON:
				this->RaiseWidget(_settings_client.gui.station_show_coverage + WID_BROS_LT_OFF);
				_settings_client.gui.station_show_coverage = (widget != WID_BROS_LT_OFF);
				this->LowerWidget(_settings_client.gui.station_show_coverage + WID_BROS_LT_OFF);
				SndClickBeep();
				this->SetDirty();
				SetViewportCatchmentStation(nullptr, true);
				break;

			default:
				this->PickerWindow::OnClick(pt, widget, click_count);
				break;
		}
	}

	void OnRealtimeTick([[maybe_unused]] uint delta_ms) override
	{
		CheckRedrawStationCoverage(this);
	}

	static inline HotkeyList road_hotkeys{"buildroadstop", {
		Hotkey('F', "focus_filter_box", PCWHK_FOCUS_FILTER_BOX),
	}};

	static inline HotkeyList tram_hotkeys{"buildtramstop", {
		Hotkey('F', "focus_filter_box", PCWHK_FOCUS_FILTER_BOX),
	}};
};

/** Widget definition of the build road station window */
static constexpr std::initializer_list<NWidgetPart> _nested_road_station_picker_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION,  Colours::DarkGreen, WID_BROS_CAPTION),
		NWidget(WWT_SHADEBOX, Colours::DarkGreen),
		NWidget(WWT_DEFSIZEBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			NWidgetFunction(MakePickerClassWidgets),
			NWidget(WWT_PANEL, Colours::DarkGreen),
				NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_picker, 0), SetPadding(WidgetDimensions::unscaled.picker),
					NWidget(NWID_SELECTION, Colours::Invalid, WID_BROS_AVAILABLE_ORIENTATIONS),
						/* 6-orientation plane. */
						NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0),
							NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0), SetPIPRatio(1, 0, 1),
								NWidget(NWID_HORIZONTAL_LTR), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
									NWidget(WWT_PANEL, Colours::Grey, WID_BROS_STATION_NW), SetFill(0, 0), EndContainer(),
									NWidget(WWT_PANEL, Colours::Grey, WID_BROS_STATION_NE), SetFill(0, 0), EndContainer(),
								EndContainer(),
								NWidget(WWT_PANEL, Colours::Grey, WID_BROS_STATION_X), SetFill(0, 0), EndContainer(),
							EndContainer(),
							NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0), SetPIPRatio(1, 0, 1),
								NWidget(NWID_HORIZONTAL_LTR), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
									NWidget(WWT_PANEL, Colours::Grey, WID_BROS_STATION_SW), SetFill(0, 0), EndContainer(),
									NWidget(WWT_PANEL, Colours::Grey, WID_BROS_STATION_SE), SetFill(0, 0), EndContainer(),
								EndContainer(),
								NWidget(WWT_PANEL, Colours::Grey, WID_BROS_STATION_Y), SetFill(0, 0), EndContainer(),
							EndContainer(),
						EndContainer(),
						/* 2-orientation plane. */
						NWidget(NWID_VERTICAL), SetPIPRatio(0, 0, 1),
							NWidget(NWID_HORIZONTAL_LTR), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0), SetPIPRatio(1, 0, 1),
								NWidget(WWT_PANEL, Colours::Grey, WID_BROS_STATION_X), SetFill(0, 0), EndContainer(),
								NWidget(WWT_PANEL, Colours::Grey, WID_BROS_STATION_Y), SetFill(0, 0), EndContainer(),
							EndContainer(),
						EndContainer(),
					EndContainer(),
					NWidget(WWT_LABEL, Colours::Invalid), SetStringTip(STR_STATION_BUILD_COVERAGE_AREA_TITLE), SetFill(1, 0),
					NWidget(NWID_HORIZONTAL), SetPIPRatio(1, 0, 1),
						NWidget(WWT_TEXTBTN, Colours::Grey, WID_BROS_LT_OFF), SetMinimalSize(60, 12),
								SetStringTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
						NWidget(WWT_TEXTBTN, Colours::Grey, WID_BROS_LT_ON), SetMinimalSize(60, 12),
								SetStringTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
					EndContainer(),
					NWidget(WWT_EMPTY, Colours::Invalid, WID_BROS_ACCEPTANCE), SetFill(1, 1), SetResize(1, 0), SetMinimalTextLines(2, 0),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidgetFunction(MakePickerTypeWidgets),
	EndContainer(),
};

/** Window definition for the build road station window. */
static WindowDesc _road_station_picker_desc(
	WindowPosition::Automatic, "build_station_road", 0, 0,
	WindowClass::BuildBusStation, WindowClass::BuildToolbar,
	WindowDefaultFlag::Construction,
	_nested_road_station_picker_widgets,
	&BuildRoadStationWindow::road_hotkeys
);

/** Widget definition of the build tram station window */
static constexpr std::initializer_list<NWidgetPart> _nested_tram_station_picker_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION,  Colours::DarkGreen, WID_BROS_CAPTION),
		NWidget(WWT_SHADEBOX, Colours::DarkGreen),
		NWidget(WWT_DEFSIZEBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			NWidgetFunction(MakePickerClassWidgets),
			NWidget(WWT_PANEL, Colours::DarkGreen),
				NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_picker, 0), SetPadding(WidgetDimensions::unscaled.picker),
					NWidget(NWID_HORIZONTAL_LTR), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0), SetPIPRatio(1, 0, 1),
						NWidget(WWT_PANEL, Colours::Grey, WID_BROS_STATION_X), SetFill(0, 0), EndContainer(),
						NWidget(WWT_PANEL, Colours::Grey, WID_BROS_STATION_Y), SetFill(0, 0), EndContainer(),
					EndContainer(),
					NWidget(WWT_LABEL, Colours::Invalid), SetStringTip(STR_STATION_BUILD_COVERAGE_AREA_TITLE), SetFill(1, 0),
					NWidget(NWID_HORIZONTAL), SetPIPRatio(1, 0, 1),
						NWidget(WWT_TEXTBTN, Colours::Grey, WID_BROS_LT_OFF), SetMinimalSize(60, 12),
								SetStringTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
						NWidget(WWT_TEXTBTN, Colours::Grey, WID_BROS_LT_ON), SetMinimalSize(60, 12),
								SetStringTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
					EndContainer(),
					NWidget(WWT_EMPTY, Colours::Invalid, WID_BROS_ACCEPTANCE), SetFill(1, 1), SetResize(1, 0), SetMinimalTextLines(2, 0),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidgetFunction(MakePickerTypeWidgets),
	EndContainer(),
};

/** Window definition for the build tram station window. */
static WindowDesc _tram_station_picker_desc(
	WindowPosition::Automatic, "build_station_tram", 0, 0,
	WindowClass::BuildBusStation, WindowClass::BuildToolbar,
	WindowDefaultFlag::Construction,
	_nested_tram_station_picker_widgets,
	&BuildRoadStationWindow::tram_hotkeys
);

/** A facing button of the mobile stop picker: a preview of the bay facing that way. */
#define MOBILE_STOP_FACING(widget) NWidget(WWT_PANEL, Colours::Grey, widget), SetMinimalSize(48, 48), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_STOP_FACING_TOOLTIP), EndContainer()

/** The road stop picker of the mobile layout, in the sheet slot (round 5 design, 6.2). */
static constexpr std::initializer_list<NWidgetPart> _nested_mobile_road_stop_picker_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::DarkGreen, WID_BROS_TITLE), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), EndContainer(),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_BROS_CLOSE), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_CROSS, STR_MOBILE_STOP_CLOSE_TOOLTIP),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::DarkGreen), SetResize(1, 1),
		NWidget(NWID_VERTICAL), SetPIP(0, 4, 0), SetPadding(0, 4, 4, 4),
			NWidget(NWID_SELECTION, Colours::Invalid, WID_BROS_SEL_KIND),
				NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, 4, 0),
					NWidget(WWT_PANEL, Colours::Grey, WID_BROS_DRIVE_THROUGH), SetMinimalSize(120, 48), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_STOP_DRIVE_THROUGH_TOOLTIP), EndContainer(),
					NWidget(WWT_PANEL, Colours::Grey, WID_BROS_BAY), SetMinimalSize(120, 48), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_STOP_BAY_TOOLTIP), EndContainer(),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SELECTION, Colours::Invalid, WID_BROS_SEL_FACING),
				NWidget(NWID_VERTICAL), SetPIP(0, 4, 0),
					NWidget(WWT_TEXT, Colours::Invalid), SetFill(1, 0), SetResize(1, 0), SetPadding(0, MOBILE_TEXT_INSET, 0, MOBILE_TEXT_INSET), SetStringTip(STR_MOBILE_STOP_FACING), SetTextStyle(TextColour::Black, FontSize::Normal),
					NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, 4, 0),
						MOBILE_STOP_FACING(WID_BROS_STATION_NE), MOBILE_STOP_FACING(WID_BROS_STATION_SE), MOBILE_STOP_FACING(WID_BROS_STATION_SW), MOBILE_STOP_FACING(WID_BROS_STATION_NW),
					EndContainer(),
				EndContainer(),
				NWidget(WWT_TEXT, Colours::Invalid), SetFill(1, 0), SetResize(1, 0), SetPadding(0, MOBILE_TEXT_INSET, 0, MOBILE_TEXT_INSET), SetStringTip(STR_MOBILE_STOP_FOLLOWS_ROAD), SetTextStyle(TextColour::Black, FontSize::Normal),
			EndContainer(),
			NWidget(WWT_PANEL, Colours::Grey, WID_BROS_COVERAGE_TOGGLE), SetMinimalSize(0, 32), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP), EndContainer(),
			NWidget(NWID_SELECTION, Colours::Invalid, WID_BROS_SEL_JOIN),
				NWidget(WWT_DROPDOWN, Colours::Grey, WID_BROS_JOIN), SetMinimalSize(0, 32), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_STATION_JOIN_TOOLTIP),
			EndContainer(),
			NWidget(WWT_EMPTY, Colours::Invalid, WID_BROS_ACCEPTANCE), SetFill(1, 1), SetResize(1, 1), SetMinimalTextLines(2, 0),
		EndContainer(),
	EndContainer(),
};

#undef MOBILE_STOP_FACING

static WindowDesc _mobile_road_stop_picker_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::BuildBusStation, WindowClass::BuildToolbar,
	WindowDefaultFlag::Construction,
	_nested_mobile_road_stop_picker_widgets
);

static void ShowRVStationPicker(Window *parent, RoadStopType rs)
{
	if (IsMobileLayout()) {
		MobileShowInSheet(new BuildRoadStationWindow(_mobile_road_stop_picker_desc, parent, rs, true));
		return;
	}
	new BuildRoadStationWindow(RoadTypeIsRoad(_cur_roadtype) ? _road_station_picker_desc : _tram_station_picker_desc, parent, rs);
}

class RoadWaypointPickerCallbacks : public PickerCallbacksNewGRFClass<RoadStopClass> {
public:
	RoadWaypointPickerCallbacks() : PickerCallbacksNewGRFClass<RoadStopClass>("fav_road_waypoints") {}

	GrfSpecFeature GetFeature() const override { return GrfSpecFeature::RoadStops; }

	StringID GetClassTooltip() const override { return STR_PICKER_WAYPOINT_CLASS_TOOLTIP; }
	StringID GetTypeTooltip() const override { return STR_PICKER_WAYPOINT_TYPE_TOOLTIP; }
	StringID GetCollectionTooltip() const override { return STR_PICKER_WAYPOINT_COLLECTION_TOOLTIP; }

	bool IsActive() const override
	{
		for (const auto &cls : RoadStopClass::Classes()) {
			if (!IsWaypointClass(cls)) continue;
			for (const auto *spec : cls.Specs()) {
				if (spec != nullptr) return true;
			}
		}
		return false;
	}

	bool HasClassChoice() const override
	{
		return std::ranges::count_if(RoadStopClass::Classes(), [](const auto &cls) { return IsWaypointClass(cls); }) > 1;
	}

	void Close(int) override { ResetObjectToPlace(); }
	int GetSelectedClass() const override { return _waypoint_gui.sel_class.base(); }
	void SetSelectedClass(int id) const override { _waypoint_gui.sel_class = this->GetClassIndex(id); }

	StringID GetClassName(int id) const override
	{
		const auto *sc = GetClass(id);
		if (!IsWaypointClass(*sc)) return INVALID_STRING_ID;
		return sc->name;
	}

	int GetSelectedType() const override { return _waypoint_gui.sel_type; }
	void SetSelectedType(int id) const override { _waypoint_gui.sel_type = id; }

	StringID GetTypeName(int cls_id, int id) const override
	{
		const auto *spec = this->GetSpec(cls_id, id);
		return (spec == nullptr) ? STR_STATION_CLASS_WAYP_WAYPOINT : spec->name;
	}

	std::span<const BadgeID> GetTypeBadges(int cls_id, int id) const override
	{
		const auto *spec = this->GetSpec(cls_id, id);
		if (spec == nullptr) return {};
		return spec->badges;
	}

	bool IsTypeAvailable(int cls_id, int id) const override
	{
		return IsRoadStopAvailable(this->GetSpec(cls_id, id), StationType::RoadWaypoint);
	}

	void DrawType(int x, int y, int cls_id, int id) const override
	{
		const auto *spec = this->GetSpec(cls_id, id);
		if (spec == nullptr) {
			StationPickerDrawSprite(x, y, StationType::RoadWaypoint, INVALID_RAILTYPE, _cur_roadtype, RSV_DRIVE_THROUGH_X);
		} else {
			DrawRoadStopTile(x, y, _cur_roadtype, spec, StationType::RoadWaypoint, RSV_DRIVE_THROUGH_X);
		}
	}

	void FillUsedItems(std::set<PickerItem> &items) override
	{
		for (const Waypoint *wp : Waypoint::Iterate()) {
			if (wp->owner != _local_company || !HasBit(wp->waypoint_flags, WPF_ROAD)) continue;
			items.insert({GrfID{}, 0, ROADSTOP_CLASS_WAYP.base(), 0}); // We would need to scan the map to find out if default is used.
			for (const auto &sm : wp->roadstop_speclist) {
				if (sm.spec == nullptr) continue;
				items.insert({sm.grfid, sm.localidx, sm.spec->class_index.base(), sm.spec->index});
			}
		}
	}

	static RoadWaypointPickerCallbacks instance;
};
/* static */ RoadWaypointPickerCallbacks RoadWaypointPickerCallbacks::instance;

struct BuildRoadWaypointWindow : public PickerWindow {
	BuildRoadWaypointWindow(WindowDesc &desc, Window *parent) : PickerWindow(desc, parent, TransportType::Road, RoadWaypointPickerCallbacks::instance)
	{
		this->ConstructWindow();
	}

	static inline HotkeyList hotkeys{"buildroadwaypoint", {
		Hotkey('F', "focus_filter_box", PCWHK_FOCUS_FILTER_BOX),
	}};
};

/** Nested widget definition for the build NewGRF road waypoint window */
static constexpr std::initializer_list<NWidgetPart> _nested_build_road_waypoint_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen), SetStringTip(STR_WAYPOINT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::DarkGreen),
		NWidget(WWT_DEFSIZEBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidgetFunction(MakePickerClassWidgets),
		NWidgetFunction(MakePickerTypeWidgets),
	EndContainer(),
};

/** Window definition for the build road waypoint window. */
static WindowDesc _build_road_waypoint_desc(
	WindowPosition::Automatic, "build_road_waypoint", 0, 0,
	WindowClass::BuildWaypoint, WindowClass::BuildToolbar,
	WindowDefaultFlag::Construction,
	_nested_build_road_waypoint_widgets,
	&BuildRoadWaypointWindow::hotkeys
);

static void ShowBuildRoadWaypointPicker(Window *parent)
{
	if (!RoadWaypointPickerCallbacks::instance.IsActive()) return;
	new BuildRoadWaypointWindow(_build_road_waypoint_desc, parent);
}

void InitializeRoadGui()
{
	_road_depot_orientation = DiagDirection::NW;
	_roadstop_gui.orientation = DiagDirection::NW;
	_waypoint_gui.sel_class = ROADSTOP_CLASS_WAYP;
	_waypoint_gui.sel_type = 0;
}

DropDownList GetRoadTypeDropDownList(RoadTramTypes rtts, bool for_replacement, bool all_option)
{
	RoadTypes used_roadtypes;
	RoadTypes avail_roadtypes;

	const Company *c = Company::Get(_local_company);

	/* Find the used roadtypes. */
	if (for_replacement) {
		avail_roadtypes = GetCompanyRoadTypes(c->index, false);
		used_roadtypes  = GetRoadTypes(false);
	} else {
		avail_roadtypes = c->avail_roadtypes;
		used_roadtypes  = GetRoadTypes(true);
	}

	/* Filter listed road types */
	if (!rtts.Test(RoadTramType::Road)) used_roadtypes.Reset(GetMaskForRoadTramType(RoadTramType::Road));
	if (!rtts.Test(RoadTramType::Tram)) used_roadtypes.Reset(GetMaskForRoadTramType(RoadTramType::Tram));

	DropDownList list;

	if (all_option) {
		list.push_back(MakeDropDownListStringItem(STR_REPLACE_ALL_ROADTYPE, INVALID_ROADTYPE));
	}

	Dimension d = { 0, 0 };
	/* Get largest icon size, to ensure text is aligned on each menu item. */
	if (!for_replacement) {
		used_roadtypes.Reset(_roadtypes_hidden_mask);
		for (const auto &rt : _sorted_roadtypes) {
			if (!used_roadtypes.Test(rt)) continue;
			const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
			d = maxdim(d, GetSpriteSize(rti->gui_sprites.build_x_road));
		}
	}

	/* Shared list so that each item can take ownership. */
	auto badge_class_list = std::make_shared<GUIBadgeClasses>(GrfSpecFeature::RoadTypes);

	for (const auto &rt : _sorted_roadtypes) {
		/* If it's not used ever, don't show it to the user. */
		if (!used_roadtypes.Test(rt)) continue;

		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);

		if (for_replacement) {
			list.push_back(MakeDropDownListBadgeItem(badge_class_list, rti->badges, GrfSpecFeature::RoadTypes, rti->introduction_date, GetString(rti->strings.replace_text), rt, !avail_roadtypes.Test(rt)));
		} else {
			std::string str = rti->max_speed > 0
				? GetString(STR_TOOLBAR_ROADTYPE_VELOCITY, rti->strings.menu_text, rti->max_speed / 2)
				: GetString(rti->strings.menu_text);
			list.push_back(MakeDropDownListBadgeIconItem(badge_class_list, rti->badges, GrfSpecFeature::RoadTypes, rti->introduction_date, RoadBuildCost(rt), d, rti->gui_sprites.build_x_road, PAL_NONE, std::move(str), rt, !avail_roadtypes.Test(rt)));
		}
	}

	if (list.empty()) {
		/* Empty dropdowns are not allowed */
		list.push_back(MakeDropDownListStringItem(STR_NONE, INVALID_ROADTYPE, true));
	}

	return list;
}

DropDownList GetScenRoadTypeDropDownList(RoadTramTypes rtts)
{
	RoadTypes avail_roadtypes = GetRoadTypes(false);
	avail_roadtypes = AddDateIntroducedRoadTypes(avail_roadtypes, TimerGameCalendar::date);
	RoadTypes used_roadtypes = GetRoadTypes(true);

	/* Filter listed road types */
	used_roadtypes.Reset(_roadtypes_hidden_mask);
	if (!rtts.Test(RoadTramType::Road)) used_roadtypes.Reset(GetMaskForRoadTramType(RoadTramType::Road));
	if (!rtts.Test(RoadTramType::Tram)) used_roadtypes.Reset(GetMaskForRoadTramType(RoadTramType::Tram));

	DropDownList list;

	/* If it's not used ever, don't show it to the user. */
	Dimension d = { 0, 0 };
	for (const auto &rt : _sorted_roadtypes) {
		if (!used_roadtypes.Test(rt)) continue;
		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
		d = maxdim(d, GetSpriteSize(rti->gui_sprites.build_x_road));
	}

	/* Shared list so that each item can take ownership. */
	auto badge_class_list = std::make_shared<GUIBadgeClasses>(GrfSpecFeature::RoadTypes);

	for (const auto &rt : _sorted_roadtypes) {
		if (!used_roadtypes.Test(rt)) continue;

		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);

		std::string str = rti->max_speed > 0
			? GetString(STR_TOOLBAR_ROADTYPE_VELOCITY, rti->strings.menu_text, rti->max_speed / 2)
			: GetString(rti->strings.menu_text);
		list.push_back(MakeDropDownListBadgeIconItem(badge_class_list, rti->badges, GrfSpecFeature::RoadTypes, rti->introduction_date, RoadBuildCost(rt), d, rti->gui_sprites.build_x_road, PAL_NONE, std::move(str), rt, !avail_roadtypes.Test(rt)));
	}

	if (list.empty()) {
		/* Empty dropdowns are not allowed */
		list.push_back(MakeDropDownListStringItem(STR_NONE, -1, true));
	}

	return list;
}

/**
 * Determine the default road type to use for a given RoadTramType.
 * @param rtt Whether to find a road or tram type.
 * @return The default RoadType based on the setting.
 */
static RoadType GetDefaultRoadType(RoadTramType rtt)
{
	const auto find_available = [rtt]<typename It>(It begin, It end) {
		const RoadTypes mask = GetMaskForRoadTramType(rtt);
		auto it = std::find_if(begin, end, [&](RoadType r) {
			return HasRoadTypeAvail(_local_company, r) && mask.Test(r);
		});
		return it != end ? *it : ROADTYPE_BEGIN;
	};

	switch (_settings_client.gui.default_rail_road_type) {
		case DefaultRailRoadType::MostUsed: {
			const Company *c = Company::Get(_local_company);
			std::array<uint, ROADTYPE_END> count{};
			for (RoadType rt : GetMaskForRoadTramType(rtt)) {
				count[rt] = c->infrastructure.road[rt];
			}
			auto best = static_cast<RoadType>(std::distance(std::begin(count), std::ranges::max_element(count)));
			if (c->infrastructure.road[best] > 0) return best;

			/* No tile of this kind has been built yet, fall through to first available. */
			[[fallthrough]];
		}
		case DefaultRailRoadType::FirstAvailable:
			return find_available(_sorted_roadtypes.begin(), _sorted_roadtypes.end());
		case DefaultRailRoadType::LastAvailable:
			return find_available(_sorted_roadtypes.rbegin(), _sorted_roadtypes.rend());
		default:
			NOT_REACHED();
	}
}

/** Set the initial (default) road & tram type to use. */
void SetDefaultRoadGui()
{
	if (_local_company == COMPANY_SPECTATOR || !Company::IsValidID(_local_company)) return;

	_last_built_roadtype = GetDefaultRoadType(RoadTramType::Road);
	_last_built_tramtype = GetDefaultRoadType(RoadTramType::Tram);
}
