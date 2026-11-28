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
#include "network/network_content.h"
#include "language.h"
#include "sound_func.h"
#include "core/math_func.hpp"
#include "video/video_driver.hpp"
#include "widgets/mobile_widget.h"

#include "table/strings.h"

#include "safeguards.h"

static constexpr int SETTING_ROW = 56; ///< Height of a setting row, in GUI pixels at 1x.
static constexpr int SECTION_ROW = 40; ///< Height of a section row, in GUI pixels at 1x.

/** What a section of the settings shows or does. */
struct MobileSettingsSection {
	StringID name; ///< Its name.
	std::vector<std::string_view> settings; ///< The settings (or options, see \c MobileOption) it lists; empty for a section that opens a window.
	void (*open)(); ///< Window it opens instead, if any.
};

static void OpenNewGRFs() { ShowNewGRFSettings(!_networking && _settings_client.gui.UserIsAllowedToChangeNewGRFs(), true, true, _grfconfig); }
static void OpenScripts() { ShowAIConfigWindow(); }
static void OpenOnlineContent() { ShowNetworkContentListWindow(); }
static void OpenBaseGraphicsContent() { ShowNetworkContentListWindow(nullptr, ContentType::BaseGraphics); }
static void OpenAllSettings() { ShowGameSettings(); }

/** A row that isn't a setting of the tree: a Game Options control, or a link to a window (round 5 design, batch 2). */
struct MobileOption {
	/** How the row is changed. */
	enum class Kind : uint8_t {
		Dropdown, ///< A touch dropdown.
		Stepper, ///< − value +.
		Link, ///< The row opens a window.
	};
	std::string_view key; ///< Name used in the section lists.
	Kind kind; ///< How it is changed.
	StringID name; ///< Its name.
	std::string (*help)(); ///< Its help text.
	std::string (*value)() = nullptr; ///< Current value (dropdown, stepper).
	DropDownList (*list)(int *selected) = nullptr; ///< Dropdown entries.
	void (*select)(int index) = nullptr; ///< Dropdown choice.
	void (*step)(bool plus) = nullptr; ///< Stepper.
	void (*open)() = nullptr; ///< Link.
	bool (*editable)() = nullptr; ///< Whether it can be changed now; always when \c nullptr.
};

/** Phones get interface sizes from 250% to 400%; larger screens the full range. */
static bool IsPhoneScreen()
{
	return std::min(_screen.width, _screen.height) < 1600;
}

static DropDownList BuildGUIScaleList(int *selected)
{
	DropDownList list;
	int lowest = IsPhoneScreen() ? 250 : MIN_INTERFACE_SCALE;
	int highest = std::min(IsPhoneScreen() ? 400 : MAX_INTERFACE_SCALE, GetMaximumScale());
	*selected = _gui_scale;
	for (int scale = lowest; scale <= highest; scale += 25) list.push_back(MakeDropDownListStringItem(GetString(STR_MOBILE_PERCENT, scale), scale));
	return list;
}

/**
 * Volume in percent.
 * @param vol Volume, 0 to 127.
 * @return The percentage, rounded.
 */
static int VolumeToPercent(uint8_t vol)
{
	return (vol * 100 + INT8_MAX / 2) / INT8_MAX;
}

/**
 * Change a volume by 10%.
 * @param music The music volume, else the sound effects volume.
 * @param plus Louder, else quieter.
 */
static void StepVolume(bool music, bool plus)
{
	int percent = VolumeToPercent(music ? _settings_client.music.music_vol : _settings_client.music.effect_vol);
	percent = Clamp((percent / 10 + (plus ? 1 : (percent % 10 == 0 ? -1 : 0))) * 10, 0, 100);
	ChangeVolume(music, static_cast<uint8_t>((percent * INT8_MAX + 50) / 100));
}

/** Help of the base graphics row: the description, plus how many sprites the set is missing. */
static std::string GetBaseGraphicsHelp()
{
	std::string help = GetString(STR_GAME_OPTIONS_BASE_GRF_TOOLTIP);
	if (_missing_extra_graphics > 0) help += " " + GetString(STR_INTRO_BASESET, _missing_extra_graphics);
	return help;
}

