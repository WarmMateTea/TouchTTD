/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file mobile_build.cpp Review-before-building for construction tools in the mobile layout.
 *
 * On desktop, releasing the mouse after dragging a selection builds at once. In the mobile layout the
 * selection is frozen on the map instead ("pending"), with its cost or error in the toolbar's status line,
 * until the player taps Build or Cancel. Dragging one of its ends ("handles") adjusts it.
 *
 * Nothing here knows how to build: building replays the toolbar's own OnPlaceMouseUp() with the frozen
 * selection, exactly as the desktop does on release. The cost estimate runs that same replay with a
 * #CommandResultCapture in estimate mode, so the commands are only tested.
 */

#include "stdafx.h"
#include "mobile_build.h"
#include "mobile_gui.h"
#include "window_gui.h"
#include "window_func.h"
#include "viewport_func.h"
#include "tilehighlight_func.h"
#include "command_func.h"
#include "strings_func.h"
#include "string_func.h"
#include "gfx_func.h"
#include "zoom_func.h"
#include "landscape.h"
#include "tile_map.h"
#include "tunnelbridge.h"
#include "tunnelbridge_cmd.h"
#include "landscape_cmd.h"
#include "rail_gui.h"
#include "rail_cmd.h"
#include "settings_type.h"
#include "company_func.h"
#include "openttd.h"
#include "rail_map.h"
#include "station_map.h"
#include "station_gui.h"
#include "station_cmd.h"
#include "station_base.h"
#include "map_func.h"
#include "economy_func.h"
#include "timer/timer.h"
#include "timer/timer_window.h"

#include "table/strings.h"

#include "safeguards.h"

extern TileIndex _build_tunnel_endtile;

/** A selection as the toolbar saw it when the player lifted their finger. */
struct PendingSelection {
	WindowClass cls = WindowClass::Invalid; ///< Class of the toolbar.
	WindowNumber num{}; ///< Number of the toolbar.
	ViewportPlaceMethod method{}; ///< How tiles were selected.
	ViewportDragDropSelectionProcess proc{}; ///< What the toolbar does with the selection.
	Point selstart{}; ///< Start of the selection, as in #TileHighlightData.
	Point selend{}; ///< End of the selection, as in #TileHighlightData.
	HighLightStyle drawstyle{}; ///< Drawing style, which also holds the track direction.
	HighLightStyle place_mode{}; ///< Placement mode to return to when the selection is finished.
	bool remove = false; ///< The rail toolbar was in remove mode.
	bool tunnel = false; ///< A tunnel tap rather than a dragged selection.
	TileIndex tunnel_start = INVALID_TILE; ///< Entrance of the tunnel.
	TileIndex tunnel_end = INVALID_TILE; ///< Computed exit of the tunnel.
	bool station = false; ///< A rail station footprint, from a tap (the picker's size) or a dragged area.
	bool depot = false; ///< A rail depot on a single tile.
	DiagDirection depot_dir = DiagDirection::NE; ///< Direction the depot's entrance faces.
	bool depot_faces_track = false; ///< The direction was found from track next to the depot.
	Point fract{}; ///< Where in its tile a single tile was tapped (#_tile_fract_coords): which track and side a signal goes on.
	uint8_t cycles = 1; ///< Turning a signal: how many times to turn it (each tap on the pending signal adds one).

	/** @return Whether the selection places, turns, converts or removes signals. */
	bool IsSignals() const { return this->proc == DDSP_BUILD_SIGNALS; }

	/** @return Whether it is a single tile (a tap) rather than a dragged stretch. */
	bool IsSingleTile() const { return this->selstart.x == this->selend.x && this->selstart.y == this->selend.y; }

	TileIndex StartTile() const { return this->tunnel ? this->tunnel_start : TileVirtXY(this->selstart.x, this->selstart.y); }
	TileIndex EndTile() const { return this->tunnel ? this->tunnel_end : TileVirtXY(this->selend.x, this->selend.y); }

	/** @return The area between the two ends. */
	TileArea Area() const { return TileArea(this->StartTile(), this->EndTile()); }

	/** @return Whether the selection places a rail waypoint (the station tool's sibling, along a track). */
	bool IsWaypoint() const { return this->proc == DDSP_BUILD_STATION && !this->station && !this->depot && !this->remove; }

	/** @return Whether the selection is a thing on the map that can be moved by dragging inside it. */
	bool IsMovable() const { return this->station || this->depot; }

	/**
	 * Number of tiles in the selection.
	 * @return The count shown to the player.
	 */
	uint TileCount() const
	{
		uint dx = Delta(TileX(this->StartTile()), TileX(this->EndTile())) + 1;
		uint dy = Delta(TileY(this->StartTile()), TileY(this->EndTile())) + 1;
		if (this->proc == DDSP_DEMOLISH_AREA || this->proc == DDSP_CONVERT_RAIL || this->station) return dx * dy;
		return std::max(dx, dy);
	}
};

/** Result of trying a construction, either as an estimate or for real. */
struct BuildResult {
	bool valid = false; ///< Whether there is a result at all.
	bool failed = false; ///< Whether it failed.
	Money cost = 0; ///< Its cost when it succeeded.
	std::string error; ///< Its error when it failed.
	bool paused = false; ///< It can't be built because the game is paused.
};

/** All state of the review flow. There is at most one construction in review at a time. */
static struct {
	MobileBuildState state = MobileBuildState::Idle;
	PendingSelection sel; ///< The selection being reviewed.
	BuildResult estimate; ///< Latest estimate of the selection.
	TileIndex estimated_end = INVALID_TILE; ///< End tile the estimate was made for while dragging.
	TileIndex estimated_start = INVALID_TILE; ///< Start tile the estimate was made for while dragging.
	bool adjusting = false; ///< The drag is an adjustment of a pending selection.
	StringID hint = STR_NULL; ///< Hint for the selected tool.
	std::string message; ///< Error from the last construction attempt, shown until the next action.

	uint done_tiles = 0; ///< Tiles built by the last construction.
	Money done_cost = 0; ///< Cost of the last construction.
	PendingSelection undo; ///< What to remove to undo it.
	bool undo_available = false; ///< Whether undo is offered.
	Money undo_cost = 0; ///< Estimated cost of undoing.
	bool undo_keep_rail = false; ///< Undoing a station keeps the track that was there before.

	bool moving = false; ///< The drag moves the pending selection.
	PendingSelection move_from; ///< The pending selection before the move.
	TileIndex move_grab = INVALID_TILE; ///< Tile the finger grabbed the selection at.
	TileIndexDiff move_delta = 0; ///< How far the selection moved so far.

	TileIndex station_anchor = INVALID_TILE; ///< Tapped tile, or corner of the dragged area, the footprint is placed at.
	bool station_centred = false; ///< The footprint is centred on the anchor (a tap) rather than starting at it (a drag).
	std::vector<StationID> join_candidates; ///< Stations the pending footprint can join.
	StationID join_default = NEW_STATION; ///< What the desktop would join without a choice.
	StationID join_choice = StationID::Invalid(); ///< The player's choice; invalid for the desktop default.
} _mb;

/** Undo is offered for this long after a construction. */
static constexpr std::chrono::seconds UNDO_OFFER_TIME{8};

static void SetMobileBuildDirty()
{
	/* The toolbar updates its buttons on invalidation, not just on redraw. */
	InvalidateWindowClassesData(WindowClass::BuildToolbar, 0, true);
}

/**
 * Is the window a construction toolbar that uses the review flow?
 * @param w The window.
 * @return \c true for the rail toolbar in the mobile layout.
 */
static bool IsReviewToolbar(const Window *w)
{
	return w != nullptr && IsMobileLayout() && w->window_class == WindowClass::BuildToolbar && w->window_number == TransportType::Rail;
}

