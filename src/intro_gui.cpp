/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file intro_gui.cpp The main menu GUI. */

#include "stdafx.h"
#include "error.h"
#include "gui.h"
#include "window_gui.h"
#include "window_func.h"
#include "textbuf_gui.h"
#include "help_gui.h"
#include "network/network.h"
#include "genworld.h"
#include "network/network_gui.h"
#include "network/network_content.h"
#include "network/network_survey.h"
#include "landscape.h"
#include "strings_func.h"
#include "fios.h"
#include "gfx_func.h"
#include "core/string_consumer.hpp"
#include "language.h"
#include "rev.h"
#include "highscore.h"
#include "signs_base.h"
#include "viewport_func.h"
#include "vehicle_base.h"
#include <regex>
#include <filesystem>

#include "mobile_gui.h"
#include "zoom_func.h"
#include "mobile_app.h"
#include "mobile_newgame_gui.h"
#include "mobile_settings_gui.h"
#include "mobile_saveload_gui.h"
#include "video/video_driver.hpp"
#include "widgets/intro_widget.h"

#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"


/**
 * A viewport command for the main menu background (intro game).
 */
struct IntroGameViewportCommand {
	int command_index = 0;               ///< Sequence number of the command (order they are performed in).
	Point position{ 0, 0 };              ///< Calculated world coordinate to position viewport top-left at.
	VehicleID vehicle = VehicleID::Invalid(); ///< Vehicle to follow, or VehicleID::Invalid() if not following a vehicle.
	uint delay = 0;                      ///< Delay until next command.
	int zoom_adjust = 0;                 ///< Adjustment to zoom level from base zoom level.
	bool pan_to_next = false;            ///< If true, do a smooth pan from this position to the next.
	Alignment align = {AlignmentH::Centre, AlignmentV::Middle}; ///< Alignment.

	/**
	 * Calculate effective position.
	 * This will update the position field if a vehicle is followed.
	 * @param vp Viewport to calculate position for.
	 * @return Calculated position in the viewport.
	 */
	Point PositionForViewport(const Viewport &vp)
	{
		if (this->vehicle != VehicleID::Invalid()) {
			const Vehicle *v = Vehicle::Get(this->vehicle);
			this->position = RemapCoords(v->x_pos, v->y_pos, v->z_pos);
		}

		Point p;
		switch (this->align.ResolveRTL()) {
			case AlignmentH::ForceLeft: p.x = this->position.x; break;
			case AlignmentH::Centre: p.x = this->position.x - vp.virtual_width / 2; break;
			case AlignmentH::ForceRight: p.x = this->position.x - vp.virtual_width; break;
			default: NOT_REACHED();
		}
		switch (this->align.v) {
			case AlignmentV::Top: p.y = this->position.y; break;
			case AlignmentV::Middle: p.y = this->position.y - vp.virtual_height / 2; break;
			case AlignmentV::Bottom: p.y = this->position.y - vp.virtual_height; break;
		}
		return p;
	}
};


struct SelectGameWindow : public Window {
	bool mobile = false; ///< Uses the mobile layout: a panel on the left (landscape) or across the top (portrait), with Continue.
	std::string continue_save; ///< Save that Continue loads; empty for none.
	std::string continue_company; ///< Company name in that save.
	std::string continue_details; ///< Date and money in that save.
	bool warned = false; ///< The startup warnings were shown.

	/** Vector of viewport commands parsed. */
	std::vector<IntroGameViewportCommand> intro_viewport_commands{};
	/** Index of currently active viewport command. */
	size_t cur_viewport_command_index = SIZE_MAX;
	/** Time spent (milliseconds) on current viewport command. */
	uint cur_viewport_command_time = 0;
	uint mouse_idle_time = 0;
	Point mouse_idle_pos{};

