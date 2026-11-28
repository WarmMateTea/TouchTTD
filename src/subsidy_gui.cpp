/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file subsidy_gui.cpp GUI for subsidies. */

#include "stdafx.h"
#include "industry.h"
#include "town.h"
#include "window_gui.h"
#include "strings_func.h"
#include "viewport_func.h"
#include "gui.h"
#include "subsidy_base.h"
#include "core/geometry_func.hpp"

#include "widgets/subsidy_widget.h"

#include "mobile_gui.h"
#include "zoom_func.h"
#include "timer/timer_game_economy.h"
#include "widgets/mobile_widget.h"

#include "table/strings.h"

#include "safeguards.h"

struct SubsidyListWindow : Window {
	Scrollbar *vscroll = nullptr;
	Dimension cargo_icon_size{};

	SubsidyListWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_SUL_SCROLLBAR);
		this->FinishInitNested(window_number);
		this->OnInvalidateData(0);
	}

	void OnInit() override
	{
		this->cargo_icon_size = GetLargestCargoIconSize();
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget != WID_SUL_PANEL) return;

		int y = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_SUL_PANEL, WidgetDimensions::scaled.framerect.top);
		int num = 0;
		for (const Subsidy *s : Subsidy::Iterate()) {
			if (!s->IsAwarded()) {
				y--;
				if (y == 0) {
					this->HandleClick(s);
					return;
				}
				num++;
			}
		}

		if (num == 0) {
			y--; // "None"
			if (y < 0) return;
		}

		y -= 2; // "Services already subsidised:"
		if (y < 0) return;

		for (const Subsidy *s : Subsidy::Iterate()) {
			if (s->IsAwarded()) {
				y--;
				if (y == 0) {
					this->HandleClick(s);
					return;
				}
			}
		}
	}

	void HandleClick(const Subsidy *s)
	{
		/* determine src coordinate for subsidy and try to scroll to it */
		TileIndex xy;
		switch (s->src.type) {
			case SourceType::Industry: xy = Industry::Get(s->src.ToIndustryID())->location.tile; break;
			case SourceType::Town:     xy =     Town::Get(s->src.ToTownID())->xy; break;
			default: NOT_REACHED();
		}

		if (IsCtrlPressed() || !ScrollMainWindowToTile(xy)) {
			if (IsCtrlPressed()) ShowExtraViewportWindow(xy);

			/* otherwise determine dst coordinate for subsidy and scroll to it */
			switch (s->dst.type) {
				case SourceType::Industry: xy = Industry::Get(s->dst.ToIndustryID())->location.tile; break;
				case SourceType::Town:     xy =     Town::Get(s->dst.ToTownID())->xy; break;
				default: NOT_REACHED();
			}

			if (IsCtrlPressed()) {
				ShowExtraViewportWindow(xy);
			} else {
				ScrollMainWindowToTile(xy);
			}
		}
	}

	/**
	 * Count the number of lines in this window.
	 * @return the number of lines
	 */
	uint CountLines()
	{
		/* Count number of (non) awarded subsidies */
		uint num_awarded = 0;
		uint num_not_awarded = 0;
		for (const Subsidy *s : Subsidy::Iterate()) {
			if (!s->IsAwarded()) {
				num_not_awarded++;
			} else {
				num_awarded++;
			}
		}

		/* Count the 'none' lines */
		if (num_awarded     == 0) num_awarded = 1;
		if (num_not_awarded == 0) num_not_awarded = 1;

		/* Offered, accepted and an empty line before the accepted ones. */
		return 3 + num_awarded + num_not_awarded;
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_SUL_PANEL) return;
		Dimension d = maxdim(GetStringBoundingBox(STR_SUBSIDIES_OFFERED_TITLE), GetStringBoundingBox(STR_SUBSIDIES_SUBSIDISED_TITLE));

		fill.height = resize.height = GetCharacterHeight(FontSize::Normal);

		d.height *= 5;
		d.width += WidgetDimensions::scaled.framerect.Horizontal();
		d.height += WidgetDimensions::scaled.framerect.Vertical();
		size = maxdim(size, d);
	}

	void DrawCargoIcon(const Rect &r, int y_offset, CargoType cargo_type) const
	{
		bool rtl = _current_text_dir == TD_RTL;
		SpriteID icon = CargoSpec::Get(cargo_type)->GetCargoIcon();
		Dimension d = GetSpriteSize(icon);
		Rect ir = r.WithWidth(this->cargo_icon_size.width, rtl).WithHeight(GetCharacterHeight(FontSize::Normal));
		DrawSprite(icon, PAL_NONE, CentreBounds(ir.left, ir.right, d.width), CentreBounds(ir.top, ir.bottom, this->cargo_icon_size.height) + y_offset);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_SUL_PANEL) return;

		TimerGameEconomy::YearMonthDay ymd = TimerGameEconomy::ConvertDateToYMD(TimerGameEconomy::date);

		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
		Rect sr = tr.Indent(this->cargo_icon_size.width + WidgetDimensions::scaled.hsep_normal, _current_text_dir == TD_RTL);

		int pos = -this->vscroll->GetPosition();
		const int cap = this->vscroll->GetCapacity();

		/* Section for drawing the offered subsidies */
		if (IsInsideMM(pos, 0, cap)) DrawString(tr.left, tr.right, tr.top + pos * GetCharacterHeight(FontSize::Normal), STR_SUBSIDIES_OFFERED_TITLE);
		pos++;

		uint num = 0;
		for (const Subsidy *s : Subsidy::Iterate()) {
			if (!s->IsAwarded()) {
				if (IsInsideMM(pos, 0, cap)) {
					/* Displays the two offered towns */
					const CargoSpec *cs = CargoSpec::Get(s->cargo_type);
					std::string text;

					/* If using wallclock units, show minutes remaining. Otherwise show the date when the subsidy ends. */
					if (TimerGameEconomy::UsingWallclockUnits()) {
						text = GetString(STR_SUBSIDIES_OFFERED_FROM_TO,
							cs->name, s->src.GetFormat(), s->src.id, s->dst.GetFormat(), s->dst.id,
							STR_SUBSIDIES_OFFERED_EXPIRY_TIME,
							s->remaining + 1); // We get the rest of the current economy month for free, since the expiration is checked on each new month.
					} else {
						text = GetString(STR_SUBSIDIES_OFFERED_FROM_TO,
							cs->name, s->src.GetFormat(), s->src.id, s->dst.GetFormat(), s->dst.id,
							STR_SUBSIDIES_OFFERED_EXPIRY_DATE,
							TimerGameEconomy::date.base() - ymd.day + s->remaining * 32);
					}

					DrawCargoIcon(tr, pos * GetCharacterHeight(FontSize::Normal), s->cargo_type);
					DrawString(sr.left, sr.right, sr.top + pos * GetCharacterHeight(FontSize::Normal), text);
				}
				pos++;
				num++;
			}
		}

		if (num == 0) {
			if (IsInsideMM(pos, 0, cap)) DrawString(tr.left, tr.right, tr.top + pos * GetCharacterHeight(FontSize::Normal), STR_SUBSIDIES_NONE);
			pos++;
		}

		/* Section for drawing the already granted subsidies */
		pos++;
		if (IsInsideMM(pos, 0, cap)) DrawString(tr.left, tr.right, tr.top + pos * GetCharacterHeight(FontSize::Normal), STR_SUBSIDIES_SUBSIDISED_TITLE);
		pos++;
		num = 0;

		for (const Subsidy *s : Subsidy::Iterate()) {
			if (s->IsAwarded()) {
				if (IsInsideMM(pos, 0, cap)) {
					const CargoSpec *cs = CargoSpec::Get(s->cargo_type);
					std::string text;

					/* If using wallclock units, show minutes remaining. Otherwise show the date when the subsidy ends. */
					if (TimerGameEconomy::UsingWallclockUnits()) {
						text = GetString(STR_SUBSIDIES_SUBSIDISED_FROM_TO,
							cs->name, s->src.GetFormat(), s->src.id, s->dst.GetFormat(), s->dst.id,
							s->awarded,
							STR_SUBSIDIES_SUBSIDISED_EXPIRY_TIME,
							s->remaining + 1); // We get the rest of the current economy month for free, since the expiration is checked on each new month.
					} else {
						text = GetString(STR_SUBSIDIES_SUBSIDISED_FROM_TO,
							cs->name, s->src.GetFormat(), s->src.id, s->dst.GetFormat(), s->dst.id,
							s->awarded,
							STR_SUBSIDIES_SUBSIDISED_EXPIRY_DATE,
							TimerGameEconomy::date.base() - ymd.day + s->remaining * 32);
					}

					/* Displays the two connected stations */
					DrawCargoIcon(tr, pos * GetCharacterHeight(FontSize::Normal), s->cargo_type);
					DrawString(sr.left, sr.right, sr.top + pos * GetCharacterHeight(FontSize::Normal), text);
				}
				pos++;
				num++;
			}
		}

		if (num == 0) {
			if (IsInsideMM(pos, 0, cap)) DrawString(tr.left, tr.right, tr.top + pos * GetCharacterHeight(FontSize::Normal), STR_SUBSIDIES_NONE);
			pos++;
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_SUL_PANEL, WidgetDimensions::scaled.framerect.Vertical());
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->vscroll->SetCount(this->CountLines());
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_subsidies_list_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown), SetStringTip(STR_SUBSIDIES_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_DEFSIZEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Brown, WID_SUL_PANEL), SetToolTip(STR_SUBSIDIES_TOOLTIP_CLICK_ON_SERVICE_TO_CENTER), SetResize(1, 1), SetScrollbar(WID_SUL_SCROLLBAR), EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, Colours::Brown, WID_SUL_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, Colours::Brown),
		EndContainer(),
	EndContainer(),
};