static Window *GetSelectionToolbar(const PendingSelection &sel)
{
	return FindWindowById(sel.cls, sel.num);
}

/**
 * Copy a selection back into the tile highlight, so the toolbar's own placement code sees it.
 * @param sel The selection.
 */
static void RestoreSelection(const PendingSelection &sel)
{
	_thd.selstart = sel.selstart;
	_thd.selend = sel.selend;
	_thd.drawstyle = sel.drawstyle;
	_thd.select_method = sel.method;
	_thd.select_proc = sel.proc;
}

/**
 * Perform (or, under an estimating capture, test) the construction of a selection.
 * @param sel The selection.
 */
static void ReplaySelection(const PendingSelection &sel)
{
	Window *w = GetSelectionToolbar(sel);
	if (w == nullptr) return;

	if (sel.depot) {
		PostRailDepot(sel.StartTile(), sel.depot_dir);
		return;
	}

	bool old_remove = IsRailRemoveModeActive();
	SetRailRemoveMode(sel.remove);
	if (sel.tunnel) {
		PostRailTunnel(sel.tunnel_start);
	} else {
		RestoreSelection(sel);
		/* A tapped signal goes on the track and side under the finger; turning repeats for each tap. */
		_tile_fract_coords = sel.fract;
		int times = sel.IsSignals() && sel.IsSingleTile() ? sel.cycles : 1;
		for (int i = 0; i < times; i++) w->OnPlaceMouseUp(sel.method, sel.proc, sel.selend, sel.StartTile(), sel.EndTile());
	}
	SetRailRemoveMode(old_remove);
}

/**
 * Run a selection's construction under a capture.
 * @param sel The selection.
 * @param estimate_only Only test, don't build.
 * @return The result.
 */
static BuildResult TrySelection(const PendingSelection &sel, bool estimate_only)
{
	CommandResultCapture capture(estimate_only);
	/* Building a depot also lays track to connect it where it can; whether that works doesn't matter. */
	capture.first_only = sel.depot;
	ReplaySelection(sel);

	BuildResult result;
	result.valid = capture.any;
	result.failed = capture.failed;
	result.cost = capture.cost;
	if (capture.failed) result.error = capture.error.empty() ? GetString(capture.error_summary) : capture.error.GetDecodedString();
	if (capture.blocked_by_pause) {
		/* Estimates work while paused, building doesn't: say so, and keep Build disabled until the game runs. */
		result.failed = true;
		result.paused = true;
		result.error = GetString(STR_MOBILE_BUILD_STATUS_PAUSED);
	}
	return result;
}

/**
 * Take a snapshot of the selection the tile highlight currently describes.
 * @param w The toolbar the selection belongs to.
 * @return The snapshot.
 */
static PendingSelection SnapshotSelection(const Window *w)
{
	PendingSelection sel;
	sel.cls = w->window_class;
	sel.num = w->window_number;
	sel.method = _thd.select_method;
	sel.proc = _thd.select_proc;
	sel.selstart = _thd.selstart;
	sel.selend = _thd.selend;
	sel.drawstyle = _thd.drawstyle;
	sel.remove = IsRailRemoveModeActive();
	sel.fract = _tile_fract_coords;
	return sel;
}

/** Stop showing the selection and return the tile highlight to following the cursor. */
static void ReleaseHighlight()
{
	if (!_thd.freeze) return;
	_thd.freeze = false;
	_thd.place_mode = _mb.sel.place_mode;
	SetTileSelectSize(1, 1);
	MarkWholeScreenDirty();
}

/** Catchment radius of rail stations. */
static int GetRailStationCatchmentRadius()
{
	return _settings_game.station.modified_catchment ? CA_TRAIN : CA_UNMODIFIED;
}

/**
 * Show a selection frozen on the map, with the catchment area of a station footprint when that is switched on.
 * @param sel The selection.
 */
static void ShowFrozen(const PendingSelection &sel)
{
	_thd.freeze = false;
	RestoreSelection(sel);
	_thd.place_mode = HT_SPECIAL | (sel.place_mode & ~(HT_DRAG_MASK | HT_DIR_MASK));
	_thd.next_drawstyle = sel.drawstyle;
	if (sel.tunnel) VpSetPresizeRange(sel.tunnel_start, sel.tunnel_end);
	if (sel.station && _settings_client.gui.station_show_coverage) {
		int rad = GetRailStationCatchmentRadius();
		SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);
	} else {
		SetTileSelectBigSize(0, 0, 0, 0);
	}
	UpdateTileSelection();
	_thd.freeze = true;
	MarkWholeScreenDirty();
}

/**
 * Set the area of a selection.
 * @param sel The selection.
 * @param start One corner.
 * @param end The opposite corner.
 */
static void SetSelectionArea(PendingSelection &sel, TileIndex start, TileIndex end)
{
	sel.selstart = {static_cast<int>(TileX(start) * TILE_SIZE), static_cast<int>(TileY(start) * TILE_SIZE)};
	sel.selend = {static_cast<int>(TileX(end) * TILE_SIZE), static_cast<int>(TileY(end) * TILE_SIZE)};
}

/**
 * Place the pending station footprint at its anchor: centred on a tapped tile, or starting at a dragged corner.
 * @param x Tiles along the map's X axis.
 * @param y Tiles along the map's Y axis.
 */
static void PlaceStationFootprint(uint x, uint y)
{
	int sx = TileX(_mb.station_anchor);
	int sy = TileY(_mb.station_anchor);
	if (_mb.station_centred) {
		sx -= (static_cast<int>(x) - 1) / 2;
		sy -= (static_cast<int>(y) - 1) / 2;
	}
	sx = Clamp(sx, 0, std::max<int>(0, Map::MaxX() - static_cast<int>(x) + 1));
	sy = Clamp(sy, 0, std::max<int>(0, Map::MaxY() - static_cast<int>(y) + 1));
	SetSelectionArea(_mb.sel, TileXY(sx, sy), TileXY(std::min<int>(sx + x - 1, Map::MaxX()), std::min<int>(sy + y - 1, Map::MaxY())));
}

/** Find the stations the pending footprint can join, and what joining does by default. */
static void UpdateJoinCandidates()
{
	_mb.join_candidates.clear();
	_mb.join_default = NEW_STATION;
	if (!_mb.sel.station) return;

	TileArea ta = _mb.sel.Area();
	_mb.join_candidates = FindJoinableRailStations(ta, _settings_game.station.distant_join_stations);
	/* Without a choice, a station adjacent to exactly one other station joins it. */
	std::vector<StationID> adjacent = FindJoinableRailStations(ta, false);
	if (adjacent.size() == 1) _mb.join_default = adjacent[0];
	if (_mb.join_choice != NEW_STATION && std::ranges::find(_mb.join_candidates, _mb.join_choice) == _mb.join_candidates.end()) {
		_mb.join_choice = StationID::Invalid();
	}
}

static void CloseBuildLabel()
{
	CloseWindowById(WindowClass::MobileBuildLabel, 0);
}

static void ClearUndo()
{
	_mb.undo_available = false;
}

static void SetState(MobileBuildState state)
{
	_mb.state = state;
	if (state != MobileBuildState::Dragging) CloseBuildLabel();
	SetMobileBuildDirty();
	RelocateMobileWindows();
}

/** Drop the pending selection, if any, and return to the selected tool. */
static void DropPending()
{
	if (_mb.state == MobileBuildState::BridgePicking) CloseWindowByClass(WindowClass::BuildBridge);
	ReleaseHighlight();
	_mb.estimate = {};
	_mb.adjusting = false;
	_mb.moving = false;
	_mb.sel.station = false;
	_mb.join_candidates.clear();
	SetState(MobileBuildState::Idle);
	InvalidateWindowData(WindowClass::BuildStation, TransportType::Rail);
}