	/**
	 * Find and parse all viewport command signs.
	 * Fills the intro_viewport_commands vector and deletes parsed signs from the world.
	 */
	void ReadIntroGameViewportCommand()
	{
		intro_viewport_commands.clear();

		/* Regular expression matching the commands: T, spaces, integer, spaces, flags, spaces, integer */
		static const std::string sign_language = "^T\\s*([0-9]+)\\s*([-+A-Z0-9]+)\\s*([0-9]+)";
		std::regex re(sign_language, std::regex_constants::icase);

		/* List of signs successfully parsed to delete afterwards. */
		std::vector<SignID> signs_to_delete;

		for (const Sign *sign : Sign::Iterate()) {
			std::smatch match;
			if (!std::regex_search(sign->name, match, re)) continue;

			IntroGameViewportCommand vc;
			/* Sequence index from the first matching group. */
			if (auto value = ParseInteger<int>(match[1].str()); value.has_value()) {
				vc.command_index = *value;
			} else {
				continue;
			}
			/* Sign coordinates for positioning. */
			vc.position = RemapCoords(sign->x, sign->y, sign->z);
			/* Delay from the third matching group. */
			if (auto value = ParseInteger<uint>(match[3].str()); value.has_value()) {
				vc.delay = *value * 1000; // milliseconds
			} else {
				continue;
			}

			/* Parse flags from second matching group. */
			auto flags = match[2].str();
			StringConsumer consumer{flags};
			while (consumer.AnyBytesLeft()) {
				auto c = consumer.ReadUtf8();
				switch (toupper(c)) {
					case '-': vc.zoom_adjust = +1; break;
					case '+': vc.zoom_adjust = -1; break;
					case 'T': vc.align.v = AlignmentV::Top; break;
					case 'M': vc.align.v = AlignmentV::Middle; break;
					case 'B': vc.align.v = AlignmentV::Bottom; break;
					case 'L': vc.align.h = AlignmentH::ForceLeft; break;
					case 'C': vc.align.h = AlignmentH::Centre; break;
					case 'R': vc.align.h = AlignmentH::ForceRight; break;
					case 'P': vc.pan_to_next = true; break;
					case 'V': vc.vehicle = static_cast<VehicleID>(consumer.ReadIntegerBase<uint32_t>(10, VehicleID::Invalid().base())); break;
				}
			}

			/* Successfully parsed, store. */
			intro_viewport_commands.push_back(vc);
			signs_to_delete.push_back(sign->index);
		}

		/* Sort the commands by sequence index. */
		std::sort(intro_viewport_commands.begin(), intro_viewport_commands.end(), [](const IntroGameViewportCommand &a, const IntroGameViewportCommand &b) { return a.command_index < b.command_index; });

		/* Delete all the consumed signs, from last ID to first ID. */
		std::sort(signs_to_delete.begin(), signs_to_delete.end(), [](SignID a, SignID b) { return a > b; });
		for (SignID sign_id : signs_to_delete) {
			delete Sign::Get(sign_id);
		}
	}

	SelectGameWindow(WindowDesc &desc, bool mobile = false) : Window(desc), mobile(mobile), mouse_idle_pos(_cursor.pos)
	{
		this->CreateNestedTree();
		this->FinishInitNested(0);
		this->OnInvalidateData();

		this->ReadIntroGameViewportCommand();
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		/* Move the main game viewport according to intro viewport commands. */

		if (intro_viewport_commands.empty()) return;

		bool suppress_panning = true;
		if (this->mouse_idle_pos.x != _cursor.pos.x || this->mouse_idle_pos.y != _cursor.pos.y) {
			this->mouse_idle_pos = _cursor.pos;
			this->mouse_idle_time = 2000;
		} else if (this->mouse_idle_time > delta_ms) {
			this->mouse_idle_time -= delta_ms;
		} else {
			this->mouse_idle_time = 0;
			suppress_panning = false;
		}

		/* Determine whether to move to the next command or stay at current. */
		bool changed_command = false;
		if (this->cur_viewport_command_index >= intro_viewport_commands.size()) {
			/* Reached last, rotate back to start of the list. */
			this->cur_viewport_command_index = 0;
			changed_command = true;
		} else {
			/* Check if current command has elapsed and switch to next. */
			this->cur_viewport_command_time += delta_ms;
			if (this->cur_viewport_command_time >= intro_viewport_commands[this->cur_viewport_command_index].delay) {
				this->cur_viewport_command_index = (this->cur_viewport_command_index + 1) % intro_viewport_commands.size();
				this->cur_viewport_command_time = 0;
				changed_command = true;
			}
		}

		IntroGameViewportCommand &vc = intro_viewport_commands[this->cur_viewport_command_index];
		Window *mw = GetMainWindow();

		/* Early exit if the current command hasn't elapsed and isn't animated. */
		if (!changed_command && !vc.pan_to_next && vc.vehicle == VehicleID::Invalid()) return;

		/* Suppress panning commands, while user interacts with GUIs. */
		if (!changed_command && suppress_panning) return;

		/* Reset the zoom level. */
		if (changed_command) FixTitleGameZoom(vc.zoom_adjust);

		/* Calculate current command position (updates followed vehicle coordinates). */
		Point pos = vc.PositionForViewport(*mw->viewport);

		/* Calculate panning (linear interpolation between current and next command position). */
		if (vc.pan_to_next) {
			size_t next_command_index = (this->cur_viewport_command_index + 1) % intro_viewport_commands.size();
			IntroGameViewportCommand &nvc = intro_viewport_commands[next_command_index];
			Point pos2 = nvc.PositionForViewport(*mw->viewport);
			const double t = this->cur_viewport_command_time / (double)vc.delay;
			pos.x = pos.x + (int)(t * (pos2.x - pos.x));
			pos.y = pos.y + (int)(t * (pos2.y - pos.y));
		}

		/* Update the viewport position. */
		mw->viewport->dest_scrollpos_x = mw->viewport->scrollpos_x = pos.x;
		mw->viewport->dest_scrollpos_y = mw->viewport->scrollpos_y = pos.y;
		UpdateViewportPosition(mw, delta_ms);
		mw->SetDirty(); // Required during panning, otherwise logo graphics disappears

		/* If there is only one command, we just executed it and don't need to do any more */
		if (intro_viewport_commands.size() == 1 && vc.vehicle == VehicleID::Invalid()) intro_viewport_commands.clear();
	}

