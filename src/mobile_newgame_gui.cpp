/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file mobile_newgame_gui.cpp Starting a new game in the mobile layout: a preset for the rules and a few world
 * options, with the desktop's world generation window as "Advanced options". See the round 4 design, section 6.
 * Everything here only sets the existing new-game settings.
 */

#include "stdafx.h"
#include "mobile_newgame_gui.h"
#include "mobile_gui.h"
#include "window_gui.h"
#include "window_func.h"
#include "strings_func.h"
#include "gfx_func.h"
#include "zoom_func.h"
#include "settings_type.h"
#include "genworld.h"
#include "openttd.h"
#include "economy_func.h"
#include "gui.h"
#include "sound_func.h"
#include "network/network.h"
#include "timer/timer_game_calendar.h"
#include "video/video_driver.hpp"
#include "widgets/mobile_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** Rule presets; see the round 4 design, section 6 (values confirmed by the user, 28 Nov). */
enum class NewGamePreset : uint8_t { Classic, Relaxed, Sandbox };
static NewGamePreset _new_game_preset = NewGamePreset::Classic;

/**
 * Apply a preset to the new-game settings. Classic restores the defaults the other presets change.
 * @param preset The preset.
 */
static void ApplyPreset(NewGamePreset preset)
{
	_new_game_preset = preset;
	DifficultySettings &d = _settings_newgame.difficulty;
	EconomySettings &e = _settings_newgame.economy;
	/* Classic: the defaults of the settings the presets touch. */
	d.vehicle_breakdowns = VehicleBreakdowns::Normal;
	d.disasters = true;
	d.economy = true;
	d.max_loan = 300000;
	d.max_no_competitors = 0;
	e.inflation = false;
	e.town_growth_rate = 2;
	switch (preset) {
		case NewGamePreset::Classic: break;
		case NewGamePreset::Relaxed:
			d.vehicle_breakdowns = VehicleBreakdowns::None;
			d.disasters = false;
			d.economy = false; // Steady.
			e.inflation = false;
			break;
		case NewGamePreset::Sandbox:
			d.max_loan = 2000000;
			d.max_no_competitors = 0;
			e.town_growth_rate = 1; // Slow.
			d.disasters = false;
			d.vehicle_breakdowns = VehicleBreakdowns::None;
			break;
	}
}