/* --- The live label above the finger --- */

/** The label above the finger while dragging, with the length and cost or the error. */
struct MobileBuildLabelWindow : Window {
	std::string text; ///< Text shown.
	bool error = false; ///< Whether the text is an error.

	MobileBuildLabelWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
	}

	void UpdateWidgetSize(WidgetID, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.framerect.Vertical());
	}

	void DrawWidget(const Rect &r, WidgetID) const override
	{
		if (this->error) GfxFillRect(r.Shrink(WidgetDimensions::scaled.bevel), GetColourGradient(Colours::Red, Shade::Normal));
		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
		DrawString(tr.left, tr.right, CentreBounds(r.top, r.bottom, GetCharacterHeight(FontSize::Normal)), this->text, this->error ? TextColour::White : TextColour::Black, AlignmentH::Centre);
	}

	/**
	 * Show new text, resizing to fit it, and move above the cursor.
	 * @param text The text.
	 * @param error Whether it is an error.
	 */
	void Update(const std::string &text, bool error)
	{
		if (text != this->text || error != this->error) {
			this->text = text;
			this->error = error;
			int width = GetStringBoundingBox(text).width + WidgetDimensions::scaled.framerect.Horizontal() + WidgetDimensions::scaled.bevel.Horizontal();
			this->SetDirty();
			ResizeWindow(this, std::max(width, ScaleGUITrad(40)) - this->width, 0, false);
		}

		/* Centred on the finger, its bottom 48 GUI pixels above it, kept inside the visible map. */
		Rect map = GetMobileVisibleMapRect();
		int x = Clamp(_cursor.pos.x - this->width / 2, map.left, std::max(map.left, map.right + 1 - this->width));
		int y = Clamp(_cursor.pos.y - ScaleGUITrad(48) - this->height, map.top, std::max(map.top, map.bottom + 1 - this->height));
		if (x != this->left || y != this->top) {
			this->SetDirty();
			this->left = x;
			this->top = y;
			this->SetDirty();
		}
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_build_label_widgets = {
	NWidget(WWT_PANEL, Colours::Yellow, 0), SetMinimalSize(40, 0), SetResize(1, 0), EndContainer(),
};

static WindowDesc _mobile_build_label_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileBuildLabel, WindowClass::None,
	{WindowDefaultFlag::NoFocus},
	_nested_mobile_build_label_widgets
);

/**
 * Text for a selection: "9 tiles · £1,395", or what removing, demolishing or converting it does.
 * @param sel The selection.
 * @param cost Its cost.
 * @return The text.
 */
/**
 * How many signals a dragged stretch builds, converts or removes. Each costs the same, so the cost tells;
 * without a cost, the count follows from the spacing.
 * @param sel The selection.
 * @param cost Its cost.
 * @param each The cost of one signal.
 * @return The number of signals.
 */
static uint GetSignalCount(const PendingSelection &sel, Money cost, Money each)
{
	if (cost > 0 && each > 0) return static_cast<uint>((cost + each / 2) / each);
	uint density = std::max<uint>(1, _settings_client.gui.drag_signals_density);
	return (sel.TileCount() - 1) / density + 1;
}

static std::string GetSelectionCostText(const PendingSelection &sel, Money cost)
{
	/* Removing track refunds part of its cost: show that as a gain, not as a negative cost. */
	std::string money = cost < 0 ? GetString(STR_MOBILE_BUILD_REFUND, -cost) : GetString(STR_JUST_CURRENCY_LONG, cost);
	if (sel.IsSignals()) {
		uint density = std::max<uint>(1, _settings_client.gui.drag_signals_density);
		switch (GetMobileBuildKind()) {
			case MobileBuildKind::Remove:
				if (sel.IsSingleTile()) return GetString(STR_MOBILE_BUILD_STATUS_SIGNAL_REMOVE_ONE, money);
				return GetString(STR_MOBILE_BUILD_STATUS_SIGNAL_REMOVE, GetSignalCount(sel, cost, _price[Price::ClearSignals]), money);
			case MobileBuildKind::Convert:
				if (sel.IsSingleTile()) return GetString(STR_MOBILE_BUILD_STATUS_SIGNAL_CONVERT_ONE, money);
				return GetString(STR_MOBILE_BUILD_STATUS_SIGNAL_CONVERT, GetSignalCount(sel, cost, _price[Price::BuildSignals] + _price[Price::ClearSignals]), money);
			case MobileBuildKind::Change:
				if (sel.cycles > 1) return GetString(STR_MOBILE_BUILD_STATUS_SIGNAL_TURN_TIMES, sel.cycles, money);
				return GetString(STR_MOBILE_BUILD_STATUS_SIGNAL_TURN, money);
			default:
				if (sel.IsSingleTile()) return GetString(STR_MOBILE_BUILD_STATUS_SIGNAL_ONE, money);
				return GetString(STR_MOBILE_BUILD_STATUS_SIGNALS, GetSignalCount(sel, cost, _price[Price::BuildSignals]), density, money);
		}
	}
	if (sel.station) {
		TileArea ta = sel.Area();
		uint tracks, length;
		GetRailStationTracksAndLength(ta.w, ta.h, tracks, length);
		StationID join = GetMobileStationJoinShown();
		if (join != NEW_STATION && Station::IsValidID(join)) return GetString(STR_MOBILE_BUILD_STATUS_STATION_JOIN, tracks, length, join, money);
		return GetString(STR_MOBILE_BUILD_STATUS_STATION, tracks, length, money);
	}
	if (sel.depot) return GetString(sel.depot_faces_track ? STR_MOBILE_BUILD_STATUS_DEPOT_FACES_TRACK : STR_MOBILE_BUILD_STATUS_DEPOT_CHECK, money);
	if (sel.IsWaypoint()) return GetString(STR_MOBILE_BUILD_STATUS_WAYPOINT, sel.TileCount(), money);
	switch (GetMobileBuildKind()) {
		case MobileBuildKind::Remove: return GetString(STR_MOBILE_BUILD_STATUS_REMOVE, sel.TileCount(), money);
		case MobileBuildKind::Demolish: return GetString(STR_MOBILE_BUILD_STATUS_DEMOLISH, sel.TileCount(), money);
		case MobileBuildKind::Convert: return GetString(STR_MOBILE_BUILD_STATUS_CONVERT, sel.TileCount(), money);
		default: return GetString(STR_MOBILE_BUILD_STATUS_COST, sel.TileCount(), money);
	}
}

/**
 * Start an error with a capital letter, as the status line shows it ("Site unsuitable...", not "site unsuitable...").
 * Formatting codes before the first letter are skipped.
 * @param text The error.
 * @return The error, capitalised.
 */
static std::string CapitaliseError(std::string text)
{
	for (size_t i = 0; i < text.size();) {
		unsigned char c = static_cast<unsigned char>(text[i]);
		if (c < 0x80) {
			if (c >= 'a' && c <= 'z') text[i] = static_cast<char>(c - 'a' + 'A');
			break;
		}
		/* Formatting codes are in the private use area, U+E000 and up: three bytes starting with 0xEE or 0xEF. */
		if (c != 0xEE && c != 0xEF) break;
		i += 3;
	}
	return text;
}

/* --- Edge scrolling --- */

/**
 * While dragging near the edge of the visible map, scroll the map towards that edge.
 * The speed ramps from nothing at the inner edge of a 32 GUI pixel band to half a screen per second at the edge.
 */
