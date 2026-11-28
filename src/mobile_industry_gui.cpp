/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file mobile_industry_gui.cpp Industries in the mobile layout: the directory, the industry window, and funding new
 * industries (round 5 design, batch 7).
 */

#include "stdafx.h"
#include "mobile_industry_gui.h"
#include "mobile_gui.h"
#include "window_gui.h"
#include "window_func.h"
#include "command_func.h"
#include "company_func.h"
#include "industry.h"
#include "industrytype.h"
#include "industry_cmd.h"
#include "cargotype.h"
#include "strings_func.h"
#include "string_func.h"
#include "gfx_func.h"
#include "zoom_func.h"
#include "viewport_func.h"
#include "tilehighlight_func.h"
#include "textbuf_gui.h"
#include "dropdown_type.h"
#include "dropdown_func.h"
#include "graph_gui.h"
#include "newgrf_industries.h"
#include "gui.h"
#include "core/random_func.hpp"
#include "misc/history_type.hpp"
#include "timer/timer.h"
#include "timer/timer_window.h"
#include "widgets/mobile_widget.h"

#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

static constexpr int INDUSTRY_ROW = 48; ///< Height of a directory or type row, in GUI pixels at 1x.

/**
 * How many whole rows a list shows.
 * @param w The window.
 * @param widget The list.
 * @param row Height of a row.
 * @return The number of rows.
 */
static int GetIndustryRowCapacity(const Window *w, WidgetID widget, int row)
{
	const NWidgetBase *nwid = w->GetWidget<NWidgetBase>(widget);
	return std::max(1, (static_cast<int>(nwid->current_y) - static_cast<int>(WidgetDimensions::scaled.framerect.Vertical())) / row);
}

/**
 * Draw a small square of a colour, as the map legend does.
 * @param r Row to draw it at the start of.
 * @param colour The colour.
 * @return The rest of the row.
 */
static Rect DrawSwatch(const Rect &r, PixelColour colour)
{
	bool rtl = _current_text_dir == TD_RTL;
	int size = GetCharacterHeight(FontSize::Normal);
	int top = CentreBounds(r.top, r.bottom, size);
	Rect swatch = r.WithWidth(size, rtl).WithY(top, top + size - 1);
	GfxFillRect(swatch, PC_BLACK);
	GfxFillRect(swatch.Shrink(WidgetDimensions::scaled.bevel), colour);
	return r.Indent(size + WidgetDimensions::scaled.hsep_wide, rtl);
}

/**
 * Produced cargo of an industry last month, as "Coal 144 t · 0% transported" for up to two cargoes.
 * @param i The industry.
 * @return The text.
 */
static std::string GetIndustryProductionSummary(const Industry *i)
{
	std::string text;
	int shown = 0;
	for (const auto &p : i->produced) {
		if (!IsValidCargoType(p.cargo)) continue;
		if (shown == 2) {
			text += GetString(STR_MOBILE_ELLIPSIS);
			break;
		}
		if (!text.empty()) text += GetString(STR_MOBILE_ORDERS_SEPARATOR);
		text += GetString(STR_MOBILE_INDUSTRY_PRODUCED, p.cargo, p.history[LAST_MONTH].production, ToPercent8(p.history[LAST_MONTH].PctTransported()));
		shown++;
	}
	return text.empty() ? GetString(STR_MOBILE_INDUSTRY_PRODUCES_NOTHING) : text;
}

/* --- Industry window --- */

/** An industry in the sheet slot: what it produces and accepts, and links to the map, the cargo chain and the graph. */
struct MobileIndustryWindow : Window {
	bool over_directory; ///< Opened over the directory: ‹ returns to it.

