/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file mobile_settings_gui.cpp Settings in the mobile layout: a few sections of the most-used settings with finger-sized
 * controls, a search over every setting of the desktop tree, and the desktop windows for the rest.
 * See the round 4 design, section 8.
 */

#include "stdafx.h"
#include "mobile_settings_gui.h"
#include "mobile_gui.h"
#include "window_gui.h"
#include "window_func.h"
#include "querystring_gui.h"
#include "strings_func.h"
#include "string_func.h"
#include "stringfilter_type.h"
#include "gfx_func.h"
#include "zoom_func.h"
#include "settings_internal.h"
#include "settingentry_gui.h"
#include "settings_gui.h"
#include "settings_type.h"
#include "dropdown_type.h"
#include "dropdown_func.h"
#include "newgrf_config.h"
#include "ai/ai_gui.hpp"
#include "newgrf.h"
#include "gui.h"
#include "openttd.h"
#include "network/network.h"
#include "video/video_driver.hpp"
#include "widgets/mobile_widget.h"

#include "table/strings.h"

#include "safeguards.h"

static constexpr int SETTING_ROW = 56; ///< Height of a setting row, in GUI pixels at 1x.
static constexpr int SECTION_ROW = 40; ///< Height of a section row, in GUI pixels at 1x.

/** What a section of the settings shows or does. */
struct MobileSettingsSection {
	StringID name; ///< Its name.
	std::vector<std::string_view> settings; ///< The settings it lists; empty for a section that opens a window.
	void (*open)(); ///< Window it opens instead, if any.
};

static void OpenGameOptions() { ShowGameOptions(); }
static void OpenNewGRFs() { ShowNewGRFSettings(!_networking && _settings_client.gui.UserIsAllowedToChangeNewGRFs(), true, true, _grfconfig); }
static void OpenScripts() { ShowAIConfigWindow(); }
static void OpenAllSettings() { ShowGameSettings(); }

/** The sections; see the round 4 design, section 8. */
static const MobileSettingsSection _mobile_settings_sections[] = {
	{STR_MOBILE_SETTINGS_PHONE, {"gui.mobile_layout", "gui.mobile_confirm_build", "gui.mobile_build_picker", "gui.mobile_pause_in_background", "gui.mobile_show_navigation_bar"}, nullptr},
	{STR_MOBILE_SETTINGS_DISPLAY, {}, OpenGameOptions},
	{STR_MOBILE_SETTINGS_BUILDING, {"gui.persistent_buildingtools", "construction.autoslope", "station.modified_catchment", "station.distant_join_stations",
			"station.station_spread", "gui.new_nonstop", "gui.stop_location", "vehicle.train_acceleration_model", "vehicle.max_trains",
			"vehicle.never_expire_vehicles", "difficulty.vehicle_breakdowns", "vehicle.servint_trains"}, nullptr},
	{STR_MOBILE_SETTINGS_ECONOMY, {"difficulty.max_loan", "difficulty.initial_interest", "difficulty.vehicle_costs", "difficulty.construction_cost",
			"economy.inflation", "difficulty.economy", "economy.town_growth_rate", "economy.found_town", "difficulty.disasters",
			"difficulty.subsidy_multiplier", "economy.bribe", "economy.exclusive_rights"}, nullptr},
	{STR_MOBILE_SETTINGS_NEWGRF, {}, OpenNewGRFs},
	{STR_MOBILE_SETTINGS_SCRIPTS, {}, OpenScripts},
	{STR_MOBILE_SETTINGS_ALL, {}, OpenAllSettings},
};

/** A setting as a row: the setting and, for search results, the page it is on. */
struct MobileSettingRow {
	const IntSettingDesc *sd; ///< The setting.
	StringID page = STR_NULL; ///< Page of the desktop tree it is on (search results).
};

/**
 * Collect every setting of the desktop tree with the page it is on.
 * @param container A page or the tree.
 * @param page Title of that page.
 * @param[out] out The settings.
 */