static void EdgeScroll()
{
	static auto last = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	float seconds = std::min(std::chrono::duration<float>(now - last).count(), 0.1f);
	last = now;

	Window *main = GetMainWindow();
	if (main == nullptr || main->viewport == nullptr) return;

	Rect map = GetMobileVisibleMapRect();
	int band = ScaleGUITrad(32);
	auto ramp = [band](int depth) -> float { return depth <= 0 ? 0.0f : std::min(1.0f, depth / (float)band); };

	float fx = ramp(map.left + band - _cursor.pos.x) * -1 + ramp(_cursor.pos.x - (map.right - band));
	float fy = ramp(map.top + band - _cursor.pos.y) * -1 + ramp(_cursor.pos.y - (map.bottom - band));
	if (fx == 0 && fy == 0) return;

	float speed = _screen.width / 2.0f * seconds; // Screen pixels to move this frame at full speed.
	int dx = ScaleByZoom((int)(fx * speed), main->viewport->zoom);
	int dy = ScaleByZoom((int)(fy * speed), main->viewport->zoom);
	main->viewport->scrollpos_x += dx;
	main->viewport->scrollpos_y += dy;
	main->viewport->dest_scrollpos_x = main->viewport->scrollpos_x;
	main->viewport->dest_scrollpos_y = main->viewport->scrollpos_y;
	main->SetDirty();
}

/* --- Hooks called by the viewport --- */

/**
 * Should releasing the current selection freeze it for review instead of building it?
 * @param w The window that started the selection.
 * @return \c true for the reviewed tools of the mobile rail toolbar.
 */
bool MobileBuildReviewWanted(const Window *w)
{
	if (!IsReviewToolbar(w)) return false;
	if (_mb.moving) return true;
	switch (_thd.select_proc) {
		case DDSP_BUILD_STATION: // Stations and waypoints.
			return !IsRailRemoveModeActive();

		case DDSP_PLACE_RAIL:
			/* Building track follows the "confirm before building" setting; removing always asks. */
			return _settings_client.gui.mobile_confirm_build || IsRailRemoveModeActive();
		case DDSP_DEMOLISH_AREA:
		case DDSP_CONVERT_RAIL:
		case DDSP_BUILD_BRIDGE:
		case DDSP_BUILD_SIGNALS: // Signals are always reviewed (round 5 design, batch 3).
			return true;
		default:
			return false;
	}
}

/**
 * The pending selection moved to where the finger is now, keeping it on the map.
 * @return The moved selection.
 */
static PendingSelection MovedSelection()
{
	PendingSelection sel = _mb.move_from;
	Point pt = GetTileBelowCursor();
	if (pt.x != -1) {
		TileArea ta = sel.Area();
		TileIndex grab_now = TileVirtXY(pt.x, pt.y);
		int dx = static_cast<int>(TileX(grab_now)) - static_cast<int>(TileX(_mb.move_grab));
		int dy = static_cast<int>(TileY(grab_now)) - static_cast<int>(TileY(_mb.move_grab));
		dx = Clamp(dx, -static_cast<int>(TileX(ta.tile)), static_cast<int>(Map::MaxX()) - static_cast<int>(TileX(ta.tile) + ta.w - 1));
		dy = Clamp(dy, -static_cast<int>(TileY(ta.tile)), static_cast<int>(Map::MaxY()) - static_cast<int>(TileY(ta.tile) + ta.h - 1));
		_mb.move_delta = TileDiffXY(dx, dy);
	}
	TileArea ta = sel.Area();
	SetSelectionArea(sel, ta.tile + _mb.move_delta, TileAddXY(ta.tile, ta.w - 1, ta.h - 1) + _mb.move_delta);
	if (sel.depot) sel.depot_faces_track = false;
	return sel;
}

/**
 * Is a selection a station footprint being placed (rather than a waypoint, or removal)?
 * @param sel The selection.
 * @return \c true for the station tool's area.
 */
static bool IsStationPlacement(const PendingSelection &sel)
{
	return sel.proc == DDSP_BUILD_STATION && sel.method == VPM_X_AND_Y_LIMITED && !sel.remove;
}

/**
 * The finger was lifted after dragging a reviewed selection.
 * @param w The toolbar that started the selection.
 */
void MobileBuildOnRelease(Window *w)
{
	_mb.message.clear();

	bool was_adjusting = _mb.adjusting;
	bool was_moving = _mb.moving;
	_mb.adjusting = false;
	_mb.moving = false;

	/* Lifting over a window cancels the drag; a moved selection goes back to where it was. */
	Window *under = FindWindowFromPt(_cursor.pos.x, _cursor.pos.y);
	if (under != nullptr && under->window_class != WindowClass::MainWindow && under->window_class != WindowClass::MobileBuildLabel) {
		RestorePlaceModeAfterSizing();
		if (was_moving) {
			_mb.sel = _mb.move_from;
			ShowFrozen(_mb.sel);
			_mb.estimate = TrySelection(_mb.sel, true);
			SetState(MobileBuildState::Pending);
			return;
		}
		SetState(MobileBuildState::Idle);
		return;
	}

	if (was_moving) {
		TileIndex old_start = _mb.move_from.Area().tile;
		_mb.sel = MovedSelection();
		_mb.sel.place_mode = GetPlaceModeAfterSizing();
		if (_mb.sel.station) {
			TileIndex new_start = _mb.sel.Area().tile;
			int dx = static_cast<int>(TileX(new_start)) - static_cast<int>(TileX(old_start));
			int dy = static_cast<int>(TileY(new_start)) - static_cast<int>(TileY(old_start));
			_mb.station_anchor = TileXY(TileX(_mb.station_anchor) + dx, TileY(_mb.station_anchor) + dy);
		}
	} else {
		_mb.sel = SnapshotSelection(w);
		_mb.sel.place_mode = GetPlaceModeAfterSizing();
		if (IsStationPlacement(_mb.sel)) {
			_mb.sel.station = true;
			if (!was_adjusting) _mb.join_choice = StationID::Invalid();
			if (!was_adjusting && _mb.sel.StartTile() == _mb.sel.EndTile()) {
				/* A tap: the picker's size, centred on the tapped tile so the finger isn't at a corner. */
				_mb.station_anchor = _mb.sel.StartTile();
				_mb.station_centred = true;
				uint x, y;
				GetRailStationFootprintSize(x, y);
				PlaceStationFootprint(x, y);
			} else {
				/* A dragged area: its sides become tracks and length. */
				TileArea ta = _mb.sel.Area();
				_mb.station_anchor = ta.tile;
				_mb.station_centred = false;
				SetRailStationSizeFromFootprint(ta.w, ta.h);
			}
		}
	}
	ClearUndo();
	if (_mb.sel.IsMovable()) {
		ShowFrozen(_mb.sel);
		UpdateJoinCandidates();
	} else {
		_thd.freeze = true;
	}

	if (_mb.sel.proc == DDSP_BUILD_BRIDGE) {
		/* The bridge list is the confirmation: tapping a bridge builds it. */
		SetState(MobileBuildState::BridgePicking);
		w->OnPlaceMouseUp(_mb.sel.method, _mb.sel.proc, _mb.sel.selend, _mb.sel.StartTile(), _mb.sel.EndTile());
		if (FindWindowByClass(WindowClass::BuildBridge) == nullptr) {
			/* No bridge can be built here; the error is in the status line. */
			ReleaseHighlight();
			SetState(MobileBuildState::Idle);
		}
		return;
	}

	_mb.estimate = TrySelection(_mb.sel, true);
	SetState(MobileBuildState::Pending);
	InvalidateWindowData(WindowClass::BuildStation, TransportType::Rail);
}

/**
 * Called every frame while a selection is dragged: update the live estimate and label, and scroll at the edges.
 * @param w The toolbar that started the selection.
 */