	MobileIndustryWindow(WindowDesc &desc, IndustryID industry, bool over_directory) : Window(desc), over_directory(over_directory)
	{
		this->CreateNestedTree();
		this->GetWidget<NWidgetStacked>(WID_MIV_SEL_BACK)->SetDisplayedPlane(over_directory ? 0 : SZSP_NONE);
		this->FinishInitNested(industry);
		this->owner = Industry::Get(industry)->owner;
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_MIV_INFO:
				fill.height = resize.height = 1;
				size.height = 4 * ScaleGUITrad(40);
				break;
			case WID_MIV_TITLE:
				size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal));
				break;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		const Industry *i = Industry::GetIfValid(static_cast<IndustryID>(this->window_number));
		if (i == nullptr) return;
		int line = GetCharacterHeight(FontSize::Normal);
		switch (widget) {
			case WID_MIV_TITLE:
				DrawMobileTitle(r, GetString(STR_INDUSTRY_NAME, i->index));
				break;

			case WID_MIV_INFO: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
				bool any_produced = false;
				bool transported = false;
				DrawString(tr.left, tr.right, tr.top, STR_MOBILE_INDUSTRY_PRODUCES, TextColour::Black);
				tr.top += line + ScaleGUITrad(2);
				for (const auto &p : i->produced) {
					if (!IsValidCargoType(p.cargo)) continue;
					any_produced = true;
					const CargoSpec *cs = CargoSpec::Get(p.cargo);
					Rect rr = tr.WithHeight(ScaleGUITrad(40));
					Rect text = DrawSwatch(rr, cs->legend_colour);
					uint pct = ToPercent8(p.history[LAST_MONTH].PctTransported());
					if (pct > 0) transported = true;
					std::string pct_text = GetString(STR_MOBILE_INDUSTRY_TRANSPORTED, pct);
					int y = CentreBounds(rr.top, rr.bottom, 2 * line);
					DrawString(text.left, text.right, CentreBounds(rr.top, rr.bottom, line), pct_text, pct == 0 ? TextColour::Red : TextColour::DarkGreen, AlignmentH::End);
					Rect name = text.Indent(GetStringBoundingBox(pct_text).width + WidgetDimensions::scaled.hsep_wide, _current_text_dir != TD_RTL);
					DrawString(name.left, name.right, y, cs->name, TextColour::Black);
					DrawString(name.left, name.right, y + line, GetString(STR_MOBILE_INDUSTRY_LAST_MONTH, p.cargo, p.history[LAST_MONTH].production), TextColour::Black);
					tr.top += rr.Height();
				}
				if (!any_produced) {
					DrawString(tr.left, tr.right, tr.top, STR_MOBILE_INDUSTRY_NOTHING, TextColour::Black);
					tr.top += line;
				}

				tr.top += ScaleGUITrad(8);
				DrawString(tr.left, tr.right, tr.top, STR_MOBILE_INDUSTRY_ACCEPTS, TextColour::Black);
				tr.top += line + ScaleGUITrad(2);
				bool any_accepted = false;
				for (const auto &a : i->accepted) {
					if (!IsValidCargoType(a.cargo)) continue;
					any_accepted = true;
					const CargoSpec *cs = CargoSpec::Get(a.cargo);
					Rect rr = tr.WithHeight(ScaleGUITrad(32));
					Rect text = DrawSwatch(rr, cs->legend_colour);
					int y = CentreBounds(rr.top, rr.bottom, line);
					if (a.waiting > 0) DrawString(text.left, text.right, y, GetString(STR_MOBILE_INDUSTRY_WAITING, a.cargo, a.waiting), TextColour::Black, AlignmentH::End);
					DrawString(text.left, text.right, y, cs->name, TextColour::Black);
					tr.top += rr.Height();
				}
				if (!any_accepted) {
					DrawString(tr.left, tr.right, tr.top, STR_MOBILE_INDUSTRY_NOTHING, TextColour::Black);
					tr.top += line;
				}

				if (any_produced && !transported) {
					/* Nobody serves it yet: say how to. */
					tr.top += ScaleGUITrad(8);
					DrawStringMultiLine(tr, GetString(STR_MOBILE_INDUSTRY_HINT, GetIndustrySpec(i->type)->name), TextColour::Black);
				}
				break;
			}
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		const Industry *i = Industry::GetIfValid(static_cast<IndustryID>(this->window_number));
		if (i == nullptr) return;
		switch (widget) {
			case WID_MIV_BACK:
			case WID_MIV_CLOSE:
				this->Close();
				break;
			case WID_MIV_SHOW_ON_MAP:
				ScrollMainWindowToTile(i->location.GetCentreTile());
				break;
			case WID_MIV_CHAIN:
				ShowIndustryCargoChain(i->type);
				break;
			case WID_MIV_GRAPH:
				ShowIndustryProductionGraph(i->index);
				break;
		}
	}

	const IntervalTimer<TimerWindow> refresh = {std::chrono::milliseconds(1000), [this](auto) {
		if (Industry::GetIfValid(static_cast<IndustryID>(this->window_number)) == nullptr) {
			this->Close();
			return;
		}
		this->SetDirty();
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_industry_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_SELECTION, Colours::Invalid, WID_MIV_SEL_BACK),
			NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MIV_BACK), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_BACK, STR_MOBILE_BACK_TOOLTIP),
		EndContainer(),
		NWidget(WWT_PANEL, Colours::Brown, WID_MIV_TITLE), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), EndContainer(),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MIV_CLOSE), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_CROSS, STR_TOOLTIP_CLOSE_WINDOW),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_MIV_INFO), SetFill(1, 1), SetResize(1, 1), EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPadding(4), SetPIP(0, 4, 0),
			NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MIV_SHOW_ON_MAP), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_SHOW_ON_MAP, STR_INDUSTRY_VIEW_LOCATION_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MIV_CHAIN), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_INDUSTRY_CHAIN, STR_INDUSTRY_DISPLAY_CHAIN_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MIV_GRAPH), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_INDUSTRY_GRAPH, STR_INDUSTRY_VIEW_CARGO_GRAPH_TOOLTIP),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _mobile_industry_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::IndustryView, WindowClass::None,
	{},
	_nested_mobile_industry_widgets
);

