/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file mobile_vehicle_gui.cpp From depot to running train in the mobile layout: the depot sheet, the New train view,
 * the train window with its orders and details, and adding stops by tapping stations. See the round 4 design, part A.
 *
 * These are new windows rather than other layouts of the desktop ones: the desktop depot, purchase list and orders
 * windows are built around drag and drop and many small buttons. They reuse the desktop's window classes (depot,
 * build vehicle, vehicle view), so the game's own invalidations reach them.
 */

#include "stdafx.h"
#include "mobile_vehicle_gui.h"
#include "mobile_gui.h"
#include "window_gui.h"
#include "window_func.h"
#include "command_func.h"
#include "company_func.h"
#include "company_base.h"
#include "depot_cmd.h"
#include "error.h"
#include "textbuf_gui.h"
#include "strings_func.h"
#include "string_func.h"
#include "gfx_func.h"
#include "zoom_func.h"
#include "viewport_func.h"
#include "tilehighlight_func.h"
#include "depot_map.h"
#include "rail_map.h"
#include "station_base.h"
#include "station_map.h"
#include "engine_base.h"
#include "engine_func.h"
#include "engine_gui.h"
#include "train.h"
#include "roadveh.h"
#include "road_map.h"
#include "aircraft.h"
#include "station_func.h"
#include "waypoint_base.h"
#include "road.h"
#include "articulated_vehicles.h"
#include "cargotype.h"
#include "train_cmd.h"
#include "vehicle_cmd.h"
#include "vehicle_func.h"
#include "vehicle_gui.h"
#include "order_base.h"
#include "order_cmd.h"
#include "dropdown_type.h"
#include "dropdown_func.h"
#include "settings_type.h"
#include "timer/timer.h"
#include "timer/timer_window.h"
#include "widgets/mobile_widget.h"

#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

static constexpr int MOBILE_ROW = 48; ///< Height of list rows, in GUI pixels at 1x.
static constexpr int MOBILE_ORDER_ROW = 48; ///< Height of order rows, in GUI pixels at 1x.
static constexpr int MOBILE_STRIP_HEIGHT = 46; ///< Height of a composition strip (sprites and sell buttons), in GUI pixels at 1x.
static constexpr int DEPOT_ROW = 56; ///< Height of a depot row, in GUI pixels at 1x.

/**
 * How many whole rows a list shows. The lists resize by pixel, so their window fills the sheet slot exactly.
 * @param w The window.
 * @param widget The list.
 * @param row Height of a row.
 * @return The number of rows.
 */
static int GetRowCapacity(const Window *w, WidgetID widget, int row)
{
	const NWidgetBase *nwid = w->GetWidget<NWidgetBase>(widget);
	return std::max(1, (static_cast<int>(nwid->current_y) - static_cast<int>(WidgetDimensions::scaled.framerect.Vertical())) / row);
}

/** The train whose selling waits for the player's confirmation. */
static VehicleID _mobile_train_sell = VehicleID::Invalid();

/** The train that vehicles bought in the New train view are added to. */
static VehicleID _mobile_train_target = VehicleID::Invalid();

/** A road vehicle, ship or aircraft was just bought in the mobile buy view: its window opens adding stops. */
static bool _mobile_open_adding_stops = false;

/**
 * Callback of buying a vehicle in the New train view: the first engine becomes the train being built; later
 * vehicles are attached to its end (not relying on the "exactly one stopped train in the depot" rule of the desktop).
 */
void CcMobileBuildTrainPart(Commands, const CommandCost &result, VehicleID new_veh_id, uint, uint16_t, CargoArray, TileIndex tile, EngineID, bool, CargoType, ClientID)
{
	if (result.Failed()) return;

	const Train *target = Train::GetIfValid(_mobile_train_target);
	if (target == nullptr || !target->IsStoppedInDepot() || target->tile != tile) {
		const Train *t = Train::GetIfValid(new_veh_id);
		if (t != nullptr && t->IsFrontEngine()) _mobile_train_target = new_veh_id;
	} else if (new_veh_id != target->index) {
		Command<Commands::MoveRailVehicle>::Post(STR_ERROR_CAN_T_MOVE_VEHICLE, tile, new_veh_id, target->Last()->index, false);
	}
	InvalidateWindowClassesData(WindowClass::BuildVehicle, 0);
	InvalidateWindowClassesData(WindowClass::VehicleDepot, 0);
}

/* --- Shared drawing --- */

/**
 * Draw a train's vehicles side by side; optionally each (with its articulated parts) as a tappable cell of at least
 * 40×40 (round 5 design, section 3.2).
 * @param t The train (first vehicle).
 * @param r Where to draw.
 * @param cells Draw the vehicles as cells, and report where they are.
 * @param[out] out With \p cells, the cell of each vehicle.
 */
static void DrawTrainStrip(const Train *t, const Rect &r, bool cells, std::vector<std::pair<Rect, VehicleID>> *out)
{
	if (out != nullptr) out->clear();
	bool rtl = _current_text_dir == TD_RTL;
	int y = CentreBounds(r.top, r.bottom, 0);
	int x = rtl ? r.right : r.left;
	int min_cell = ScaleGUITrad(40);
	for (const Train *u = t; u != nullptr;) {
		/* A vehicle and its articulated parts. */
		int width = 0;
		for (const Train *part = u; part != nullptr && (part == u || part->IsArticulatedPart()); part = part->Next()) width += part->GetDisplayImageWidth();
		int cell = cells ? std::max(width, min_cell) : width;
		if (rtl ? x - cell < r.left : x + cell > r.right) {
			/* Out of room: the rest is "...". */
			DrawString(r.left, r.right, y - GetCharacterHeight(FontSize::Normal) / 2, STR_MOBILE_ELLIPSIS, TextColour::Black, AlignmentH::End);
			break;
		}
		Rect box{rtl ? x - cell + 1 : x, r.top, rtl ? x : x + cell - 1, r.bottom};
		if (cells) {
			DrawFrameRect(box, Colours::Grey, {});
			if (out != nullptr) out->emplace_back(box, u->index);
		}
		int px = rtl ? CentreBounds(box.left, box.right, 0) + width / 2 : CentreBounds(box.left, box.right, 0) - width / 2;
		const Train *part = u;
		for (; part != nullptr && (part == u || part->IsArticulatedPart()); part = part->Next()) {
			Point offset;
			int w = part->GetDisplayImageWidth(&offset);
			VehicleSpriteSeq seq;
			part->GetImage(rtl ? Direction::E : Direction::W, EngineImageType::InDepot, &seq);
			seq.Draw(px + (rtl ? -offset.x : offset.x), y + offset.y, GetVehiclePalette(part), false);
			px += rtl ? -w : w;
		}
		u = part;
		x += rtl ? -(cell + WidgetDimensions::scaled.hsep_normal) : cell + WidgetDimensions::scaled.hsep_normal;
	}
}

/**
 * Draw a vehicle as a strip: a train's vehicles side by side (see DrawTrainStrip()); any other vehicle (with its
 * articulated parts) as one cell.
 * @param v The vehicle (first part).
 * @param r Where to draw.
 * @param cells Draw the vehicle as a cell, and report where it is.
 * @param[out] out With \p cells, the cells.
 */
static void DrawVehicleStrip(const Vehicle *v, const Rect &r, bool cells, std::vector<std::pair<Rect, VehicleID>> *out)
{
	if (v->type == VehicleType::Train) {
		DrawTrainStrip(Train::From(v), r, cells, out);
		return;
	}
	if (out != nullptr) out->clear();
	bool rtl = _current_text_dir == TD_RTL;
	Rect box = r.WithWidth(std::min(r.Width(), ScaleGUITrad(cells ? 64 : 48)), rtl);
	if (cells) {
		DrawFrameRect(box, Colours::Grey, {});
		if (out != nullptr) out->emplace_back(box, v->index);
	}
	DrawVehicleImage(v, box.Shrink(ScaleGUITrad(4), 0), VehicleID::Invalid(), EngineImageType::InDepot, 0);
}

/**
 * Find the vehicle cell under a point.
 * @param cells The cells, as DrawTrainStrip() reported them, in window coordinates.
 * @param pt The point.
 * @return The vehicle, or \c VehicleID::Invalid().
 */
static VehicleID FindVehicleCell(const std::vector<std::pair<Rect, VehicleID>> &cells, Point pt)
{
	for (const auto &[r, id] : cells) {
		if (r.Contains(pt)) return id;
	}
	return VehicleID::Invalid();
}

/** Entries of a vehicle cell's menu. */
enum VehicleCellMenu : int { VCM_SELL, VCM_REFIT, VCM_DETAILS };

/**
 * Title of a vehicle cell's menu: the vehicle's model.
 * @param id The vehicle; may be invalid.
 * @return The title; empty for none.
 */
static std::string GetVehicleCellMenuTitle(VehicleID id)
{
	const Vehicle *v = Vehicle::GetIfValid(id);
	return v == nullptr ? std::string{} : GetString(STR_ENGINE_NAME, v->engine_type);
}

/**
 * Open a vehicle cell's menu: Sell (+£x), Refit, Details.
 * @param w The window.
 * @param widget The strip widget.
 * @param id The vehicle.
 */
static void ShowVehicleCellMenu(Window *w, WidgetID widget, VehicleID id)
{
	const Vehicle *v = Vehicle::GetIfValid(id);
	if (v == nullptr) return;
	Money value = 0;
	for (const Vehicle *part = v; part != nullptr && (part == v || part->IsArticulatedPart()); part = part->Next()) value += part->value;
	DropDownList list;
	list.push_back(MakeDropDownListStringItem(GetString(STR_MOBILE_VEHICLE_MENU_SELL, value), VCM_SELL, !v->IsStoppedInDepot()));
	list.push_back(MakeDropDownListStringItem(STR_MOBILE_VEHICLE_MENU_REFIT, VCM_REFIT));
	list.push_back(MakeDropDownListStringItem(STR_MOBILE_VEHICLE_MENU_DETAILS, VCM_DETAILS));
	ShowDropDownList(w, std::move(list), -1, widget);
}

/**
 * Sell one vehicle of a train. The engine sells the whole train, after asking.
 * @param parent Window asking.
 * @param id The vehicle.
 */
static void SellTrainVehicle(Window *parent, VehicleID id)
{
	if (const Vehicle *other = Vehicle::GetIfValid(id); other != nullptr && other->type != VehicleType::Train) {
		/* A road vehicle, ship or aircraft is sold whole. */
		Command<Commands::SellVehicle>::Post(GetCmdSellVehMsg(other), other->tile, other->index, true, false, ClientID::Invalid);
		return;
	}
	const Train *v = Train::GetIfValid(id);
	if (v == nullptr) return;
	if (v->IsFrontEngine() && v->Next() != nullptr) {
		_mobile_train_sell = v->index;
		ShowQuery(GetEncodedString(STR_MOBILE_TRAIN_SELL_TRAIN_TITLE), GetEncodedString(STR_MOBILE_TRAIN_SELL_TRAIN_TEXT), parent, [](Window *, bool confirmed) {
			if (!confirmed) return;
			if (const Train *t = Train::GetIfValid(_mobile_train_sell); t != nullptr) {
				Command<Commands::SellVehicle>::Post(STR_ERROR_CAN_T_SELL_TRAIN, t->tile, t->index, true, false, ClientID::Invalid);
			}
		}, false, STR_MOBILE_TRAIN_SELL_TRAIN_VERB);
		return;
	}
	Command<Commands::SellVehicle>::Post(STR_ERROR_CAN_T_SELL_TRAIN, v->tile, v->index, false, false, ClientID::Invalid);
}

/**
 * Summary of a train: vehicles, length, capacity and speed.
 * @param t The train.
 * @return The text.
 */
static std::string GetTrainSummary(const Train *t)
{
	uint vehicles = 0;
	CargoArray capacity{};
	for (const Train *u = t; u != nullptr; u = u->Next()) {
		if (!u->IsArticulatedPart()) vehicles++;
		if (IsValidCargoType(u->cargo_type)) capacity[u->cargo_type] += u->cargo_cap;
	}
	std::string cargo;
	for (CargoType c : EnumRange(NUM_CARGO)) {
		if (capacity[c] == 0) continue;
		if (!cargo.empty()) cargo += ", ";
		cargo += GetString(STR_JUST_CARGO, c, capacity[c]);
	}
	uint length = CeilDiv(t->gcache.cached_total_length, TILE_SIZE);
	return GetString(STR_MOBILE_TRAIN_SUMMARY, vehicles, length, cargo.empty() ? GetString(STR_MOBILE_TRAIN_NO_CARGO) : cargo, PackVelocity(t->GetDisplayMaxSpeed(), VehicleType::Train));
}

