/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file bridge_gui.cpp Graphical user interface for bridge construction. */

#include "stdafx.h"
#include "bridge.h"
#include "error.h"
#include "command_func.h"
#include "rail.h"
#include "road.h"
#include "strings_func.h"
#include "window_func.h"
#include "sound_func.h"
#include "gfx_func.h"
#include "tunnelbridge.h"
#include "sortlist_type.h"
#include "dropdown_func.h"
#include "core/geometry_func.hpp"
#include "tunnelbridge_map.h"
#include "road_gui.h"
#include "tunnelbridge_cmd.h"

#include "mobile_build.h"
#include "zoom_func.h"
#include "mobile_gui.h"

#include "widgets/bridge_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** The type of the last built rail bridge */
static BridgeType _last_railbridge_type = 0;
/** The type of the last built road bridge */
static BridgeType _last_roadbridge_type = 0;

/**
 * Carriage for the data we need if we want to build a bridge
 */
struct BuildBridgeData {
	BridgeType index;
	const BridgeSpec *spec;
	Money cost;
};

typedef GUIList<BuildBridgeData> GUIBridgeList; ///< List of bridges, used in #BuildBridgeWindow.

/**
 * Callback executed after a build Bridge CMD has been called
 *
 * @param result Whether the build succeeded
 * @param end_tile End tile of the bridge.
 * @param tile_start start tile
 * @param transport_type transport type.
 */
void CcBuildBridge(Commands, const CommandCost &result, TileIndex end_tile, TileIndex tile_start, TransportType transport_type, BridgeType, RailType, RoadType)
{
	if (result.Failed()) return;
	if (_settings_client.sound.confirm) SndPlayTileFx(SND_27_CONSTRUCTION_BRIDGE, end_tile);

	if (transport_type == TransportType::Road) {
		DiagDirection end_direction = ReverseDiagDir(GetTunnelBridgeDirection(end_tile));
		ConnectRoadToStructure(end_tile, end_direction);

		DiagDirection start_direction = ReverseDiagDir(GetTunnelBridgeDirection(tile_start));
		ConnectRoadToStructure(tile_start, start_direction);
	}
}

/** Window class for handling the bridge-build GUI. */
class BuildBridgeWindow : public Window {
private:
	/* Runtime saved values */
	static Listing last_sorting; ///< Last setting of the sort.

	/** Strings describing how bridges are sorted. */
	static inline const StringID sorter_names[] = {
		STR_SORT_BY_NUMBER,
		STR_SORT_BY_COST,
		STR_SORT_BY_MAX_SPEED,
	};
	static const std::initializer_list<GUIBridgeList::SortFunction * const> sorter_funcs; ///< Functions to sort bridges.

	/** Entries of the mobile sort dropdown, which folds the sort direction into the criteria. */
	static inline const StringID mobile_sorter_names[] = {
		STR_MOBILE_BRIDGE_SORT_SPEED_DESC,
		STR_MOBILE_BRIDGE_SORT_SPEED_ASC,
		STR_MOBILE_BRIDGE_SORT_COST_DESC,
		STR_MOBILE_BRIDGE_SORT_COST_ASC,
		STR_MOBILE_BRIDGE_SORT_NAME,
	};
	/** Listing (descending, criteria) of each entry of #mobile_sorter_names. */
	static inline const Listing mobile_sorter_listings[] = {
		{true, 2}, {false, 2}, {true, 1}, {false, 1}, {false, 0},
	};
	static constexpr int MOBILE_ROW_HEIGHT = 48; ///< Height of a row in the mobile layout, in GUI pixels at 1x.

	const bool mobile; ///< Whether this is the mobile layout, docked in the sheet slot.

	TileIndex start_tile = INVALID_TILE;
	TileIndex end_tile = INVALID_TILE;
	TransportType transport_type = TransportType::Invalid;
	RailType railtype = INVALID_RAILTYPE; ///< Rail type to build if building a rail bridge.
	RoadType roadtype = INVALID_ROADTYPE; ///< Road type to build if building a road bridge.
	GUIBridgeList bridges{};
	int icon_width = 0; ///< Scaled width of the the bridge icon sprite.
	Scrollbar *vscroll = nullptr;

	/** Sort the bridges by their index. @copydoc GUIList::Sorter */
	static bool BridgeIndexSorter(const BuildBridgeData &a, const BuildBridgeData &b)
	{
		return a.index < b.index;
	}