	/** Mobile layout: fill the left side (landscape) or the width (portrait), 12 GUI pixels from the safe edges. */
	void PlaceMobile()
	{
		if (!this->mobile) return;
		int margin = ScaleGUITrad(12);
		Rect safe{0, 0, _screen.width - 1, _screen.height - 1};
		if (VideoDriver::GetInstance() != nullptr) safe = VideoDriver::GetInstance()->GetSafeArea();
		bool landscape = _screen.width > _screen.height;
		int width = landscape ? ScaleGUITrad(380) : safe.Width() - 2 * margin;
		int height = this->nested_root->smallest_y;
		ResizeWindow(this, width - this->width, std::max<int>(0, height - this->height), false);
		this->left = safe.left + margin;
		this->top = safe.top + margin;
		/* The startup warning: beside the panel in landscape, above it in portrait. */
		if (Window *banner = FindWindowById(WindowClass::MobileError, 0); banner != nullptr) {
			PlaceMobileMenuBanner(banner);
			if (!landscape) this->top = banner->top + banner->height + margin / 2;
		}
		this->SetDirty();
	}

	void OnInit() override
	{
		if (this->mobile) {
			this->continue_save = GetMobileContinueSave();
			if (!this->continue_save.empty()) GetMobileSaveHeader(this->continue_save, this->continue_company, this->continue_details);
			if (NWidgetStacked *sel = this->GetWidget<NWidgetStacked>(WID_SGI_SEL_CONTINUE); sel != nullptr) sel->SetDisplayedPlane(this->continue_save.empty() ? SZSP_HORIZONTAL : 0);
		}

		/* The mobile layout resizes: hidden rows must stay horizontally resizable (SZSP_HORIZONTAL). */
		int hidden = this->mobile ? SZSP_HORIZONTAL : SZSP_NONE;
		bool missing_sprites = _missing_extra_graphics > 0 && !IsReleasedVersion();
		bool missing_lang = _current_language->missing >= _settings_client.gui.missing_strings_threshold && !IsReleasedVersion();
		if (this->mobile) {
			/* Startup warnings go to the banner at the top, not into the menu. */
			this->GetWidget<NWidgetStacked>(WID_SGI_BASESET_SELECTION)->SetDisplayedPlane(hidden);
			this->GetWidget<NWidgetStacked>(WID_SGI_TRANSLATION_SELECTION)->SetDisplayedPlane(hidden);
			if (missing_sprites && !this->warned) ShowMobileMenuBanner(GetString(STR_INTRO_BASESET, _missing_extra_graphics));
			this->warned = true;
		} else {
			this->GetWidget<NWidgetStacked>(WID_SGI_BASESET_SELECTION)->SetDisplayedPlane(missing_sprites ? 0 : hidden);
			this->GetWidget<NWidgetStacked>(WID_SGI_TRANSLATION_SELECTION)->SetDisplayedPlane(missing_lang ? 0 : hidden);
		}
	}