/**
 * Summary of a vehicle: for a train its vehicles, length, capacity and speed; otherwise capacity and speed.
 * @param v The vehicle.
 * @return The text.
 */
static std::string GetVehicleSummary(const Vehicle *v)
{
	if (v->type == VehicleType::Train) return GetTrainSummary(Train::From(v));
	CargoArray capacity{};
	for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
		if (IsValidCargoType(u->cargo_type)) capacity[u->cargo_type] += u->cargo_cap;
	}
	std::string cargo;
	for (CargoType c : EnumRange(NUM_CARGO)) {
		if (capacity[c] == 0) continue;
		if (!cargo.empty()) cargo += ", ";
		cargo += GetString(STR_JUST_CARGO, c, capacity[c]);
	}
	return GetString(STR_MOBILE_VEHICLE_SUMMARY, cargo.empty() ? GetString(STR_MOBILE_TRAIN_NO_CARGO) : cargo, PackVelocity(v->GetDisplayMaxSpeed(), v->type));
}

/* --- Depot sheet --- */

/** The depot in the sheet slot: the vehicles in it, New and Clone (any vehicle type; round 5 design, 7.1). */
struct MobileDepotWindow : Window {
	VehicleType type; ///< The vehicles this depot (or hangar) serves.
	std::vector<const Vehicle *> chains; ///< Vehicles (and for trains free wagon chains) in the depot.
	Scrollbar *vscroll = nullptr;

	MobileDepotWindow(WindowDesc &desc, TileIndex tile, VehicleType type) : Window(desc), type(type)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_MDEP_SCROLL);
		static const StringID new_labels[] = {STR_MOBILE_DEPOT_NEW_TRAIN, STR_MOBILE_DEPOT_NEW_ROAD_VEHICLE, STR_MOBILE_DEPOT_NEW_SHIP, STR_MOBILE_DEPOT_NEW_AIRCRAFT};
		this->GetWidget<NWidgetCore>(WID_MDEP_NEW)->SetString(new_labels[to_underlying(type)]);
		this->FinishInitNested(tile);
		this->owner = GetTileOwner(tile);
		this->BuildList();
	}

	void BuildList()
	{
		this->chains.clear();
		TileIndex tile = static_cast<TileIndex>(this->window_number);
		if (this->type == VehicleType::Train) {
			for (const Train *t : Train::Iterate()) {
				if (t->tile != tile || !t->IsInDepot() || t->Previous() != nullptr) continue;
				if (t->IsFrontEngine() || t->IsFreeWagon()) this->chains.push_back(t);
			}
		} else {
			for (const Vehicle *v : Vehicle::Iterate()) {
				if (v->type != this->type || !v->IsPrimaryVehicle() || v->tile != tile || !v->IsInDepot()) continue;
				this->chains.push_back(v);
			}
		}
		this->vscroll->SetCount(this->chains.size());
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->BuildList();
		this->SetDirty();
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		if (widget == WID_MDEP_LIST) {
			/* Resize by pixel, so the window fills the sheet and the buttons sit on its bottom edge. */
			fill.height = resize.height = 1;
			size.height = 2 * ScaleGUITrad(DEPOT_ROW);
		}
		if (widget == WID_MDEP_TITLE) size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal));
	}

	void OnResize() override
	{
		this->vscroll->SetCapacity(GetRowCapacity(this, WID_MDEP_LIST, ScaleGUITrad(DEPOT_ROW)));
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_MDEP_TITLE:
				DrawMobileTitle(r, GetString(STR_DEPOT_NAME, this->type, GetDepotDestinationIndex(static_cast<TileIndex>(this->window_number))));
				break;

			case WID_MDEP_LIST: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
				if (this->chains.empty()) {
					DrawString(tr.Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0), STR_MOBILE_DEPOT_EMPTY, TextColour::Black);
					break;
				}
				int row = ScaleGUITrad(DEPOT_ROW);
				auto [first, last] = this->vscroll->GetVisibleRangeIterators(this->chains);
				bool rtl = _current_text_dir == TD_RTL;
				for (auto it = first; it != last; ++it) {
					const Vehicle *t = *it;
					Rect rr = tr.WithHeight(row);
					Rect arrow = rr.WithWidth(ScaleGUITrad(40), !rtl);
					DrawString(arrow.left, arrow.right, CentreBounds(rr.top, rr.bottom, GetCharacterHeight(FontSize::Normal)), STR_JUST_RIGHT_ARROW, TextColour::Black, AlignmentH::Centre);
					Rect content = rr.Indent(ScaleGUITrad(40), !rtl).Indent(ScaleGUITrad(MOBILE_TEXT_INSET), rtl);
					int line = GetCharacterHeight(FontSize::Normal);
					Rect strip = content.WithHeight(rr.Height() - 2 * line);
					DrawVehicleStrip(t, strip, false, nullptr);
					int y = rr.bottom - 2 * line;
					if (t->IsPrimaryVehicle()) {
						DrawString(content.left, content.right, y, GetString(STR_MOBILE_TRAIN_NAME, t->index), TextColour::Black);
						bool no_orders = t->GetNumManualOrders() == 0;
						DrawString(content.left, content.right, y + line, no_orders ? STR_MOBILE_TRAIN_STOPPED_NO_ORDERS : STR_MOBILE_TRAIN_STOPPED, no_orders ? TextColour::Red : TextColour::Black);
					} else {
						DrawString(content.left, content.right, y + line, STR_MOBILE_DEPOT_FREE_WAGONS, TextColour::Black);
					}
					tr.top += row;
				}
				break;
			}
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MDEP_CLOSE: this->Close(); break;
			case WID_MDEP_NEW:
				_mobile_train_target = VehicleID::Invalid();
				ShowMobileNewTrainWindow(static_cast<TileIndex>(this->window_number), VehicleID::Invalid());
				break;
			case WID_MDEP_CLONE: {
				/* As on desktop: pick the vehicle to clone on the map or in a list. */
				static const CursorID cursors[] = {SPR_CURSOR_CLONE_TRAIN, SPR_CURSOR_CLONE_ROADVEH, SPR_CURSOR_CLONE_SHIP, SPR_CURSOR_CLONE_AIRPLANE};
				SetObjectToPlaceWnd(cursors[to_underlying(this->type)], PAL_NONE, HT_VEHICLE, this);
				break;
			}
			case WID_MDEP_LIST: {
				auto it = this->vscroll->GetScrolledItemFromWidget(this->chains, pt.y, this, WID_MDEP_LIST, WidgetDimensions::scaled.framerect.top, ScaleGUITrad(DEPOT_ROW));
				if (it == this->chains.end()) break;
				if ((*it)->IsPrimaryVehicle()) ShowMobileTrainWindow(*it);
				break;
			}
		}
	}

	bool OnVehicleSelect(const Vehicle *v) override
	{
		if (v->type != this->type) return false;
		Command<Commands::CloneVehicle>::Post(GetCmdBuildVehMsg(this->type), CcCloneVehicle, static_cast<TileIndex>(this->window_number), v->First()->index, false);
		ResetObjectToPlace();
		return true;
	}

	const IntervalTimer<TimerWindow> refresh = {std::chrono::milliseconds(500), [this](auto) {
		this->BuildList();
		this->SetWidgetDirty(WID_MDEP_LIST);
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_depot_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_MDEP_TITLE), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), EndContainer(),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MDEP_CLOSE), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_CROSS, STR_TOOLTIP_CLOSE_WINDOW),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_MDEP_LIST), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_MDEP_SCROLL), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_MDEP_SCROLL),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPadding(4), SetPIP(0, 4, 0),
			NWidget(WWT_PUSHTXTBTN, Colours::Green, WID_MDEP_NEW), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_DEPOT_NEW_TRAIN, STR_DEPOT_TRAIN_NEW_VEHICLES_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MDEP_CLONE), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_DEPOT_CLONE, STR_DEPOT_CLONE_TRAIN_DEPOT_TOOLTIP),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _mobile_depot_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::VehicleDepot, WindowClass::None,
	{},
	_nested_mobile_depot_widgets
);

/**
 * Show a train depot in the sheet slot.
 * @param tile The depot.
 */
void ShowMobileDepotWindow(TileIndex tile, VehicleType type)
{
	Window *w = BringWindowToFrontById(WindowClass::VehicleDepot, tile);
	if (w == nullptr) w = new MobileDepotWindow(_mobile_depot_desc, tile, type);
	MobileShowInSheet(w);
}

/* --- New train --- */

/** Buying a train: engines first, then wagons, each attached to the train being built. */
struct MobileNewTrainWindow : Window {
	RailType railtype; ///< Rail type of the depot.
	bool wagons = false; ///< Showing wagons rather than engines.
	std::vector<EngineID> engines; ///< The list shown.
	Money spent = 0; ///< Money spent since the view opened.
	Money money_at_open = 0; ///< Company money when the view opened.
	mutable std::vector<std::pair<Rect, VehicleID>> cells; ///< Vehicle cells of the strip, in window coordinates.
	VehicleID cell_vehicle = VehicleID::Invalid(); ///< Vehicle whose cell menu is open.
	Scrollbar *vscroll = nullptr;