	/** Sort the bridges by their price. @copydoc GUIList::Sorter */
	static bool BridgePriceSorter(const BuildBridgeData &a, const BuildBridgeData &b)
	{
		return a.cost < b.cost;
	}

	/** Sort the bridges by their maximum speed. @copydoc GUIList::Sorter */
	static bool BridgeSpeedSorter(const BuildBridgeData &a, const BuildBridgeData &b)
	{
		return a.spec->speed < b.spec->speed;
	}

	void BuildBridge(BridgeType type)
	{
		switch (this->transport_type) {
			case TransportType::Rail: _last_railbridge_type = type; break;
			case TransportType::Road: _last_roadbridge_type = type; break;
			default: break;
		}
		if (this->mobile) {
			/* The outcome goes to the construction toolbar's status line. */
			bool success;
			Money cost;
			std::string error;
			{
				CommandResultCapture capture(false);
				Command<Commands::BuildBridge>::Post(STR_ERROR_CAN_T_BUILD_BRIDGE_HERE, CcBuildBridge,
							this->end_tile, this->start_tile, this->transport_type, type, this->railtype, this->roadtype);
				success = capture.any && !capture.failed;
				cost = capture.cost;
				if (capture.failed) error = capture.error.empty() ? GetString(capture.error_summary) : capture.error.GetDecodedString();
			}
			/* Only one capture can be active; reporting the result estimates the undo with another. */
			MobileBuildBridgeResult(success, cost, this->start_tile, error);
			return;
		}
		Command<Commands::BuildBridge>::Post(STR_ERROR_CAN_T_BUILD_BRIDGE_HERE, CcBuildBridge,
					this->end_tile, this->start_tile, this->transport_type, type, this->railtype, this->roadtype);
	}

	/**
	 * Get the entry of the mobile sort dropdown matching the current sorting.
	 * @return Index into #mobile_sorter_names.
	 */
	int GetMobileSortIndex() const
	{
		for (int i = 0; i < (int)std::size(mobile_sorter_listings); i++) {
			if (mobile_sorter_listings[i].criteria == this->bridges.SortType() && mobile_sorter_listings[i].order == this->bridges.IsDescSortOrder()) return i;
		}
		return 0;
	}

	/** Sort the builable bridges */
	void SortBridgeList()
	{
		this->bridges.Sort();

		/* Display the current sort variant */
		this->GetWidget<NWidgetCore>(WID_BBS_DROPDOWN_CRITERIA)->SetString(this->mobile ? BuildBridgeWindow::mobile_sorter_names[this->GetMobileSortIndex()] : BuildBridgeWindow::sorter_names[this->bridges.SortType()]);

		/* Set the modified widgets dirty */
		this->SetWidgetDirty(WID_BBS_DROPDOWN_CRITERIA);
		this->SetWidgetDirty(WID_BBS_BRIDGE_LIST);
	}