void MobileBuildOnSizingUpdate(Window *w)
{
	if (!IsReviewToolbar(w)) return;

	if (_mb.state != MobileBuildState::Dragging) {
		_mb.message.clear();
		ClearUndo();
		_mb.estimated_end = INVALID_TILE;
		_mb.estimated_start = INVALID_TILE;
		SetState(MobileBuildState::Dragging);
	}

	EdgeScroll();

	PendingSelection sel;
	if (_mb.moving) {
		/* The whole selection follows the finger. */
		sel = MovedSelection();
		_thd.selstart = sel.selstart;
		_thd.selend = sel.selend;
	} else {
		sel = SnapshotSelection(w);
		sel.station = IsStationPlacement(sel);
		/* Until the finger moves, a station tap shows the picker's size when lifted; no label yet. */
		if (sel.station && !_mb.adjusting && sel.StartTile() == sel.EndTile()) return;
	}

	TileIndex end = sel.EndTile();
	bool estimate = sel.proc == DDSP_PLACE_RAIL || sel.proc == DDSP_DEMOLISH_AREA || sel.proc == DDSP_CONVERT_RAIL || sel.proc == DDSP_BUILD_SIGNALS || sel.station || sel.depot;
	if (end != _mb.estimated_end || sel.StartTile() != _mb.estimated_start) {
		_mb.estimated_end = end;
		_mb.estimated_start = sel.StartTile();
		_mb.sel = sel;
		if (sel.station) UpdateJoinCandidates();
		_mb.estimate = estimate ? TrySelection(sel, true) : BuildResult{};
	}

	std::string text;
	bool error = false;
	if (!_mb.estimate.valid) {
		text = GetString(STR_MOBILE_BUILD_LABEL_TILES, sel.TileCount());
	} else if (_mb.estimate.failed) {
		text = _mb.estimate.error;
		error = true;
	} else {
		text = GetSelectionCostText(sel, _mb.estimate.cost);
	}

	MobileBuildLabelWindow *label = dynamic_cast<MobileBuildLabelWindow *>(FindWindowById(WindowClass::MobileBuildLabel, 0));
	if (label == nullptr) label = new MobileBuildLabelWindow(_mobile_build_label_desc);
	label->Update(text, error);
}

/**
 * Screen position of the centre of a tile.
 * @param vp The viewport.
 * @param tile The tile.
 * @return The position in screen pixels.
 */
static Point TileCentreOnScreen(const Viewport &vp, TileIndex tile)
{
	Point pt = RemapCoords(TileX(tile) * TILE_SIZE + TILE_SIZE / 2, TileY(tile) * TILE_SIZE + TILE_SIZE / 2, GetTilePixelZ(tile));
	return {vp.left + UnScaleByZoom(pt.x - vp.virtual_left, vp.zoom), vp.top + UnScaleByZoom(pt.y - vp.virtual_top, vp.zoom)};
}

/**
 * Find the handle under a point: the nearer end of the pending selection whose hit area contains it.
 * @param vp The viewport.
 * @param x Screen x.
 * @param y Screen y.
 * @return The tile of the handle, or \c INVALID_TILE.
 */
/**
 * The handles of the pending selection: its two ends, or the four corners of a station footprint.
 * @return The handle tiles.
 */
static std::vector<TileIndex> GetHandles()
{
	if (_mb.sel.depot) return {};
	if (!_mb.sel.station) return {_mb.sel.StartTile(), _mb.sel.EndTile()};
	TileArea ta = _mb.sel.Area();
	return {ta.tile, TileAddXY(ta.tile, ta.w - 1, 0), TileAddXY(ta.tile, 0, ta.h - 1), TileAddXY(ta.tile, ta.w - 1, ta.h - 1)};
}

/**
 * The handle opposite to a handle, which stays put while the handle is dragged.
 * @param handle A handle.
 * @return The opposite end or corner.
 */
static TileIndex GetOppositeHandle(TileIndex handle)
{
	if (!_mb.sel.station) return handle == _mb.sel.StartTile() ? _mb.sel.EndTile() : _mb.sel.StartTile();
	TileArea ta = _mb.sel.Area();
	uint x = TileX(handle) == TileX(ta.tile) ? TileX(ta.tile) + ta.w - 1 : TileX(ta.tile);
	uint y = TileY(handle) == TileY(ta.tile) ? TileY(ta.tile) + ta.h - 1 : TileY(ta.tile);
	return TileXY(x, y);
}

static TileIndex FindHandle(const Viewport &vp, int x, int y)
{
	int half = std::max(ScaleGUITrad(20), UnScaleByZoom(TILE_PIXELS * 2 * ZOOM_BASE, vp.zoom) / 2);
	TileIndex best = INVALID_TILE;
	int best_dist = INT_MAX;
	for (TileIndex tile : GetHandles()) {
		Point c = TileCentreOnScreen(vp, tile);
		int dx = abs(x - c.x);
		int dy = abs(y - c.y);
		if (dx > half || dy > half) continue;
		if (dx + dy < best_dist) {
			best_dist = dx + dy;
			best = tile;
		}
	}
	return best;
}

/**
 * A click on the main viewport while a construction is in review.
 * @param vp The viewport.
 * @param x Screen x.
 * @param y Screen y.
 * @return What to do with the click.
 */
MobileViewportClick MobileBuildOnViewportClick(const Viewport &vp, int x, int y)
{
	if (!IsMobileLayout()) return MobileViewportClick::NotHandled;

	switch (_mb.state) {
		case MobileBuildState::Done:
			/* The next touch on the map ends the undo offer and is handled as usual. */
			ClearUndo();
			SetState(MobileBuildState::Idle);
			return MobileViewportClick::NotHandled;

		case MobileBuildState::Pending: {
			if (_mb.sel.tunnel) return MobileViewportClick::Pan;
			if (_mb.sel.IsSignals() && GetMobileBuildKind() == MobileBuildKind::Change) {
				Point pt = GetTileBelowCursor();
				if (pt.x != -1 && TileVirtXY(pt.x, pt.y) == _mb.sel.StartTile()) {
					_mb.sel.cycles++;
					_mb.estimate = TrySelection(_mb.sel, true);
					SetState(MobileBuildState::Pending);
					return MobileViewportClick::Consumed;
				}
			}
			TileIndex handle = FindHandle(vp, x, y);
			if (handle != INVALID_TILE) {
				/* Adjust: drag the touched end or corner, the opposite one stays where it is. */
				TileIndex fixed = GetOppositeHandle(handle);
				ReleaseHighlight();
				SetRailRemoveMode(_mb.sel.remove);
				VpStartPlaceSizing(fixed, _mb.sel.method, _mb.sel.proc);
				if (_mb.sel.station) VpSetPlaceSizingLimit(_settings_game.station.station_spread);
				_mb.adjusting = true;
				return MobileViewportClick::Consumed;
			}

			/* Move: a drag starting inside a station or depot moves it. */
			Point pt = GetTileBelowCursor();
			if (!_mb.sel.IsMovable() || pt.x == -1 || !_mb.sel.Area().Contains(TileVirtXY(pt.x, pt.y))) return MobileViewportClick::Pan;
			_mb.moving = true;
			_mb.move_from = _mb.sel;
			_mb.move_grab = TileVirtXY(pt.x, pt.y);
			_mb.move_delta = 0;
			ReleaseHighlight();
			VpStartPlaceSizing(_mb.sel.StartTile(), _mb.sel.method, _mb.sel.proc);
			return MobileViewportClick::Consumed;
		}

		case MobileBuildState::BridgePicking:
			return MobileViewportClick::Pan;

		default:
			return MobileViewportClick::NotHandled;
	}
}