	MobileNewTrainWindow(WindowDesc &desc, TileIndex tile) : Window(desc), railtype(GetRailType(tile))
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_MNT_SCROLL);
		this->FinishInitNested(tile);
		this->owner = _local_company;
		if (const Company *c = Company::GetIfValid(_local_company); c != nullptr) this->money_at_open = c->money;
		this->wagons = this->GetTarget() != nullptr;
		this->BuildList();
		this->UpdateButtons();
	}

	/** @return The train being built, when it is still in this depot. */
	const Train *GetTarget() const
	{
		const Train *t = Train::GetIfValid(_mobile_train_target);
		return (t != nullptr && t->tile == static_cast<TileIndex>(this->window_number) && t->IsStoppedInDepot()) ? t : nullptr;
	}

	void BuildList()
	{
		this->engines.clear();
		for (const Engine *e : Engine::IterateType(VehicleType::Train)) {
			if (e->IsVariantHidden(_local_company)) continue;
			const RailVehicleInfo &rvi = e->VehInfo<RailVehicleInfo>();
			if (!HasPowerOnRail(rvi.railtypes, this->railtype)) continue;
			if (!IsEngineBuildable(e->index, VehicleType::Train, _local_company)) continue;
			if ((rvi.railveh_type == RailVehicleType::Wagon) != this->wagons) continue;
			this->engines.push_back(e->index);
		}
		/* Cheapest first. */
		std::ranges::sort(this->engines, [](EngineID a, EngineID b) { return Engine::Get(a)->GetCost() < Engine::Get(b)->GetCost(); });
		this->vscroll->SetCount(this->engines.size());
	}

	void UpdateButtons()
	{
		bool has_engine = this->GetTarget() != nullptr;
		this->SetWidgetDisabledState(WID_MNT_TAB_WAGONS, !has_engine);
		this->SetWidgetDisabledState(WID_MNT_NEXT, !has_engine);
		this->GetWidget<NWidgetCore>(WID_MNT_NEXT)->colour = has_engine ? Colours::Green : Colours::Grey;
		this->SetWidgetLoweredState(WID_MNT_TAB_ENGINES, !this->wagons);
		this->SetWidgetLoweredState(WID_MNT_TAB_WAGONS, this->wagons);
		if (const Company *c = Company::GetIfValid(_local_company); c != nullptr) this->spent = std::max<Money>(0, this->money_at_open - c->money);
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		/* The first engine bought: show the wagons. */
		if (!this->wagons && this->GetTarget() != nullptr && data == 0) {
			this->wagons = true;
		}
		this->BuildList();
		this->UpdateButtons();
		this->SetDirty();
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_MNT_LIST:
				fill.height = resize.height = 1;
				size.height = 2 * ScaleGUITrad(MOBILE_ROW);
				break;
			case WID_MNT_STRIP:
				size.height = ScaleGUITrad(MOBILE_STRIP_HEIGHT);
				break;
			case WID_MNT_TITLE:
			case WID_MNT_SUMMARY:
			case WID_MNT_SPENT:
				size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal));
				break;
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacity(GetRowCapacity(this, WID_MNT_LIST, ScaleGUITrad(MOBILE_ROW)));
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_MNT_SPENT: return GetString(STR_MOBILE_TRAIN_SPENT, this->spent);
			default: return this->Window::GetWidgetString(widget, stringid);
		}
	}

	/**
	 * The "+ £price" button of a list row.
	 * @param row The row.
	 * @return The button.
	 */
	static Rect GetBuyButton(const Rect &row)
	{
		return row.WithWidth(ScaleGUITrad(80), _current_text_dir != TD_RTL).Shrink(ScaleGUITrad(2), ScaleGUITrad(4));
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_MNT_TITLE:
				DrawMobileTitle(r, GetString(STR_MOBILE_TRAIN_NEW_TITLE, GetRailTypeInfo(this->railtype)->strings.name));
				break;

			case WID_MNT_STRIP: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
				const Train *t = this->GetTarget();
				if (t == nullptr) {
					DrawString(tr.left, tr.right, CentreBounds(tr.top, tr.bottom, GetCharacterHeight(FontSize::Normal)), STR_MOBILE_TRAIN_BUY_ENGINE_FIRST, TextColour::Black);
					this->cells.clear();
				} else {
					DrawTrainStrip(t, tr, true, &this->cells);
				}
				break;
			}

			case WID_MNT_SUMMARY:
				if (const Train *t = this->GetTarget(); t != nullptr) {
					DrawString(r.Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0), GetTrainSummary(t), TextColour::Black);
				}
				break;

			case WID_MNT_LIST: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
				int row = ScaleGUITrad(MOBILE_ROW);
				bool rtl = _current_text_dir == TD_RTL;
				auto [first, last] = this->vscroll->GetVisibleRangeIterators(this->engines);
				for (auto it = first; it != last; ++it) {
					const Engine *e = Engine::Get(*it);
					Rect rr = tr.WithHeight(row);
					Rect buy = GetBuyButton(rr);
					DrawFrameRect(buy, Colours::Green, {});
					DrawString(buy.left, buy.right, CentreBounds(buy.top, buy.bottom, GetCharacterHeight(FontSize::Normal)), GetString(STR_MOBILE_TRAIN_BUY, e->GetCost()), TextColour::White, AlignmentH::Centre);

					int sprite_width = ScaleGUITrad(70);
					Rect sprite = rr.WithWidth(sprite_width, rtl);
					DrawTrainEngine(sprite.left, sprite.right, CentreBounds(sprite.left, sprite.right, 0), CentreBounds(rr.top, rr.bottom, 0), e->index, GetEnginePalette(e->index, _local_company), EngineImageType::Purchase);

					Rect text = rr.Indent(sprite_width, rtl).Indent(buy.Width() + WidgetDimensions::scaled.hsep_wide, !rtl);
					int line = GetCharacterHeight(FontSize::Normal);
					int y = CentreBounds(rr.top, rr.bottom, 2 * line);
					DrawString(text.left, text.right, y, GetString(STR_ENGINE_NAME, e->index), TextColour::Black);
					std::string key;
					if (this->wagons) {
						key = IsValidCargoType(e->GetDefaultCargoType()) ? GetString(STR_JUST_CARGO, e->GetDefaultCargoType(), e->GetDisplayDefaultCapacity()) : std::string{};
					} else {
						key = GetString(STR_MOBILE_TRAIN_ENGINE_LINE, PackVelocity(e->GetDisplayMaxSpeed(), VehicleType::Train), e->GetPower());
					}
					DrawString(text.left, text.right, y + line, key, TextColour::Black);
					tr.top += row;
				}
				break;
			}
		}
	}

	std::string GetMobileDropDownTitle(WidgetID) const override
	{
		return GetVehicleCellMenuTitle(this->cell_vehicle);
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		if (widget != WID_MNT_STRIP) return;
		const Vehicle *v = Vehicle::GetIfValid(this->cell_vehicle);
		if (v == nullptr) return;
		switch (index) {
			case VCM_SELL: SellTrainVehicle(this, v->index); break;
			case VCM_REFIT: ShowVehicleRefitWindow(v, INVALID_VEH_ORDER_ID, this, false); break;
			case VCM_DETAILS: ShowMobileTrainWindow(v->First(), false, true); break;
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		TileIndex tile = static_cast<TileIndex>(this->window_number);
		switch (widget) {
			case WID_MNT_CLOSE: this->Close(); break;
			case WID_MNT_BACK: ShowMobileDepotWindow(tile, VehicleType::Train); break;
			case WID_MNT_TAB_ENGINES:
			case WID_MNT_TAB_WAGONS:
				this->wagons = widget == WID_MNT_TAB_WAGONS;
				this->vscroll->SetPosition(0);
				this->BuildList();
				this->UpdateButtons();
				this->SetDirty();
				break;

			case WID_MNT_STRIP:
				this->cell_vehicle = FindVehicleCell(this->cells, pt);
				if (this->cell_vehicle != VehicleID::Invalid()) ShowVehicleCellMenu(this, widget, this->cell_vehicle);
				break;

			case WID_MNT_LIST: {
				auto it = this->vscroll->GetScrolledItemFromWidget(this->engines, pt.y, this, WID_MNT_LIST, WidgetDimensions::scaled.framerect.top, ScaleGUITrad(MOBILE_ROW));
				if (it == this->engines.end()) break;
				/* Only the "+ £x" button buys; the rest of the row is for reading. */
				const NWidgetBase *list = this->GetWidget<NWidgetBase>(WID_MNT_LIST);
				Rect rr = list->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect);
				if (!GetBuyButton(rr).WithY(0, INT32_MAX / 2).Contains({pt.x, 1})) break;
				const Engine *e = Engine::Get(*it);
				Command<Commands::BuildVehicle>::Post(GetCmdBuildVehMsg(VehicleType::Train), CcMobileBuildTrainPart, tile, e->index, true, e->GetDefaultCargoType(), ClientID::Invalid);
				break;
			}

			case WID_MNT_NEXT:
				if (const Train *t = this->GetTarget(); t != nullptr) {
					ShowMobileTrainWindow(t, true);
				}
				break;
		}
	}

	const IntervalTimer<TimerWindow> refresh = {std::chrono::milliseconds(500), [this](auto) {
		this->UpdateButtons();
		this->SetWidgetDirty(WID_MNT_STRIP);
		this->SetWidgetDirty(WID_MNT_SUMMARY);
		this->SetWidgetDirty(WID_MNT_SPENT);
		this->SetWidgetDirty(WID_MNT_NEXT);
		this->SetWidgetDirty(WID_MNT_TAB_WAGONS);
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_new_train_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MNT_BACK), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_BACK, STR_MOBILE_BACK_TOOLTIP),
		NWidget(WWT_PANEL, Colours::Grey, WID_MNT_TITLE), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), EndContainer(),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MNT_CLOSE), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_CROSS, STR_TOOLTIP_CLOSE_WINDOW),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(WWT_EMPTY, Colours::Invalid, WID_MNT_STRIP), SetFill(1, 0), SetResize(1, 0),
		NWidget(WWT_EMPTY, Colours::Invalid, WID_MNT_SUMMARY), SetFill(1, 0), SetResize(1, 0), SetPadding(0, 0, 4, 0),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_MNT_TAB_ENGINES), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_TRAIN_TAB_ENGINES),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_MNT_TAB_WAGONS), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_TRAIN_TAB_WAGONS),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_MNT_LIST), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_MNT_SCROLL), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_MNT_SCROLL),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_HORIZONTAL), SetPadding(4), SetPIP(0, 4, 0),
			NWidget(WWT_TEXT, Colours::Invalid, WID_MNT_SPENT), SetFill(1, 1), SetResize(1, 0), SetPadding(0, MOBILE_TEXT_INSET, 0, MOBILE_TEXT_INSET),
			NWidget(WWT_PUSHTXTBTN, Colours::Green, WID_MNT_NEXT), SetMinimalSize(130, 40), SetStringTip(STR_MOBILE_TRAIN_NEXT_ORDERS),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _mobile_new_train_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::BuildVehicle, WindowClass::None,
	{},
	_nested_mobile_new_train_widgets
);

/** Buying a road vehicle, ship or aircraft: one list; "+ £x" buys a complete vehicle and opens its window (round 5 design, 7.1). */
struct MobileNewVehicleWindow : Window {
	VehicleType type; ///< What the depot builds.
	std::vector<EngineID> engines; ///< The list shown.
	Scrollbar *vscroll = nullptr;

	MobileNewVehicleWindow(WindowDesc &desc, TileIndex tile, VehicleType type) : Window(desc), type(type)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_MNT_SCROLL);
		this->FinishInitNested(tile);
		this->owner = _local_company;
		this->BuildList();
	}

	void BuildList()
	{
		this->engines.clear();
		TileIndex tile = static_cast<TileIndex>(this->window_number);
		RoadType roadtype = INVALID_ROADTYPE;
		if (this->type == VehicleType::Road) {
			roadtype = GetRoadTypeRoad(tile);
			if (roadtype == INVALID_ROADTYPE) roadtype = GetRoadTypeTram(tile);
		}
		const Station *st = this->type == VehicleType::Aircraft ? Station::GetByTile(tile) : nullptr;
		for (const Engine *e : Engine::IterateType(this->type)) {
			if (e->IsVariantHidden(_local_company)) continue;
			if (!IsEngineBuildable(e->index, this->type, _local_company)) continue;
			/* As the desktop's purchase list: road vehicles for the depot's road type, aircraft the hangar's airport takes. */
			if (roadtype != INVALID_ROADTYPE && !HasPowerOnRoad(e->VehInfo<RoadVehicleInfo>().roadtype, roadtype)) continue;
			if (st != nullptr && !CanVehicleUseStation(e->index, st)) continue;
			this->engines.push_back(e->index);
		}
		std::ranges::sort(this->engines, [](EngineID a, EngineID b) { return Engine::Get(a)->GetCost() < Engine::Get(b)->GetCost(); });
		this->vscroll->SetCount(this->engines.size());
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->BuildList();
		this->SetDirty();
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_MNT_LIST:
				fill.height = resize.height = 1;
				size.height = 2 * ScaleGUITrad(MOBILE_ROW);
				break;
			case WID_MNT_TITLE:
				size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal));
				break;
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacity(GetRowCapacity(this, WID_MNT_LIST, ScaleGUITrad(MOBILE_ROW)));
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_MNT_TITLE: {
				static const StringID titles[] = {STR_MOBILE_TRAIN_NEW_TITLE, STR_MOBILE_NEW_ROAD_VEHICLE, STR_MOBILE_NEW_SHIP, STR_MOBILE_NEW_AIRCRAFT};
				DrawMobileTitle(r, GetString(titles[to_underlying(this->type)]));
				break;
			}

			case WID_MNT_LIST: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
				if (this->engines.empty()) {
					DrawString(tr.Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0), STR_MOBILE_NEW_VEHICLE_NONE, TextColour::Black);
					break;
				}
				int row = ScaleGUITrad(MOBILE_ROW);
				bool rtl = _current_text_dir == TD_RTL;
				int line = GetCharacterHeight(FontSize::Normal);
				auto [first, last] = this->vscroll->GetVisibleRangeIterators(this->engines);
				for (auto it = first; it != last; ++it) {
					const Engine *e = Engine::Get(*it);
					Rect rr = tr.WithHeight(row);
					Rect buy = MobileNewTrainWindow::GetBuyButton(rr);
					DrawFrameRect(buy, Colours::Green, {});
					DrawString(buy.left, buy.right, CentreBounds(buy.top, buy.bottom, line), GetString(STR_MOBILE_TRAIN_BUY, e->GetCost()), TextColour::White, AlignmentH::Centre);

					int sprite_width = ScaleGUITrad(70);
					Rect sprite = rr.WithWidth(sprite_width, rtl);
					int x = CentreBounds(sprite.left, sprite.right, 0);
					int y = CentreBounds(rr.top, rr.bottom, 0);
					PaletteID pal = GetEnginePalette(e->index, _local_company);
					switch (this->type) {
						case VehicleType::Road: DrawRoadVehEngine(sprite.left, sprite.right, x, y, e->index, pal, EngineImageType::Purchase); break;
						case VehicleType::Ship: DrawShipEngine(sprite.left, sprite.right, x, y, e->index, pal, EngineImageType::Purchase); break;
						case VehicleType::Aircraft: DrawAircraftEngine(sprite.left, sprite.right, x, y, e->index, pal, EngineImageType::Purchase); break;
						default: break;
					}

					Rect text = rr.Indent(sprite_width, rtl).Indent(buy.Width() + WidgetDimensions::scaled.hsep_wide, !rtl);
					int ty = CentreBounds(rr.top, rr.bottom, 2 * line);
					DrawString(text.left, text.right, ty, GetString(STR_ENGINE_NAME, e->index), TextColour::Black);
					std::string cargo = IsValidCargoType(e->GetDefaultCargoType()) ? GetString(STR_JUST_CARGO, e->GetDefaultCargoType(), e->GetDisplayDefaultCapacity()) : std::string{};
					DrawString(text.left, text.right, ty + line, GetString(STR_MOBILE_NEW_VEHICLE_LINE, cargo, PackVelocity(e->GetDisplayMaxSpeed(), this->type)), TextColour::Black);
					tr.top += row;
				}
				break;
			}
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		TileIndex tile = static_cast<TileIndex>(this->window_number);
		switch (widget) {
			case WID_MNT_CLOSE: this->Close(); break;
			case WID_MNT_BACK: ShowMobileDepotWindow(tile, this->type); break;
			case WID_MNT_LIST: {
				auto it = this->vscroll->GetScrolledItemFromWidget(this->engines, pt.y, this, WID_MNT_LIST, WidgetDimensions::scaled.framerect.top, ScaleGUITrad(MOBILE_ROW));
				if (it == this->engines.end()) break;
				/* Only the "+ £x" button buys; the rest of the row is for reading. */
				const NWidgetBase *list = this->GetWidget<NWidgetBase>(WID_MNT_LIST);
				Rect rr = list->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect);
				if (!MobileNewTrainWindow::GetBuyButton(rr).WithY(0, INT32_MAX / 2).Contains({pt.x, 1})) break;
				const Engine *e = Engine::Get(*it);
				_mobile_open_adding_stops = true;
				Command<Commands::BuildVehicle>::Post(GetCmdBuildVehMsg(this->type), CcBuildPrimaryVehicle, tile, e->index, true, e->GetDefaultCargoType(), ClientID::Invalid);
				break;
			}
		}
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_new_vehicle_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MNT_BACK), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_BACK, STR_MOBILE_BACK_TOOLTIP),
		NWidget(WWT_PANEL, Colours::Grey, WID_MNT_TITLE), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), EndContainer(),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MNT_CLOSE), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_CROSS, STR_TOOLTIP_CLOSE_WINDOW),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_MNT_LIST), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_MNT_SCROLL), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_MNT_SCROLL),
	EndContainer(),
};