/** The New game screen. */
struct MobileNewGameWindow : Window {
	MobileNewGameWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
		this->UpdateButtons();
	}

	/** Lower the buttons that match the current settings. */
	void UpdateButtons()
	{
		const GameCreationSettings &gc = _settings_newgame.game_creation;
		const DifficultySettings &d = _settings_newgame.difficulty;
		for (int i = 0; i < 3; i++) this->SetWidgetLoweredState(WID_MNG_PRESET_FIRST + i, to_underlying(_new_game_preset) == i);
		for (int i = 0; i < 4; i++) this->SetWidgetLoweredState(WID_MNG_CLIMATE_FIRST + i, to_underlying(gc.landscape) == i);
		static constexpr uint8_t sizes[] = {8, 9, 10};
		for (int i = 0; i < 3; i++) this->SetWidgetLoweredState(WID_MNG_SIZE_FIRST + i, gc.map_x == sizes[i] && gc.map_y == sizes[i]);
		static constexpr uint8_t towns[] = {1, 2, 3};
		for (int i = 0; i < 3; i++) this->SetWidgetLoweredState(WID_MNG_TOWNS_FIRST + i, d.number_towns == towns[i]);
		static constexpr IndustryDensity industries[] = {IndustryDensity::Low, IndustryDensity::Normal, IndustryDensity::High};
		for (int i = 0; i < 3; i++) this->SetWidgetLoweredState(WID_MNG_INDUSTRIES_FIRST + i, d.industry_density == industries[i]);
		static constexpr GenworldMaxHeight terrain[] = {GenworldMaxHeight::Flat, GenworldMaxHeight::Hilly, GenworldMaxHeight::Mountainous};
		for (int i = 0; i < 3; i++) this->SetWidgetLoweredState(WID_MNG_TERRAIN_FIRST + i, d.terrain_type == terrain[i]);
		this->SetDirty();
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget == WID_MNG_TITLE) size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal));
		if (widget >= WID_MNG_PRESET_FIRST && widget < WID_MNG_PRESET_FIRST + 3) size.height = std::max<uint>(size.height, 2 * GetCharacterHeight(FontSize::Normal) + padding.height);
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_MNG_YEAR) return GetString(STR_JUST_INT, _settings_newgame.game_creation.starting_year);
		return this->Window::GetWidgetString(widget, stringid);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget == WID_MNG_TITLE) {
			DrawMobileTitle(r, GetString(STR_MOBILE_NEW_GAME_TITLE));
			return;
		}
		if (widget >= WID_MNG_PRESET_FIRST && widget < WID_MNG_PRESET_FIRST + 3) {
			static constexpr StringID titles[] = {STR_MOBILE_NEW_GAME_CLASSIC, STR_MOBILE_NEW_GAME_RELAXED, STR_MOBILE_NEW_GAME_SANDBOX};
			static constexpr StringID lines[] = {STR_MOBILE_NEW_GAME_CLASSIC_LINE, STR_MOBILE_NEW_GAME_RELAXED_LINE, STR_MOBILE_NEW_GAME_SANDBOX_LINE};
			int i = widget - WID_MNG_PRESET_FIRST;
			Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(4), 0);
			int line = GetCharacterHeight(FontSize::Normal);
			int y = CentreBounds(r.top, r.bottom, 2 * line);
			DrawString(tr.left, tr.right, y, titles[i], TextColour::Black);
			DrawString(tr.left, tr.right, y + line, lines[i], TextColour::DarkGreen);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		GameCreationSettings &gc = _settings_newgame.game_creation;
		DifficultySettings &d = _settings_newgame.difficulty;
		if (widget >= WID_MNG_PRESET_FIRST && widget < WID_MNG_PRESET_FIRST + 3) {
			ApplyPreset(static_cast<NewGamePreset>(widget - WID_MNG_PRESET_FIRST));
		} else if (widget >= WID_MNG_CLIMATE_FIRST && widget < WID_MNG_CLIMATE_FIRST + 4) {
			gc.landscape = static_cast<LandscapeType>(widget - WID_MNG_CLIMATE_FIRST);
		} else if (widget >= WID_MNG_SIZE_FIRST && widget < WID_MNG_SIZE_FIRST + 3) {
			gc.map_x = gc.map_y = 8 + (widget - WID_MNG_SIZE_FIRST);
		} else if (widget >= WID_MNG_TOWNS_FIRST && widget < WID_MNG_TOWNS_FIRST + 3) {
			d.number_towns = 1 + (widget - WID_MNG_TOWNS_FIRST);
		} else if (widget >= WID_MNG_INDUSTRIES_FIRST && widget < WID_MNG_INDUSTRIES_FIRST + 3) {
			static constexpr IndustryDensity industries[] = {IndustryDensity::Low, IndustryDensity::Normal, IndustryDensity::High};
			d.industry_density = industries[widget - WID_MNG_INDUSTRIES_FIRST];
		} else if (widget >= WID_MNG_TERRAIN_FIRST && widget < WID_MNG_TERRAIN_FIRST + 3) {
			static constexpr GenworldMaxHeight terrain[] = {GenworldMaxHeight::Flat, GenworldMaxHeight::Hilly, GenworldMaxHeight::Mountainous};
			d.terrain_type = terrain[widget - WID_MNG_TERRAIN_FIRST];
		} else {
			switch (widget) {
				case WID_MNG_BACK: this->Close(); return;
				case WID_MNG_YEAR_DOWN:
					gc.starting_year = Clamp(gc.starting_year - 10, CalendarTime::MIN_YEAR, CalendarTime::MAX_YEAR);
					break;
				case WID_MNG_YEAR_UP:
					gc.starting_year = Clamp(gc.starting_year + 10, CalendarTime::MIN_YEAR, CalendarTime::MAX_YEAR);
					break;
				case WID_MNG_ADVANCED:
					this->Close();
					ShowGenerateLandscape();
					return;
				case WID_MNG_START:
					_is_network_server = false;
					this->Close();
					StartNewGameWithoutGUI(GENERATE_NEW_SEED);
					return;
				default: return;
			}
		}
		SndClickBeep();
		this->UpdateButtons();
	}
};

/** A row of the options: label, then radio buttons. */
#define NEW_GAME_LABEL(string) NWidget(WWT_TEXT, Colours::Invalid), SetMinimalSize(100, 40), SetStringTip(string), SetTextStyle(TextColour::Black)
#define NEW_GAME_LABEL_ABOVE(string) NWidget(WWT_TEXT, Colours::Invalid), SetMinimalSize(0, 20), SetFill(1, 0), SetResize(1, 0), SetStringTip(string), SetTextStyle(TextColour::Black)
#define NEW_GAME_RADIO(widget, string) NWidget(WWT_TEXTBTN, Colours::Brown, widget), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(string)