/** The placement tool was reset (another tool, closing the toolbar, ...): forget the pending selection. */
void MobileBuildOnPlacementReset()
{
	if (_mb.state == MobileBuildState::Idle && !_thd.freeze) return;
	_thd.freeze = false;
	_mb.estimate = {};
	_mb.adjusting = false;
	_mb.moving = false;
	/* Done keeps describing what was built ("Built station") while Undo is offered. */
	if (_mb.state != MobileBuildState::Done) _mb.sel.station = false;
	_mb.join_candidates.clear();
	CloseBuildLabel();
	if (_mb.state == MobileBuildState::BridgePicking) CloseWindowByClass(WindowClass::BuildBridge);
	if (_mb.state != MobileBuildState::Done) _mb.state = MobileBuildState::Idle;
	SetMobileBuildDirty();
}

/**
 * Should the measurement tooltip ("Length: 5") show? The mobile label replaces it for reviewed tools.
 * @return \c true when it should show.
 */
bool MobileBuildMeasurementTooltipsAllowed()
{
	return !IsReviewToolbar(_thd.GetCallbackWnd());
}

/**
 * A tap with the tunnel tool: show the tunnel and its cost for review instead of building it.
 * @param w The rail toolbar.
 * @param tile The tapped tile.
 * @return \c true when the tunnel is now in review; \c false to build it at once.
 */
bool MobileBuildTunnelTap(Window *w, TileIndex tile)
{
	if (!IsReviewToolbar(w) || !_settings_client.gui.mobile_confirm_build) return false;

	_mb.message.clear();
	ClearUndo();
	/* Find the other end, as the desktop does while hovering. */
	Command<Commands::BuildTunnel>::Do(DoCommandFlag::Auto, tile, TransportType::Rail, GetCurrentRailType(), INVALID_ROADTYPE);
	VpSetPresizeRange(tile, _build_tunnel_endtile == INVALID_TILE ? tile : _build_tunnel_endtile);
	UpdateTileSelection();

	PendingSelection sel = SnapshotSelection(w);
	sel.tunnel = true;
	sel.tunnel_start = tile;
	sel.tunnel_end = _build_tunnel_endtile == INVALID_TILE ? tile : _build_tunnel_endtile;
	sel.place_mode = _thd.place_mode;
	_mb.sel = sel;
	_thd.freeze = true;

	_mb.estimate = TrySelection(_mb.sel, true);
	SetState(MobileBuildState::Pending);
	return true;
}

/**
 * Which way a new depot faces: towards the one neighbouring track that ends at it, or else the way the last depot faced.
 * @param tile The depot tile.
 * @param[out] faces_track Whether a track was found.
 * @return The direction of the entrance.
 */
static DiagDirection GetAutoDepotDirection(TileIndex tile, bool &faces_track)
{
	DiagDirection found = DiagDirection::Invalid;
	int count = 0;
	for (DiagDirection dir : EnumRange(DiagDirection::End)) {
		TileIndex neighbour = TileAddByDiagDir(tile, dir);
		if (!IsValidTile(neighbour) || !IsPlainRailTile(neighbour) || !IsTileOwner(neighbour, _local_company)) continue;
		/* Track in the neighbour that a train leaving the depot this way would run onto. */
		if (GetTrackBits(neighbour).Any(DiagdirReachesTracks(dir))) {
			found = dir;
			count++;
		}
	}
	faces_track = count == 1;
	return faces_track ? found : GetRailDepotDirection();
}

/**
 * A tap with the depot tool: show the depot and its cost for review instead of building it.
 * @param w The rail toolbar.
 * @param tile The tapped tile.
 * @return \c true when the depot is now in review; \c false to build it at once.
 */
bool MobileBuildDepotTap(Window *w, TileIndex tile)
{
	if (!IsReviewToolbar(w)) return false;

	_mb.message.clear();
	ClearUndo();
	PendingSelection sel;
	sel.cls = w->window_class;
	sel.num = w->window_number;
	sel.method = VPM_X_AND_Y;
	sel.proc = DDSP_BUILD_STATION;
	sel.drawstyle = HT_RECT;
	sel.place_mode = _thd.place_mode;
	SetSelectionArea(sel, tile, tile);
	sel.depot = true;
	sel.depot_dir = GetAutoDepotDirection(tile, sel.depot_faces_track);
	_mb.sel = sel;
	ShowFrozen(sel);

	_mb.estimate = TrySelection(_mb.sel, true);
	SetState(MobileBuildState::Pending);
	return true;
}

/** @return The tile the pending selection starts at. */
TileIndex GetMobileBuildPendingTile()
{
	return _mb.sel.StartTile();
}

/** Turn the entrance of the depot in review clockwise. */
void MobileBuildRotateDepot()
{
	if (_mb.state != MobileBuildState::Pending || !_mb.sel.depot) return;
	_mb.sel.depot_dir = ChangeDiagDir(_mb.sel.depot_dir, DiagDirDiff::Right90);
	_mb.sel.depot_faces_track = false;
	_mb.estimate = TrySelection(_mb.sel, true);
	MarkTileDirtyByTile(_mb.sel.StartTile());
	SetMobileBuildDirty();
}

/**
 * Is a depot in review on this tile, and which way does it face?
 * @param tile The tile.
 * @param[out] dir The direction of its entrance.
 * @return \c true for the tile of a depot in review.
 */
bool GetMobileBuildDepotArrow(TileIndex tile, DiagDirection &dir)
{
	if (_mb.state != MobileBuildState::Pending || !_mb.sel.depot || tile != _mb.sel.StartTile()) return false;
	dir = _mb.sel.depot_dir;
	return true;
}

/* --- The station picker --- */

/**
 * The size of the station footprint in review.
 * @param[out] x Tiles along the map's X axis.
 * @param[out] y Tiles along the map's Y axis.
 * @return \c true when a station is in review.
 */
bool GetMobilePendingStationSize(uint &x, uint &y)
{
	if (_mb.state != MobileBuildState::Pending || !_mb.sel.station) return false;
	TileArea ta = _mb.sel.Area();
	x = ta.w;
	y = ta.h;
	return true;
}

/** @return Whether a station is in review. */
bool GetMobilePendingStationSize()
{
	uint x, y;
	return GetMobilePendingStationSize(x, y);
}

/**
 * The picker changed: resize the station footprint in review, where it was placed.
 * @param x Tiles along the map's X axis.
 * @param y Tiles along the map's Y axis.
 */
void MobileBuildStationResize(uint x, uint y)
{
	if (!GetMobilePendingStationSize()) return;
	PlaceStationFootprint(x, y);
	MobileBuildStationResize();
}

/** Show the station footprint in review again, e.g. after the catchment area was switched on or off. */
void MobileBuildStationResize()
{
	if (!GetMobilePendingStationSize()) return;
	ShowFrozen(_mb.sel);
	UpdateJoinCandidates();
	_mb.estimate = TrySelection(_mb.sel, true);
	SetMobileBuildDirty();
	InvalidateWindowData(WindowClass::BuildStation, TransportType::Rail);
}

/** @return The stations the footprint in review can join; empty when there is no choice to make. */
const std::vector<StationID> &GetMobileStationJoinCandidates()
{
	static const std::vector<StationID> none;
	return GetMobilePendingStationSize() ? _mb.join_candidates : none;
}

/** @return The station the footprint in review joins, or \c NEW_STATION. */
StationID GetMobileStationJoinShown()
{
	return _mb.join_choice != StationID::Invalid() ? _mb.join_choice : _mb.join_default;
}

/**
 * The player chose which station the footprint in review joins.
 * @param station The station, or \c NEW_STATION.
 */
void MobileBuildSetStationJoin(StationID station)
{
	if (!GetMobilePendingStationSize()) return;
	_mb.join_choice = station;
	_mb.estimate = TrySelection(_mb.sel, true);
	SetMobileBuildDirty();
}