static WindowDesc _mobile_new_vehicle_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::BuildVehicle, WindowClass::None,
	{},
	_nested_mobile_new_vehicle_widgets
);

/**
 * Show the New train (or road vehicle, ship, aircraft) view for a depot in the sheet slot.
 * @param tile The depot.
 * @param target Train to add vehicles to, or \c VehicleID::Invalid() for a new train.
 */
void ShowMobileNewTrainWindow(TileIndex tile, VehicleID target)
{
	if (target != VehicleID::Invalid()) _mobile_train_target = target;
	CloseWindowById(WindowClass::BuildVehicle, tile);
	VehicleType type = GetDepotVehicleType(tile);
	if (type != VehicleType::Train) {
		MobileShowInSheet(new MobileNewVehicleWindow(_mobile_new_vehicle_desc, tile, type));
		return;
	}
	MobileShowInSheet(new MobileNewTrainWindow(_mobile_new_train_desc, tile));
}

/* --- Refit --- */

/** Changing what a vehicle carries: the cargoes it can take with their capacity, the cost, Refit (round 5 design, 7.3). */
struct MobileRefitWindow : Window {
	VehicleID vehicle; ///< The vehicle (for a train: the wagon or engine it was opened for).
	bool only_this; ///< A train: refit only this vehicle, not the whole train.
	std::vector<CargoType> cargoes; ///< The cargoes it can carry.
	CargoType selected = INVALID_CARGO; ///< The cargo picked.
	Scrollbar *vscroll = nullptr;

	MobileRefitWindow(WindowDesc &desc, const Vehicle *v, bool only_this) : Window(desc), vehicle(v->index), only_this(only_this)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_MRF_SCROLL);
		/* The choice only matters for a train of more than one vehicle. */
		bool train = v->type == VehicleType::Train && (v->First()->Next() != nullptr);
		this->GetWidget<NWidgetStacked>(WID_MRF_SEL_SCOPE)->SetDisplayedPlane(train ? 0 : SZSP_HORIZONTAL);
		this->FinishInitNested(v->index);
		this->owner = v->owner;
		this->BuildList();
	}

	/** @return The vehicle refitted first: the one picked, or the train's front for the whole train. */
	const Vehicle *GetVehicle() const
	{
		const Vehicle *v = Vehicle::GetIfValid(this->vehicle);
		if (v == nullptr) return nullptr;
		return this->only_this ? v : v->First();
	}

	/** @return How many vehicles the command refits: 1 for only this one (with its articulated parts), else all. */
	uint8_t GetNumVehicles() const
	{
		return this->only_this ? 1 : 0;
	}

	void BuildList()
	{
		this->cargoes.clear();
		const Vehicle *v = this->GetVehicle();
		if (v == nullptr) return;
		CargoTypes mask{};
		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			if (u != v && this->only_this && !u->IsArticulatedPart()) break;
			mask.Set(GetUnionOfArticulatedRefitMasks(u->engine_type, true));
		}
		for (const CargoSpec *cs : _sorted_cargo_specs) {
			if (mask.Test(cs->Index())) this->cargoes.push_back(cs->Index());
		}
		this->vscroll->SetCount(this->cargoes.size());
	}

	/**
	 * The capacity a refit to a cargo gives, and what it costs.
	 * @param cargo The cargo.
	 * @param[out] cost The cost.
	 * @return The capacity; 0 when the refit can't be done.
	 */
	uint GetRefitCapacity(CargoType cargo, Money &cost) const
	{
		const Vehicle *v = this->GetVehicle();
		cost = 0;
		if (v == nullptr) return 0;
		auto [result, capacity, mail_capacity, cargo_capacities] = Command<Commands::RefitVehicle>::Do(DoCommandFlag::QueryCost, v->index, cargo, 0, false, this->only_this, this->GetNumVehicles());
		if (result.Failed()) return 0;
		cost = result.GetCost();
		return capacity;
	}

	/**
	 * The current capacity of the refitted vehicles for their cargo.
	 * @param[out] cargo Their cargo.
	 * @return The capacity.
	 */
	uint GetCurrentCapacity(CargoType &cargo) const
	{
		const Vehicle *v = this->GetVehicle();
		cargo = INVALID_CARGO;
		uint capacity = 0;
		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			if (u != v && this->only_this && !u->IsArticulatedPart()) break;
			if (u->cargo_cap == 0) continue;
			if (cargo == INVALID_CARGO) cargo = u->cargo_type;
			if (u->cargo_type == cargo) capacity += u->cargo_cap;
		}
		return capacity;
	}

	/** @return Whether the vehicle is where it can be refitted: stopped in a depot or hangar. */
	bool CanRefitNow() const
	{
		const Vehicle *v = this->GetVehicle();
		return v != nullptr && v->First()->IsStoppedInDepot();
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_MRF_LIST:
				fill.height = resize.height = 1;
				size.height = 3 * ScaleGUITrad(40);
				break;
			case WID_MRF_TITLE:
				size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal));
				break;
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacity(GetRowCapacity(this, WID_MRF_LIST, ScaleGUITrad(40)));
	}

	void OnPaint() override
	{
		bool can = this->CanRefitNow();
		CargoType current;
		this->GetCurrentCapacity(current);
		this->SetWidgetDisabledState(WID_MRF_REFIT, !can || this->selected == INVALID_CARGO || this->selected == current);
		this->GetWidget<NWidgetCore>(WID_MRF_REFIT)->colour = this->IsWidgetDisabled(WID_MRF_REFIT) ? Colours::Grey : Colours::Green;
		this->SetWidgetLoweredState(WID_MRF_WHOLE, !this->only_this);
		this->SetWidgetLoweredState(WID_MRF_ONLY_THIS, this->only_this);
		this->DrawWidgets();
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_MRF_COST) {
			if (!this->CanRefitNow()) return GetString(STR_MOBILE_REFIT_STOP_IN_DEPOT);
			if (this->selected == INVALID_CARGO) return GetString(STR_MOBILE_REFIT_PICK);
			Money cost;
			this->GetRefitCapacity(this->selected, cost);
			return GetString(STR_MOBILE_REFIT_COST, cost);
		}
		return this->Window::GetWidgetString(widget, stringid);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		const Vehicle *v = Vehicle::GetIfValid(this->vehicle);
		if (v == nullptr) return;
		switch (widget) {
			case WID_MRF_TITLE:
				DrawMobileTitle(r, GetString(STR_MOBILE_REFIT_TITLE, this->only_this ? GetString(STR_ENGINE_NAME, v->engine_type) : GetString(STR_MOBILE_TRAIN_NAME, v->First()->index)));
				break;

			case WID_MRF_LIST: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
				bool can = this->CanRefitNow();
				CargoType current;
				uint current_capacity = this->GetCurrentCapacity(current);
				int row = ScaleGUITrad(40);
				int line = GetCharacterHeight(FontSize::Normal);
				bool rtl = _current_text_dir == TD_RTL;
				if (this->cargoes.empty()) {
					DrawString(tr.left, tr.right, tr.top + ScaleGUITrad(8), STR_MOBILE_REFIT_NONE, TextColour::Black);
					break;
				}
				auto [first, last] = this->vscroll->GetVisibleRangeIterators(this->cargoes);
				for (auto it = first; it != last; ++it) {
					CargoType cargo = *it;
					Rect rr = tr.WithHeight(row);
					if (cargo == this->selected) DrawFrameRect(rr.Shrink(-ScaleGUITrad(MOBILE_TEXT_INSET), 0), Colours::Grey, FrameFlag::Lowered);
					TextColour tc = can ? TextColour::Black : TextColour::Grey;
					int y = CentreBounds(rr.top, rr.bottom, line);
					/* The cargo's colour, its name, and the capacity it gives. */
					const CargoSpec *cs = CargoSpec::Get(cargo);
					int sq = line - ScaleGUITrad(2);
					Rect swatch = rr.WithWidth(sq, rtl).WithY(CentreBounds(rr.top, rr.bottom, sq), CentreBounds(rr.top, rr.bottom, sq) + sq - 1);
					GfxFillRect(swatch, PC_BLACK);
					GfxFillRect(swatch.Shrink(WidgetDimensions::scaled.bevel), cs->legend_colour);
					Rect text = rr.Indent(sq + WidgetDimensions::scaled.hsep_wide, rtl);
					std::string capacity;
					if (cargo == current) {
						capacity = GetString(STR_JUST_CARGO, current, current_capacity);
					} else {
						Money cost;
						uint cap = this->GetRefitCapacity(cargo, cost);
						capacity = current == INVALID_CARGO ? GetString(STR_JUST_CARGO, cargo, cap) : GetString(STR_MOBILE_REFIT_CAPACITY, GetString(STR_JUST_CARGO, current, current_capacity), GetString(STR_JUST_CARGO, cargo, cap));
					}
					DrawString(text.left, text.right, y, capacity, tc, AlignmentH::End);
					DrawString(text.left, text.right - GetStringBoundingBox(capacity).width - WidgetDimensions::scaled.hsep_wide, y, cs->name, cargo == current ? TextColour::DarkGreen : tc);
					tr.top += row;
				}
				break;
			}
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		const Vehicle *v = this->GetVehicle();
		if (v == nullptr) return;
		switch (widget) {
			case WID_MRF_BACK:
			case WID_MRF_CLOSE:
				this->Close();
				break;
			case WID_MRF_WHOLE:
			case WID_MRF_ONLY_THIS:
				this->only_this = widget == WID_MRF_ONLY_THIS;
				this->selected = INVALID_CARGO;
				this->BuildList();
				this->SetDirty();
				break;
			case WID_MRF_LIST: {
				if (!this->CanRefitNow()) break;
				auto it = this->vscroll->GetScrolledItemFromWidget(this->cargoes, pt.y, this, WID_MRF_LIST, WidgetDimensions::scaled.framerect.top, ScaleGUITrad(40));
				if (it == this->cargoes.end()) break;
				this->selected = *it;
				this->SetDirty();
				break;
			}
			case WID_MRF_REFIT:
				if (this->selected == INVALID_CARGO) break;
				if (Command<Commands::RefitVehicle>::Post(GetCmdRefitVehMsg(v), v->tile, v->index, this->selected, 0, false, this->only_this, this->GetNumVehicles())) {
					this->selected = INVALID_CARGO;
					this->SetDirty();
				}
				break;
		}
	}

	const IntervalTimer<TimerWindow> refresh = {std::chrono::milliseconds(500), [this](auto) {
		if (Vehicle::GetIfValid(this->vehicle) == nullptr) {
			this->Close();
			return;
		}
		this->SetDirty();
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_refit_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MRF_BACK), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_BACK, STR_MOBILE_BACK_TOOLTIP),
		NWidget(WWT_PANEL, Colours::Grey, WID_MRF_TITLE), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), EndContainer(),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MRF_CLOSE), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_CROSS, STR_TOOLTIP_CLOSE_WINDOW),
	EndContainer(),
	NWidget(NWID_SELECTION, Colours::Invalid, WID_MRF_SEL_SCOPE),
		NWidget(WWT_PANEL, Colours::Grey),
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPadding(4), SetPIP(0, 4, 0),
				NWidget(WWT_TEXTBTN, Colours::Grey, WID_MRF_WHOLE), SetMinimalSize(60, 32), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_REFIT_WHOLE_TRAIN),
				NWidget(WWT_TEXTBTN, Colours::Grey, WID_MRF_ONLY_THIS), SetMinimalSize(60, 32), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_REFIT_ONLY_THIS),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_MRF_LIST), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_MRF_SCROLL), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_MRF_SCROLL),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_HORIZONTAL), SetPadding(4), SetPIP(0, 4, 0),
			NWidget(WWT_TEXT, Colours::Invalid, WID_MRF_COST), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), SetPadding(0, MOBILE_TEXT_INSET, 0, MOBILE_TEXT_INSET), SetTextStyle(TextColour::Black, FontSize::Normal), SetAlignment({AlignmentH::Start, AlignmentV::Middle}),
			NWidget(WWT_PUSHTXTBTN, Colours::Green, WID_MRF_REFIT), SetMinimalSize(100, 40), SetStringTip(STR_MOBILE_REFIT_REFIT),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _mobile_refit_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::VehicleRefit, WindowClass::None,
	{},
	_nested_mobile_refit_widgets
);