static void CollectSettings(const SettingsContainer &container, StringID page, std::vector<MobileSettingRow> &out)
{
	for (const auto &entry : container.entries) {
		if (const SettingsPage *sub = dynamic_cast<const SettingsPage *>(entry); sub != nullptr) {
			CollectSettings(*sub, sub->title, out);
		} else if (const SettingEntry *se = dynamic_cast<const SettingEntry *>(entry); se != nullptr && se->setting != nullptr) {
			out.push_back({se->setting, page});
		}
	}
}

/** The settings window of the mobile layout. */
struct MobileSettingsWindow : Window {
	bool landscape; ///< Sections and settings side by side, rather than one pane at a time.
	int section = 0; ///< Section shown.
	bool showing_settings = false; ///< Portrait: the settings of the section are shown rather than the sections.
	std::vector<MobileSettingRow> rows; ///< Settings listed.
	QueryString search; ///< Search text.
	Scrollbar *vscroll = nullptr;

	MobileSettingsWindow(WindowDesc &desc, bool landscape) : Window(desc), landscape(landscape), search(64)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_MSET_SCROLL);
		this->querystrings[WID_MSET_SEARCH] = &this->search;
		this->search.cancel_button = QueryString::ACTION_CLEAR;
		this->FinishInitNested(0);
		GetSettingsTree().Init();
		this->BuildRows();
	}

	/** @return The settings object being shown: the new-game settings at the main menu, else the game's. */
	static GameSettings *GetSettingsObject()
	{
		return _game_mode == GameMode::Menu ? &_settings_newgame : &GetGameSettings();
	}

	/** Fill the rows from the section or the search. */
	void BuildRows()
	{
		this->rows.clear();
		std::string_view text = this->search.text.GetText();
		if (!text.empty()) {
			/* Search every setting's name and help text. */
			std::vector<MobileSettingRow> all;
			CollectSettings(GetSettingsTree(), STR_NULL, all);
			StringFilter filter;
			filter.SetFilterTerm(text);
			for (const MobileSettingRow &r : all) {
				filter.ResetState();
				filter.AddLine(this->GetSettingName(r.sd));
				filter.AddLine(GetString(r.sd->GetHelp()));
				if (filter.GetState()) this->rows.push_back(r);
			}
		} else {
			for (std::string_view name : _mobile_settings_sections[this->section].settings) {
				const SettingDesc *sd = GetSettingFromName(name);
				if (sd == nullptr || !sd->IsIntSetting()) continue;
				const IntSettingDesc *isd = sd->AsIntSetting();
				if (isd->str == STR_NULL || isd->str == INVALID_STRING_ID) continue;
				this->rows.push_back({isd, STR_NULL});
			}
		}
		this->vscroll->SetCount(this->rows.size());
		this->vscroll->SetPosition(0);
		this->UpdatePanes();
	}

	/** Portrait: show the sections or the settings. */
	void UpdatePanes()
	{
		if (this->landscape) return;
		bool settings = this->showing_settings || !std::string_view(this->search.text.GetText()).empty();
		NWidgetStacked *sel = this->GetWidget<NWidgetStacked>(WID_MSET_SEL_PANE);
		int plane = settings ? 1 : 0;
		if (sel->shown_plane != plane) {
			sel->SetDisplayedPlane(plane);
			this->ReInit();
		}
	}

	/**
	 * The name of a setting, without its value.
	 * @param sd The setting.
	 * @return The name.
	 */
	std::string GetSettingName(const IntSettingDesc *sd) const
	{
		/* The title strings end with ": {value}"; format with an empty value and drop that. */
		std::string title = GetString(sd->GetTitle(), STR_EMPTY, 0, 0);
		while (!title.empty() && (title.back() == ' ' || title.back() == ':')) title.pop_back();
		return title;
	}

	/**
	 * The control of a row: where it is.
	 * @param row The row.
	 * @return The control's area.
	 */
	static Rect GetControl(const Rect &row)
	{
		return row.WithWidth(ScaleGUITrad(130), _current_text_dir != TD_RTL).Shrink(0, ScaleGUITrad(12));
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_MSET_LIST:
				fill.height = resize.height = 1;
				size.height = 3 * ScaleGUITrad(SETTING_ROW);
				break;
			case WID_MSET_SECTIONS:
				size.width = ScaleGUITrad(220);
				size.height = static_cast<uint>(std::size(_mobile_settings_sections)) * ScaleGUITrad(SECTION_ROW);
				fill.height = resize.height = 1;
				break;
			case WID_MSET_TITLE:
				size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal));
				break;
		}
	}

	void OnResize() override
	{
		const NWidgetBase *nwid = this->GetWidget<NWidgetBase>(WID_MSET_LIST);
		this->vscroll->SetCapacity(std::max(1, static_cast<int>(nwid->current_y) / ScaleGUITrad(SETTING_ROW)));
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		int line = GetCharacterHeight(FontSize::Normal);
		bool rtl = _current_text_dir == TD_RTL;
		switch (widget) {
			case WID_MSET_TITLE:
				DrawMobileTitle(r, GetString(STR_MOBILE_SETTINGS_TITLE));
				break;

			case WID_MSET_SECTIONS: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
				for (size_t i = 0; i < std::size(_mobile_settings_sections); i++) {
					Rect rr = tr.WithHeight(ScaleGUITrad(SECTION_ROW));
					bool current = static_cast<int>(i) == this->section && this->search.text.GetText().empty() && _mobile_settings_sections[i].open == nullptr;
					if (current && this->landscape) DrawFrameRect(rr, Colours::Grey, FrameFlag::Lowered);
					Rect text = rr.Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
					DrawString(text.left, text.right, CentreBounds(rr.top, rr.bottom, line), _mobile_settings_sections[i].name, TextColour::Black);
					DrawString(text.left, text.right, CentreBounds(rr.top, rr.bottom, line), STR_JUST_RIGHT_ARROW, TextColour::Black, AlignmentH::End);
					tr.top += ScaleGUITrad(SECTION_ROW);
				}
				break;
			}

			case WID_MSET_LIST: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
				if (this->rows.empty()) {
					DrawString(tr, STR_MOBILE_SETTINGS_NONE, TextColour::Black);
					break;
				}
				const GameSettings *settings = GetSettingsObject();
				int small = GetCharacterHeight(FontSize::Small);
				auto [first, last] = this->vscroll->GetVisibleRangeIterators(this->rows);
				for (auto it = first; it != last; ++it) {
					const IntSettingDesc *sd = it->sd;
					Rect rr = tr.WithHeight(ScaleGUITrad(SETTING_ROW));
					bool editable = sd->IsEditable();
					int32_t value = sd->Read(ResolveObject(settings, sd));

					/* The control. */
					Rect control = GetControl(rr);
					TextColour tc = editable ? TextColour::Black : TextColour::Grey;
					if (sd->IsBoolSetting()) {
						Rect off = control.WithWidth(control.Width() / 2, rtl);
						Rect on = control.Indent(off.Width(), rtl);
						DrawFrameRect(off, value == 0 ? Colours::Grey : Colours::Grey, value == 0 ? FrameFlag::Lowered : FrameFlags{});
						DrawFrameRect(on, value != 0 ? Colours::Green : Colours::Grey, value != 0 ? FrameFlag::Lowered : FrameFlags{});
						DrawString(off.left, off.right, CentreBounds(off.top, off.bottom, line), STR_MOBILE_OFF, tc, AlignmentH::Centre);
						DrawString(on.left, on.right, CentreBounds(on.top, on.bottom, line), STR_MOBILE_ON, value != 0 && editable ? TextColour::White : tc, AlignmentH::Centre);
					} else {
						auto [param1, param2] = sd->GetValueParams(value);
						std::string text = GetString(STR_JUST_STRING1, param1, param2);
						if (sd->flags.Test(SettingFlag::GuiDropdown)) {
							DrawFrameRect(control, Colours::Grey, {});
							DrawString(control.left + ScaleGUITrad(4), control.right - ScaleGUITrad(4), CentreBounds(control.top, control.bottom, line), text, tc, AlignmentH::Centre);
						} else {
							int bw = ScaleGUITrad(32);
							Rect minus = control.WithWidth(bw, rtl);
							Rect plus = control.WithWidth(bw, !rtl);
							DrawFrameRect(minus, Colours::Grey, {});
							DrawFrameRect(plus, Colours::Grey, {});
							DrawString(minus.left, minus.right, CentreBounds(minus.top, minus.bottom, line), STR_MOBILE_MINUS, tc, AlignmentH::Centre);
							DrawString(plus.left, plus.right, CentreBounds(plus.top, plus.bottom, line), STR_MOBILE_PLUS, tc, AlignmentH::Centre);
							Rect val = control.Indent(bw, rtl).Indent(bw, !rtl);
							DrawString(val.left, val.right, CentreBounds(val.top, val.bottom, small), text, tc, AlignmentH::Centre, false, FontSize::Small);
						}
					}

					/* Name and help. */
					Rect text = rr.Indent(control.Width() + WidgetDimensions::scaled.hsep_wide, !rtl);
					int y = rr.top + ScaleGUITrad(4);
					if (it->page != STR_NULL) {
						DrawString(text.left, text.right, y, it->page, TextColour::Grey, AlignmentH::Start, false, FontSize::Small);
						y += small;
					}
					DrawString(text.left, text.right, y, this->GetSettingName(sd), tc);
					y += line;
					std::string help = editable ? GetString(sd->GetHelp()) : GetString(STR_MOBILE_SETTINGS_ONLY_BEFORE_START);
					DrawStringMultiLineWithClipping(text.left, text.right, y, rr.bottom - ScaleGUITrad(2), help, TextColour::Black, AlignmentH::Start, false, FontSize::Small);
					GfxFillRect(tr.left, rr.bottom, tr.right, rr.bottom, GetColourGradient(Colours::Grey, Shade::Dark));
					tr.top += ScaleGUITrad(SETTING_ROW);
				}
				break;
			}
		}
	}

	const IntSettingDesc *dropdown_setting = nullptr; ///< Setting whose dropdown is open.

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MSET_BACK:
				if (!this->landscape && (this->showing_settings || !std::string_view(this->search.text.GetText()).empty())) {
					this->showing_settings = false;
					this->search.text.DeleteAll();
					this->BuildRows();
					this->SetDirty();
				} else {
					this->Close();
				}
				break;

			case WID_MSET_SECTIONS: {
				int index = (pt.y - this->GetWidget<NWidgetBase>(WID_MSET_SECTIONS)->pos_y - WidgetDimensions::scaled.framerect.top) / ScaleGUITrad(SECTION_ROW);
				if (index < 0 || index >= static_cast<int>(std::size(_mobile_settings_sections))) break;
				const MobileSettingsSection &s = _mobile_settings_sections[index];
				if (s.open != nullptr) {
					s.open();
					break;
				}
				this->section = index;
				this->showing_settings = true;
				this->search.text.DeleteAll();
				this->BuildRows();
				this->SetDirty();
				break;
			}

			case WID_MSET_LIST: {
				auto it = this->vscroll->GetScrolledItemFromWidget(this->rows, pt.y, this, WID_MSET_LIST, WidgetDimensions::scaled.framerect.top, ScaleGUITrad(SETTING_ROW));
				if (it == this->rows.end()) break;
				const IntSettingDesc *sd = it->sd;
				if (!sd->IsEditable()) break;
				const NWidgetBase *nwid = this->GetWidget<NWidgetBase>(WID_MSET_LIST);
				Rect list = nwid->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
				Rect control = GetControl(list);
				if (pt.x < control.left || pt.x > control.right) break;

				GameSettings *settings = GetSettingsObject();
				int32_t value = sd->Read(ResolveObject(settings, sd));
				auto [min_val, max_val] = sd->GetRange();
				bool rtl = _current_text_dir == TD_RTL;
				if (sd->IsBoolSetting()) {
					bool on_side = (pt.x >= control.left + control.Width() / 2) != rtl;
					value = on_side ? 1 : 0;
				} else if (sd->flags.Test(SettingFlag::GuiDropdown)) {
					this->dropdown_setting = sd;
					DropDownList dd;
					for (int32_t i = min_val; i <= static_cast<int32_t>(max_val); i++) {
						auto [p1, p2] = sd->GetValueParams(i);
						dd.push_back(MakeDropDownListStringItem(GetString(STR_JUST_STRING1, p1, p2), i));
					}
					ShowDropDownList(this, std::move(dd), value, WID_MSET_LIST);
					return;
				} else {
					uint32_t step = (sd->interval == 0) ? ((max_val - min_val) / 50) : sd->interval;
					if (step == 0) step = 1;
					bool plus = (pt.x >= control.left + control.Width() / 2) != rtl;
					value = plus ? std::min<int64_t>(static_cast<int64_t>(value) + step, max_val) : std::max<int64_t>(static_cast<int64_t>(value) - step, sd->flags.Test(SettingFlag::GuiZeroIsSpecial) ? 0 : min_val);
				}
				SetSettingValue(sd, value, _game_mode == GameMode::Menu);
				this->SetDirty();
				break;
			}
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		if (widget != WID_MSET_LIST || this->dropdown_setting == nullptr) return;
		SetSettingValue(this->dropdown_setting, index, _game_mode == GameMode::Menu);
		this->dropdown_setting = nullptr;
		this->SetDirty();
	}

	void OnEditboxChanged(WidgetID wid) override
	{
		if (wid != WID_MSET_SEARCH) return;
		this->BuildRows();
		this->SetDirty();
	}
};