#define NEW_GAME_OPTIONS \
	NWidget(NWID_VERTICAL), SetPIP(0, 4, 0), \
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, 2, 0), \
			NEW_GAME_LABEL(STR_MOBILE_NEW_GAME_CLIMATE), \
			NEW_GAME_RADIO(WID_MNG_CLIMATE_FIRST + 0, STR_MOBILE_NEW_GAME_TEMPERATE), \
			NEW_GAME_RADIO(WID_MNG_CLIMATE_FIRST + 1, STR_MOBILE_NEW_GAME_ARCTIC), \
			NEW_GAME_RADIO(WID_MNG_CLIMATE_FIRST + 2, STR_MOBILE_NEW_GAME_TROPIC), \
			NEW_GAME_RADIO(WID_MNG_CLIMATE_FIRST + 3, STR_MOBILE_NEW_GAME_TOYLAND), \
		EndContainer(), \
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, 2, 0), \
			NEW_GAME_LABEL(STR_MOBILE_NEW_GAME_MAP_SIZE), \
			NEW_GAME_RADIO(WID_MNG_SIZE_FIRST + 0, STR_MOBILE_NEW_GAME_SMALL), \
			NEW_GAME_RADIO(WID_MNG_SIZE_FIRST + 1, STR_MOBILE_NEW_GAME_MEDIUM), \
			NEW_GAME_RADIO(WID_MNG_SIZE_FIRST + 2, STR_MOBILE_NEW_GAME_LARGE), \
		EndContainer(), \
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, 2, 0), \
			NEW_GAME_LABEL(STR_MOBILE_NEW_GAME_TOWNS), \
			NEW_GAME_RADIO(WID_MNG_TOWNS_FIRST + 0, STR_MOBILE_NEW_GAME_FEW), \
			NEW_GAME_RADIO(WID_MNG_TOWNS_FIRST + 1, STR_MOBILE_NEW_GAME_NORMAL), \
			NEW_GAME_RADIO(WID_MNG_TOWNS_FIRST + 2, STR_MOBILE_NEW_GAME_MANY), \
		EndContainer(), \
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, 2, 0), \
			NEW_GAME_LABEL(STR_MOBILE_NEW_GAME_INDUSTRIES), \
			NEW_GAME_RADIO(WID_MNG_INDUSTRIES_FIRST + 0, STR_MOBILE_NEW_GAME_FEW), \
			NEW_GAME_RADIO(WID_MNG_INDUSTRIES_FIRST + 1, STR_MOBILE_NEW_GAME_NORMAL), \
			NEW_GAME_RADIO(WID_MNG_INDUSTRIES_FIRST + 2, STR_MOBILE_NEW_GAME_MANY), \
		EndContainer(), \
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, 2, 0), \
			NEW_GAME_LABEL(STR_MOBILE_NEW_GAME_TERRAIN), \
			NEW_GAME_RADIO(WID_MNG_TERRAIN_FIRST + 0, STR_MOBILE_NEW_GAME_FLAT), \
			NEW_GAME_RADIO(WID_MNG_TERRAIN_FIRST + 1, STR_MOBILE_NEW_GAME_HILLY), \
			NEW_GAME_RADIO(WID_MNG_TERRAIN_FIRST + 2, STR_MOBILE_NEW_GAME_MOUNTAINS), \
		EndContainer(), \
		NWidget(NWID_HORIZONTAL), SetPIP(0, 2, 0), \
			NEW_GAME_LABEL(STR_MOBILE_NEW_GAME_START_YEAR), \
			NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MNG_YEAR_DOWN), SetMinimalSize(48, 40), SetStringTip(STR_MOBILE_MINUS), \
			NWidget(WWT_TEXT, Colours::Invalid, WID_MNG_YEAR), SetMinimalSize(80, 40), SetTextStyle(TextColour::Black), SetAlignment({AlignmentH::Centre, AlignmentV::Middle}), \
			NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MNG_YEAR_UP), SetMinimalSize(48, 40), SetStringTip(STR_MOBILE_PLUS), \
			NWidget(NWID_SPACER), SetFill(1, 0), SetResize(1, 0), \
			NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MNG_ADVANCED), SetMinimalSize(150, 40), SetStringTip(STR_MOBILE_NEW_GAME_ADVANCED), \
		EndContainer(), \
	EndContainer()