/**
 * Show the refit view over the window it was opened from, in the sheet slot.
 * @param v The vehicle (for a train: the wagon or engine tapped; only it is refitted unless the player picks the whole train).
 */
void ShowMobileRefitWindow(const Vehicle *v)
{
	CloseWindowByClass(WindowClass::VehicleRefit);
	bool only_this = v->type == VehicleType::Train && v->First()->Next() != nullptr;
	Window *w = new MobileRefitWindow(_mobile_refit_desc, v, only_this);
	MobileDockOverlayInSheet(w);
}

/* --- Train window --- */

/** The bar along the bottom while adding stops: what to do, and Done. */
struct MobileStopsBarWindow : Window {
	VehicleID vehicle; ///< The train getting stops.
	std::string message; ///< Error of the last tap, if any.

	MobileStopsBarWindow(WindowDesc &desc, VehicleID vehicle) : Window(desc), vehicle(vehicle)
	{
		this->InitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget == WID_MSB_GAP_BOTTOM) size = {0, static_cast<uint>(GetMobileEdgeGaps().bottom)};
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MSB_STATUS) return;
		const Vehicle *v = Vehicle::GetIfValid(this->vehicle);
		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(4), 0);
		int y = CentreBounds(r.top, r.bottom, GetCharacterHeight(FontSize::Normal));
		if (!this->message.empty()) {
			DrawString(tr.left, tr.right, y, GetString(STR_MOBILE_BUILD_STATUS_ERROR, this->message), TextColour::Red);
		} else if (v != nullptr) {
			DrawString(tr.left, tr.right, y, GetString(STR_MOBILE_ORDERS_TAP_STATION, v->GetNumManualOrders() + 1), TextColour::Black);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget == WID_MSB_DONE) MobileStopAddingStops();
	}

	/**
	 * Show the result of the last tap.
	 * @param message Error, or empty.
	 */
	void SetMessage(std::string &&message)
	{
		this->message = std::move(message);
		this->SetDirty();
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_stops_bar_widgets = {
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_HORIZONTAL), SetPadding(4), SetPIP(0, 4, 0),
			NWidget(WWT_INSET, Colours::Grey, WID_MSB_STATUS), SetMinimalSize(120, 40), SetFill(1, 0), SetResize(1, 0), EndContainer(),
			NWidget(WWT_PUSHTXTBTN, Colours::Green, WID_MSB_DONE), SetMinimalSize(70, 40), SetStringTip(STR_MOBILE_ORDERS_DONE, STR_MOBILE_ORDERS_DONE_TOOLTIP),
		EndContainer(),
		NWidget(WWT_EMPTY, Colours::Invalid, WID_MSB_GAP_BOTTOM), SetFill(1, 0), SetResize(1, 0),
	EndContainer(),
};

static WindowDesc _mobile_stops_bar_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileStopsBar, WindowClass::None,
	{WindowDefaultFlag::NoFocus},
	_nested_mobile_stops_bar_widgets
);

/** A train in the sheet slot: state and Start/Stop, its orders or details, and Show on map, Send to depot, Clone. */
struct MobileTrainWindow : Window {
	bool details = false; ///< Showing the Details tab.
	bool adding = false; ///< Adding stops: map taps on stations add orders.
	std::vector<StationID> chips; ///< Suggested stations while adding stops.
	size_t shown_chips = 0; ///< Number of suggestion rows the layout has room for.
	mutable std::vector<std::pair<Rect, VehicleID>> cells; ///< Vehicle cells of the details strip.
	VehicleID cell_vehicle = VehicleID::Invalid(); ///< Vehicle whose cell menu is open.
	Scrollbar *vscroll = nullptr;