	/**
	 * Mobile layout: the Continue button, "Continue" large and the save's name and age below.
	 * @param r The button.
	 */
	void DrawContinue(const Rect &r) const
	{
		/* One font, colour for the hierarchy: "Continue · <company>" over "<date> · <money> · <time ago>". */
		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(8), 0);
		bool rtl = _current_text_dir == TD_RTL;
		Dimension play = GetSpriteSize(SPR_IMG_PLAY_MUSIC);
		DrawSprite(SPR_IMG_PLAY_MUSIC, PAL_NONE, rtl ? tr.right - play.width + 1 : tr.left, CentreBounds(r.top, r.bottom, play.height));
		tr = tr.Indent(play.width + ScaleGUITrad(8), rtl);
		int line = GetCharacterHeight(FontSize::Normal);
		int y = CentreBounds(r.top, r.bottom, 2 * line + ScaleGUITrad(3));
		std::string title = this->continue_company.empty() ? GetString(STR_MOBILE_MENU_CONTINUE) : GetString(STR_MOBILE_MENU_CONTINUE_COMPANY, this->continue_company);
		DrawString(tr.left, tr.right, y, title, TextColour::White);

		std::string ago;
		std::error_code ec;
		auto written = std::filesystem::last_write_time(OTTD2FS(this->continue_save), ec);
		if (!ec) {
			auto minutes = std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::minutes>(std::filesystem::file_time_type::clock::now() - written).count());
			StringID str = minutes < 60 ? STR_MOBILE_AGO_MINUTES : (minutes < 48 * 60 ? STR_MOBILE_AGO_HOURS : STR_MOBILE_AGO_DAYS);
			ago = GetString(str, minutes < 60 ? minutes : (minutes < 48 * 60 ? minutes / 60 : minutes / (24 * 60)));
		}
		std::string details = this->continue_details.empty() ? ago : (ago.empty() ? this->continue_details : this->continue_details + GetString(STR_MOBILE_ORDERS_SEPARATOR) + ago);
		DrawString(tr.left, tr.right, y + line + ScaleGUITrad(3), details, TextColour::Cream);
	}

	/**
	 * The OpenTTD logo, as drawn behind the menu, in the menu's header.
	 * @param r The header.
	 */
	static void DrawLogo(const Rect &r)
	{
		static const std::initializer_list<SpriteID> letters = {SPR_OTTD_O, SPR_OTTD_P, SPR_OTTD_E, SPR_OTTD_N, SPR_OTTD_T, SPR_OTTD_T, SPR_OTTD_D};
		int spacing = ScaleGUITrad(10);
		int x = _current_text_dir == TD_RTL ? r.right : r.left;
		for (SpriteID sprite : letters) {
			Dimension d = GetSpriteSize(sprite);
			if (_current_text_dir == TD_RTL) x -= d.width;
			DrawSprite(sprite, PAL_NONE, x, r.top);
			x += _current_text_dir == TD_RTL ? -spacing : static_cast<int>(d.width) + spacing;
		}
	}

	/** @return Size of the logo. */
	static Dimension GetLogoSize()
	{
		static const std::initializer_list<SpriteID> letters = {SPR_OTTD_O, SPR_OTTD_P, SPR_OTTD_E, SPR_OTTD_N, SPR_OTTD_T, SPR_OTTD_T, SPR_OTTD_D};
		Dimension d{0, 0};
		for (SpriteID sprite : letters) {
			Dimension s = GetSpriteSize(sprite);
			d.width += s.width + ScaleGUITrad(10);
			d.height = std::max(d.height, s.height);
		}
		return d;
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (this->mobile && widget == WID_SGI_VERSION) size.height = std::max(size.height, GetLogoSize().height);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_SGI_CONTINUE:
				this->DrawContinue(r);
				break;

			case WID_SGI_VERSION:
				DrawLogo(r);
				break;

			case WID_SGI_BASESET:
				DrawStringMultiLine(r, GetString(STR_INTRO_BASESET, _missing_extra_graphics), TextColour::FromString, {AlignmentH::Centre, AlignmentV::Middle});
				break;

			case WID_SGI_TRANSLATION:
				DrawStringMultiLine(r, GetString(STR_INTRO_TRANSLATION, _current_language->missing), TextColour::FromString, {AlignmentH::Centre, AlignmentV::Middle});
				break;
		}
	}

	void OnResize() override
	{
		bool changed = false;

		if (NWidgetResizeBase *wid = this->GetWidget<NWidgetResizeBase>(WID_SGI_BASESET); wid != nullptr && wid->current_x > 0) {
			changed |= wid->UpdateMultilineWidgetSize(GetString(STR_INTRO_BASESET, _missing_extra_graphics), 3);
		}

		if (NWidgetResizeBase *wid = this->GetWidget<NWidgetResizeBase>(WID_SGI_TRANSLATION); wid != nullptr && wid->current_x > 0) {
			changed |= wid->UpdateMultilineWidgetSize(GetString(STR_INTRO_TRANSLATION, _current_language->missing), 3);
		}

		if (changed) this->ReInit(0, 0, this->flags.Test(WindowFlag::Centred));
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_SGI_CONTINUE:
				_is_network_server = false;
				MobileContinueGame();
				break;

			case WID_SGI_GENERATE_GAME:
				_is_network_server = false;
				if (this->mobile) {
					ShowMobileNewGame();
				} else if (IsCtrlPressed()) {
					StartNewGameWithoutGUI(GENERATE_NEW_SEED);
				} else {
					ShowGenerateLandscape();
				}
				break;
			case WID_SGI_LOAD_GAME:
				_is_network_server = false;
				ShowSaveLoadDialog(AbstractFileType::Savegame, SaveLoadOperation::Load);
				break;
			case WID_SGI_PLAY_SCENARIO:
				_is_network_server = false;
				ShowSaveLoadDialog(AbstractFileType::Scenario, SaveLoadOperation::Load);
				break;
			case WID_SGI_PLAY_HEIGHTMAP:
				_is_network_server = false;
				ShowSaveLoadDialog(AbstractFileType::Heightmap,SaveLoadOperation::Load);
				break;
			case WID_SGI_EDIT_SCENARIO:
				_is_network_server = false;
				StartScenarioEditor();
				break;

			case WID_SGI_PLAY_NETWORK:
				if (!_network_available) {
					ShowErrorMessage(GetEncodedString(STR_NETWORK_ERROR_NOTAVAILABLE), {}, WarningLevel::Error);
				} else {
					ShowNetworkGameWindow();
				}
				break;

			case WID_SGI_OPTIONS:
				if (this->mobile) {
					ShowMobileSettings();
				} else {
					ShowGameOptions();
				}
				break;
			case WID_SGI_HIGHSCORE:       ShowHighscoreTable(); break;
			case WID_SGI_HELP:            ShowHelpWindow(); break;
			case WID_SGI_CONTENT_DOWNLOAD:
				if (!_network_available) {
					ShowErrorMessage(GetEncodedString(STR_NETWORK_ERROR_NOTAVAILABLE), {}, WarningLevel::Error);
				} else {
					ShowNetworkContentListWindow();
				}
				break;
			case WID_SGI_EXIT:            HandleExitGameRequest(); break;
		}
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_select_game_widgets = {
	NWidget(WWT_CAPTION, Colours::Brown), SetStringTip(STR_INTRO_CAPTION),
	NWidget(WWT_PANEL, Colours::Brown),
		NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_wide, 0), SetPadding(WidgetDimensions::unscaled.sparse),

			/* Single player */
			NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0),
				NWidget(WWT_PUSHIMGTEXTBTN, Colours::Orange, WID_SGI_GENERATE_GAME), SetToolbarMinimalSize(1), SetSpriteStringTip(SPR_IMG_LANDSCAPING, STR_INTRO_NEW_GAME, STR_INTRO_TOOLTIP_NEW_GAME), SetAlignment({AlignmentH::Start, AlignmentV::Middle}), SetFill(1, 0),
				NWidget(WWT_PUSHIMGTEXTBTN, Colours::Orange, WID_SGI_PLAY_HEIGHTMAP), SetToolbarMinimalSize(1), SetSpriteStringTip(SPR_IMG_SHOW_COUNTOURS, STR_INTRO_PLAY_HEIGHTMAP, STR_INTRO_TOOLTIP_PLAY_HEIGHTMAP), SetAlignment({AlignmentH::Start, AlignmentV::Middle}), SetFill(1, 0),
				NWidget(WWT_PUSHIMGTEXTBTN, Colours::Orange, WID_SGI_PLAY_SCENARIO), SetToolbarMinimalSize(1), SetSpriteStringTip(SPR_IMG_SUBSIDIES, STR_INTRO_PLAY_SCENARIO, STR_INTRO_TOOLTIP_PLAY_SCENARIO), SetAlignment({AlignmentH::Start, AlignmentV::Middle}), SetFill(1, 0),
				NWidget(WWT_PUSHIMGTEXTBTN, Colours::Orange, WID_SGI_LOAD_GAME), SetToolbarMinimalSize(1), SetSpriteStringTip(SPR_IMG_SAVE, STR_INTRO_LOAD_GAME, STR_INTRO_TOOLTIP_LOAD_GAME), SetAlignment({AlignmentH::Start, AlignmentV::Middle}), SetFill(1, 0),
				NWidget(WWT_PUSHIMGTEXTBTN, Colours::Orange, WID_SGI_HIGHSCORE), SetToolbarMinimalSize(1), SetSpriteStringTip(SPR_IMG_COMPANY_LEAGUE, STR_INTRO_HIGHSCORE, STR_INTRO_TOOLTIP_HIGHSCORE), SetAlignment({AlignmentH::Start, AlignmentV::Middle}), SetFill(1, 0),
			EndContainer(),

			/* Multi player */
			NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0),
				NWidget(WWT_PUSHIMGTEXTBTN, Colours::Orange, WID_SGI_PLAY_NETWORK), SetToolbarMinimalSize(1), SetSpriteStringTip(SPR_IMG_COMPANY_GENERAL, STR_INTRO_MULTIPLAYER, STR_INTRO_TOOLTIP_MULTIPLAYER), SetAlignment({AlignmentH::Start, AlignmentV::Middle}), SetFill(1, 0),
			EndContainer(),

			NWidget(NWID_SELECTION, Colours::Invalid, WID_SGI_BASESET_SELECTION),
				NWidget(NWID_VERTICAL),
					NWidget(WWT_EMPTY, Colours::Invalid, WID_SGI_BASESET), SetFill(1, 0),
				EndContainer(),
			EndContainer(),

			NWidget(NWID_SELECTION, Colours::Invalid, WID_SGI_TRANSLATION_SELECTION),
				NWidget(NWID_VERTICAL),
					NWidget(WWT_EMPTY, Colours::Invalid, WID_SGI_TRANSLATION), SetFill(1, 0),
				EndContainer(),
			EndContainer(),

			/* Other */
			NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0),
				NWidget(WWT_PUSHIMGTEXTBTN, Colours::Orange, WID_SGI_OPTIONS), SetToolbarMinimalSize(1), SetSpriteStringTip(SPR_IMG_SETTINGS, STR_INTRO_GAME_OPTIONS, STR_INTRO_TOOLTIP_GAME_OPTIONS), SetAlignment({AlignmentH::Start, AlignmentV::Middle}), SetFill(1, 0),
				NWidget(WWT_PUSHIMGTEXTBTN, Colours::Orange, WID_SGI_CONTENT_DOWNLOAD), SetToolbarMinimalSize(1), SetSpriteStringTip(SPR_IMG_SHOW_VEHICLES, STR_INTRO_ONLINE_CONTENT, STR_INTRO_TOOLTIP_ONLINE_CONTENT), SetAlignment({AlignmentH::Start, AlignmentV::Middle}), SetFill(1, 0),
				NWidget(WWT_PUSHIMGTEXTBTN, Colours::Orange, WID_SGI_EDIT_SCENARIO), SetToolbarMinimalSize(1), SetSpriteStringTip(SPR_IMG_SMALLMAP, STR_INTRO_SCENARIO_EDITOR, STR_INTRO_TOOLTIP_SCENARIO_EDITOR), SetAlignment({AlignmentH::Start, AlignmentV::Middle}), SetFill(1, 0),
				NWidget(WWT_PUSHIMGTEXTBTN, Colours::Orange, WID_SGI_HELP), SetToolbarMinimalSize(1), SetSpriteStringTip(SPR_IMG_QUERY, STR_INTRO_HELP, STR_INTRO_TOOLTIP_HELP), SetAlignment({AlignmentH::Start, AlignmentV::Middle}), SetFill(1, 0),
			EndContainer(),

			NWidget(NWID_VERTICAL),
				NWidget(WWT_PUSHTXTBTN, Colours::Orange, WID_SGI_EXIT), SetToolbarMinimalSize(1), SetStringTip(STR_INTRO_QUIT, STR_INTRO_TOOLTIP_QUIT),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