	/**
	 * Get the StringID to draw in the selection list and set the appropriate DParams.
	 * @param bridge_data the bridge to get the StringID of.
	 * @return the StringID.
	 */
	std::string GetBridgeSelectString(const BuildBridgeData &bridge_data) const
	{
		/* If the bridge has no meaningful speed limit, don't display it. */
		if (bridge_data.spec->speed == UINT16_MAX) {
			return _game_mode == GameMode::Editor
				? GetString(STR_SELECT_BRIDGE_INFO_NAME, bridge_data.spec->material)
				: GetString(STR_SELECT_BRIDGE_INFO_NAME_COST, bridge_data.spec->material, bridge_data.cost);
		}

		uint64_t packed_velocity = PackVelocity(bridge_data.spec->speed, static_cast<VehicleType>(this->transport_type));
		return _game_mode == GameMode::Editor
			? GetString(STR_SELECT_BRIDGE_INFO_NAME_MAX_SPEED, bridge_data.spec->material, packed_velocity)
			: GetString(STR_SELECT_BRIDGE_INFO_NAME_MAX_SPEED_COST, bridge_data.spec->material, packed_velocity, bridge_data.cost);
	}

public:
	/**
	 * Create this BuildBridgeWindoow.
	 * @param desc Window description.
	 * @param start Start tile of bridge.
	 * @param end End tile of bridge.
	 * @param transport_type Transport type of bridge (i.e. rail, road, or water).
	 * @param railtype Rail type for a rail bridge.
	 * @param roadtype Road type for a road bridge.
	 * @param bl List of bridges that can be built.
	 */
	BuildBridgeWindow(WindowDesc &desc, TileIndex start, TileIndex end, TransportType transport_type, RailType railtype, RoadType roadtype, GUIBridgeList &&bl, bool mobile = false) : Window(desc),
		mobile(mobile),
		start_tile(start),
		end_tile(end),
		transport_type(transport_type),
		railtype(railtype),
		roadtype(roadtype),
		bridges(std::move(bl))
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_BBS_SCROLLBAR);
		/* Change the data, or the caption of the gui. Set it to road or rail, accordingly. */
		if (!mobile) this->GetWidget<NWidgetCore>(WID_BBS_CAPTION)->SetString((transport_type == TransportType::Road) ? STR_SELECT_ROAD_BRIDGE_CAPTION : STR_SELECT_RAIL_BRIDGE_CAPTION);
		this->FinishInitNested(transport_type); // Initializes 'this->icon_width'.

		this->parent = FindWindowById(WindowClass::BuildToolbar, transport_type);
		this->bridges.SetListing(BuildBridgeWindow::last_sorting);
		this->bridges.SetSortFuncs(BuildBridgeWindow::sorter_funcs);
		this->bridges.NeedResort();
		this->SortBridgeList();

		this->vscroll->SetCount(this->bridges.size());
	}

	/** Save the last sorting state. */
	~BuildBridgeWindow() override
	{
		BuildBridgeWindow::last_sorting = this->bridges.GetListing();
	}

	void Close(int data = 0) override
	{
		/* Closing the list without building a bridge cancels the span. */
		if (this->mobile) MobileBuildBridgeListClosed();
		this->Window::Close(data);
		/* Give the toolbar and the map their space back. */
		if (this->mobile) RelocateMobileWindows();
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_BBS_TITLE) return GetString(STR_MOBILE_BRIDGE_TITLE, GetTunnelBridgeLength(this->start_tile, this->end_tile) + 2);
		return this->Window::GetWidgetString(widget, stringid);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_BBS_DROPDOWN_ORDER: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->GetString());
				d.width += padding.width + Window::SortButtonWidth() * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				size = maxdim(size, d);
				break;
			}
			case WID_BBS_DROPDOWN_CRITERIA: {
				Dimension d = GetStringListBoundingBox(BuildBridgeWindow::sorter_names);
				d.width += padding.width;
				d.height += padding.height;
				size = maxdim(size, d);
				break;
			}
			case WID_BBS_BRIDGE_LIST: {
				if (this->mobile) {
					Dimension sprite_dim = {0, 0};
					for (const BuildBridgeData &bridge_data : this->bridges) sprite_dim = maxdim(sprite_dim, GetScaledSpriteSize(bridge_data.spec->sprite));
					this->icon_width = sprite_dim.width;
					fill.height = resize.height = std::max<uint>(ScaleGUITrad(MOBILE_ROW_HEIGHT), std::max(sprite_dim.height, (uint)GetCharacterHeight(FontSize::Normal) * 2) + padding.height);
					size.height = 4 * resize.height;
					break;
				}
				Dimension sprite_dim = {0, 0}; // Biggest bridge sprite dimension
				Dimension text_dim   = {0, 0}; // Biggest text dimension
				for (const BuildBridgeData &bridge_data : this->bridges) {
					sprite_dim = maxdim(sprite_dim, GetScaledSpriteSize(bridge_data.spec->sprite));
					text_dim = maxdim(text_dim, GetStringBoundingBox(GetBridgeSelectString(bridge_data)));
				}
				fill.height = resize.height = std::max(sprite_dim.height, text_dim.height) + padding.height; // Max of both sizes + account for matrix edges.

				this->icon_width = sprite_dim.width; // Width of bridge icon.
				size.width = this->icon_width + WidgetDimensions::scaled.hsep_normal + text_dim.width + padding.width;
				size.height = 4 * resize.height; // Smallest bridge gui is 4 entries high in the matrix.
				break;
			}
		}
	}

	Point OnInitialPosition([[maybe_unused]] int16_t sm_width, [[maybe_unused]] int16_t sm_height, [[maybe_unused]] int window_number) override
	{
		/* The mobile layout docks the list in the sheet slot. */
		if (this->mobile) return {0, 0};
		/* Position the window so hopefully the first bridge from the list is under the mouse pointer. */
		NWidgetBase *list = this->GetWidget<NWidgetBase>(WID_BBS_BRIDGE_LIST);
		Point corner; // point of the top left corner of the window.
		corner.y = Clamp(_cursor.pos.y - list->pos_y - 5, GetMainViewTop(), GetMainViewBottom() - sm_height);
		corner.x = Clamp(_cursor.pos.x - list->pos_x - 5, 0, _screen.width - sm_width);
		return corner;
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_BBS_DROPDOWN_ORDER:
				this->DrawSortButton(widget, this->bridges.IsDescSortOrder());
				break;

			case WID_BBS_TITLE:
				DrawMobileTitle(r, this->GetWidgetString(WID_BBS_TITLE, STR_NULL));
				break;

			case WID_BBS_BRIDGE_LIST: {
				if (this->mobile) {
					this->DrawMobileList(r);
					break;
				}
				Rect tr = r.WithHeight(this->resize.step_height).Shrink(WidgetDimensions::scaled.matrix);
				bool rtl = _current_text_dir == TD_RTL;
				auto [first, last] = this->vscroll->GetVisibleRangeIterators(this->bridges);
				for (auto it = first; it != last; ++it) {
					const BridgeSpec *b = it->spec;
					DrawSpriteIgnorePadding(b->sprite, b->pal, tr.WithWidth(this->icon_width, rtl), {AlignmentH::Centre, AlignmentV::Bottom});
					DrawStringMultiLineWithClipping(tr.Indent(this->icon_width + WidgetDimensions::scaled.hsep_normal, rtl), GetBridgeSelectString(*it));
					tr = tr.Translate(0, this->resize.step_height);
				}
				break;
			}
		}
	}

	/**
	 * Draw the bridge list of the mobile layout: picture, name and speed, and the total cost of this span.
	 * @param r The list widget.
	 */
	void DrawMobileList(const Rect &r) const
	{
		bool rtl = _current_text_dir == TD_RTL;
		Rect tr = r.WithHeight(this->resize.step_height).Shrink(WidgetDimensions::scaled.framerect);
		auto [first, last] = this->vscroll->GetVisibleRangeIterators(this->bridges);
		for (auto it = first; it != last; ++it) {
			const BridgeSpec *b = it->spec;
			DrawSpriteIgnorePadding(b->sprite, b->pal, tr.WithWidth(this->icon_width, rtl), {AlignmentH::Centre, AlignmentV::Middle});
			Rect text = tr.Indent(this->icon_width + WidgetDimensions::scaled.hsep_wide, rtl);

			std::string cost = GetString(STR_JUST_CURRENCY_LONG, it->cost);
			int line = GetCharacterHeight(FontSize::Normal);
			int top = CentreBounds(tr.top, tr.bottom, line * 2);
			DrawString(text.left, text.right, CentreBounds(tr.top, tr.bottom, line), cost, TextColour::Black, AlignmentH::End);
			text = text.Indent(GetStringBoundingBox(cost).width + WidgetDimensions::scaled.hsep_wide, !rtl);
			DrawString(text.left, text.right, top, b->material, TextColour::Black);
			if (b->speed != UINT16_MAX) {
				DrawString(text.left, text.right, top + line, GetString(STR_MOBILE_BRIDGE_SPEED, PackVelocity(b->speed, static_cast<VehicleType>(this->transport_type))), TextColour::Black);
			}
			tr = tr.Translate(0, this->resize.step_height);
		}
	}

	EventState OnKeyPress([[maybe_unused]] char32_t key, uint16_t keycode) override
	{
		const uint8_t i = keycode - '1';
		if (i < 9 && i < this->bridges.size()) {
			/* Build the requested bridge */
			this->BuildBridge(this->bridges[i].index);
			this->Close();
			return EventState::Handled;
		}
		return EventState::NotHandled;
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			default: break;
			case WID_BBS_BRIDGE_LIST: {
				auto it = this->vscroll->GetScrolledItemFromWidget(this->bridges, pt.y, this, WID_BBS_BRIDGE_LIST);
				if (it != this->bridges.end()) {
					this->BuildBridge(it->index);
					this->Close();
				}
				break;
			}

			case WID_BBS_DROPDOWN_ORDER:
				this->bridges.ToggleSortOrder();
				this->SetDirty();
				break;

			case WID_BBS_DROPDOWN_CRITERIA:
				if (this->mobile) {
					ShowDropDownMenu(this, BuildBridgeWindow::mobile_sorter_names, this->GetMobileSortIndex(), WID_BBS_DROPDOWN_CRITERIA, 0, 0);
					break;
				}
				ShowDropDownMenu(this, BuildBridgeWindow::sorter_names, this->bridges.SortType(), WID_BBS_DROPDOWN_CRITERIA, 0, 0);
				break;

			case WID_BBS_CLOSE:
				this->Close();
				break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		if (this->mobile && widget == WID_BBS_DROPDOWN_CRITERIA) {
			this->bridges.SetListing(BuildBridgeWindow::mobile_sorter_listings[index]);
			this->bridges.ForceResort();
			this->SortBridgeList();
			return;
		}
		if (widget == WID_BBS_DROPDOWN_CRITERIA && this->bridges.SortType() != index) {
			this->bridges.SetSortType(index);

			this->SortBridgeList();
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_BBS_BRIDGE_LIST);
	}
};