	MobileTrainWindow(WindowDesc &desc, const Vehicle *v) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_MTR_ORDERS_SCROLL);
		this->FinishInitNested(v->index);
		this->owner = v->owner;
		this->UpdateState();
	}

	/** @return The vehicle (a train, road vehicle, ship or aircraft; round 5 design, 7.1). */
	const Vehicle *GetTrain() const { return Vehicle::GetIfValid(static_cast<VehicleID>(this->window_number)); }

	void Close(int data = 0) override
	{
		this->SetAdding(false);
		this->Window::Close(data);
	}

	/** Update the planes and buttons to the train's state. */
	void UpdateState()
	{
		const Vehicle *v = this->GetTrain();
		if (v == nullptr) return;
		bool reinit = false;
		auto set_plane = [&](WidgetID widget, int plane) {
			NWidgetStacked *sel = this->GetWidget<NWidgetStacked>(widget);
			if (sel->shown_plane == plane) return;
			sel->SetDisplayedPlane(plane);
			reinit = true;
		};
		bool stopped = v->vehstatus.Test(VehState::Stopped);
		set_plane(WID_MTR_SEL_START, stopped ? 0 : 1);
		set_plane(WID_MTR_SEL_CONTENT, this->details ? 1 : 0);
		set_plane(WID_MTR_SEL_CHIPS, this->adding && !this->details && !this->chips.empty() ? 0 : SZSP_HORIZONTAL);
		/* The suggestions panel is as tall as its rows. */
		if (this->shown_chips != this->chips.size()) {
			this->shown_chips = this->chips.size();
			reinit = true;
		}
		/* Start needs a route; disabled, it's grey rather than a dimmed green that reads as active. */
		bool can_start = v->GetNumManualOrders() >= 2 || !stopped;
		this->SetWidgetDisabledState(WID_MTR_START, !can_start);
		this->GetWidget<NWidgetCore>(WID_MTR_START)->colour = can_start ? Colours::Green : Colours::Grey;
		this->SetWidgetDisabledState(WID_MTR_DEPOT, v->IsInDepot());
		/* Only trains get more vehicles; other vehicles are bought whole. */
		this->SetWidgetDisabledState(WID_MTR_ADD_VEHICLES, v->type != VehicleType::Train || !v->IsStoppedInDepot());
		this->SetWidgetLoweredState(WID_MTR_TAB_ORDERS, !this->details);
		this->SetWidgetLoweredState(WID_MTR_TAB_DETAILS, this->details);
		this->SetWidgetLoweredState(WID_MTR_ADD_STOPS, this->adding);
		this->vscroll->SetCount(v->GetNumOrders());
		if (reinit) {
			this->ReInit();
			RelocateMobileWindows();
		}
		this->SetDirty();
	}

	/**
	 * Can the vehicle stop at a station or waypoint? Ships also go to buoys.
	 * @param st The station or waypoint.
	 * @return \c true when it can be an order of the vehicle.
	 */
	bool CanStopAt(const BaseStation *st) const
	{
		const Vehicle *v = this->GetTrain();
		if (v == nullptr || st == nullptr) return false;
		if (v->type == VehicleType::Ship && Waypoint::IsExpected(st) && st->facilities.Test(StationFacility::Dock)) return true;
		if (!Station::IsExpected(st) || st->owner != _local_company) return false;
		return CanVehicleUseStation(v, Station::From(st));
	}

	/** Suggested stations: the two most recently built stations the vehicle can use, then the one nearest the depot. */
	void UpdateChips()
	{
		this->chips.clear();
		const Vehicle *v = this->GetTrain();
		if (v == nullptr) return;
		std::vector<const Station *> own;
		for (const Station *st : Station::Iterate()) {
			if (this->CanStopAt(st)) own.push_back(st);
		}
		std::ranges::sort(own, [](const Station *a, const Station *b) { return a->build_date > b->build_date; });
		for (const Station *st : own) {
			if (this->chips.size() >= 2) break;
			this->chips.push_back(st->index);
		}
		const Station *nearest = nullptr;
		for (const Station *st : own) {
			if (std::ranges::find(this->chips, st->index) != this->chips.end()) continue;
			if (nearest == nullptr || DistanceManhattan(st->xy, v->tile) < DistanceManhattan(nearest->xy, v->tile)) nearest = st;
		}
		if (nearest != nullptr) this->chips.push_back(nearest->index);
	}

	/**
	 * Start or stop adding stops by tapping stations on the map.
	 * @param adding Whether to add stops.
	 */
	void SetAdding(bool adding)
	{
		if (this->adding == adding) return;
		this->adding = adding;
		if (adding) {
			this->details = false;
			this->UpdateChips();
			SetObjectToPlaceWnd(SPR_CURSOR_MOUSE, PAL_NONE, HT_RECT, this);
			if (FindWindowById(WindowClass::MobileStopsBar, 0) == nullptr) new MobileStopsBarWindow(_mobile_stops_bar_desc, this->GetTrain()->index);
		} else {
			if (_thd.GetCallbackWnd() == this) ResetObjectToPlace();
			CloseWindowById(WindowClass::MobileStopsBar, 0);
		}
		this->UpdateState();
		RelocateMobileWindows();
	}

	void OnPlaceObjectAbort() override
	{
		this->adding = false;
		CloseWindowById(WindowClass::MobileStopsBar, 0);
		this->UpdateState();
		RelocateMobileWindows();
	}

	/**
	 * Add a station as the next stop.
	 * @param st The station.
	 */
	void AddStop(const BaseStation *st)
	{
		const Vehicle *v = this->GetTrain();
		MobileStopsBarWindow *bar = dynamic_cast<MobileStopsBarWindow *>(FindWindowById(WindowClass::MobileStopsBar, 0));
		if (v == nullptr || bar == nullptr) return;
		if (!this->CanStopAt(st)) {
			bar->SetMessage(GetString(STR_MOBILE_ORDERS_NOT_A_STATION));
			return;
		}
		/* The same station twice in a row is no route. */
		VehicleOrderID count = v->GetNumOrders();
		if (count > 0) {
			const Order *last = v->GetOrder(count - 1);
			if (last != nullptr && last->IsType(OT_GOTO_STATION) && last->GetDestination() == st->index) {
				bar->SetMessage(GetString(STR_MOBILE_ORDERS_ALREADY_PREVIOUS));
				return;
			}
		}
		Order order;
		if (Waypoint::IsExpected(st)) {
			order.MakeGoToWaypoint(st->index); // A buoy.
		} else {
			order.MakeGoToStation(st->index);
		}
		if (_settings_client.gui.new_nonstop && (v->type == VehicleType::Train || v->type == VehicleType::Road)) order.SetNonStopType(OrderNonStopFlag::NonStop);
		/* Where in the platform to stop is for trains only; other vehicles' orders must say "far end", as the desktop's do. */
		order.SetStopLocation(v->type == VehicleType::Train ? static_cast<OrderStopLocation>(_settings_client.gui.stop_location) : OrderStopLocation::FarEnd);
		bar->SetMessage({});
		Command<Commands::InsertOrder>::Post(STR_ERROR_CAN_T_INSERT_NEW_ORDER, v->tile, v->index, count, order);
		MarkTileDirtyByTile(st->xy);
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		this->AddStop(IsTileType(tile, TileType::Station) ? BaseStation::GetByTile(tile) : nullptr);
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->UpdateState();
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_MTR_ORDERS:
				fill.height = resize.height = 1;
				size.height = 2 * ScaleGUITrad(MOBILE_ORDER_ROW);
				break;
			case WID_MTR_STRIP:
				size.height = ScaleGUITrad(MOBILE_STRIP_HEIGHT);
				break;
			case WID_MTR_STATUS:
				size.height = std::max<uint>(size.height, 2 * GetCharacterHeight(FontSize::Normal));
				break;
			case WID_MTR_CHIP_FIRST:
				size.height = static_cast<uint>(std::max<size_t>(1, this->chips.size())) * ScaleGUITrad(32);
				break;
			case WID_MTR_TITLE:
				size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal));
				break;
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacity(GetRowCapacity(this, WID_MTR_ORDERS, ScaleGUITrad(MOBILE_ORDER_ROW)));
	}


	/**
	 * Text of an order's flags line: non-stop and loading.
	 * @param o The order.
	 * @return The text.
	 */
	/**
	 * Where an order goes, as its row names it.
	 * @param o The order.
	 * @return The station, waypoint, "Depot", ...
	 */
	static std::string GetOrderName(const Order &o)
	{
		switch (o.GetType()) {
			case OT_GOTO_STATION: return GetString(STR_STATION_NAME, o.GetDestination());
			case OT_GOTO_WAYPOINT: return GetString(STR_WAYPOINT_NAME, o.GetDestination());
			case OT_GOTO_DEPOT: return GetString(STR_MOBILE_ORDERS_DEPOT);
			case OT_CONDITIONAL: return GetString(STR_MOBILE_ORDERS_CONDITIONAL);
			default: return GetString(STR_MOBILE_ORDERS_OTHER);
		}
	}

	std::string GetMobileDropDownTitle(WidgetID widget) const override
	{
		if (widget == WID_MTR_ORDERS) {
			const Vehicle *v = this->GetTrain();
			const Order *o = v == nullptr ? nullptr : v->GetOrder(this->menu_order);
			return o == nullptr ? std::string{} : GetOrderName(*o);
		}
		return GetVehicleCellMenuTitle(this->cell_vehicle);
	}

	static std::string GetOrderFlagsText(const Order &o)
	{
		std::string text = GetString(o.GetNonStopType().Test(OrderNonStopFlag::NonStop) ? STR_MOBILE_ORDERS_NON_STOP : STR_MOBILE_ORDERS_STOPS_EVERYWHERE);
		StringID load = STR_NULL;
		switch (o.GetLoadType()) {
			case OrderLoadType::FullLoad: load = STR_MOBILE_ORDERS_LOAD_FULL_ALL; break;
			case OrderLoadType::FullLoadAny: load = STR_MOBILE_ORDERS_LOAD_FULL_ANY; break;
			case OrderLoadType::NoLoad: load = STR_MOBILE_ORDERS_LOAD_NONE; break;
			default: load = STR_MOBILE_ORDERS_LOAD_NORMAL; break;
		}
		text += GetString(STR_MOBILE_ORDERS_SEPARATOR) + GetString(load);
		switch (o.GetUnloadType()) {
			case OrderUnloadType::Unload: text += GetString(STR_MOBILE_ORDERS_SEPARATOR) + GetString(STR_MOBILE_ORDERS_UNLOAD_ALL); break;
			case OrderUnloadType::Transfer: text += GetString(STR_MOBILE_ORDERS_SEPARATOR) + GetString(STR_MOBILE_ORDERS_TRANSFER); break;
			case OrderUnloadType::NoUnload: text += GetString(STR_MOBILE_ORDERS_SEPARATOR) + GetString(STR_MOBILE_ORDERS_NO_UNLOAD); break;
			default: break;
		}
		return text;
	}

	/**
	 * The ⋯ button of an order row.
	 * @param row The row.
	 * @return The button.
	 */
	static Rect GetOrderMenuButton(const Rect &row)
	{
		return row.WithWidth(ScaleGUITrad(40), _current_text_dir != TD_RTL);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		const Vehicle *v = this->GetTrain();
		if (v == nullptr) return;
		bool rtl = _current_text_dir == TD_RTL;
		int line = GetCharacterHeight(FontSize::Normal);

		/* Suggested stations: full-width rows of 32, "+ <station>". */
		if (widget == WID_MTR_CHIP_FIRST) {
			Rect tr = r.Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
			int row = ScaleGUITrad(32);
			for (StationID st : this->chips) {
				Rect rr = tr.WithHeight(row);
				DrawString(rr.left, rr.right, CentreBounds(rr.top, rr.bottom, line), GetString(STR_MOBILE_ORDERS_CHIP, st), TextColour::Black);
				GfxFillRect(r.left, rr.bottom, r.right, rr.bottom, GetColourGradient(Colours::Grey, Shade::Dark));
				tr.top += row;
			}
			return;
		}

		switch (widget) {
			case WID_MTR_TITLE:
				DrawMobileTitle(r, GetString(STR_MOBILE_TRAIN_NAME, v->index));
				break;

			case WID_MTR_STATUS: {
				Rect tr = r.Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
				int y = CentreBounds(r.top, r.bottom, 2 * line);
				bool stopped = v->vehstatus.Test(VehState::Stopped);
				std::string state;
				std::string detail;
				if (stopped) {
					state = GetString(STR_MOBILE_TRAIN_STOPPED);
					uint orders = v->GetNumManualOrders();
					detail = orders == 0 ? GetString(STR_MOBILE_TRAIN_ADD_TWO_STOPS) : (orders == 1 ? GetString(STR_MOBILE_TRAIN_ADD_ONE_STOP) : GetString(STR_MOBILE_TRAIN_READY));
				} else {
					if (v->current_order.IsType(OT_GOTO_STATION)) {
						state = GetString(STR_MOBILE_TRAIN_HEADING_TO, v->current_order.GetDestination());
					} else if (v->current_order.IsType(OT_LOADING)) {
						state = GetString(STR_VEHICLE_STATUS_LOADING_UNLOADING);
					} else {
						state = GetString(STR_MOBILE_TRAIN_RUNNING);
					}
					detail = GetString(STR_MOBILE_TRAIN_SPEED_PROFIT, PackVelocity(v->GetDisplaySpeed(), v->type), v->GetDisplayProfitThisYear());
				}
				DrawString(tr.left, tr.right, y, state, stopped ? TextColour::Red : TextColour::DarkGreen);
				DrawString(tr.left, tr.right, y + line, detail, TextColour::Black);
				break;
			}

			case WID_MTR_ORDERS: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
				if (v->GetNumOrders() == 0) {
					DrawString(tr.Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0), STR_MOBILE_ORDERS_EMPTY, TextColour::Black);
					break;
				}
				int row = ScaleGUITrad(MOBILE_ORDER_ROW);
				int pos = this->vscroll->GetPosition();
				int index = 0;
				for (const Order &o : v->Orders()) {
					if (index < pos) {
						index++;
						continue;
					}
					if (tr.top + row - 1 > tr.bottom) break;
					Rect rr = tr.WithHeight(row);
					bool current = index == v->cur_implicit_order_index || index == v->cur_real_order_index;
					if (current) DrawFrameRect(rr, Colours::Grey, FrameFlag::Lowered);
					/* Dragging: the picked-up order is lit, a line marks where it would go. */
					if (index == this->drag_order) GfxFillRect(rr, GetColourGradient(Colours::Grey, Shade::Lightest));
					if (this->drag_target >= 0 && index == this->drag_target && this->drag_target != this->drag_order) {
						int ly = this->drag_target < this->drag_order ? rr.top : rr.bottom - ScaleGUITrad(2);
						GfxFillRect(rr.left, ly, rr.right, ly + ScaleGUITrad(2), PC_BLACK);
					}
					Rect menu = GetOrderMenuButton(rr);
					DrawString(menu.left, menu.right, CentreBounds(rr.top, rr.bottom, line), STR_MOBILE_ORDERS_MENU, TextColour::Black, AlignmentH::Centre);
					Rect num = rr.WithWidth(ScaleGUITrad(32), rtl);
					DrawString(num.left, num.right, CentreBounds(rr.top, rr.bottom, line), current ? GetString(STR_JUST_RIGHT_ARROW) : GetString(STR_JUST_INT, index + 1), TextColour::Black, AlignmentH::Centre);
					Rect text = rr.Indent(num.Width(), rtl).Indent(menu.Width(), !rtl);
					int y = CentreBounds(rr.top, rr.bottom, 2 * line);
					DrawString(text.left, text.right, y, GetOrderName(o), TextColour::Black);
					if (o.IsType(OT_GOTO_STATION)) DrawString(text.left, text.right, y + line, GetOrderFlagsText(o), TextColour::Black);
					tr.top += row;
					index++;
				}
				break;
			}

			case WID_MTR_STRIP:
				DrawVehicleStrip(v, r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0), true, &this->cells);
				break;

			case WID_MTR_DETAILS: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
				tr.top = DrawStringMultiLine(tr, GetVehicleSummary(v), TextColour::Black);
				tr.top = DrawStringMultiLine(tr, GetString(STR_MOBILE_TRAIN_DETAILS_AGE, v->age, v->max_age), TextColour::Black);
				tr.top = DrawStringMultiLine(tr, GetString(STR_MOBILE_TRAIN_DETAILS_RELIABILITY, ToPercent16(v->reliability)), TextColour::Black);
				tr.top = DrawStringMultiLine(tr, GetString(STR_MOBILE_TRAIN_DETAILS_RUNNING_COST, v->GetDisplayRunningCost()), TextColour::Black);
				DrawStringMultiLine(tr, GetString(STR_MOBILE_TRAIN_DETAILS_PROFIT, v->GetDisplayProfitThisYear(), v->GetDisplayProfitLastYear()), TextColour::Black);
				break;
			}
		}
	}

	/**
	 * Open the ⋯ menu of an order: the touch dropdown with its options.
	 * @param index The order.
	 */
	void ShowOrderMenu(VehicleOrderID index)
	{
		const Vehicle *v = this->GetTrain();
		const Order *o = v == nullptr ? nullptr : v->GetOrder(index);
		if (o == nullptr) return;
		this->menu_order = index;
		DropDownList list;
		if (o->IsType(OT_GOTO_STATION)) {
			bool non_stop = o->GetNonStopType().Test(OrderNonStopFlag::NonStop);
			list.push_back(MakeDropDownListStringItem(non_stop ? STR_MOBILE_ORDERS_MENU_STOP_EVERYWHERE : STR_MOBILE_ORDERS_MENU_NON_STOP, 0));
			list.push_back(MakeDropDownListStringItem(STR_MOBILE_ORDERS_MENU_LOAD_NORMAL, 1, o->GetLoadType() == OrderLoadType::LoadIfPossible));
			list.push_back(MakeDropDownListStringItem(STR_MOBILE_ORDERS_MENU_LOAD_FULL_ANY, 2, o->GetLoadType() == OrderLoadType::FullLoadAny));
			list.push_back(MakeDropDownListStringItem(STR_MOBILE_ORDERS_MENU_LOAD_FULL_ALL, 3, o->GetLoadType() == OrderLoadType::FullLoad));
			list.push_back(MakeDropDownListStringItem(STR_MOBILE_ORDERS_MENU_LOAD_NONE, 4, o->GetLoadType() == OrderLoadType::NoLoad));
			list.push_back(MakeDropDownListStringItem(STR_MOBILE_ORDERS_MENU_UNLOAD_NORMAL, 5, o->GetUnloadType() == OrderUnloadType::UnloadIfPossible));
			list.push_back(MakeDropDownListStringItem(STR_MOBILE_ORDERS_MENU_UNLOAD_ALL, 6, o->GetUnloadType() == OrderUnloadType::Unload));
			list.push_back(MakeDropDownListStringItem(STR_MOBILE_ORDERS_MENU_TRANSFER, 7, o->GetUnloadType() == OrderUnloadType::Transfer));
			list.push_back(MakeDropDownListStringItem(STR_MOBILE_ORDERS_MENU_NO_UNLOAD, 8, o->GetUnloadType() == OrderUnloadType::NoUnload));
		}
		list.push_back(MakeDropDownListStringItem(STR_MOBILE_ORDERS_MENU_SKIP, 9));
		list.push_back(MakeDropDownListStringItem(STR_MOBILE_ORDERS_MENU_DELETE, 10));
		ShowDropDownList(this, std::move(list), -1, WID_MTR_ORDERS);
	}

	VehicleOrderID menu_order = 0; ///< Order whose ⋯ menu is open.
	int drag_order = -1; ///< Order being moved by a long-press drag; -1 for none.
	int drag_target = -1; ///< Where it would go.

	/**
	 * The order row at a point of the window.
	 * @param y Window y.
	 * @return The order index, clamped to the list; -1 when there are no orders.
	 */
	int GetOrderRowAt(int y) const
	{
		const Vehicle *v = this->GetTrain();
		if (v == nullptr || v->GetNumOrders() == 0) return -1;
		int row = (y - this->GetWidget<NWidgetBase>(WID_MTR_ORDERS)->pos_y - WidgetDimensions::scaled.framerect.top) / ScaleGUITrad(MOBILE_ORDER_ROW);
		return Clamp(this->vscroll->GetPosition() + row, 0, v->GetNumOrders() - 1);
	}

	bool OnLongPress(Point pt, WidgetID widget) override
	{
		/* Long press an order to pick it up, drag it up or down, lift to drop it (round 4 design, section 4.5). */
		if (widget != WID_MTR_ORDERS || this->details) return false;
		this->drag_order = this->drag_target = this->GetOrderRowAt(pt.y);
		if (this->drag_order < 0) return false;
		this->SetWidgetDirty(WID_MTR_ORDERS);
		return true;
	}

	void OnLongPressMove(Point pt) override
	{
		int target = this->GetOrderRowAt(pt.y);
		if (target != this->drag_target) {
			this->drag_target = target;
			this->SetWidgetDirty(WID_MTR_ORDERS);
		}
	}

	void OnLongPressEnd(Point pt, bool cancelled) override
	{
		const Vehicle *v = this->GetTrain();
		if (!cancelled && v != nullptr && this->drag_order >= 0) {
			int target = this->GetOrderRowAt(pt.y);
			if (target >= 0 && target != this->drag_order) {
				Command<Commands::MoveOrder>::Post(STR_ERROR_CAN_T_MOVE_THIS_ORDER, v->tile, v->index, this->drag_order, target);
			}
		}
		this->drag_order = this->drag_target = -1;
		this->SetWidgetDirty(WID_MTR_ORDERS);
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		if (widget == WID_MTR_STRIP) {
			const Vehicle *cv = Vehicle::GetIfValid(this->cell_vehicle);
			if (cv == nullptr) return;
			switch (index) {
				case VCM_SELL: SellTrainVehicle(this, cv->index); break;
				case VCM_REFIT: ShowVehicleRefitWindow(cv, INVALID_VEH_ORDER_ID, this, false); break;
				case VCM_DETAILS: this->details = true; this->UpdateState(); break;
			}
			return;
		}
		if (widget != WID_MTR_ORDERS) return;
		const Vehicle *v = this->GetTrain();
		const Order *o = v == nullptr ? nullptr : v->GetOrder(this->menu_order);
		if (o == nullptr) return;
		auto modify = [&](ModifyOrderFlags mof, uint16_t value) {
			Command<Commands::ModifyOrder>::Post(STR_ERROR_CAN_T_MODIFY_THIS_ORDER, v->tile, v->index, this->menu_order, mof, value);
		};
		switch (index) {
			case 0: {
				OrderNonStopFlags flags = o->GetNonStopType();
				flags.Flip(OrderNonStopFlag::NonStop);
				modify(MOF_NON_STOP, flags.base());
				break;
			}
			case 1: modify(MOF_LOAD, to_underlying(OrderLoadType::LoadIfPossible)); break;
			case 2: modify(MOF_LOAD, to_underlying(OrderLoadType::FullLoadAny)); break;
			case 3: modify(MOF_LOAD, to_underlying(OrderLoadType::FullLoad)); break;
			case 4: modify(MOF_LOAD, to_underlying(OrderLoadType::NoLoad)); break;
			case 5: modify(MOF_UNLOAD, to_underlying(OrderUnloadType::UnloadIfPossible)); break;
			case 6: modify(MOF_UNLOAD, to_underlying(OrderUnloadType::Unload)); break;
			case 7: modify(MOF_UNLOAD, to_underlying(OrderUnloadType::Transfer)); break;
			case 8: modify(MOF_UNLOAD, to_underlying(OrderUnloadType::NoUnload)); break;
			case 9: Command<Commands::SkipToOrder>::Post(STR_ERROR_CAN_T_SKIP_TO_ORDER, v->tile, v->index, this->menu_order); break;
			case 10: Command<Commands::DeleteOrder>::Post(STR_ERROR_CAN_T_DELETE_THIS_ORDER, v->tile, v->index, this->menu_order); break;
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		const Vehicle *v = this->GetTrain();
		if (v == nullptr) return;
		switch (widget) {
			case WID_MTR_CLOSE: this->Close(); break;
			case WID_MTR_RENAME:
				ShowQueryString(GetString(STR_VEHICLE_NAME, v->index), STR_QUERY_RENAME_TRAIN_CAPTION + to_underlying(v->type), MAX_LENGTH_VEHICLE_NAME_CHARS, this, CS_ALPHANUMERAL, {QueryStringFlag::EnableDefault, QueryStringFlag::LengthIsInChars});
				break;
			case WID_MTR_START:
			case WID_MTR_STOP:
				StartStopVehicle(v, false);
				if (widget == WID_MTR_START) this->SetAdding(false);
				break;
			case WID_MTR_TAB_ORDERS:
			case WID_MTR_TAB_DETAILS:
				this->details = widget == WID_MTR_TAB_DETAILS;
				if (this->details) this->SetAdding(false);
				this->UpdateState();
				break;
			case WID_MTR_ADD_STOPS:
				this->SetAdding(!this->adding);
				break;
			case WID_MTR_ADD_VEHICLES:
				ShowMobileNewTrainWindow(v->tile, v->index);
				break;
			case WID_MTR_SHOW_ON_MAP:
				ScrollMainWindowTo(v->x_pos, v->y_pos, v->z_pos);
				break;
			case WID_MTR_DEPOT:
				Command<Commands::SendVehicleToDepot>::Post(GetCmdSendToDepotMsg(v), v->index, {}, {});
				break;
			case WID_MTR_CLONE:
				if (v->IsInDepot()) {
					Command<Commands::CloneVehicle>::Post(GetCmdBuildVehMsg(v), CcCloneVehicle, v->tile, v->index, false);
				} else {
					ShowErrorMessage(GetEncodedString(GetCmdBuildVehMsg(v)), GetEncodedString(STR_MOBILE_TRAIN_CLONE_IN_DEPOT), WarningLevel::Info);
				}
				break;
			case WID_MTR_STRIP:
				this->cell_vehicle = FindVehicleCell(this->cells, pt);
				if (this->cell_vehicle != VehicleID::Invalid()) ShowVehicleCellMenu(this, widget, this->cell_vehicle);
				break;
			case WID_MTR_ORDERS: {
				int row = (pt.y - this->GetWidget<NWidgetBase>(WID_MTR_ORDERS)->pos_y - WidgetDimensions::scaled.framerect.top) / ScaleGUITrad(MOBILE_ORDER_ROW);
				VehicleOrderID index = static_cast<VehicleOrderID>(this->vscroll->GetPosition() + std::max(0, row));
				if (index < v->GetNumOrders()) this->ShowOrderMenu(index);
				break;
			}
			default:
				if (widget == WID_MTR_CHIP_FIRST) {
					size_t i = (pt.y - this->GetWidget<NWidgetBase>(WID_MTR_CHIP_FIRST)->pos_y) / ScaleGUITrad(32);
					if (i < this->chips.size()) this->AddStop(Station::GetIfValid(this->chips[i]));
				}
				break;
		}
	}

	void OnQueryTextFinished(std::optional<std::string> str) override
	{
		if (!str.has_value()) return;
		const Vehicle *v = this->GetTrain();
		Command<Commands::RenameVehicle>::Post(v != nullptr ? STR_ERROR_CAN_T_RENAME_TRAIN + to_underlying(v->type) : STR_ERROR_CAN_T_RENAME_TRAIN, static_cast<VehicleID>(this->window_number), *str);
	}

	const IntervalTimer<TimerWindow> refresh = {std::chrono::milliseconds(250), [this](auto) {
		if (this->GetTrain() == nullptr) {
			this->Close();
			return;
		}
		this->UpdateState();
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_train_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_MTR_TITLE), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), EndContainer(),
		NWidget(WWT_PUSHIMGBTN, Colours::Grey, WID_MTR_RENAME), SetMinimalSize(40, 40), SetSpriteTip(SPR_RENAME, STR_VEHICLE_DETAILS_TRAIN_RENAME),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MTR_CLOSE), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_CROSS, STR_TOOLTIP_CLOSE_WINDOW),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_HORIZONTAL), SetPadding(4), SetPIP(0, 4, 0),
			NWidget(WWT_EMPTY, Colours::Invalid, WID_MTR_STATUS), SetFill(1, 1), SetResize(1, 0),
			NWidget(NWID_SELECTION, Colours::Invalid, WID_MTR_SEL_START),
				NWidget(WWT_PUSHTXTBTN, Colours::Green, WID_MTR_START), SetMinimalSize(90, 40), SetStringTip(STR_MOBILE_TRAIN_START, STR_VEHICLE_VIEW_TRAIN_STATUS_START_STOP_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, Colours::Red, WID_MTR_STOP), SetMinimalSize(90, 40), SetStringTip(STR_MOBILE_TRAIN_STOP, STR_VEHICLE_VIEW_TRAIN_STATUS_START_STOP_TOOLTIP),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_MTR_TAB_ORDERS), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_TRAIN_TAB_ORDERS),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_MTR_TAB_DETAILS), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_TRAIN_TAB_DETAILS),
	EndContainer(),
	NWidget(NWID_SELECTION, Colours::Invalid, WID_MTR_SEL_CONTENT),
		/* Orders. */
		NWidget(NWID_VERTICAL),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PANEL, Colours::Grey, WID_MTR_ORDERS), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_MTR_ORDERS_SCROLL), EndContainer(),
				NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_MTR_ORDERS_SCROLL),
			EndContainer(),
			NWidget(NWID_SELECTION, Colours::Invalid, WID_MTR_SEL_CHIPS),
				NWidget(WWT_PANEL, Colours::Grey, WID_MTR_CHIP_FIRST), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_ORDERS_CHIP_TOOLTIP), EndContainer(),
			EndContainer(),
			NWidget(WWT_TEXTBTN, Colours::Grey, WID_MTR_ADD_STOPS), SetMinimalSize(0, 32), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_ORDERS_ADD_STOPS, STR_MOBILE_ORDERS_ADD_STOPS_TOOLTIP),
		EndContainer(),
		/* Details. */
		NWidget(NWID_VERTICAL),
			NWidget(WWT_PANEL, Colours::Grey),
				NWidget(WWT_EMPTY, Colours::Invalid, WID_MTR_STRIP), SetFill(1, 0), SetResize(1, 0),
			EndContainer(),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MTR_ADD_VEHICLES), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_TRAIN_ADD_VEHICLES, STR_MOBILE_TRAIN_ADD_VEHICLES_TOOLTIP),
			NWidget(WWT_PANEL, Colours::Grey, WID_MTR_DETAILS), SetFill(1, 1), SetResize(1, 1), EndContainer(),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MTR_SHOW_ON_MAP), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_SHOW_ON_MAP, STR_VEHICLE_VIEW_TRAIN_CENTER_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MTR_DEPOT), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_TRAIN_SEND_TO_DEPOT, STR_VEHICLE_VIEW_TRAIN_SEND_TO_DEPOT_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MTR_CLONE), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_TRAIN_CLONE, STR_VEHICLE_VIEW_CLONE_TRAIN_INFO),
	EndContainer(),
};