#define NEW_GAME_OPTIONS_PORTRAIT \
	NWidget(NWID_VERTICAL), SetPIP(0, 4, 0), \
		NWidget(NWID_VERTICAL), \
			NEW_GAME_LABEL_ABOVE(STR_MOBILE_NEW_GAME_CLIMATE), \
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, 2, 0), \
			NEW_GAME_RADIO(WID_MNG_CLIMATE_FIRST + 0, STR_MOBILE_NEW_GAME_TEMPERATE), \
			NEW_GAME_RADIO(WID_MNG_CLIMATE_FIRST + 1, STR_MOBILE_NEW_GAME_ARCTIC), \
			NEW_GAME_RADIO(WID_MNG_CLIMATE_FIRST + 2, STR_MOBILE_NEW_GAME_TROPIC), \
			NEW_GAME_RADIO(WID_MNG_CLIMATE_FIRST + 3, STR_MOBILE_NEW_GAME_TOYLAND), \
			EndContainer(), \
		EndContainer(), \
		NWidget(NWID_VERTICAL), \
			NEW_GAME_LABEL_ABOVE(STR_MOBILE_NEW_GAME_MAP_SIZE), \
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, 2, 0), \
			NEW_GAME_RADIO(WID_MNG_SIZE_FIRST + 0, STR_MOBILE_NEW_GAME_SMALL), \
			NEW_GAME_RADIO(WID_MNG_SIZE_FIRST + 1, STR_MOBILE_NEW_GAME_MEDIUM), \
			NEW_GAME_RADIO(WID_MNG_SIZE_FIRST + 2, STR_MOBILE_NEW_GAME_LARGE), \
			EndContainer(), \
		EndContainer(), \
		NWidget(NWID_VERTICAL), \
			NEW_GAME_LABEL_ABOVE(STR_MOBILE_NEW_GAME_TOWNS), \
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, 2, 0), \
			NEW_GAME_RADIO(WID_MNG_TOWNS_FIRST + 0, STR_MOBILE_NEW_GAME_FEW), \
			NEW_GAME_RADIO(WID_MNG_TOWNS_FIRST + 1, STR_MOBILE_NEW_GAME_NORMAL), \
			NEW_GAME_RADIO(WID_MNG_TOWNS_FIRST + 2, STR_MOBILE_NEW_GAME_MANY), \
			EndContainer(), \
		EndContainer(), \
		NWidget(NWID_VERTICAL), \
			NEW_GAME_LABEL_ABOVE(STR_MOBILE_NEW_GAME_INDUSTRIES), \
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, 2, 0), \
			NEW_GAME_RADIO(WID_MNG_INDUSTRIES_FIRST + 0, STR_MOBILE_NEW_GAME_FEW), \
			NEW_GAME_RADIO(WID_MNG_INDUSTRIES_FIRST + 1, STR_MOBILE_NEW_GAME_NORMAL), \
			NEW_GAME_RADIO(WID_MNG_INDUSTRIES_FIRST + 2, STR_MOBILE_NEW_GAME_MANY), \
			EndContainer(), \
		EndContainer(), \
		NWidget(NWID_VERTICAL), \
			NEW_GAME_LABEL_ABOVE(STR_MOBILE_NEW_GAME_TERRAIN), \
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, 2, 0), \
			NEW_GAME_RADIO(WID_MNG_TERRAIN_FIRST + 0, STR_MOBILE_NEW_GAME_FLAT), \
			NEW_GAME_RADIO(WID_MNG_TERRAIN_FIRST + 1, STR_MOBILE_NEW_GAME_HILLY), \
			NEW_GAME_RADIO(WID_MNG_TERRAIN_FIRST + 2, STR_MOBILE_NEW_GAME_MOUNTAINS), \
			EndContainer(), \
		EndContainer(), \
		NEW_GAME_LABEL_ABOVE(STR_MOBILE_NEW_GAME_START_YEAR), \
		NWidget(NWID_HORIZONTAL), SetPIP(0, 2, 0), \
			NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MNG_YEAR_DOWN), SetMinimalSize(48, 40), SetStringTip(STR_MOBILE_MINUS), \
			NWidget(WWT_TEXT, Colours::Invalid, WID_MNG_YEAR), SetMinimalSize(80, 40), SetTextStyle(TextColour::Black), SetAlignment({AlignmentH::Centre, AlignmentV::Middle}), \
			NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MNG_YEAR_UP), SetMinimalSize(48, 40), SetStringTip(STR_MOBILE_PLUS), \
			NWidget(NWID_SPACER), SetFill(1, 0), SetResize(1, 0), \
			NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MNG_ADVANCED), SetMinimalSize(150, 40), SetStringTip(STR_MOBILE_NEW_GAME_ADVANCED), \
		EndContainer(), \
	EndContainer()