/** Window definition for the subsidies window. */
static WindowDesc _subsidies_list_desc(
	WindowPosition::Automatic, "list_subsidies", 500, 127,
	WindowClass::SubsidyList, WindowClass::None,
	{},
	_nested_subsidies_list_widgets
);



/** The subsidies in the mobile layout: offered, then awarded; From and To scroll the map to each end (round 5 design, 10.2). */
struct MobileSubsidiesWindow : Window {
	Scrollbar *vscroll = nullptr;

	/** A line of the list: a section header, a subsidy, or "None". */
	struct Row {
		StringID header = STR_NULL; ///< A header's text, or STR_NULL.
		const Subsidy *subsidy = nullptr; ///< The subsidy of a subsidy row.
	};

	MobileSubsidiesWindow(WindowDesc &desc) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_MWL_SCROLL);
		this->FinishInitNested(0);
		this->UpdateCount();
	}

	/** @return The lines. */
	static std::vector<Row> GetRows()
	{
		std::vector<Row> rows;
		for (bool awarded : {false, true}) {
			rows.push_back({awarded ? STR_MOBILE_SUBSIDIES_AWARDED : STR_MOBILE_SUBSIDIES_OFFERED});
			bool any = false;
			for (const Subsidy *s : Subsidy::Iterate()) {
				if (s->IsAwarded() != awarded) continue;
				rows.push_back({STR_NULL, s});
				any = true;
			}
			if (!any) rows.push_back({STR_MOBILE_SUBSIDIES_NONE});
		}
		return rows;
	}

	/**
	 * Height of a line.
	 * @param row The line.
	 * @return Its height in pixels.
	 */
	static int GetRowHeight(const Row &row)
	{
		return ScaleGUITrad(row.subsidy != nullptr ? 48 : 32);
	}

	void UpdateCount()
	{
		int height = 0;
		for (const Row &row : GetRows()) height += GetRowHeight(row);
		this->vscroll->SetCount(height);
		this->vscroll->SetStepSize(ScaleGUITrad(48));
	}

	/**
	 * Where one end of a subsidy is.
	 * @param src The end.
	 * @return Its tile.
	 */
	static TileIndex GetEndTile(const Source &src)
	{
		switch (src.type) {
			case SourceType::Industry: return Industry::Get(src.ToIndustryID())->location.tile;
			case SourceType::Town: return Town::Get(src.ToTownID())->xy;
			default: return INVALID_TILE;
		}
	}

	/**
	 * The From and To buttons of a subsidy row.
	 * @param row The row.
	 * @param to The To button, else the From button.
	 * @return The button.
	 */
	static Rect GetEndButton(const Rect &row, bool to)
	{
		bool rtl = _current_text_dir == TD_RTL;
		Rect buttons = row.WithWidth(2 * ScaleGUITrad(40) + ScaleGUITrad(4), !rtl).Shrink(0, ScaleGUITrad(4));
		return buttons.WithWidth(ScaleGUITrad(40), to != rtl);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		if (widget != WID_MWL_LIST) return;
		fill.height = resize.height = 1;
		size.height = 4 * ScaleGUITrad(48);
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_MWL_LIST);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MWL_LIST) return;
		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
		int line = GetCharacterHeight(FontSize::Normal);
		const TimerGameEconomy::YearMonthDay ymd = TimerGameEconomy::ConvertDateToYMD(TimerGameEconomy::date);
		int y = tr.top - this->vscroll->GetPosition();
		for (const Row &row : GetRows()) {
			int h = GetRowHeight(row);
			if (y + h <= r.top) {
				y += h;
				continue;
			}
			if (y >= r.bottom) break;
			Rect rr = tr.WithY(y, y + h - 1);
			if (row.subsidy == nullptr) {
				DrawString(rr.left, rr.right, CentreBounds(rr.top, rr.bottom, line), row.header, TextColour::Black);
			} else {
				const Subsidy *s = row.subsidy;
				for (bool to : {false, true}) {
					Rect button = GetEndButton(rr, to);
					DrawFrameRect(button, Colours::Brown, {});
					DrawString(button.left, button.right, CentreBounds(button.top, button.bottom, line), to ? STR_MOBILE_SUBSIDY_TO : STR_MOBILE_SUBSIDY_FROM, TextColour::White, AlignmentH::Centre);
				}
				Rect text = rr.Indent(2 * ScaleGUITrad(40) + ScaleGUITrad(4) + WidgetDimensions::scaled.hsep_wide, _current_text_dir != TD_RTL);
				int ty = CentreBounds(rr.top, rr.bottom, 2 * line);
				const CargoSpec *cs = CargoSpec::Get(s->cargo_type);
				DrawString(text.left, text.right, ty, GetString(STR_MOBILE_SUBSIDY_ROUTE, cs->name, s->src.GetFormat(), s->src.id, s->dst.GetFormat(), s->dst.id), TextColour::Black);
				std::string when;
				if (TimerGameEconomy::UsingWallclockUnits()) {
					when = s->IsAwarded() ? GetString(STR_MOBILE_SUBSIDY_AWARDED_TO_TIME, s->awarded, s->remaining + 1) : GetString(STR_MOBILE_SUBSIDY_OFFER_ENDS_TIME, s->remaining + 1);
				} else {
					auto end = TimerGameEconomy::date.base() - ymd.day + s->remaining * 32;
					when = s->IsAwarded() ? GetString(STR_MOBILE_SUBSIDY_AWARDED_TO, s->awarded, end) : GetString(STR_MOBILE_SUBSIDY_OFFER_ENDS, end);
				}
				DrawString(text.left, text.right, ty + line, when, TextColour::Black);
			}
			GfxFillRect(r.left, rr.bottom, r.right, rr.bottom, GetColourGradient(Colours::Brown, Shade::Dark));
			y += h;
		}
	}

	void OnClick(Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget != WID_MWL_LIST) return;
		Rect tr = this->GetWidget<NWidgetBase>(WID_MWL_LIST)->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
		int y = tr.top - this->vscroll->GetPosition();
		for (const Row &row : GetRows()) {
			int h = GetRowHeight(row);
			if (pt.y >= y && pt.y < y + h) {
				if (row.subsidy == nullptr) break;
				Rect rr = tr.WithY(y, y + h - 1);
				for (bool to : {false, true}) {
					if (!GetEndButton(rr, to).Contains(pt)) continue;
					TileIndex tile = GetEndTile(to ? row.subsidy->dst : row.subsidy->src);
					if (tile != INVALID_TILE) ScrollMainWindowToTile(tile);
				}
				break;
			}
			y += h;
		}
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->UpdateCount();
		this->SetDirty();
	}
};

/* In the World hub, whose tabs are the title. */
static constexpr std::initializer_list<NWidgetPart> _nested_mobile_subsidies_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Brown, WID_MWL_LIST), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_MWL_SCROLL), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Brown, WID_MWL_SCROLL),
	EndContainer(),
};

static WindowDesc _mobile_subsidies_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::SubsidyList, WindowClass::None,
	{},
	_nested_mobile_subsidies_widgets
);
void ShowSubsidiesList()
{
	if (IsMobileLayout()) {
		if (BringWindowToFrontById(WindowClass::SubsidyList, 0) == nullptr) new MobileSubsidiesWindow(_mobile_subsidies_desc);
		return;
	}
	AllocateWindowDescFront<SubsidyListWindow>(_subsidies_list_desc, 0);
}