/** Set the default sorting for the bridges */
Listing BuildBridgeWindow::last_sorting = {true, 2};

/** Available bridge sorting functions. */
const std::initializer_list<GUIBridgeList::SortFunction * const> BuildBridgeWindow::sorter_funcs = {
	&BridgeIndexSorter,
	&BridgePriceSorter,
	&BridgeSpeedSorter
};

/** Widgets of the bridge gui. */
static constexpr std::initializer_list<NWidgetPart> _nested_build_bridge_widgets = {
	/* Header */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen, WID_BBS_CAPTION), SetStringTip(STR_SELECT_RAIL_BRIDGE_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEFSIZEBOX, Colours::DarkGreen),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			/* Sort order + criteria buttons */
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_TEXTBTN, Colours::DarkGreen, WID_BBS_DROPDOWN_ORDER), SetFill(1, 0), SetStringTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER),
				NWidget(WWT_DROPDOWN, Colours::DarkGreen, WID_BBS_DROPDOWN_CRITERIA), SetFill(1, 0), SetToolTip(STR_TOOLTIP_SORT_CRITERIA),
			EndContainer(),
			/* Matrix. */
			NWidget(WWT_MATRIX, Colours::DarkGreen, WID_BBS_BRIDGE_LIST), SetFill(1, 0), SetResize(0, 22), SetMatrixDataTip(1, 0, STR_SELECT_BRIDGE_SELECTION_TOOLTIP), SetScrollbar(WID_BBS_SCROLLBAR),
		EndContainer(),

		/* scrollbar + resize button */
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, Colours::DarkGreen, WID_BBS_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, Colours::DarkGreen),
		EndContainer(),
	EndContainer(),
};