/**
 * Show an industry in the sheet slot: over the directory when it is open (‹ returns to it), else as the sheet.
 * @param industry The industry.
 */
void ShowMobileIndustryWindow(IndustryID industry)
{
	Window *w = BringWindowToFrontById(WindowClass::IndustryView, industry);
	if (w == nullptr) {
		bool over = FindWindowById(WindowClass::IndustryDirectory, 0) != nullptr && FindWindowById(WindowClass::MobileHubTabs, 0) != nullptr;
		w = new MobileIndustryWindow(_mobile_industry_desc, industry, over);
	}
	MobileShowInfoWindow(w);
}

/* --- Directory --- */

/** The industries of the map: filtered by what they produce and accept, sorted; a row scrolls the map there. */
struct MobileIndustryDirectoryWindow : Window {
	std::vector<const Industry *> industries; ///< The industries listed.
	CargoType produces = INVALID_CARGO; ///< Only industries producing this; invalid for any.
	CargoType accepts = INVALID_CARGO; ///< Only industries accepting this; invalid for any.
	int sort = 0; ///< Index into #sort_names.
	Scrollbar *vscroll = nullptr;

	static inline const StringID sort_names[] = {STR_SORT_BY_NAME, STR_SORT_BY_TYPE, STR_SORT_BY_PRODUCTION, STR_SORT_BY_TRANSPORTED};