#define NEW_GAME_RULES \
	NWidget(NWID_VERTICAL), SetPIP(0, 4, 0), \
		NWidget(WWT_TEXT, Colours::Invalid), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_NEW_GAME_RULES), SetTextStyle(TextColour::Black), \
		NWidget(WWT_TEXTBTN, Colours::Brown, WID_MNG_PRESET_FIRST + 0), SetMinimalSize(220, 44), SetFill(1, 0), SetResize(1, 0), \
		NWidget(WWT_TEXTBTN, Colours::Brown, WID_MNG_PRESET_FIRST + 1), SetMinimalSize(220, 44), SetFill(1, 0), SetResize(1, 0), \
		NWidget(WWT_TEXTBTN, Colours::Brown, WID_MNG_PRESET_FIRST + 2), SetMinimalSize(220, 44), SetFill(1, 0), SetResize(1, 0), \
		NWidget(WWT_TEXT, Colours::Invalid), SetMinimalSize(220, 0), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_NEW_GAME_PRESET_NOTE), SetTextStyle(TextColour::Black), \
	EndContainer()

#define NEW_GAME_HEADER \
	NWidget(NWID_HORIZONTAL), \
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MNG_BACK), SetMinimalSize(48, 48), SetStringTip(STR_MOBILE_BACK, STR_MOBILE_BACK_TOOLTIP), \
		NWidget(WWT_PANEL, Colours::Brown, WID_MNG_TITLE), SetMinimalSize(0, 48), SetFill(1, 0), SetResize(1, 0), EndContainer(), \
		NWidget(WWT_PANEL, Colours::Brown), \
			NWidget(WWT_PUSHTXTBTN, Colours::Green, WID_MNG_START), SetMinimalSize(130, 40), SetPadding(4), SetStringTip(STR_MOBILE_NEW_GAME_START), \
		EndContainer(), \
	EndContainer()

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_new_game_landscape_widgets = {
	NEW_GAME_HEADER,
	NWidget(WWT_PANEL, Colours::Brown), SetResize(1, 1),
		NWidget(NWID_HORIZONTAL), SetPadding(8), SetPIP(0, 8, 0),
			NEW_GAME_RULES,
			NEW_GAME_OPTIONS,
		EndContainer(),
		/* Fills the rest of the screen. */
		NWidget(NWID_SPACER), SetFill(1, 1), SetResize(1, 1),
	EndContainer(),
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_new_game_portrait_widgets = {
	NEW_GAME_HEADER,
	NWidget(WWT_PANEL, Colours::Brown), SetResize(1, 1),
		NWidget(NWID_VERTICAL), SetPadding(8), SetPIP(0, 12, 0),
			NEW_GAME_RULES,
			NEW_GAME_OPTIONS_PORTRAIT,
		EndContainer(),
		/* Fills the rest of the screen. */
		NWidget(NWID_SPACER), SetFill(1, 1), SetResize(1, 1),
	EndContainer(),
};

#undef NEW_GAME_OPTIONS_PORTRAIT
#undef NEW_GAME_LABEL_ABOVE
#undef NEW_GAME_HEADER
#undef NEW_GAME_RULES
#undef NEW_GAME_OPTIONS
#undef NEW_GAME_RADIO
#undef NEW_GAME_LABEL

static WindowDesc _mobile_new_game_landscape_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::GenerateLandscape, WindowClass::None,
	{},
	_nested_mobile_new_game_landscape_widgets
);

static WindowDesc _mobile_new_game_portrait_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::GenerateLandscape, WindowClass::None,
	{},
	_nested_mobile_new_game_portrait_widgets
);

/** Show the New game screen, over the whole safe area. */
void ShowMobileNewGame()
{
	CloseWindowByClass(WindowClass::GenerateLandscape);
	bool landscape = _screen.width > _screen.height;
	Window *w = new MobileNewGameWindow(landscape ? _mobile_new_game_landscape_desc : _mobile_new_game_portrait_desc);
	Rect safe = VideoDriver::GetInstance() != nullptr ? VideoDriver::GetInstance()->GetSafeArea() : Rect{0, 0, _screen.width - 1, _screen.height - 1};
	ResizeWindow(w, std::max(0, safe.Width() - w->width), std::max(0, safe.Height() - w->height), false);
	w->left = safe.left;
	w->top = safe.top;
	w->SetDirty();
}