static WindowDesc _mobile_train_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::VehicleView, WindowClass::None,
	{},
	_nested_mobile_train_widgets
);

/**
 * Show a train's window in the sheet slot.
 * @param v The train.
 * @param add_stops Start adding stops right away (from "Next: orders").
 * @param show_details Show the Details tab.
 */
void ShowMobileTrainWindow(const Vehicle *v, bool add_stops, bool show_details)
{
	if (v == nullptr) return;
	v = v->First();
	/* Just bought in the buy view: start with adding stops. */
	if (_mobile_open_adding_stops && v->type != VehicleType::Train && v->IsStoppedInDepot()) add_stops = true;
	_mobile_open_adding_stops = false;
	Window *w = BringWindowToFrontById(WindowClass::VehicleView, v->index);
	if (w == nullptr) w = new MobileTrainWindow(_mobile_train_desc, v);
	MobileShowInfoWindow(w);
	if (add_stops) static_cast<MobileTrainWindow *>(w)->SetAdding(true);
	if (show_details) {
		MobileTrainWindow *tw = static_cast<MobileTrainWindow *>(w);
		tw->details = true;
		tw->UpdateState();
	}
}

/** Stop adding stops (Done in the stops bar). */
void MobileStopAddingStops()
{
	if (Window *bar = FindWindowById(WindowClass::MobileStopsBar, 0); bar != nullptr) {
		VehicleID id = static_cast<MobileStopsBarWindow *>(bar)->vehicle;
		if (MobileTrainWindow *w = dynamic_cast<MobileTrainWindow *>(FindWindowById(WindowClass::VehicleView, id)); w != nullptr) {
			w->SetAdding(false);
			return;
		}
		bar->Close();
	}
}