	MobileIndustryDirectoryWindow(WindowDesc &desc) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_MIND_SCROLL);
		this->FinishInitNested(0);
		this->BuildList();
	}

	/**
	 * Total production of an industry last month.
	 * @param i The industry.
	 * @return The amount.
	 */
	static uint GetProduction(const Industry *i)
	{
		uint total = 0;
		for (const auto &p : i->produced) total += p.history[LAST_MONTH].production;
		return total;
	}

	/**
	 * Best share transported of an industry's cargoes last month.
	 * @param i The industry.
	 * @return The share, 0-255.
	 */
	static uint GetTransported(const Industry *i)
	{
		uint best = 0;
		for (const auto &p : i->produced) best = std::max<uint>(best, p.history[LAST_MONTH].PctTransported());
		return best;
	}

	void BuildList()
	{
		this->industries.clear();
		for (const Industry *i : Industry::Iterate()) {
			if (this->produces != INVALID_CARGO && i->GetCargoProduced(this->produces) == i->produced.end()) continue;
			if (this->accepts != INVALID_CARGO && i->GetCargoAccepted(this->accepts) == i->accepted.end()) continue;
			this->industries.push_back(i);
		}
		switch (this->sort) {
			case 0: {
				std::vector<std::pair<std::string, const Industry *>> named;
				for (const Industry *i : this->industries) named.emplace_back(GetString(STR_INDUSTRY_NAME, i->index), i);
				std::ranges::sort(named, [](const auto &a, const auto &b) { return StrNaturalCompare(a.first, b.first) < 0; });
				this->industries.clear();
				for (const auto &[name, i] : named) this->industries.push_back(i);
				break;
			}
			case 1: std::ranges::stable_sort(this->industries, [](const Industry *a, const Industry *b) { return a->type < b->type; }); break;
			case 2: std::ranges::stable_sort(this->industries, [](const Industry *a, const Industry *b) { return GetProduction(a) > GetProduction(b); }); break;
			case 3: std::ranges::stable_sort(this->industries, [](const Industry *a, const Industry *b) { return GetTransported(a) > GetTransported(b); }); break;
		}
		this->vscroll->SetCount(this->industries.size());
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		if (widget == WID_MIND_LIST) {
			fill.height = resize.height = 1;
			size.height = 3 * ScaleGUITrad(INDUSTRY_ROW);
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacity(GetIndustryRowCapacity(this, WID_MIND_LIST, ScaleGUITrad(INDUSTRY_ROW)));
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_MIND_PRODUCES: return this->produces == INVALID_CARGO ? GetString(STR_MOBILE_INDUSTRY_PRODUCES_ANY) : GetString(STR_MOBILE_INDUSTRY_PRODUCES_CARGO, CargoSpec::Get(this->produces)->name);
			case WID_MIND_ACCEPTS: return this->accepts == INVALID_CARGO ? GetString(STR_MOBILE_INDUSTRY_ACCEPTS_ANY) : GetString(STR_MOBILE_INDUSTRY_ACCEPTS_CARGO, CargoSpec::Get(this->accepts)->name);
			case WID_MIND_SORT: return GetString(sort_names[this->sort]);
			default: return this->Window::GetWidgetString(widget, stringid);
		}
	}

	/**
	 * The (i) button of a row.
	 * @param row The row.
	 * @return The button.
	 */
	static Rect GetInfoButton(const Rect &row)
	{
		return row.WithWidth(ScaleGUITrad(40), _current_text_dir != TD_RTL).Shrink(0, ScaleGUITrad(4));
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MIND_LIST) return;
		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
		if (this->industries.empty()) {
			DrawString(tr, STR_MOBILE_INDUSTRY_NONE, TextColour::Black);
			return;
		}
		int row = ScaleGUITrad(INDUSTRY_ROW);
		int line = GetCharacterHeight(FontSize::Normal);
		auto [first, last] = this->vscroll->GetVisibleRangeIterators(this->industries);
		for (auto it = first; it != last; ++it) {
			const Industry *i = *it;
			Rect rr = tr.WithHeight(row);
			Rect info = GetInfoButton(rr);
			DrawFrameRect(info, Colours::Brown, {});
			DrawString(info.left, info.right, CentreBounds(info.top, info.bottom, line), STR_MOBILE_INDUSTRY_INFO, TextColour::White, AlignmentH::Centre);
			Rect text = DrawSwatch(rr.Indent(info.Width() + WidgetDimensions::scaled.hsep_wide, _current_text_dir != TD_RTL), GetIndustrySpec(i->type)->map_colour);
			int y = CentreBounds(rr.top, rr.bottom, 2 * line);
			DrawString(text.left, text.right, y, GetString(STR_INDUSTRY_NAME, i->index), TextColour::Black);
			DrawString(text.left, text.right, y + line, GetIndustryProductionSummary(i), TextColour::Black);
			GfxFillRect(r.left, rr.bottom, r.right, rr.bottom, GetColourGradient(Colours::Brown, Shade::Dark));
			tr.top += row;
		}
	}

	/**
	 * A cargo filter dropdown: any, then every cargo some industry type produces (or accepts).
	 * @param produced Produced cargoes, else accepted ones.
	 */
	void ShowCargoFilter(bool produced)
	{
		CargoTypes cargoes{};
		for (IndustryType type = 0; type < NUM_INDUSTRYTYPES; type++) {
			const IndustrySpec *indsp = GetIndustrySpec(type);
			if (!indsp->enabled) continue;
			if (produced) {
				for (CargoType c : indsp->produced_cargo) if (IsValidCargoType(c)) cargoes.Set(c);
			} else {
				for (CargoType c : indsp->accepts_cargo) if (IsValidCargoType(c)) cargoes.Set(c);
			}
		}
		DropDownList list;
		list.push_back(MakeDropDownListStringItem(produced ? STR_MOBILE_INDUSTRY_PRODUCES_ANY : STR_MOBILE_INDUSTRY_ACCEPTS_ANY, INVALID_CARGO));
		for (const CargoSpec *cs : _sorted_cargo_specs) {
			if (cargoes.Test(cs->Index())) list.push_back(MakeDropDownListStringItem(cs->name, cs->Index()));
		}
		CargoType current = produced ? this->produces : this->accepts;
		ShowDropDownList(this, std::move(list), current, produced ? WID_MIND_PRODUCES : WID_MIND_ACCEPTS);
	}

	void OnClick(Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MIND_PRODUCES: this->ShowCargoFilter(true); break;
			case WID_MIND_ACCEPTS: this->ShowCargoFilter(false); break;
			case WID_MIND_SORT: ShowDropDownMenu(this, sort_names, this->sort, WID_MIND_SORT, 0, 0); break;
			case WID_MIND_FUND: ShowBuildIndustryWindow(); break;
			case WID_MIND_LIST: {
				auto it = this->vscroll->GetScrolledItemFromWidget(this->industries, pt.y, this, WID_MIND_LIST, WidgetDimensions::scaled.framerect.top, ScaleGUITrad(INDUSTRY_ROW));
				if (it == this->industries.end()) break;
				/* The (i) button opens the industry; the rest of the row shows it on the map. */
				Rect rr = this->GetWidget<NWidgetBase>(WID_MIND_LIST)->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
				if (GetInfoButton(rr).WithY(0, INT32_MAX / 2).Contains({pt.x, 1})) {
					ShowMobileIndustryWindow((*it)->index);
				} else {
					ScrollMainWindowToTile((*it)->location.GetCentreTile());
				}
				break;
			}
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		switch (widget) {
			case WID_MIND_PRODUCES: this->produces = index < 0 ? INVALID_CARGO : static_cast<CargoType>(index); break;
			case WID_MIND_ACCEPTS: this->accepts = index < 0 ? INVALID_CARGO : static_cast<CargoType>(index); break;
			case WID_MIND_SORT: this->sort = index; break;
			default: return;
		}
		this->vscroll->SetPosition(0);
		this->BuildList();
		this->SetDirty();
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->BuildList();
		this->SetDirty();
	}

	const IntervalTimer<TimerWindow> refresh = {std::chrono::seconds(3), [this](auto) {
		this->BuildList();
		this->SetDirty();
	}};
};