/** The options; sections refer to them by key. */
static const MobileOption _mobile_options[] = {
	{"opt.gui_scale", MobileOption::Kind::Dropdown, STR_GAME_OPTIONS_GUI_SCALE_FRAME, []() { return GetString(STR_MOBILE_SETTINGS_GUI_SCALE_HELP); },
		[]() { return GetString(STR_MOBILE_PERCENT, _gui_scale); }, BuildGUIScaleList, ChangeGUIScale},
	{"opt.base_graphics", MobileOption::Kind::Dropdown, STR_GAME_OPTIONS_BASE_GRF, GetBaseGraphicsHelp,
		[]() { return GetBaseSetLabel(BaseSetType::Graphics); }, [](int *selected) { return BuildBaseSetDropDownList(BaseSetType::Graphics, selected); },
		[](int index) { if (ChangeBaseSet(BaseSetType::Graphics, index)) _switch_mode = SwitchMode::Menu; },
		nullptr, nullptr, []() { return _game_mode == GameMode::Menu; }},
	{"opt.base_graphics_updates", MobileOption::Kind::Link, STR_MOBILE_SETTINGS_CHECK_UPDATES, []() { return GetString(STR_MOBILE_SETTINGS_CHECK_UPDATES_HELP); },
		nullptr, nullptr, nullptr, nullptr, OpenBaseGraphicsContent},
	{"opt.base_sounds", MobileOption::Kind::Dropdown, STR_GAME_OPTIONS_BASE_SFX, []() { return GetString(STR_GAME_OPTIONS_BASE_SFX_TOOLTIP); },
		[]() { return GetBaseSetLabel(BaseSetType::Sounds); }, [](int *selected) { return BuildBaseSetDropDownList(BaseSetType::Sounds, selected); },
		[](int index) { ChangeBaseSet(BaseSetType::Sounds, index); }},
	{"opt.base_music", MobileOption::Kind::Dropdown, STR_GAME_OPTIONS_BASE_MUSIC, []() { return GetString(STR_GAME_OPTIONS_BASE_MUSIC_TOOLTIP); },
		[]() { return GetBaseSetLabel(BaseSetType::Music); }, [](int *selected) { return BuildBaseSetDropDownList(BaseSetType::Music, selected); },
		[](int index) { ChangeBaseSet(BaseSetType::Music, index); }},
	{"opt.music_volume", MobileOption::Kind::Stepper, STR_MOBILE_SETTINGS_MUSIC_VOLUME, []() { return GetString(STR_MOBILE_SETTINGS_VOLUME_HELP); },
		[]() { return GetString(STR_MOBILE_PERCENT, VolumeToPercent(_settings_client.music.music_vol)); }, nullptr, nullptr,
		[](bool plus) { StepVolume(true, plus); }},
	{"opt.effect_volume", MobileOption::Kind::Stepper, STR_MOBILE_SETTINGS_EFFECT_VOLUME, []() { return GetString(STR_MOBILE_SETTINGS_VOLUME_HELP); },
		[]() { return GetString(STR_MOBILE_PERCENT, VolumeToPercent(_settings_client.music.effect_vol)); }, nullptr, nullptr,
		[](bool plus) { StepVolume(false, plus); }},
	{"opt.language", MobileOption::Kind::Dropdown, STR_GAME_OPTIONS_LANGUAGE, []() { return GetString(STR_GAME_OPTIONS_LANGUAGE_TOOLTIP); },
		[]() { return std::string{_current_language->own_name}; }, BuildLanguageDropDownList, ChangeLanguage},
	{"opt.currency", MobileOption::Kind::Dropdown, STR_GAME_OPTIONS_CURRENCY_UNITS_FRAME, []() { return GetString(STR_GAME_OPTIONS_CURRENCY_UNITS_DROPDOWN_TOOLTIP); },
		[]() { return GetCurrencyLabel(_game_mode == GameMode::Menu ? _settings_newgame : _settings_game); },
		[](int *selected) { return BuildCurrencyDropDownList(_game_mode == GameMode::Menu ? _settings_newgame : _settings_game, selected); },
		[](int index) { ChangeCurrency(_game_mode == GameMode::Menu ? _settings_newgame : _settings_game, index); }},
	{"opt.autosave", MobileOption::Kind::Dropdown, STR_GAME_OPTIONS_AUTOSAVE_FRAME, []() { return GetString(STR_GAME_OPTIONS_AUTOSAVE_DROPDOWN_TOOLTIP); },
		GetAutosaveLabel, BuildAutosaveDropDownList, ChangeAutosaveInterval},
	{"opt.autosaves_kept", MobileOption::Kind::Stepper, STR_MOBILE_SETTINGS_AUTOSAVES_KEPT, []() { return GetString(STR_MOBILE_SETTINGS_AUTOSAVES_KEPT_HELP); },
		[]() { return GetString(STR_JUST_INT, _settings_client.gui.max_num_autosaves); }, nullptr, nullptr,
		[](bool plus) { _settings_client.gui.max_num_autosaves = Clamp(_settings_client.gui.max_num_autosaves + (plus ? 1 : -1), 1, UINT8_MAX); }},
	{"opt.newgrf", MobileOption::Kind::Link, STR_MOBILE_SETTINGS_NEWGRF, []() { return GetString(STR_MOBILE_SETTINGS_NEWGRF_HELP); },
		nullptr, nullptr, nullptr, nullptr, OpenNewGRFs},
	{"opt.scripts", MobileOption::Kind::Link, STR_MOBILE_SETTINGS_SCRIPTS, []() { return GetString(STR_MOBILE_SETTINGS_SCRIPTS_HELP); },
		nullptr, nullptr, nullptr, nullptr, OpenScripts},
	{"opt.content", MobileOption::Kind::Link, STR_MOBILE_SETTINGS_CONTENT, []() { return GetString(STR_MOBILE_SETTINGS_CONTENT_HELP); },
		nullptr, nullptr, nullptr, nullptr, OpenOnlineContent},
};