/** A button of the mobile main menu: icon and text, full width, 40 tall. */
#define MOBILE_MENU_BUTTON(widget, colour, sprite, string, tooltip) \
	NWidget(WWT_PUSHIMGTEXTBTN, colour, widget), SetMinimalSize(0, 40), SetSpriteStringTip(sprite, string, tooltip), SetAlignment({AlignmentH::Start, AlignmentV::Middle}), SetFill(1, 0), SetResize(1, 0)

/** The parts of the mobile main menu above and below the buttons; see the round 4 design, section 5. */
/** The parts of the mobile main menu above the buttons; see the round 5 design, section 2. */
#define MOBILE_MENU_HEAD \
	NWidget(WWT_EMPTY, Colours::Invalid, WID_SGI_VERSION), SetFill(1, 0), SetResize(1, 0), \
	NWidget(NWID_SELECTION, Colours::Invalid, WID_SGI_SEL_CONTINUE), \
		NWidget(WWT_PUSHBTN, Colours::Green, WID_SGI_CONTINUE), SetMinimalSize(0, 48), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_MENU_CONTINUE_TOOLTIP), \
	EndContainer(), \
	NWidget(NWID_SELECTION, Colours::Invalid, WID_SGI_BASESET_SELECTION), \
		NWidget(WWT_EMPTY, Colours::Invalid, WID_SGI_BASESET), SetFill(1, 0), SetResize(1, 0), \
	EndContainer(), \
	NWidget(NWID_SELECTION, Colours::Invalid, WID_SGI_TRANSLATION_SELECTION), \
		NWidget(WWT_EMPTY, Colours::Invalid, WID_SGI_TRANSLATION), SetFill(1, 0), SetResize(1, 0), \
	EndContainer()

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_select_game_landscape_widgets = {
	NWidget(WWT_PANEL, Colours::Brown),
		NWidget(NWID_VERTICAL), SetPIP(0, 7, 0), SetPadding(10),
			MOBILE_MENU_HEAD,
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, 5, 0),
				NWidget(NWID_VERTICAL), SetPIP(0, 5, 0),
					MOBILE_MENU_BUTTON(WID_SGI_GENERATE_GAME, Colours::Orange, SPR_IMG_LANDSCAPING, STR_INTRO_NEW_GAME, STR_INTRO_TOOLTIP_NEW_GAME),
					MOBILE_MENU_BUTTON(WID_SGI_OPTIONS, Colours::Orange, SPR_IMG_SETTINGS, STR_MOBILE_MAIN_SETTINGS, STR_INTRO_TOOLTIP_GAME_OPTIONS),
					MOBILE_MENU_BUTTON(WID_SGI_PLAY_NETWORK, Colours::Orange, SPR_IMG_COMPANY_GENERAL, STR_INTRO_MULTIPLAYER, STR_INTRO_TOOLTIP_MULTIPLAYER),
					MOBILE_MENU_BUTTON(WID_SGI_HELP, Colours::Orange, SPR_IMG_QUERY, STR_INTRO_HELP, STR_INTRO_TOOLTIP_HELP),
				EndContainer(),
				NWidget(NWID_VERTICAL), SetPIP(0, 5, 0),
					MOBILE_MENU_BUTTON(WID_SGI_LOAD_GAME, Colours::Orange, SPR_IMG_SAVE, STR_INTRO_LOAD_GAME, STR_INTRO_TOOLTIP_LOAD_GAME),
					MOBILE_MENU_BUTTON(WID_SGI_CONTENT_DOWNLOAD, Colours::Orange, SPR_IMG_SHOW_VEHICLES, STR_INTRO_ONLINE_CONTENT, STR_INTRO_TOOLTIP_ONLINE_CONTENT),
					MOBILE_MENU_BUTTON(WID_SGI_EDIT_SCENARIO, Colours::Orange, SPR_IMG_SMALLMAP, STR_INTRO_SCENARIO_EDITOR, STR_INTRO_TOOLTIP_SCENARIO_EDITOR),
					NWidget(WWT_PUSHTXTBTN, Colours::Red, WID_SGI_EXIT), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_MAIN_QUIT, STR_INTRO_TOOLTIP_QUIT),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_select_game_portrait_widgets = {
	NWidget(WWT_PANEL, Colours::Brown),
		NWidget(NWID_VERTICAL), SetPIP(0, 5, 0), SetPadding(10),
			MOBILE_MENU_HEAD,
			MOBILE_MENU_BUTTON(WID_SGI_GENERATE_GAME, Colours::Orange, SPR_IMG_LANDSCAPING, STR_INTRO_NEW_GAME, STR_INTRO_TOOLTIP_NEW_GAME),
			MOBILE_MENU_BUTTON(WID_SGI_LOAD_GAME, Colours::Orange, SPR_IMG_SAVE, STR_INTRO_LOAD_GAME, STR_INTRO_TOOLTIP_LOAD_GAME),
			MOBILE_MENU_BUTTON(WID_SGI_OPTIONS, Colours::Orange, SPR_IMG_SETTINGS, STR_MOBILE_MAIN_SETTINGS, STR_INTRO_TOOLTIP_GAME_OPTIONS),
			MOBILE_MENU_BUTTON(WID_SGI_CONTENT_DOWNLOAD, Colours::Orange, SPR_IMG_SHOW_VEHICLES, STR_INTRO_ONLINE_CONTENT, STR_INTRO_TOOLTIP_ONLINE_CONTENT),
			MOBILE_MENU_BUTTON(WID_SGI_PLAY_NETWORK, Colours::Orange, SPR_IMG_COMPANY_GENERAL, STR_INTRO_MULTIPLAYER, STR_INTRO_TOOLTIP_MULTIPLAYER),
			MOBILE_MENU_BUTTON(WID_SGI_EDIT_SCENARIO, Colours::Orange, SPR_IMG_SMALLMAP, STR_INTRO_SCENARIO_EDITOR, STR_INTRO_TOOLTIP_SCENARIO_EDITOR),
			MOBILE_MENU_BUTTON(WID_SGI_HELP, Colours::Orange, SPR_IMG_QUERY, STR_INTRO_HELP, STR_INTRO_TOOLTIP_HELP),
			NWidget(WWT_PUSHTXTBTN, Colours::Red, WID_SGI_EXIT), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_MAIN_QUIT, STR_INTRO_TOOLTIP_QUIT),
		EndContainer(),
	EndContainer(),
};