/* In the World hub, whose tabs are the title. */
static constexpr std::initializer_list<NWidgetPart> _nested_mobile_industry_directory_widgets = {
	NWidget(WWT_PANEL, Colours::Brown),
		NWidget(NWID_HORIZONTAL), SetPadding(4), SetPIP(0, 4, 0),
			NWidget(WWT_DROPDOWN, Colours::Brown, WID_MIND_PRODUCES), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_INDUSTRY_FILTER_TOOLTIP),
			NWidget(WWT_DROPDOWN, Colours::Brown, WID_MIND_ACCEPTS), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_INDUSTRY_FILTER_TOOLTIP),
			NWidget(WWT_DROPDOWN, Colours::Brown, WID_MIND_SORT), SetMinimalSize(80, 40), SetToolTip(STR_TOOLTIP_SORT_CRITERIA),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Brown, WID_MIND_LIST), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_MIND_SCROLL), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Brown, WID_MIND_SCROLL),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MIND_FUND), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), SetPadding(4), SetStringTip(STR_MOBILE_INDUSTRY_FUND, STR_FUND_INDUSTRY_MANY_RANDOM_INDUSTRIES_TOOLTIP),
	EndContainer(),
};

static WindowDesc _mobile_industry_directory_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::IndustryDirectory, WindowClass::None,
	{},
	_nested_mobile_industry_directory_widgets
);

/** Show the industry directory (the World hub's Industries tab). */
void ShowMobileIndustryDirectory()
{
	if (BringWindowToFrontById(WindowClass::IndustryDirectory, 0) == nullptr) new MobileIndustryDirectoryWindow(_mobile_industry_directory_desc);
}

/* --- Fund new industry --- */

/** Funding (or prospecting) industries: their types, each expanding to what it makes and Place it / Prospect. */
struct MobileBuildIndustryWindow : Window {
	std::vector<IndustryType> types; ///< The types that can be funded or prospected.
	IndustryType expanded = IT_INVALID; ///< The type showing its details.
	IndustryType placing = IT_INVALID; ///< The type being placed.
	TileIndex pending = INVALID_TILE; ///< Where the industry in review goes.
	uint32_t seed = 0; ///< Random seed of the industry in review, so the estimate and the construction match.
	uint32_t layout = 0; ///< Layout of the industry in review.
	bool estimate_failed = false; ///< The industry in review can't be built there.
	Money estimate_cost = 0; ///< Its cost.
	std::string estimate_error; ///< Why it can't be built.
	Scrollbar *vscroll = nullptr;