/** Widgets of the bridge list in the mobile layout; it sits in the sheet slot. */
static constexpr std::initializer_list<NWidgetPart> _nested_mobile_build_bridge_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::DarkGreen, WID_BBS_TITLE), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_BRIDGE_TITLE), EndContainer(),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_BBS_CLOSE), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_CROSS, STR_MOBILE_BUILD_CANCEL_TOOLTIP),
	EndContainer(),
	NWidget(WWT_DROPDOWN, Colours::DarkGreen, WID_BBS_DROPDOWN_CRITERIA), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_TOOLTIP_SORT_CRITERIA),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::DarkGreen, WID_BBS_BRIDGE_LIST), SetFill(1, 1), SetResize(1, 1), SetToolTip(STR_SELECT_BRIDGE_SELECTION_TOOLTIP), SetScrollbar(WID_BBS_SCROLLBAR), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::DarkGreen, WID_BBS_SCROLLBAR),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::DarkGreen),
		NWidget(WWT_TEXT, Colours::Invalid, WID_BBS_FOOTER), SetMinimalSize(0, 24), SetPadding(0, 4, 0, 4), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_BRIDGE_FOOTER),
	EndContainer(),
};

static WindowDesc _mobile_build_bridge_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::BuildBridge, WindowClass::BuildToolbar,
	WindowDefaultFlag::Construction,
	_nested_mobile_build_bridge_widgets
);

/** Window definition for the rail bridge selection window. */
static WindowDesc _build_bridge_desc(
	WindowPosition::Automatic, "build_bridge", 200, 114,
	WindowClass::BuildBridge, WindowClass::BuildToolbar,
	WindowDefaultFlag::Construction,
	_nested_build_bridge_widgets
);

/**
 * Prepare the data for the build a bridge window.
 *  If we can't build a bridge under the given conditions
 *  show an error message.
 *
 * @param start The start tile of the bridge
 * @param end The end tile of the bridge
 * @param transport_type The transport type
 * @param railtype The rail type for a rail bridge, or \c INVALID_RAILTYPE
 * @param roadtype The road type for a road bridge, or \c INVALID_ROADTYPE
 */