/**
 * The join choice for building the station in review, replacing the desktop's join window.
 * @param[out] join The station to join, or \c NEW_STATION.
 * @param[out] adjacent Whether building next to another station is allowed.
 * @return \c false when the player made no choice, so the desktop's default applies.
 */
bool GetMobileStationJoin(StationID &join, bool &adjacent)
{
	if (!_mb.sel.station || _mb.join_choice == StationID::Invalid()) return false;
	join = _mb.join_choice;
	adjacent = true;
	return true;
}

/**
 * The player tapped a bridge in the bridge list.
 * @param success Whether it was built.
 * @param cost Its cost.
 * @param start A head of the bridge, which demolishes the whole bridge.
 * @param error The error when it failed.
 */
void MobileBuildBridgeResult(bool success, Money cost, TileIndex start, const std::string &error)
{
	if (_mb.state != MobileBuildState::BridgePicking) return;

	PendingSelection built = _mb.sel;
	ReleaseHighlight();
	if (!success) {
		_mb.message = error;
		SetState(MobileBuildState::Idle);
		return;
	}

	_mb.done_tiles = built.TileCount();
	_mb.done_cost = cost;
	_mb.undo = {};
	_mb.undo.cls = built.cls;
	_mb.undo.num = built.num;
	_mb.undo.tunnel = true; // Undo by demolishing one head; see MobileBuildUndo().
	_mb.undo.tunnel_start = start;
	{
		CommandResultCapture capture(true);
		Command<Commands::LandscapeClear>::Post(STR_ERROR_CAN_T_CLEAR_THIS_AREA, start);
		_mb.undo_available = !capture.failed;
		_mb.undo_cost = capture.cost;
	}
	SetState(MobileBuildState::Done);
}

/** The bridge list closed; when no bridge was built, the span is cancelled. */
void MobileBuildBridgeListClosed()
{
	if (_mb.state != MobileBuildState::BridgePicking) return;
	ReleaseHighlight();
	SetState(MobileBuildState::Idle);
}

/* --- Toolbar actions --- */

/**
 * Do any of the tiles of a selection already have track? Undoing a construction removes the whole
 * selection, which would then also remove track the player had before.
 * @param sel The selection.
 * @return \c true when a tile in the selection's rectangle has track.
 */
static bool SelectionHasTrack(const PendingSelection &sel)
{
	for (TileIndex tile : TileArea(sel.StartTile(), sel.EndTile())) {
		if (IsTileType(tile, TileType::Railway)) return true;
	}
	return false;
}

/**
 * Does a station footprint cover tiles of an existing station? Undoing would then remove part of that station too.
 * @param sel The selection.
 * @return \c true when a tile in the footprint is a station tile.
 */
static bool SelectionHasStation(const PendingSelection &sel)
{
	for (TileIndex tile : sel.Area()) {
		if (IsTileType(tile, TileType::Station)) return true;
	}
	return false;
}

/** Build the pending selection. */
void MobileBuildConfirm()
{
	if (!MobileBuildCanConfirm()) return;

	PendingSelection sel = _mb.sel;
	bool is_waypoint = sel.IsWaypoint();
	bool offer_undo = GetMobileBuildKind() == MobileBuildKind::Build && !sel.tunnel && !sel.depot && !sel.station && (is_waypoint || !SelectionHasTrack(sel));
	/* A station over existing track is undone keeping the track; over an existing station, not at all. */
	bool undo_station = sel.station && !SelectionHasStation(sel);
	bool keep_rail = SelectionHasTrack(sel);

	ReleaseHighlight();
	BuildResult result = TrySelection(sel, false);
	if (result.failed) {
		/* Back to review, with the real error. */
		ShowFrozen(sel);
		_mb.estimate = result;
		SetState(MobileBuildState::Pending);
		return;
	}

	_mb.done_tiles = sel.TileCount();
	_mb.done_cost = result.cost;
	_mb.undo_available = false;
	_mb.undo_keep_rail = false;
	if ((sel.tunnel || sel.depot) && result.valid) {
		/* Undo a tunnel by demolishing its entrance, which removes the whole tunnel; a depot by demolishing it. */
		_mb.undo = sel;
		if (sel.depot) {
			_mb.undo.tunnel = true;
			_mb.undo.tunnel_start = sel.StartTile();
		}
		offer_undo = true;
	} else if (undo_station && result.valid) {
		_mb.undo = sel;
		_mb.undo_keep_rail = keep_rail;
		offer_undo = true;
	} else if (offer_undo) {
		_mb.undo = sel;
		_mb.undo.remove = true;
	}
	if (offer_undo) {
		BuildResult undo_estimate;
		if (_mb.undo.tunnel) {
			CommandResultCapture capture(true);
			Command<Commands::LandscapeClear>::Post(STR_ERROR_CAN_T_CLEAR_THIS_AREA, _mb.undo.tunnel_start);
			undo_estimate.failed = capture.failed;
			undo_estimate.cost = capture.cost;
		} else if (_mb.undo.station) {
			CommandResultCapture capture(true);
			Command<Commands::RemoveFromRailStation>::Post(STR_ERROR_CAN_T_REMOVE_PART_OF_STATION, _mb.undo.EndTile(), _mb.undo.StartTile(), _mb.undo_keep_rail);
			undo_estimate.failed = capture.failed;
			undo_estimate.cost = capture.cost;
		} else {
			undo_estimate = TrySelection(_mb.undo, true);
		}
		_mb.undo_available = !undo_estimate.failed;
		_mb.undo_cost = undo_estimate.cost;
	}
	SetState(MobileBuildState::Done);
}

/** Cancel the pending selection, keeping the tool. */
void MobileBuildCancel()
{
	_mb.message.clear();
	if (_mb.state == MobileBuildState::Pending || _mb.state == MobileBuildState::BridgePicking) DropPending();
}

/** Remove what the last construction built. */
void MobileBuildUndo()
{
	if (!MobileBuildHasUndo()) return;
	_mb.undo_available = false;

	BuildResult result;
	if (_mb.undo.tunnel) {
		CommandResultCapture capture(false);
		Command<Commands::LandscapeClear>::Post(STR_ERROR_CAN_T_CLEAR_THIS_AREA, _mb.undo.tunnel_start);
		result.failed = capture.failed;
		if (capture.failed) result.error = capture.error.GetDecodedString();
	} else if (_mb.undo.station) {
		CommandResultCapture capture(false);
		Command<Commands::RemoveFromRailStation>::Post(STR_ERROR_CAN_T_REMOVE_PART_OF_STATION, _mb.undo.EndTile(), _mb.undo.StartTile(), _mb.undo_keep_rail);
		result.failed = capture.failed;
		if (capture.failed) result.error = capture.error.GetDecodedString();
	} else {
		HighLightStyle place_mode = _thd.place_mode;
		result = TrySelection(_mb.undo, false);
		_thd.place_mode = place_mode;
		SetTileSelectSize(1, 1);
	}
	_mb.message = result.failed ? result.error : std::string{};
	SetState(MobileBuildState::Idle);
}

/**
 * The back button: cancel the drag, then the pending selection, then drop the tool.
 * @return \c true when something was cancelled.
 */
bool MobileBuildBack()
{
	if (!IsMobileLayout()) return false;
	switch (_mb.state) {
		case MobileBuildState::Dragging:
			_special_mouse_mode = SpecialMouseMode::None;
			RestorePlaceModeAfterSizing();
			_mb.adjusting = false;
			SetState(MobileBuildState::Idle);
			MarkWholeScreenDirty();
			return true;

		case MobileBuildState::Pending:
		case MobileBuildState::BridgePicking:
			DropPending();
			return true;

		default:
			if (IsReviewToolbar(_thd.GetCallbackWnd())) {
				ResetObjectToPlace();
				return true;
			}
			return false;
	}
}