#undef MOBILE_MENU_HEAD
#undef MOBILE_MENU_BUTTON

static WindowDesc _mobile_select_game_landscape_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::SelectGame, WindowClass::None,
	WindowDefaultFlag::NoClose,
	_nested_mobile_select_game_landscape_widgets
);

static WindowDesc _mobile_select_game_portrait_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::SelectGame, WindowClass::None,
	WindowDefaultFlag::NoClose,
	_nested_mobile_select_game_portrait_widgets
);

/** Window definition for the select game window. */
static WindowDesc _select_game_desc(
	WindowPosition::Center, {}, 0, 0,
	WindowClass::SelectGame, WindowClass::None,
	WindowDefaultFlag::NoClose,
	_nested_select_game_widgets
);

void ShowSelectGameWindow();

/** Mobile layout: keep the main menu in place after the screen changed, in the layout for the new orientation. */
void RelocateMobileMainMenu()
{
	SelectGameWindow *w = dynamic_cast<SelectGameWindow *>(FindWindowById(WindowClass::SelectGame, 0));
	if (w == nullptr || !w->mobile) return;
	bool landscape = _screen.width > _screen.height;
	if ((&w->window_desc == &_mobile_select_game_landscape_desc) != landscape) {
		w->Close();
		ShowSelectGameWindow();
		return;
	}
	w->PlaceMobile();
}