/**
 * Find an option by its key.
 * @param key The key ("opt....").
 * @return The option, or \c nullptr.
 */
static const MobileOption *GetMobileOption(std::string_view key)
{
	for (const MobileOption &o : _mobile_options) {
		if (o.key == key) return &o;
	}
	return nullptr;
}

/** The sections; see the round 4 design, section 8, and the round 5 design, batch 2. */
static const MobileSettingsSection _mobile_settings_sections[] = {
	{STR_MOBILE_SETTINGS_PHONE, {"gui.mobile_layout", "gui.mobile_confirm_build", "gui.mobile_build_picker", "gui.mobile_pause_in_background", "gui.mobile_show_navigation_bar"}, nullptr},
	{STR_MOBILE_SETTINGS_DISPLAY, {"opt.gui_scale", "opt.base_graphics", "opt.base_graphics_updates", "opt.base_sounds", "opt.base_music", "opt.music_volume", "opt.effect_volume"}, nullptr},
	{STR_MOBILE_SETTINGS_LANGUAGE, {"opt.language", "opt.currency", "locale.units_velocity", "locale.units_velocity_nautical", "locale.units_power", "locale.units_weight",
			"locale.units_volume", "locale.units_force", "locale.units_height"}, nullptr},
	{STR_MOBILE_SETTINGS_SAVING, {"opt.autosave", "opt.autosaves_kept", "gui.autosave_on_exit"}, nullptr},
	{STR_MOBILE_SETTINGS_BUILDING, {"gui.persistent_buildingtools", "construction.autoslope", "station.modified_catchment", "station.distant_join_stations",
			"station.station_spread", "gui.new_nonstop", "gui.stop_location", "vehicle.train_acceleration_model", "vehicle.max_trains",
			"vehicle.never_expire_vehicles", "difficulty.vehicle_breakdowns", "vehicle.servint_trains"}, nullptr},
	{STR_MOBILE_SETTINGS_ECONOMY, {"difficulty.max_loan", "difficulty.initial_interest", "difficulty.vehicle_costs", "difficulty.construction_cost",
			"economy.inflation", "difficulty.economy", "economy.town_growth_rate", "economy.found_town", "difficulty.disasters",
			"difficulty.subsidy_multiplier", "economy.bribe", "economy.exclusive_rights"}, nullptr},
	{STR_MOBILE_SETTINGS_ADDONS, {"opt.newgrf", "opt.scripts", "opt.content"}, nullptr},
	{STR_MOBILE_SETTINGS_ALL, {}, OpenAllSettings},
};