/**
 * A tap on the main map while adding stops: a station sign adds that station (a station tile is handled by the
 * placement mode). Called before the placement mode gets the tap.
 * @param st The station whose sign was tapped, or \c nullptr.
 * @return \c true when the tap was used.
 */
bool MobileVehicleOnStationSignClick(const BaseStation *st)
{
	if (st == nullptr) return false;
	MobileTrainWindow *w = dynamic_cast<MobileTrainWindow *>(_thd.GetCallbackWnd());
	if (w == nullptr || !w->adding) return false;
	w->AddStop(st);
	return true;
}

/* --- Train list --- */

/** The company's trains, or those calling at a station: name and state, profit, and a row opens the train. */
struct MobileTrainListWindow : Window {
	StationID station; ///< Only trains calling here, or invalid for all.
	VehicleType type; ///< The vehicles listed.
	std::vector<const Vehicle *> trains; ///< The vehicles listed.
	int sort = 0; ///< Index into #sort_names.
	Scrollbar *vscroll = nullptr;

	static inline const StringID sort_names[] = {STR_SORT_BY_NAME, STR_SORT_BY_PROFIT_THIS_YEAR, STR_SORT_BY_PROFIT_LAST_YEAR, STR_SORT_BY_AGE, STR_SORT_BY_RELIABILITY};

	MobileTrainListWindow(WindowDesc &desc, WindowNumber number, StationID station, VehicleType type) : Window(desc), station(station), type(type)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_MTL_SCROLL);
		/* In the Vehicles hub the hub's tabs are the title; filtered to a station it has its own. */
		this->GetWidget<NWidgetStacked>(WID_MTL_SEL_TITLE)->SetDisplayedPlane(station == StationID::Invalid() ? SZSP_HORIZONTAL : 0);
		this->FinishInitNested(number);
		this->owner = _local_company;
		this->BuildList();
	}

	/**
	 * Does a train call at the station?
	 * @param t The train.
	 * @return \c true when one of its orders goes there.
	 */
	bool CallsAtStation(const Vehicle *t) const
	{
		for (const Order &o : t->Orders()) {
			if ((o.IsType(OT_GOTO_STATION) || o.IsType(OT_IMPLICIT)) && o.GetDestination() == this->station) return true;
		}
		return false;
	}

	void BuildList()
	{
		this->trains.clear();
		for (const Vehicle *t : Vehicle::Iterate()) {
			if (t->type != this->type || !t->IsPrimaryVehicle() || t->owner != _local_company) continue;
			if (this->station != StationID::Invalid() && !this->CallsAtStation(t)) continue;
			this->trains.push_back(t);
		}
		switch (this->sort) {
			case 0: std::ranges::sort(this->trains, [](const Vehicle *a, const Vehicle *b) { return a->unitnumber < b->unitnumber; }); break;
			case 1: std::ranges::sort(this->trains, [](const Vehicle *a, const Vehicle *b) { return a->GetDisplayProfitThisYear() > b->GetDisplayProfitThisYear(); }); break;
			case 2: std::ranges::sort(this->trains, [](const Vehicle *a, const Vehicle *b) { return a->GetDisplayProfitLastYear() > b->GetDisplayProfitLastYear(); }); break;
			case 3: std::ranges::sort(this->trains, [](const Vehicle *a, const Vehicle *b) { return a->age > b->age; }); break;
			case 4: std::ranges::sort(this->trains, [](const Vehicle *a, const Vehicle *b) { return a->reliability > b->reliability; }); break;
		}
		this->vscroll->SetCount(this->trains.size());
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_MTL_LIST:
				fill.height = resize.height = 1;
				size.height = 2 * ScaleGUITrad(MOBILE_ROW);
				break;
			case WID_MTL_TITLE:
			case WID_MTL_TOTAL:
				size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal));
				break;
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacity(GetRowCapacity(this, WID_MTL_LIST, ScaleGUITrad(MOBILE_ROW)));
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_MTL_SORT: return GetString(sort_names[this->sort]);
			case WID_MTL_TOTAL: {
				Money total = 0;
				for (const Vehicle *t : this->trains) total += t->GetDisplayProfitThisYear();
				return GetString(STR_MOBILE_TRAIN_LIST_TOTAL, total);
			}
			default: return this->Window::GetWidgetString(widget, stringid);
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget == WID_MTL_TITLE) {
			DrawMobileTitle(r, GetString(STR_MOBILE_TRAIN_LIST_AT_STATION, this->station));
			return;
		}
		if (widget != WID_MTL_LIST) return;

		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
		if (this->trains.empty()) {
			DrawString(tr.Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0), STR_MOBILE_TRAIN_LIST_EMPTY, TextColour::Black);
			return;
		}
		bool rtl = _current_text_dir == TD_RTL;
		int row = ScaleGUITrad(MOBILE_ROW);
		int line = GetCharacterHeight(FontSize::Normal);
		auto [first, last] = this->vscroll->GetVisibleRangeIterators(this->trains);
		for (auto it = first; it != last; ++it) {
			const Vehicle *t = *it;
			Rect rr = tr.WithHeight(row);
			Rect arrow = rr.WithWidth(ScaleGUITrad(32), !rtl);
			DrawString(arrow.left, arrow.right, CentreBounds(rr.top, rr.bottom, line), STR_JUST_RIGHT_ARROW, TextColour::Black, AlignmentH::Centre);
			Rect content = rr.Indent(arrow.Width(), !rtl).Indent(ScaleGUITrad(MOBILE_TEXT_INSET), rtl);

			Money profit = t->GetDisplayProfitThisYear();
			std::string money = GetString(STR_JUST_CURRENCY_SHORT, profit);
			int money_width = GetStringBoundingBox(money).width;
			DrawString(content.left, content.right, CentreBounds(rr.top, rr.bottom, line), money, profit < 0 ? TextColour::Red : TextColour::Black, AlignmentH::End);
			content = content.Indent(money_width + WidgetDimensions::scaled.hsep_wide, !rtl);

			Rect strip = content.WithWidth(ScaleGUITrad(70), rtl);
			DrawVehicleStrip(t, strip.WithY(rr.top, rr.bottom), false, nullptr);
			Rect text = content.Indent(strip.Width() + WidgetDimensions::scaled.hsep_wide, rtl);
			int y = CentreBounds(rr.top, rr.bottom, 2 * line);
			DrawString(text.left, text.right, y, GetString(STR_MOBILE_TRAIN_NAME, t->index), TextColour::Black);
			bool stopped = t->vehstatus.Test(VehState::Stopped);
			std::string state;
			if (stopped) {
				state = GetString(t->GetNumManualOrders() == 0 ? STR_MOBILE_TRAIN_STOPPED_NO_ORDERS : STR_MOBILE_TRAIN_STOPPED);
			} else if (t->current_order.IsType(OT_GOTO_STATION)) {
				state = GetString(STR_MOBILE_TRAIN_HEADING_TO, t->current_order.GetDestination());
			} else {
				state = GetString(STR_MOBILE_TRAIN_RUNNING);
			}
			DrawString(text.left, text.right, y + line, state, stopped ? TextColour::Red : TextColour::Black);
			tr.top += row;
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MTL_CLOSE: this->Close(); break;
			case WID_MTL_SORT: ShowDropDownMenu(this, sort_names, this->sort, WID_MTL_SORT, 0, 0); break;
			case WID_MTL_LIST: {
				auto it = this->vscroll->GetScrolledItemFromWidget(this->trains, pt.y, this, WID_MTL_LIST, WidgetDimensions::scaled.framerect.top, ScaleGUITrad(MOBILE_ROW));
				if (it != this->trains.end()) ShowMobileTrainWindow(*it);
				break;
			}
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		if (widget != WID_MTL_SORT) return;
		this->sort = index;
		this->BuildList();
		this->SetDirty();
	}

	const IntervalTimer<TimerWindow> refresh = {std::chrono::milliseconds(1000), [this](auto) {
		this->BuildList();
		this->SetDirty();
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_train_list_widgets = {
	NWidget(NWID_SELECTION, Colours::Invalid, WID_MTL_SEL_TITLE),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_PANEL, Colours::Grey, WID_MTL_TITLE), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), EndContainer(),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MTL_CLOSE), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_CROSS, STR_TOOLTIP_CLOSE_WINDOW),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(WWT_DROPDOWN, Colours::Grey, WID_MTL_SORT), SetMinimalSize(0, 32), SetFill(1, 0), SetResize(1, 0), SetPadding(4), SetToolTip(STR_TOOLTIP_SORT_CRITERIA),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_MTL_LIST), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_MTL_SCROLL), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_MTL_SCROLL),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(WWT_TEXT, Colours::Invalid, WID_MTL_TOTAL), SetMinimalSize(0, 24), SetFill(1, 0), SetResize(1, 0), SetPadding(0, MOBILE_TEXT_INSET, 0, MOBILE_TEXT_INSET),
	EndContainer(),
};

static WindowDesc _mobile_train_list_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::TrainList, WindowClass::None,
	{},
	_nested_mobile_train_list_widgets
);

static WindowDesc _mobile_road_vehicle_list_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::RoadVehicleList, WindowClass::None,
	{},
	_nested_mobile_train_list_widgets
);

static WindowDesc _mobile_ship_list_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::ShipList, WindowClass::None,
	{},
	_nested_mobile_train_list_widgets
);

static WindowDesc _mobile_aircraft_list_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::AircraftList, WindowClass::None,
	{},
	_nested_mobile_train_list_widgets
);

/**
 * Show the list of the company's trains (in the Vehicles hub), or of those calling at a station (in the sheet).
 * @param number Window number, as the desktop vehicle list uses it.
 * @param station Station to filter on, or \c StationID::Invalid().
 */
void ShowMobileTrainList(WindowNumber number, StationID station, VehicleType type)
{
	static WindowDesc *descs[] = {&_mobile_train_list_desc, &_mobile_road_vehicle_list_desc, &_mobile_ship_list_desc, &_mobile_aircraft_list_desc};
	WindowDesc &desc = *descs[to_underlying(type)];
	Window *w = BringWindowToFrontById(desc.cls, number);
	if (w == nullptr) w = new MobileTrainListWindow(desc, number, station, type);
	if (station != StationID::Invalid()) MobileShowInfoWindow(w);
}