void ShowSelectGameWindow()
{
	if (IsMobileMenus()) {
		bool landscape = _screen.width > _screen.height;
		SelectGameWindow *w = new SelectGameWindow(landscape ? _mobile_select_game_landscape_desc : _mobile_select_game_portrait_desc, true);
		w->PlaceMobile();
		return;
	}
	new SelectGameWindow(_select_game_desc);
}

static void AskExitGameCallback(Window *, bool confirmed)
{
	if (confirmed) {
		_survey.Transmit(NetworkSurveyHandler::Reason::Exit, true);
		_exit_game = true;
	}
}

void AskExitGame()
{
	if (IsMobileMenus()) {
		ShowQuery(GetEncodedString(STR_MOBILE_QUERY_QUIT_TITLE), GetEncodedString(STR_MOBILE_QUERY_QUIT_TEXT), nullptr, AskExitGameCallback, true, STR_MOBILE_QUERY_QUIT_VERB);
		return;
	}
	ShowQuery(
		GetEncodedString(STR_QUIT_CAPTION),
		GetEncodedString(STR_QUIT_ARE_YOU_SURE_YOU_WANT_TO_EXIT_OPENTTD),
		nullptr,
		AskExitGameCallback,
		true
	);
}


static void AskExitToGameMenuCallback(Window *, bool confirmed)
{
	if (confirmed) {
		_switch_mode = SwitchMode::Menu;
		ClearErrorMessages();
	}
}

void AskExitToGameMenu()
{
	if (IsMobileLayout()) {
		ShowQuery(GetEncodedString(STR_MOBILE_QUERY_EXIT_TITLE), GetEncodedString(STR_MOBILE_QUERY_EXIT_TEXT), nullptr, AskExitToGameMenuCallback, true, STR_MOBILE_QUERY_EXIT_VERB);
		return;
	}
	ShowQuery(
		GetEncodedString(STR_ABANDON_GAME_CAPTION),
		GetEncodedString((_game_mode != GameMode::Editor) ? STR_ABANDON_GAME_QUERY : STR_ABANDON_SCENARIO_QUERY),
		nullptr,
		AskExitToGameMenuCallback,
		true
	);
}