#define MOBILE_SETTINGS_HEADER \
	NWidget(NWID_HORIZONTAL), \
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MSET_BACK), SetMinimalSize(48, 48), SetStringTip(STR_MOBILE_BACK, STR_MOBILE_BACK_TOOLTIP), \
		NWidget(WWT_PANEL, Colours::Grey, WID_MSET_TITLE), SetMinimalSize(120, 48), EndContainer(), \
		NWidget(WWT_PANEL, Colours::Grey), SetFill(1, 0), SetResize(1, 0), \
			NWidget(WWT_EDITBOX, Colours::Grey, WID_MSET_SEARCH), SetMinimalSize(100, 32), SetPadding(8, 4, 8, 4), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_SETTINGS_SEARCH, STR_MOBILE_SETTINGS_SEARCH), \
		EndContainer(), \
	EndContainer()

#define MOBILE_SETTINGS_LIST \
	NWidget(NWID_HORIZONTAL), \
		NWidget(WWT_PANEL, Colours::Grey, WID_MSET_LIST), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_MSET_SCROLL), EndContainer(), \
		NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_MSET_SCROLL), \
	EndContainer()

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_settings_landscape_widgets = {
	MOBILE_SETTINGS_HEADER,
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_MSET_SECTIONS), SetFill(0, 1), SetResize(0, 1), EndContainer(),
		NWidget(NWID_SELECTION, Colours::Invalid, WID_MSET_SEL_PANE),
			MOBILE_SETTINGS_LIST,
		EndContainer(),
	EndContainer(),
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_settings_portrait_widgets = {
	MOBILE_SETTINGS_HEADER,
	NWidget(NWID_SELECTION, Colours::Invalid, WID_MSET_SEL_PANE),
		NWidget(WWT_PANEL, Colours::Grey, WID_MSET_SECTIONS), SetFill(1, 1), SetResize(1, 1), EndContainer(),
		MOBILE_SETTINGS_LIST,
	EndContainer(),
};

#undef MOBILE_SETTINGS_LIST
#undef MOBILE_SETTINGS_HEADER

static WindowDesc _mobile_settings_landscape_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileSettings, WindowClass::None,
	{},
	_nested_mobile_settings_landscape_widgets
);

static WindowDesc _mobile_settings_portrait_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileSettings, WindowClass::None,
	{},
	_nested_mobile_settings_portrait_widgets
);

/** Show the mobile settings over the whole safe area. */
void ShowMobileSettings()
{
	CloseWindowByClass(WindowClass::MobileSettings);
	bool landscape = _screen.width > _screen.height;
	Window *w = new MobileSettingsWindow(landscape ? _mobile_settings_landscape_desc : _mobile_settings_portrait_desc, landscape);
	Rect safe = VideoDriver::GetInstance() != nullptr ? VideoDriver::GetInstance()->GetSafeArea() : Rect{0, 0, _screen.width - 1, _screen.height - 1};
	ResizeWindow(w, std::max(0, safe.Width() - w->width), std::max(0, safe.Height() - w->height), false);
	w->left = safe.left;
	w->top = safe.top;
	w->SetDirty();
}