void ShowBuildBridgeWindow(TileIndex start, TileIndex end, TransportType transport_type, RailType railtype, RoadType roadtype)
{
	CloseWindowByClass(WindowClass::BuildBridge);

	/* The bridge length without ramps. */
	const uint bridge_len = GetTunnelBridgeLength(start, end);

	/* If Ctrl is being pressed, check whether the last bridge built is available
	 * If so, return this bridge type. Otherwise continue normally.
	 * We store bridge types for each transport type, so we have to check for
	 * the transport type beforehand.
	 */
	BridgeType last_bridge_type = 0;
	switch (transport_type) {
		case TransportType::Road: last_bridge_type = _last_roadbridge_type; break;
		case TransportType::Rail: last_bridge_type = _last_railbridge_type; break;
		default: break; // water ways and air routes don't have bridge types
	}
	if (IsCtrlPressed() && CheckBridgeAvailability(last_bridge_type, bridge_len).Succeeded()) {
		Command<Commands::BuildBridge>::Post(STR_ERROR_CAN_T_BUILD_BRIDGE_HERE, CcBuildBridge, end, start, transport_type, last_bridge_type, railtype, roadtype);
		return;
	}

	/* only query bridge building possibility once, result is the same for all bridges!
	 * returns CMD_ERROR on failure, and price on success */
	CommandCost ret = Command<Commands::BuildBridge>::Do(CommandFlagsToDCFlags(GetCommandFlags<Commands::BuildBridge>()) | DoCommandFlag::QueryCost, end, start, transport_type, 0, railtype, roadtype);

	GUIBridgeList bl;
	if (!ret.Failed()) {
		/* check which bridges can be built */
		const uint tot_bridgedata_len = CalcBridgeLenCostFactor(bridge_len + 2);

		Money infra_cost = 0;
		switch (transport_type) {
			case TransportType::Road: {
				/* In case we add a new road type as well, we must be aware of those costs. */
				RoadType road_rt = INVALID_ROADTYPE;
				RoadType tram_rt = INVALID_ROADTYPE;
				if (IsBridgeTile(start)) {
					road_rt = GetRoadTypeRoad(start);
					tram_rt = GetRoadTypeTram(start);
				}
				if (RoadTypeIsRoad(roadtype)) {
					road_rt = roadtype;
				} else {
					tram_rt = roadtype;
				}

				if (road_rt != INVALID_ROADTYPE) infra_cost += (bridge_len + 2) * 2 * RoadBuildCost(road_rt);
				if (tram_rt != INVALID_ROADTYPE) infra_cost += (bridge_len + 2) * 2 * RoadBuildCost(tram_rt);

				break;
			}
			case TransportType::Rail: infra_cost = (bridge_len + 2) * RailBuildCost(railtype); break;
			default: break;
		}

		bool any_available = false;
		CommandCost type_check;
		/* loop for all bridgetypes */
		for (BridgeType brd_type = 0; brd_type != MAX_BRIDGES; brd_type++) {
			type_check = CheckBridgeAvailability(brd_type, bridge_len);
			if (type_check.Succeeded()) {
				/* bridge is accepted, add to list */
				BuildBridgeData &item = bl.emplace_back();
				item.index = brd_type;
				item.spec = GetBridgeSpec(brd_type);
				/* Add to terraforming & bulldozing costs the cost of the
				 * bridge itself (not computed with DoCommandFlag::QueryCost) */
				item.cost = ret.GetCost() + (((int64_t)tot_bridgedata_len * _price[Price::BuildBridge] * item.spec->price) >> 8) + infra_cost;
				any_available = true;
			}
		}
		/* give error cause if no bridges available here*/
		if (!any_available) ret = std::move(type_check);
	}

	if (!bl.empty()) {
		if (IsMobileLayout()) {
			MobileShowInSheet(new BuildBridgeWindow(_mobile_build_bridge_desc, start, end, transport_type, railtype, roadtype, std::move(bl), true));
		} else {
			new BuildBridgeWindow(_build_bridge_desc, start, end, transport_type, railtype, roadtype, std::move(bl));
		}
	} else {
		ShowErrorMessage(GetEncodedString(STR_ERROR_CAN_T_BUILD_BRIDGE_HERE), TileX(end) * TILE_SIZE, TileY(end) * TILE_SIZE, ret);
	}
}