/**
 * Set the hint shown while the tool waits for the first touch.
 * @param hint The hint, or \c STR_NULL for none.
 */
void MobileBuildSetToolHint(StringID hint)
{
	_mb.hint = hint;
	_mb.message.clear();
	SetMobileBuildDirty();
}

/** Estimate the pending selection again, e.g. after the rail type or the remove mode changed. */
void MobileBuildReestimate()
{
	if (_mb.state != MobileBuildState::Pending) return;
	_mb.sel.remove = IsRailRemoveModeActive();
	_mb.estimate = TrySelection(_mb.sel, true);
	SetMobileBuildDirty();
}

/* --- State for the toolbar --- */

MobileBuildState GetMobileBuildState()
{
	return _mb.state;
}

MobileBuildKind GetMobileBuildKind()
{
	switch (_mb.sel.proc) {
		case DDSP_DEMOLISH_AREA: return MobileBuildKind::Demolish;
		case DDSP_CONVERT_RAIL: return MobileBuildKind::Convert;
		case DDSP_BUILD_SIGNALS:
			if (_mb.sel.remove) return MobileBuildKind::Remove;
			if (IsMobileSignalConvertOn()) return MobileBuildKind::Convert;
			/* A tap on a signal that's there turns it. */
			if (_mb.sel.IsSingleTile() && IsPlainRailTile(_mb.sel.StartTile()) && HasSignals(_mb.sel.StartTile())) return MobileBuildKind::Change;
			return MobileBuildKind::Build;
		default: return _mb.sel.remove ? MobileBuildKind::Remove : MobileBuildKind::Build;
	}
}

/**
 * The text of the toolbar's status line for the current state.
 * @param[out] colour Colour to draw it in.
 * @return The text.
 */
std::string GetMobileBuildStatus(TextColour &colour)
{
	colour = TextColour::Black;
	switch (_mb.state) {
		case MobileBuildState::Dragging:
			return GetString(STR_MOBILE_BUILD_STATUS_LIFT);

		case MobileBuildState::Pending:
			if (_mb.estimate.failed) {
				colour = TextColour::Red;
				return GetString(STR_MOBILE_BUILD_STATUS_ERROR, CapitaliseError(_mb.estimate.error));
			}
			return GetSelectionCostText(_mb.sel, _mb.estimate.cost);

		case MobileBuildState::Done: {
			std::string money = _mb.done_cost < 0 ? GetString(STR_MOBILE_BUILD_REFUND, -_mb.done_cost) : GetString(STR_JUST_CURRENCY_LONG, _mb.done_cost);
			if (_mb.sel.station) return GetString(STR_MOBILE_BUILD_STATUS_BUILT_STATION, money);
			if (_mb.sel.depot) return GetString(STR_MOBILE_BUILD_STATUS_BUILT_DEPOT, money);
			if (_mb.sel.IsWaypoint()) return GetString(STR_MOBILE_BUILD_STATUS_BUILT_WAYPOINT, money);
			if (_mb.sel.IsSignals()) return GetString(STR_MOBILE_BUILD_STATUS_DONE, money);
			return GetString(STR_MOBILE_BUILD_STATUS_BUILT, _mb.done_tiles, money);
		}

		case MobileBuildState::BridgePicking:
			return GetString(STR_MOBILE_BUILD_STATUS_PICK_BRIDGE, _mb.sel.TileCount());

		default:
			if (!_mb.message.empty()) {
				colour = TextColour::Red;
				return GetString(STR_MOBILE_BUILD_STATUS_ERROR, CapitaliseError(_mb.message));
			}
			return _mb.hint == STR_NULL ? std::string{} : GetString(_mb.hint);
	}
}

bool MobileBuildCanConfirm()
{
	return _mb.state == MobileBuildState::Pending && _mb.estimate.valid && !_mb.estimate.failed;
}

bool MobileBuildCanCancel()
{
	return _mb.state == MobileBuildState::Pending || _mb.state == MobileBuildState::BridgePicking;
}

bool MobileBuildHasUndo()
{
	return _mb.state == MobileBuildState::Done && _mb.undo_available;
}

Money GetMobileBuildUndoCost()
{
	return _mb.undo_cost;
}

/**
 * Is the tile one of the handles of the pending selection?
 * @param tile The tile.
 * @return \c true for the two ends of a pending selection.
 */
bool IsMobileBuildHandleTile(TileIndex tile)
{
	if (_mb.state != MobileBuildState::Pending && _mb.state != MobileBuildState::BridgePicking) return false;
	if (_mb.sel.station) {
		TileArea ta = _mb.sel.Area();
		uint x = TileX(tile);
		uint y = TileY(tile);
		return (x == TileX(ta.tile) || x == TileX(ta.tile) + ta.w - 1) && (y == TileY(ta.tile) || y == TileY(ta.tile) + ta.h - 1) && ta.Contains(tile);
	}
	if (_mb.sel.depot) return false;
	return tile == _mb.sel.StartTile() || tile == _mb.sel.EndTile();
}

/**
 * Show an error in the toolbar's status line when a reviewed toolbar is open.
 * @param message The error.
 * @return \c true when the error was taken.
 */
bool MobileBuildHandleError(const std::string &message)
{
	if (!IsMobileLayout() || FindWindowById(WindowClass::BuildToolbar, TransportType::Rail) == nullptr) return false;
	_mb.message = message;
	SetMobileBuildDirty();
	return true;
}

/**
 * Reveal the pending selection: when the visible part of the map changed (a sheet opened, the layout changed) and the
 * selection isn't fully inside it any more, scroll so the selection's centre is in the middle of the visible map.
 * Called by the mobile layout pass.
 */
void MobileBuildRevealPending()
{
	static Rect last_map{};
	Rect map = GetMobileVisibleMapRect();
	bool changed = map.left != last_map.left || map.top != last_map.top || map.right != last_map.right || map.bottom != last_map.bottom;
	last_map = map;
	if (!changed || (_mb.state != MobileBuildState::Pending && _mb.state != MobileBuildState::BridgePicking)) return;

	const Window *main = GetMainWindow();
	if (main == nullptr || main->viewport == nullptr) return;
	TileArea ta = _mb.sel.tunnel ? TileArea(_mb.sel.tunnel_start, _mb.sel.tunnel_end) : _mb.sel.Area();
	bool inside = true;
	for (TileIndex corner : {ta.tile, TileAddXY(ta.tile, ta.w - 1, 0), TileAddXY(ta.tile, 0, ta.h - 1), TileAddXY(ta.tile, ta.w - 1, ta.h - 1)}) {
		Point pt = TileCentreOnScreen(*main->viewport, corner);
		if (!map.Contains(pt)) inside = false;
	}
	if (!inside) ScrollMainWindowToTile(TileAddXY(ta.tile, ta.w / 2, ta.h / 2));
}

/** Ends the undo offer after a while. */
static const IntervalTimer<TimerWindow> _undo_timeout = {std::chrono::milliseconds(500), [](auto) {
	static auto done_since = std::chrono::steady_clock::now();
	static MobileBuildState last_state = MobileBuildState::Idle;
	/* Pausing or unpausing changes what can be built ("Not allowed while paused"): estimate again. */
	static bool last_paused = false;
	if (_pause_mode.Any() != last_paused) {
		last_paused = _pause_mode.Any();
		MobileBuildReestimate();
	}
	if (_mb.state != last_state) {
		last_state = _mb.state;
		done_since = std::chrono::steady_clock::now();
	}
	if (_mb.state == MobileBuildState::Done && std::chrono::steady_clock::now() - done_since > UNDO_OFFER_TIME) {
		ClearUndo();
		SetState(MobileBuildState::Idle);
	}
}};