	MobileBuildIndustryWindow(WindowDesc &desc) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_MBI_SCROLL);
		this->FinishInitNested(0);
		this->BuildList();
		this->UpdatePlanes();
	}

	void Close(int data = 0) override
	{
		if (_thd.GetCallbackWnd() == this) ResetObjectToPlace();
		this->Window::Close(data);
	}

	void BuildList()
	{
		this->types.clear();
		for (IndustryType type : _sorted_industry_types) {
			const IndustrySpec *indsp = GetIndustrySpec(type);
			if (!indsp->enabled) continue;
			if (indsp->IsRawIndustry() && _settings_game.construction.raw_industry_construction == 0) continue;
			this->types.push_back(type);
		}
		this->vscroll->SetCount(this->types.size());
	}

	/**
	 * Is a type prospected (a random place) rather than placed?
	 * @param type The type.
	 * @return \\c true when it is prospected.
	 */
	static bool IsProspected(IndustryType type)
	{
		return _settings_game.construction.raw_industry_construction == 2 && GetIndustrySpec(type)->IsRawIndustry();
	}

	/**
	 * Can a type be funded now (the game's or a NewGRF's rules)?
	 * @param type The type.
	 * @return \\c true when it can.
	 */
	static bool IsAvailable(IndustryType type)
	{
		return GetIndustryProbabilityCallback(type, IndustryAvailabilityCallType::UserCreation, 1) > 0;
	}

	/** Show the review footer while placing, the list otherwise. */
	void UpdatePlanes()
	{
		bool placing = this->placing != IT_INVALID;
		NWidgetStacked *sel = this->GetWidget<NWidgetStacked>(WID_MBI_SEL_FOOTER);
		int plane = placing ? 0 : SZSP_HORIZONTAL;
		if (sel->shown_plane != plane) {
			sel->SetDisplayedPlane(plane);
			this->ReInit();
		}
		bool can_fund = this->pending != INVALID_TILE && !this->estimate_failed;
		this->SetWidgetDisabledState(WID_MBI_FUND, !can_fund);
		this->GetWidget<NWidgetCore>(WID_MBI_FUND)->colour = can_fund ? Colours::Green : Colours::Grey;
		this->SetDirty();
	}

	/**
	 * Height of a row: taller when it shows its details.
	 * @param type The row's type.
	 * @return The height in pixels.
	 */
	int GetRowHeight(IndustryType type) const
	{
		int h = ScaleGUITrad(INDUSTRY_ROW);
		if (type == this->expanded) h += 2 * GetCharacterHeight(FontSize::Normal) + ScaleGUITrad(48);
		return h;
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_MBI_LIST:
				fill.height = resize.height = 1;
				size.height = 3 * ScaleGUITrad(INDUSTRY_ROW);
				break;
			case WID_MBI_TITLE:
				size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal));
				break;
			case WID_MBI_STATUS:
				size.height = std::max<uint>(size.height, 2 * GetCharacterHeight(FontSize::Normal));
				break;
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacity(GetIndustryRowCapacity(this, WID_MBI_LIST, ScaleGUITrad(INDUSTRY_ROW)));
	}

	/**
	 * The cargoes of a type, as "coal, iron ore".
	 * @param cargoes The cargo slots.
	 * @return The text; empty for none.
	 */
	template <size_t N>
	static std::string GetCargoNames(const std::array<CargoType, N> &cargoes)
	{
		std::string text;
		for (CargoType c : cargoes) {
			if (!IsValidCargoType(c)) continue;
			if (!text.empty()) text += ", ";
			text += GetString(CargoSpec::Get(c)->name);
		}
		return text;
	}

	/**
	 * The Place it / Prospect button of an expanded row.
	 * @param row The row.
	 * @return The button.
	 */
	static Rect GetActionButton(const Rect &row)
	{
		Rect r = row.WithY(row.bottom - ScaleGUITrad(48) + 1, row.bottom).Shrink(0, ScaleGUITrad(4));
		return r;
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		int line = GetCharacterHeight(FontSize::Normal);
		switch (widget) {
			case WID_MBI_TITLE:
				DrawMobileTitle(r, GetString(STR_MOBILE_INDUSTRY_FUND_TITLE));
				break;

			case WID_MBI_STATUS: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
				const IndustrySpec *indsp = GetIndustrySpec(this->placing);
				std::string text;
				TextColour colour = TextColour::Black;
				if (this->pending == INVALID_TILE) {
					text = GetString(STR_MOBILE_INDUSTRY_TAP_TO_PLACE, indsp->name);
				} else if (this->estimate_failed) {
					text = GetString(STR_MOBILE_BUILD_STATUS_ERROR, this->estimate_error);
					colour = TextColour::Red;
				} else {
					text = GetString(STR_MOBILE_INDUSTRY_PENDING, indsp->name, this->estimate_cost);
				}
				DrawStringMultiLineWithClipping(tr.WithHeight(std::min<int>(tr.Height(), 2 * line)).WithY(CentreBounds(r.top, r.bottom, std::min(2 * line, GetStringHeight(text, tr.Width()))), r.bottom), text, colour);
				break;
			}

			case WID_MBI_LIST: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
				if (this->types.empty()) {
					DrawString(tr, STR_MOBILE_INDUSTRY_FUND_NONE, TextColour::Black);
					break;
				}
				for (size_t idx = this->vscroll->GetPosition(); idx < this->types.size() && tr.top < tr.bottom; idx++) {
					IndustryType type = this->types[idx];
					const IndustrySpec *indsp = GetIndustrySpec(type);
					bool available = IsAvailable(type);
					TextColour tc = available ? TextColour::Black : TextColour::Grey;
					Rect rr = tr.WithHeight(this->GetRowHeight(type));
					Rect head = rr.WithHeight(ScaleGUITrad(INDUSTRY_ROW));
					Rect text = DrawSwatch(head, indsp->map_colour);
					int y = CentreBounds(head.top, head.bottom, 2 * line);
					DrawString(text.left, text.right, y, indsp->name, tc);
					std::string produces = GetCargoNames(indsp->produced_cargo);
					std::string key = produces.empty() ? GetString(STR_MOBILE_INDUSTRY_COST, indsp->GetConstructionCost()) : GetString(STR_MOBILE_INDUSTRY_PRODUCES_COST, produces, indsp->GetConstructionCost());
					DrawString(text.left, text.right, y + line, key, tc);
					if (type == this->expanded) {
						int dy = head.bottom + 1;
						std::string accepts = GetCargoNames(indsp->accepts_cargo);
						DrawString(text.left, text.right, dy, GetString(STR_MOBILE_INDUSTRY_PRODUCES_LIST, produces.empty() ? GetString(STR_MOBILE_INDUSTRY_NOTHING) : produces), TextColour::Black);
						DrawString(text.left, text.right, dy + line, GetString(STR_MOBILE_INDUSTRY_ACCEPTS_LIST, accepts.empty() ? GetString(STR_MOBILE_INDUSTRY_NOTHING) : accepts), TextColour::Black);
						Rect button = GetActionButton(rr);
						bool green = available && !IsProspected(type);
						DrawFrameRect(button, !available ? Colours::Grey : (green ? Colours::Green : Colours::Brown), {});
						StringID label = !available ? STR_MOBILE_INDUSTRY_NOT_NOW : (IsProspected(type) ? STR_MOBILE_INDUSTRY_PROSPECT : STR_MOBILE_INDUSTRY_PLACE);
						DrawString(button.left, button.right, CentreBounds(button.top, button.bottom, line), GetString(label, indsp->GetConstructionCost()), available ? TextColour::White : TextColour::Black, AlignmentH::Centre);
					}
					GfxFillRect(r.left, rr.bottom, r.right, rr.bottom, GetColourGradient(Colours::Brown, Shade::Dark));
					tr.top += rr.Height();
				}
				break;
			}
		}
	}

	/**
	 * Estimate the industry in review.
	 */
	void Estimate()
	{
		CommandResultCapture capture(true);
		Command<Commands::BuildIndustry>::Post(STR_ERROR_CAN_T_CONSTRUCT_THIS_INDUSTRY, this->pending, this->placing, this->layout, false, this->seed);
		this->estimate_failed = capture.failed;
		this->estimate_cost = capture.cost;
		this->estimate_error.clear();
		if (capture.failed) this->estimate_error = capture.error.empty() ? GetString(capture.error_summary) : capture.error.GetDecodedString();
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		/* A tap puts the industry in review: its cost or error, then Fund. */
		const IndustrySpec *indsp = GetIndustrySpec(this->placing);
		this->pending = tile;
		this->seed = InteractiveRandom();
		this->layout = InteractiveRandomRange(static_cast<uint32_t>(indsp->layouts.size()));
		this->Estimate();
		this->UpdatePlanes();
	}

	void OnPlaceObjectAbort() override
	{
		this->placing = IT_INVALID;
		this->pending = INVALID_TILE;
		this->UpdatePlanes();
	}

	/**
	 * Start placing a type, or prospect for it after asking.
	 * @param type The type.
	 */
	void Act(IndustryType type)
	{
		if (!IsAvailable(type)) return;
		if (IsProspected(type)) {
			this->placing = type; // Remembered for the confirmation.
			const IndustrySpec *indsp = GetIndustrySpec(type);
			ShowQuery(GetEncodedString(STR_MOBILE_INDUSTRY_PROSPECT_TITLE, indsp->name, indsp->GetConstructionCost()), GetEncodedString(STR_MOBILE_INDUSTRY_PROSPECT_TEXT), this, [](Window *w, bool confirmed) {
				MobileBuildIndustryWindow *bw = static_cast<MobileBuildIndustryWindow *>(w);
				IndustryType t = bw->placing;
				bw->placing = IT_INVALID;
				if (!confirmed || t == IT_INVALID) return;
				Command<Commands::BuildIndustry>::Post(STR_ERROR_CAN_T_CONSTRUCT_THIS_INDUSTRY, TileIndex{}, t, 0, false, InteractiveRandom());
			}, false, STR_MOBILE_INDUSTRY_PROSPECT_VERB, false);
			return;
		}
		this->placing = type;
		this->pending = INVALID_TILE;
		SetObjectToPlaceWnd(SPR_CURSOR_INDUSTRY, PAL_NONE, HT_RECT, this);
		this->UpdatePlanes();
	}

	void OnClick(Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MBI_CLOSE: this->Close(); break;
			case WID_MBI_CANCEL:
				if (this->pending != INVALID_TILE) {
					this->pending = INVALID_TILE;
					this->UpdatePlanes();
				} else {
					ResetObjectToPlace();
				}
				break;
			case WID_MBI_FUND:
				if (this->pending == INVALID_TILE) break;
				if (Command<Commands::BuildIndustry>::Post(STR_ERROR_CAN_T_CONSTRUCT_THIS_INDUSTRY, this->pending, this->placing, this->layout, false, this->seed)) {
					/* Done: keep the tool for another, as other construction does. */
					this->pending = INVALID_TILE;
					this->UpdatePlanes();
				}
				break;
			case WID_MBI_LIST: {
				const NWidgetBase *nwid = this->GetWidget<NWidgetBase>(WID_MBI_LIST);
				Rect tr = nwid->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
				for (size_t idx = this->vscroll->GetPosition(); idx < this->types.size() && tr.top < tr.bottom; idx++) {
					IndustryType type = this->types[idx];
					Rect rr = tr.WithHeight(this->GetRowHeight(type));
					if (pt.y >= rr.top && pt.y <= rr.bottom) {
						if (type == this->expanded && GetActionButton(rr).Contains(pt)) {
							this->Act(type);
						} else {
							this->expanded = (type == this->expanded) ? IT_INVALID : type;
							this->SetWidgetDirty(WID_MBI_LIST);
						}
						break;
					}
					tr.top += rr.Height();
				}
				break;
			}
		}
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->BuildList();
		this->SetDirty();
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_build_industry_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Brown, WID_MBI_TITLE), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), EndContainer(),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MBI_CLOSE), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_CROSS, STR_TOOLTIP_CLOSE_WINDOW),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Brown, WID_MBI_LIST), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_MBI_SCROLL), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Brown, WID_MBI_SCROLL),
	EndContainer(),
	NWidget(NWID_SELECTION, Colours::Invalid, WID_MBI_SEL_FOOTER),
		NWidget(WWT_PANEL, Colours::Brown),
			NWidget(NWID_HORIZONTAL), SetPadding(4), SetPIP(0, 4, 0),
				NWidget(WWT_INSET, Colours::Brown, WID_MBI_STATUS), SetMinimalSize(80, 40), SetFill(1, 0), SetResize(1, 0), EndContainer(),
				NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MBI_CANCEL), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_CROSS, STR_MOBILE_BUILD_CANCEL_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, Colours::Green, WID_MBI_FUND), SetMinimalSize(64, 40), SetStringTip(STR_MOBILE_INDUSTRY_FUND_VERB, STR_MOBILE_BUILD_CONFIRM_TOOLTIP),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _mobile_build_industry_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::BuildIndustry, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_mobile_build_industry_widgets
);

/** Show Fund new industry in the sheet slot. */
void ShowMobileBuildIndustry()
{
	Window *w = BringWindowToFrontById(WindowClass::BuildIndustry, 0);
	if (w == nullptr) w = new MobileBuildIndustryWindow(_mobile_build_industry_desc);
	MobileShowInfoWindow(w);
}