/** A row: a setting or an option, and for search results the page it is on. */
struct MobileSettingRow {
	const IntSettingDesc *sd; ///< The setting, or \c nullptr for an option.
	StringID page = STR_NULL; ///< Page of the desktop tree it is on (search results).
	const MobileOption *option = nullptr; ///< The option, if not a setting.
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
			/* The options, with their section as the page. */
			for (const MobileSettingsSection &section : _mobile_settings_sections) {
				for (std::string_view key : section.settings) {
					if (const MobileOption *o = GetMobileOption(key); o != nullptr) all.push_back({nullptr, section.name, o});
				}
			}
			for (const MobileSettingRow &r : all) {
				filter.ResetState();
				filter.AddLine(this->GetRowName(r));
				filter.AddLine(r.option != nullptr ? r.option->help() : GetString(r.sd->GetHelp()));
				if (filter.GetState()) this->rows.push_back(r);
			}
		} else {
			for (std::string_view name : _mobile_settings_sections[this->section].settings) {
				if (const MobileOption *o = GetMobileOption(name); o != nullptr) {
					this->rows.push_back({nullptr, STR_NULL, o});
					continue;
				}
				const SettingDesc *sd = GetSettingFromName(name);
				if (sd == nullptr || !sd->IsIntSetting()) continue;
				const IntSettingDesc *isd = sd->AsIntSetting();
				if (isd->str == STR_NULL || isd->str == INVALID_STRING_ID) continue;
				this->rows.push_back({isd, STR_NULL});
			}
		}
		this->vscroll->SetCount(this->rows.size());
		this->vscroll->SetPosition(0);
		this->expanded = -1;
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
		/* A few titles have the value in the middle ("stop by default at the {value} of the platform"). */
		for (size_t gap = title.find("  "); gap != std::string::npos; gap = title.find("  ", gap + 5)) title.replace(gap, 2, " ... ");
		return title;
	}

	/**
	 * The name of a row.
	 * @param r The row.
	 * @return The name of its setting or option.
	 */
	std::string GetRowName(const MobileSettingRow &r) const
	{
		return r.option != nullptr ? GetString(r.option->name) : this->GetSettingName(r.sd);
	}

	/**
	 * Can a row be changed now?
	 * @param r The row.
	 * @return \c true when it can.
	 */
	static bool IsRowEditable(const MobileSettingRow &r)
	{
		if (r.option != nullptr) return r.option->editable == nullptr || r.option->editable();
		return r.sd->IsEditable();
	}

	/**
	 * Draw a − value + stepper.
	 * @param control Its area.
	 * @param text The value.
	 * @param tc Colour of the text.
	 */
	static void DrawStepper(const Rect &control, const std::string &text, TextColour tc)
	{
		bool rtl = _current_text_dir == TD_RTL;
		int line = GetCharacterHeight(FontSize::Normal);
		int bw = ScaleGUITrad(32);
		Rect minus = control.WithWidth(bw, rtl);
		Rect plus = control.WithWidth(bw, !rtl);
		DrawFrameRect(minus, Colours::Grey, {});
		DrawFrameRect(plus, Colours::Grey, {});
		DrawString(minus.left, minus.right, CentreBounds(minus.top, minus.bottom, line), STR_MOBILE_MINUS, tc, AlignmentH::Centre);
		DrawString(plus.left, plus.right, CentreBounds(plus.top, plus.bottom, line), STR_MOBILE_PLUS, tc, AlignmentH::Centre);
		Rect val = control.Indent(bw, rtl).Indent(bw, !rtl);
		DrawString(val.left, val.right, CentreBounds(val.top, val.bottom, line), text, tc, AlignmentH::Centre);
	}

	/**
	 * Draw a dropdown control.
	 * @param control Its area.
	 * @param text The value.
	 * @param tc Colour of the text.
	 */
	static void DrawDropdownControl(const Rect &control, const std::string &text, TextColour tc)
	{
		DrawFrameRect(control, Colours::Grey, {});
		DrawString(control.left + ScaleGUITrad(4), control.right - ScaleGUITrad(4), CentreBounds(control.top, control.bottom, GetCharacterHeight(FontSize::Normal)), text, tc, AlignmentH::Centre);
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
			case WID_MSET_SEARCH:
				/* An edit box sizes itself to its text and ignores its minimal size. */
				size.height = std::max<uint>(size.height, ScaleGUITrad(32));
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
				for (size_t i = this->vscroll->GetPosition(); i < this->rows.size() && tr.top < tr.bottom; i++) {
					auto it = this->rows.begin() + i;
					const IntSettingDesc *sd = it->sd;
					Rect rr = tr.WithHeight(this->GetRowHeight(i, tr.Width()));
					bool editable = IsRowEditable(*it);

					/* The control. */
					Rect control = GetControl(rr);
					TextColour tc = editable ? TextColour::Black : TextColour::Grey;
					if (it->option != nullptr) {
						switch (it->option->kind) {
							case MobileOption::Kind::Dropdown: DrawDropdownControl(control, it->option->value(), tc); break;
							case MobileOption::Kind::Stepper: DrawStepper(control, it->option->value(), tc); break;
							case MobileOption::Kind::Link: DrawString(control.left, control.right, CentreBounds(control.top, control.bottom, line), STR_JUST_RIGHT_ARROW, tc, AlignmentH::End); break;
						}
					} else if (int32_t value = sd->Read(ResolveObject(settings, sd)); sd->IsBoolSetting()) {
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
							DrawDropdownControl(control, text, tc);
						} else {
							DrawStepper(control, text, tc);
						}
					}

					/* Name and help. */
					Rect text = rr.Indent(control.Width() + WidgetDimensions::scaled.hsep_wide, !rtl);
					int y = rr.top + ScaleGUITrad(4);
					if (it->page != STR_NULL) {
						DrawString(text.left, text.right, y, it->page, TextColour::Black, AlignmentH::Start, false, FontSize::Small);
						y += small;
					}
					DrawString(text.left, text.right, y, this->GetRowName(*it), tc);
					y += line;
					std::string help = this->GetHelpText(i, text.Width());
					DrawStringMultiLine(text.left, text.right, y, rr.bottom, help, TextColour::Black);
					GfxFillRect(tr.left, rr.bottom, tr.right, rr.bottom, GetColourGradient(Colours::Grey, Shade::Dark));
					tr.top += rr.Height();
				}
				break;
			}
		}
	}

	const IntSettingDesc *dropdown_setting = nullptr; ///< Setting whose dropdown is open.
	const MobileOption *dropdown_option = nullptr; ///< Option whose dropdown is open.
	int expanded = -1; ///< Row showing all of its help text; others show two lines.

	/**
	 * The help text of a row, at most two lines with "..." unless the row is expanded.
	 * @param i The row.
	 * @param width Width of the text.
	 * @return The text.
	 */
	std::string GetHelpText(size_t i, int width) const
	{
		const MobileSettingRow &r = this->rows[i];
		std::string help;
		if (r.option != nullptr) {
			help = r.option->help();
		} else {
			help = r.sd->IsEditable() ? GetString(r.sd->GetHelp()) : GetString(STR_MOBILE_SETTINGS_ONLY_BEFORE_START);
		}
		if (static_cast<int>(i) == this->expanded) return help;
		int two_lines = 2 * GetCharacterHeight(FontSize::Normal);
		if (GetStringHeight(help, width) <= two_lines) return help;
		/* Drop words until it fits two lines with the ellipsis. */
		while (!help.empty() && GetStringHeight(help + "...", width) > two_lines) {
			size_t space = help.find_last_of(' ');
			help.resize(space == std::string::npos ? 0 : space);
		}
		return help + "...";
	}

	/**
	 * Height of a row: its name, its help (two lines, or all when expanded), and the page for search results.
	 * @param i The row.
	 * @param width Width of the list.
	 * @return The height.
	 */
	int GetRowHeight(size_t i, int width) const
	{
		int text_width = width - ScaleGUITrad(130) - WidgetDimensions::scaled.hsep_wide;
		int h = ScaleGUITrad(8) + GetCharacterHeight(FontSize::Normal) + GetStringHeight(this->GetHelpText(i, text_width), text_width);
		if (this->rows[i].page != STR_NULL) h += GetCharacterHeight(FontSize::Small);
		return std::max(h, ScaleGUITrad(SETTING_ROW));
	}

	/**
	 * The row at a point of the list.
	 * @param y Window y.
	 * @param[out] row_top Top of the row (window coordinates).
	 * @return The row, or -1.
	 */
	int GetRowAt(int y, int &row_top) const
	{
		const NWidgetBase *nwid = this->GetWidget<NWidgetBase>(WID_MSET_LIST);
		Rect tr = nwid->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
		int top = tr.top;
		for (size_t i = this->vscroll->GetPosition(); i < this->rows.size(); i++) {
			int h = this->GetRowHeight(i, tr.Width());
			if (y >= top && y < top + h) {
				row_top = top;
				return static_cast<int>(i);
			}
			top += h;
		}
		return -1;
	}

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
				int row_top = 0;
				int index = this->GetRowAt(pt.y, row_top);
				if (index < 0) break;
				const MobileSettingRow &row = this->rows[index];
				const IntSettingDesc *sd = row.sd;
				const NWidgetBase *nwid = this->GetWidget<NWidgetBase>(WID_MSET_LIST);
				Rect list = nwid->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
				Rect control = GetControl(list.WithY(row_top, row_top + ScaleGUITrad(SETTING_ROW) - 1));
				if (row.option != nullptr && row.option->kind == MobileOption::Kind::Link) {
					row.option->open();
					break;
				}
				if (pt.x < control.left || pt.x > control.right) {
					/* A tap on the text shows all of the help, or folds it again. */
					this->expanded = (this->expanded == index) ? -1 : index;
					this->SetWidgetDirty(WID_MSET_LIST);
					break;
				}
				if (!IsRowEditable(row)) break;
				bool rtl = _current_text_dir == TD_RTL;
				if (row.option != nullptr) {
					if (row.option->kind == MobileOption::Kind::Stepper) {
						row.option->step((pt.x >= control.left + control.Width() / 2) != rtl);
						this->SetDirty();
					} else {
						this->dropdown_setting = nullptr;
						this->dropdown_option = row.option;
						int selected = -1;
						DropDownList list = row.option->list(&selected);
						this->ShowRowDropDown(std::move(list), selected, control);
					}
					break;
				}

				GameSettings *settings = GetSettingsObject();
				int32_t value = sd->Read(ResolveObject(settings, sd));
				auto [min_val, max_val] = sd->GetRange();
				if (sd->IsBoolSetting()) {
					bool on_side = (pt.x >= control.left + control.Width() / 2) != rtl;
					value = on_side ? 1 : 0;
				} else if (sd->flags.Test(SettingFlag::GuiDropdown)) {
					this->dropdown_setting = sd;
					this->dropdown_option = nullptr;
					DropDownList dd;
					for (int32_t i = min_val; i <= static_cast<int32_t>(max_val); i++) {
						auto [p1, p2] = sd->GetValueParams(i);
						dd.push_back(MakeDropDownListStringItem(GetString(STR_JUST_STRING1, p1, p2), i));
					}
					this->ShowRowDropDown(std::move(dd), value, control);
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

	/**
	 * Open a row's dropdown at its control, not at the whole list.
	 * @param list The entries.
	 * @param selected The current entry.
	 * @param control The row's control, in window coordinates.
	 */
	void ShowRowDropDown(DropDownList &&list, int selected, const Rect &control)
	{
		SndClickBeep();
		ShowDropDownListAt(this, std::move(list), selected, WID_MSET_LIST, control, Colours::Grey);
	}

	std::string GetMobileDropDownTitle(WidgetID widget) const override
	{
		if (widget != WID_MSET_LIST) return {};
		if (this->dropdown_option != nullptr) return GetString(this->dropdown_option->name);
		if (this->dropdown_setting == nullptr) return {};
		return this->GetSettingName(this->dropdown_setting);
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		if (widget != WID_MSET_LIST) return;
		if (const MobileOption *o = this->dropdown_option; o != nullptr) {
			this->dropdown_option = nullptr;
			/* May re-initialise every window (interface size, language): this one included. */
			o->select(index);
			this->SetDirty();
			return;
		}
		if (this->dropdown_setting == nullptr) return;
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

/**
 * Make the settings fill the safe area.
 * @param w The settings window.
 */
static void FitMobileSettings(Window *w)
{
	Rect safe = VideoDriver::GetInstance() != nullptr ? VideoDriver::GetInstance()->GetSafeArea() : Rect{0, 0, _screen.width - 1, _screen.height - 1};
	ResizeWindow(w, safe.Width() - w->width, safe.Height() - w->height, false);
	w->left = safe.left;
	w->top = safe.top;
	w->SetDirty();
}

/** Show the mobile settings over the whole safe area. */
void ShowMobileSettings()
{
	CloseWindowByClass(WindowClass::MobileSettings);
	bool landscape = _screen.width > _screen.height;
	FitMobileSettings(new MobileSettingsWindow(landscape ? _mobile_settings_landscape_desc : _mobile_settings_portrait_desc, landscape));
}

/** After the screen or the interface size changed: fill the safe area again, in the layout of the new orientation. */
void RelocateMobileSettings()
{
	MobileSettingsWindow *w = dynamic_cast<MobileSettingsWindow *>(FindWindowById(WindowClass::MobileSettings, 0));
	if (w == nullptr) return;
	bool landscape = _screen.width > _screen.height;
	if (w->landscape == landscape) {
		FitMobileSettings(w);
		return;
	}
	/* The other layout: open it on the same section. */
	int section = w->section;
	bool showing_settings = w->showing_settings;
	ShowMobileSettings();
	w = dynamic_cast<MobileSettingsWindow *>(FindWindowById(WindowClass::MobileSettings, 0));
	if (w == nullptr) return;
	w->section = section;
	w->showing_settings = showing_settings;
	w->BuildRows();
	w->SetDirty();
}
