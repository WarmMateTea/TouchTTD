/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file mobile_gui.cpp The touch-first mobile layout: the app shell that replaces the main toolbar and the status bar.
 *
 * The shell is a set of small docked windows floating over the main viewport:
 *  - the status pill (pause, fast forward, date and money),
 *  - the hub dock (a left rail in landscape, a bottom bar in portrait),
 *  - the minimap button and the news banner,
 *  - the Build and Menu flyouts,
 *  - the hub sheets: a tab window plus an existing window docked in the "sheet slot" below it.
 *
 * RelocateMobileWindows() is the layout pass; it places all of them for the current screen size and orientation.
 */

#include "stdafx.h"
#include "mobile_gui.h"
#include "gui.h"
#include "window_gui.h"
#include "window_func.h"
#include "gfx_func.h"
#include "zoom_func.h"
#include "strings_func.h"
#include "command_func.h"
#include "string_func.h"
#include "misc_cmd.h"
#include "sound_func.h"
#include "company_base.h"
#include "company_func.h"
#include "company_gui.h"
#include "vehicle_gui.h"
#include "rail_gui.h"
#include "road.h"
#include "road_gui.h"
#include "terraform_gui.h"
#include "toolbar_gui.h"
#include "rail.h"
#include "mobile_build.h"
#include "mobile_settings_gui.h"
#include "error.h"
#include "tilehighlight_func.h"
#include "smallmap_gui.h"
#include "graph_gui.h"
#include "goal_gui.h"
#include "league_gui.h"
#include "news_gui.h"
#include "news_func.h"
#include "statusbar_gui.h"
#include "signs_func.h"
#include "transparency_gui.h"
#include "help_gui.h"
#include "fios.h"
#include "openttd.h"
#include "video/video_driver.hpp"
#include "settings_type.h"
#include "settings_gui.h"
#include "newgrf_config.h"
#include "dropdown_type.h"
#include "dropdown_func.h"
#include "ai/ai_gui.hpp"
#include "linkgraph/linkgraph_gui.h"
#include "network/network.h"
#include "timer/timer.h"
#include "timer/timer_window.h"
#include "timer/timer_game_calendar.h"
#include "core/backup_type.hpp"
#ifdef __ANDROID__
#	include "os/android/android_bridge.h"
#endif
#include "widgets/mobile_widget.h"
#include "widgets/road_widget.h"
#include "widgets/rail_widget.h"

#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

/** Construction categories reachable from the Build button. */
enum class BuildCategory : uint8_t {
	Rail,
	Road,
	Tram,
	Water,
	Air,
	Landscape,
	End,
};

/** The hubs that open a sheet. */
enum class MobileHub : uint8_t {
	World,
	Vehicles,
	Company,
	End,
};

/** One tab of a hub sheet. */
struct HubTab {
	StringID label; ///< Text on the tab button.
	WindowClass content; ///< Class of the window the tab opens.
	void (*open)(); ///< Opens the window.
};

/** Static description of a hub. */
struct HubInfo {
	StringID caption; ///< Name of the hub.
	Colours colour; ///< Colour of the tab window, matching its content.
	std::span<const HubTab> tabs; ///< The tabs.
};

static const HubTab _world_tabs[] = {
	{STR_MOBILE_TAB_TOWNS, WindowClass::TownDirectory, [] { ShowTownDirectory(); }},
	{STR_MOBILE_TAB_INDUSTRIES, WindowClass::IndustryDirectory, [] { ShowIndustryDirectory(); }},
	{STR_MOBILE_TAB_SUBSIDIES, WindowClass::SubsidyList, [] { ShowSubsidiesList(); }},
	{STR_MOBILE_TAB_STORY, WindowClass::StoryBook, [] { ShowStoryBook(_local_company); }},
	{STR_MOBILE_TAB_GOALS, WindowClass::GoalList, [] { ShowGoalsList(_local_company); }},
};

static const HubTab _vehicles_tabs[] = {
	{STR_MOBILE_TAB_TRAINS, WindowClass::TrainList, [] { ShowVehicleListWindow(_local_company, VehicleType::Train); }},
	{STR_MOBILE_TAB_ROAD, WindowClass::RoadVehicleList, [] { ShowVehicleListWindow(_local_company, VehicleType::Road); }},
	{STR_MOBILE_TAB_SHIPS, WindowClass::ShipList, [] { ShowVehicleListWindow(_local_company, VehicleType::Ship); }},
	{STR_MOBILE_TAB_AIRCRAFT, WindowClass::AircraftList, [] { ShowVehicleListWindow(_local_company, VehicleType::Aircraft); }},
};

static const HubTab _company_tabs[] = {
	{STR_MOBILE_TAB_FINANCES, WindowClass::Finances, [] { ShowCompanyFinances(_local_company); }},
	{STR_MOBILE_TAB_STATIONS, WindowClass::StationList, [] { ShowCompanyStations(_local_company); }},
	{STR_MOBILE_TAB_COMPANIES, WindowClass::Company, [] { ShowCompany(_local_company); }},
	{STR_MOBILE_TAB_LEAGUE, WindowClass::CompanyLeague, [] { ShowFirstLeagueTable(); }},
	{STR_MOBILE_TAB_GRAPHS, WindowClass::OperatingProfitGraph, [] { ShowOperatingProfitGraph(); }},
};

static const HubInfo _hubs[] = {
	{STR_MOBILE_DOCK_WORLD, Colours::Brown, _world_tabs},
	{STR_MOBILE_DOCK_VEHICLES, Colours::Grey, _vehicles_tabs},
	{STR_MOBILE_DOCK_COMPANY, Colours::Grey, _company_tabs},
};
static_assert(std::size(_hubs) == to_underlying(MobileHub::End));

static BuildCategory _last_build_category = BuildCategory::End; ///< Category the Build button reopens; End when none was used yet.
static std::array<uint8_t, to_underlying(MobileHub::End)> _last_hub_tab{}; ///< Last shown tab per hub.

/** What is currently docked in the sheet slot. */
struct SheetState {
	WindowClass content_cls = WindowClass::Invalid; ///< Class of the content window.
	WindowNumber content_num{}; ///< Number of the content window.
	WindowClass overlay_cls = WindowClass::Invalid; ///< Class of a window shown on top of the content (e.g. a town opened from the list).
	WindowNumber overlay_num{}; ///< Number of the overlay window.
	bool full = false; ///< Portrait only: full height instead of half height.
};
static SheetState _sheet;

/** Fixed sizes from the design, in GUI pixels at 1x. */
static constexpr int MOBILE_MARGIN = 4; ///< Gap between the shell windows and the screen edges.
static constexpr int MOBILE_SIDE_PANEL_WIDTH = 300; ///< Width of the landscape side panel.
static constexpr int MOBILE_HALF_SHEET_HEIGHT = 400; ///< Height of the portrait half sheet.
static constexpr int MOBILE_MAP_STRIP = 48; ///< Map left visible above a full-height portrait sheet.
static constexpr int MOBILE_NEWS_WIDTH = 310; ///< Width of the landscape news banner.
static constexpr int MOBILE_NEWS_TOP_PORTRAIT = 48; ///< Top of the portrait news banner, below the status pill.

/**
 * Is the mobile layout in use?
 * @return \c true when the setting is on and a normal game is running.
 */
bool IsMobileLayout()
{
	return _settings_client.gui.mobile_layout && _game_mode == GameMode::Normal;
}

/**
 * Use the mobile layout for menus and dialogs? Unlike #IsMobileLayout(), also at the main menu.
 * @return \c true when the mobile layout setting is on, in a game or at the main menu.
 */
bool IsMobileMenus()
{
	return _settings_client.gui.mobile_layout && (_game_mode == GameMode::Normal || _game_mode == GameMode::Menu);
}

/**
 * Is the given window class one of the windows of the mobile shell?
 * @param cls The class to check.
 * @return \c true for the shell windows.
 */
bool IsMobileShellWindow(WindowClass cls)
{
	switch (cls) {
		case WindowClass::MobileStatus:
		case WindowClass::MobileDock:
		case WindowClass::MobileMinimap:
		case WindowClass::MobileNews:
		case WindowClass::MobileBuildMenu:
		case WindowClass::MobileMenu:
		case WindowClass::MobileHubTabs:
		case WindowClass::MobileBuildLabel:
		case WindowClass::MobileError:
		case WindowClass::MobileSheetFiller:
		case WindowClass::MobileRailMore:
		case WindowClass::MobileToolCancel:
		case WindowClass::MobileStopsBar:
			return true;
		default:
			return false;
	}
}

static bool IsLandscape()
{
	return _screen.width > _screen.height;
}

/**
 * Get the part of the screen not covered by system bars, the camera cutout or rounded corners, as the system reports it now.
 * @return The safe area, within the screen.
 */
static Rect GetSystemSafeArea()
{
	Rect screen{0, 0, _screen.width - 1, _screen.height - 1};
	Rect safe = VideoDriver::GetInstance() != nullptr ? VideoDriver::GetInstance()->GetSafeArea() : screen;
	safe.left = Clamp(safe.left, 0, screen.right);
	safe.top = Clamp(safe.top, 0, screen.bottom);
	safe.right = Clamp(safe.right, safe.left, screen.right);
	safe.bottom = Clamp(safe.bottom, safe.top, screen.bottom);
	return safe;
}

/** The safe area the shell is laid out in: it follows the system's safe area, sliding when system bars appear or disappear. */
static struct {
	bool valid = false; ///< Whether there was a first layout.
	Dimension screen{}; ///< Screen size of the last layout; a new size (rotation) moves without sliding.
	Rect from{}; ///< Where the slide started.
	Rect target{}; ///< Where the slide ends.
	Rect shown{}; ///< Where the shell is now.
	std::chrono::steady_clock::time_point start{}; ///< When the slide started.
	bool sliding = false; ///< Whether a slide is in progress.
} _safe;

static constexpr std::chrono::milliseconds SAFE_AREA_SLIDE_TIME{150}; ///< How long the shell takes to follow a system bar.

/** Follow the system's safe area: snap the first time and after a rotation, otherwise start a slide. */
static void UpdateSafeAreaTarget()
{
	Rect target = GetSystemSafeArea();
	Dimension screen{(uint)_screen.width, (uint)_screen.height};
	if (!_safe.valid || screen.width != _safe.screen.width || screen.height != _safe.screen.height) {
		_safe = {true, screen, target, target, target, {}, false};
		return;
	}
	if (target.left == _safe.target.left && target.top == _safe.target.top && target.right == _safe.target.right && target.bottom == _safe.target.bottom) return;
	_safe.from = _safe.shown;
	_safe.target = target;
	_safe.start = std::chrono::steady_clock::now();
	_safe.sliding = true;
}

/**
 * Get the part of the screen the shell may use: not under the system bars, the camera cutout or rounded corners.
 * While a system bar slides in or out, this slides along.
 * @return The safe area, within the screen.
 */
static Rect GetSafeArea()
{
	return _safe.valid ? _safe.shown : GetSystemSafeArea();
}

/**
 * The insets between the safe area and the screen edges. Chrome along an edge reaches the screen edge, with its buttons
 * kept inside the safe area: spacer widgets in the dock and the construction toolbars take these sizes.
 * @return The inset at each edge, in screen pixels.
 */
Rect GetMobileEdgeGaps()
{
	Rect safe = GetSafeArea();
	return {safe.left, safe.top, std::max(0, _screen.width - 1 - safe.right), std::max(0, _screen.height - 1 - safe.bottom)};
}

/** Advance the slide of the safe area, and move the shell with it. */
static const IntervalTimer<TimerWindow> _safe_area_slide = {std::chrono::milliseconds(15), [](auto) {
	if (!_safe.sliding) return;
	float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - _safe.start) / SAFE_AREA_SLIDE_TIME;
	if (t >= 1.0f) {
		_safe.shown = _safe.target;
		_safe.sliding = false;
	} else {
		/* Ease out: fast at first, settling at the end. */
		float e = 1.0f - (1.0f - t) * (1.0f - t);
		auto lerp = [e](int a, int b) { return a + static_cast<int>(std::lround((b - a) * e)); };
		_safe.shown = {lerp(_safe.from.left, _safe.target.left), lerp(_safe.from.top, _safe.target.top),
				lerp(_safe.from.right, _safe.target.right), lerp(_safe.from.bottom, _safe.target.bottom)};
	}
	RelocateMobileWindows();
}};

/**
 * Move a window, keeping its viewport (if any) in place relative to the window.
 * @param w The window.
 * @param x New left edge.
 * @param y New top edge.
 */
static void MoveWindowTo(Window *w, int x, int y)
{
	if (w->left == x && w->top == y) return;
	w->SetDirty();
	if (w->viewport != nullptr) {
		w->viewport->left += x - w->left;
		w->viewport->top += y - w->top;
	}
	w->left = x;
	w->top = y;
	w->SetDirty();
}

/**
 * Resize a window as close as possible to the given size, respecting its resize steps and minimal size.
 * @param w The window.
 * @param width Wanted width.
 * @param height Wanted height.
 */
static void FitWindowTo(Window *w, int width, int height)
{
	int dx = width - w->width;
	int dy = height - w->height;
	if (w->resize.step_width > 1) dx -= ((dx % (int)w->resize.step_width) + (int)w->resize.step_width) % (int)w->resize.step_width;
	if (w->resize.step_height > 1) dy -= ((dy % (int)w->resize.step_height) + (int)w->resize.step_height) % (int)w->resize.step_height;
	if (w->resize.step_width == 0) dx = 0;
	if (w->resize.step_height == 0) dy = 0;
	if (dx != 0 || dy != 0) ResizeWindow(w, dx, dy, false);
}

/**
 * Draw a button face with its icon above a small label.
 * @param r Rectangle of the button.
 * @param sprite Icon.
 * @param label Text below the icon.
 * @param lowered Whether the button is drawn pressed.
 * @param disabled Whether the button is disabled.
 * @param font_size Font of the label.
 */
void DrawMobileIconButton(const Rect &r, SpriteID sprite, StringID label, bool lowered, bool disabled, FontSize font_size)
{
	int offset = lowered ? ScaleGUITrad(1) : 0;
	Dimension icon = GetSpriteSize(sprite);
	int text_height = GetCharacterHeight(font_size);
	int gap = ScaleGUITrad(2);
	int top = CentreBounds(r.top, r.bottom, icon.height + gap + text_height) + offset;
	DrawSprite(sprite, disabled ? PALETTE_TO_GREY : PAL_NONE, CentreBounds(r.left, r.right, icon.width) + offset, top);
	DrawString(r.left + offset, r.right + offset, top + icon.height + gap, label, disabled ? TextColour::Grey : TextColour::White, AlignmentH::Centre, false, font_size);
}

static void CloseMobileFlyouts()
{
	CloseWindowById(WindowClass::MobileBuildMenu, 0);
	CloseWindowById(WindowClass::MobileMenu, 0);
	CloseWindowById(WindowClass::MobileRailMore, 0);
}

static bool IsMobileFlyoutOpen()
{
	return FindWindowById(WindowClass::MobileBuildMenu, 0) != nullptr || FindWindowById(WindowClass::MobileMenu, 0) != nullptr || FindWindowById(WindowClass::MobileRailMore, 0) != nullptr;
}

/* --- Sheet slot --- */

static Window *GetSheetContent()
{
	if (_sheet.content_cls == WindowClass::Invalid) return nullptr;
	return FindWindowById(_sheet.content_cls, _sheet.content_num);
}

static Window *GetSheetOverlay()
{
	if (_sheet.overlay_cls == WindowClass::Invalid) return nullptr;
	return FindWindowById(_sheet.overlay_cls, _sheet.overlay_num);
}

static void CloseSheetContent()
{
	if (Window *w = GetSheetOverlay(); w != nullptr) w->Close();
	if (Window *w = GetSheetContent(); w != nullptr) w->Close();
	CloseWindowById(WindowClass::MobileSheetFiller, 0);
	_sheet.overlay_cls = WindowClass::Invalid;
	_sheet.content_cls = WindowClass::Invalid;
}

static bool IsSheetOpen()
{
	return FindWindowById(WindowClass::MobileHubTabs, 0) != nullptr || GetSheetContent() != nullptr;
}

/** Close the hub sheet: the tab window, its content and any overlay. */
static void CloseMobileSheet()
{
	CloseSheetContent();
	CloseWindowById(WindowClass::MobileHubTabs, 0);
}

/**
 * Open a window into the sheet slot as its content, replacing the current content.
 * @param cls Class of the window that \a open creates or brings to the front.
 * @param open Function opening the window.
 */
static void OpenSheetContent(WindowClass cls, void (*open)())
{
	CloseSheetContent();
	open();

	/* The window that was just opened, or brought to the front, is the frontmost of its class. */
	for (Window *w : Window::IterateFromFront()) {
		if (w->window_class != cls) continue;
		_sheet.content_cls = w->window_class;
		_sheet.content_num = w->window_number;
		break;
	}
	RelocateMobileWindows();
}

/**
 * The bar along the bottom that replaces the dock in portrait: the construction toolbar, or the bar shown while adding
 * stops to a train.
 * @return The bar, or \c nullptr.
 */
/**
 * The construction toolbar of the mobile layout: rail, or road and tram.
 * @return The toolbar, or \c nullptr when none is open.
 */
static Window *GetMobileConstructionToolbar()
{
	if (Window *w = GetMobileRailToolbar(); w != nullptr) return w;
	return GetMobileRoadToolbar();
}

static Window *GetMobileBottomBar()
{
	if (Window *w = GetMobileConstructionToolbar(); w != nullptr) return w;
	return FindWindowById(WindowClass::MobileStopsBar, 0);
}

/**
 * Where floating chrome at the top of the screen goes, for a horizontal span.
 * @param left Left edge of the span.
 * @param right Right edge of the span.
 * @param safe The safe area.
 * @return The top of the chrome.
 */
static int GetTopChromeY(int left, int right, const Rect &safe)
{
	int margin = ScaleGUITrad(MOBILE_MARGIN);
#ifdef __ANDROID__
	if (int y = AndroidGetTopChromeY(left, right, margin); y >= 0) return y;
#else
	(void)left;
	(void)right;
#endif
	return safe.top + margin;
}

/**
 * Get the rectangle of the sheet slot for the current orientation.
 * @return The slot, including the tab window at its top.
 */
static Rect GetSheetSlot()
{
	const Window *dock = FindWindowById(WindowClass::MobileDock, 0);
	Rect safe = GetSafeArea();
	if (IsLandscape()) {
		int dock_right = dock != nullptr ? dock->left + dock->width : safe.left;
		int width = std::min(ScaleGUITrad(MOBILE_SIDE_PANEL_WIDTH), safe.right + 1 - dock_right);
		return {safe.right + 1 - width, safe.top, safe.right, safe.bottom};
	}

	/* Above the dock, or above the rail toolbar that replaces it in portrait. */
	const Window *toolbar = GetMobileBottomBar();
	int bottom = toolbar != nullptr ? toolbar->top : (dock != nullptr ? dock->top : safe.bottom + 1);
	int full_height = bottom - safe.top - ScaleGUITrad(MOBILE_MAP_STRIP);
	int height = _sheet.full ? full_height : std::min(ScaleGUITrad(MOBILE_HALF_SHEET_HEIGHT), full_height);
	return {safe.left, bottom - height, safe.right, bottom - 1};
}

/* --- Build --- */

static SpriteID GetBuildCategorySprite(BuildCategory category)
{
	switch (category) {
		case BuildCategory::Road: return SPR_IMG_BUILDROAD;
		case BuildCategory::Tram: return SPR_IMG_BUILDTRAMS;
		case BuildCategory::Water: return SPR_IMG_BUILDWATER;
		case BuildCategory::Air: return SPR_IMG_BUILDAIR;
		case BuildCategory::Landscape: return SPR_IMG_LANDSCAPING;
		default: return SPR_IMG_BUILDRAIL;
	}
}

static bool IsBuildCategoryAvailable(BuildCategory category)
{
	if (!Company::IsValidID(_local_company)) return false;
	switch (category) {
		case BuildCategory::Road: return HasAnyRoadTypesAvail(_local_company, RoadTramType::Road);
		case BuildCategory::Tram: return HasAnyRoadTypesAvail(_local_company, RoadTramType::Tram);
		default: return true;
	}
}

static bool IsConstructionToolbarOpen()
{
	return FindWindowByClass(WindowClass::BuildToolbar) != nullptr || FindWindowById(WindowClass::ScenarioGenerateLandscape, 0) != nullptr;
}

static void CloseConstructionToolbars()
{
	ResetObjectToPlace();
	CloseWindowByClass(WindowClass::BuildToolbar);
	CloseWindowById(WindowClass::ScenarioGenerateLandscape, 0);
}

/**
 * Open the construction toolbar of a category, closing any other hub, flyout or toolbar.
 * @param category The category.
 */
static void OpenBuildCategory(BuildCategory category)
{
	if (!IsBuildCategoryAvailable(category)) return;

	CloseMobileFlyouts();
	CloseMobileSheet();
	CloseConstructionToolbars();

	switch (category) {
		case BuildCategory::Rail: ShowBuildRailToolbar(_last_built_railtype); break;
		case BuildCategory::Road: ShowBuildRoadToolbar(_last_built_roadtype); break;
		case BuildCategory::Tram: ShowBuildRoadToolbar(_last_built_tramtype); break;
		case BuildCategory::Water: ShowBuildDocksToolbar(); break;
		case BuildCategory::Air: ShowBuildAirToolbar(); break;
		case BuildCategory::Landscape: ShowTerraformToolbar(); break;
		default: NOT_REACHED();
	}
	_last_build_category = category;
	SetWindowDirty(WindowClass::MobileDock, 0);
	RelocateMobileWindows();
}

/* --- Status pill --- */

/** The status pill: pause, fast forward, and the date and money. */
struct MobileStatusWindow : Window {
	MobileStatusWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
		this->SetWidgetDisabledState(WID_MS_PAUSE, _networking && !_network_server);
		this->SetWidgetDisabledState(WID_MS_FFWD, _networking);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_MS_INFO) return;

		Dimension date = GetStringBoundingBox(GetString(STR_JUST_DATE_LONG, GetParamMaxValue(TimerGameCalendar::DateAtStartOfYear(CalendarTime::MAX_YEAR).base())));
		Dimension money = GetStringBoundingBox(GetString(STR_JUST_CURRENCY_LONG, GetParamMaxDigits(10)));
		size.width = std::max<uint>(size.width, std::max(date.width, money.width) + ScaleGUITrad(7) * 2);
		size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal) * 2 + padding.height);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MS_INFO) return;

		Rect tr = r.Shrink(ScaleGUITrad(7), 0);
		int line_height = GetCharacterHeight(FontSize::Normal);
		int top = CentreBounds(r.top, r.bottom, line_height * 2);
		/* While paused the date reads "Paused", so a game left paused (e.g. by leaving the app) is obvious. */
		if (_pause_mode.Any()) {
			DrawString(tr.left, tr.right, top, STR_MOBILE_STATUS_PAUSED, TextColour::Yellow);
		} else {
			DrawString(tr.left, tr.right, top, GetString(STR_JUST_DATE_LONG, TimerGameCalendar::date), TextColour::White);
		}

		top += line_height;
		if (_local_company == COMPANY_SPECTATOR) {
			DrawString(tr.left, tr.right, top, STR_STATUSBAR_SPECTATOR);
		} else if (_settings_game.difficulty.infinite_money) {
			DrawString(tr.left, tr.right, top, STR_STATUSBAR_INFINITE_MONEY);
		} else if (const Company *c = Company::GetIfValid(_local_company); c != nullptr) {
			DrawString(tr.left, tr.right, top, GetString(STR_JUST_CURRENCY_LONG, c->money), c->money < 0 ? TextColour::Red : TextColour::Yellow);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override;

	/** Keep the toggles in sync with the game, and the money up to date. */
	const IntervalTimer<TimerWindow> refresh_interval = {std::chrono::milliseconds(30), [this](auto) {
		if (this->IsWidgetLowered(WID_MS_PAUSE) != _pause_mode.Any()) {
			this->ToggleWidgetLoweredState(WID_MS_PAUSE);
			/* Paused, the button shows what tapping it does: play. */
			this->GetWidget<NWidgetCore>(WID_MS_PAUSE)->SetSprite(_pause_mode.Any() ? SPR_IMG_PLAY_MUSIC : SPR_IMG_PAUSE);
			this->SetWidgetDirty(WID_MS_PAUSE);
			this->SetWidgetDirty(WID_MS_INFO);
		}
		if (this->IsWidgetLowered(WID_MS_FFWD) != (_game_speed != 100)) {
			this->ToggleWidgetLoweredState(WID_MS_FFWD);
			this->SetWidgetDirty(WID_MS_FFWD);
		}
	}};

	const IntervalTimer<TimerWindow> money_interval = {std::chrono::milliseconds(250), [this](auto) {
		this->SetWidgetDirty(WID_MS_INFO);
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_status_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, Colours::Grey, WID_MS_PAUSE), SetMinimalSize(40, 40), SetSpriteTip(SPR_IMG_PAUSE, STR_TOOLBAR_TOOLTIP_PAUSE_GAME),
		NWidget(WWT_IMGBTN, Colours::Grey, WID_MS_FFWD), SetMinimalSize(40, 40), SetSpriteTip(SPR_IMG_FASTFORWARD, STR_TOOLBAR_TOOLTIP_FORWARD),
		NWidget(WWT_PUSHBTN, Colours::Grey, WID_MS_INFO), SetMinimalSize(108, 40), SetToolTip(STR_MOBILE_STATUS_INFO_TOOLTIP),
	EndContainer(),
};

static WindowDesc _mobile_status_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileStatus, WindowClass::None,
	{WindowDefaultFlag::NoFocus, WindowDefaultFlag::NoClose},
	_nested_mobile_status_widgets
);

/* --- Hub sheets --- */

/**
 * Draw the title of a sheet: white, vertically centred, inset from the left edge.
 * @param r The title panel.
 * @param text The title.
 */
void DrawMobileTitle(const Rect &r, std::string_view text)
{
	Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
	DrawString(tr.left, tr.right, CentreBounds(r.top, r.bottom, GetCharacterHeight(FontSize::Normal)), text, TextColour::White);
}

/** The tab window at the top of a hub sheet. */
struct MobileHubTabsWindow : Window {
	MobileHub hub; ///< The hub shown.

	MobileHubTabsWindow(WindowDesc &desc, MobileHub hub) : Window(desc), hub(hub)
	{
		const HubInfo &info = _hubs[to_underlying(hub)];

		this->CreateNestedTree();
		for (WidgetID widget : {WID_MHUB_BACKGROUND, WID_MHUB_CAPTION, WID_MHUB_EXPAND, WID_MHUB_CLOSE}) {
			this->GetWidget<NWidgetCore>(widget)->colour = info.colour;
		}
		for (WidgetID widget = WID_MHUB_TAB_FIRST; widget <= WID_MHUB_TAB_LAST; ++widget) {
			this->GetWidget<NWidgetCore>(widget)->colour = info.colour;
		}
		for (size_t i = 0; i <= WID_MHUB_TAB_LAST - WID_MHUB_TAB_FIRST; ++i) {
			NWidgetCore *nwc = this->GetWidget<NWidgetCore>(WID_MHUB_TAB_FIRST + static_cast<WidgetID>(i));
			if (i < info.tabs.size()) nwc->SetString(info.tabs[i].label);
		}
		this->GetWidget<NWidgetStacked>(WID_MHUB_SEL_TAB5)->SetDisplayedPlane(info.tabs.size() > 4 ? 0 : SZSP_NONE);
		this->UpdateOrientation(false);
		this->FinishInitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
	}

	/**
	 * Show the expand button in portrait only.
	 * @param reinit Whether the window is already initialised and needs re-initialising.
	 */
	void UpdateOrientation(bool reinit)
	{
		int plane = IsLandscape() ? SZSP_NONE : 0;
		NWidgetStacked *sel = this->GetWidget<NWidgetStacked>(WID_MHUB_SEL_EXPAND);
		if (sel->shown_plane == plane) return;
		sel->SetDisplayedPlane(plane);
		if (reinit) this->ReInit();
	}

	void Close(int data = 0) override
	{
		CloseSheetContent();
		this->Window::Close(data);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_MHUB_CAPTION:
				DrawMobileTitle(r, GetString(_hubs[to_underlying(this->hub)].caption));
				break;

			case WID_MHUB_EXPAND: {
				SpriteID sprite = _sheet.full ? SPR_ARROW_DOWN : SPR_ARROW_UP;
				Dimension d = GetSpriteSize(sprite);
				DrawSprite(sprite, PAL_NONE, CentreBounds(r.left, r.right, d.width), CentreBounds(r.top, r.bottom, d.height));
				break;
			}
		}
	}

	void OnPaint() override
	{
		uint8_t current = _last_hub_tab[to_underlying(this->hub)];
		bool showing_tab = GetSheetContent() != nullptr && GetSheetContent()->window_class == _hubs[to_underlying(this->hub)].tabs[current].content;
		for (WidgetID widget = WID_MHUB_TAB_FIRST; widget <= WID_MHUB_TAB_LAST; ++widget) {
			this->SetWidgetLoweredState(widget, showing_tab && widget - WID_MHUB_TAB_FIRST == current);
		}
		this->DrawWidgets();
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MHUB_CLOSE:
				this->Close();
				break;

			case WID_MHUB_EXPAND:
				_sheet.full = !_sheet.full;
				RelocateMobileWindows();
				this->SetDirty();
				break;

			default:
				if (widget >= WID_MHUB_TAB_FIRST && widget <= WID_MHUB_TAB_LAST) this->OpenTab(widget - WID_MHUB_TAB_FIRST);
				break;
		}
	}

	/**
	 * Show a tab in the sheet slot.
	 * @param index Tab to show.
	 */
	void OpenTab(uint index)
	{
		const HubInfo &info = _hubs[to_underlying(this->hub)];
		if (index >= info.tabs.size()) return;
		_last_hub_tab[to_underlying(this->hub)] = index;
		OpenSheetContent(info.tabs[index].content, info.tabs[index].open);
		this->SetDirty();
	}

	/** Reflect the content being closed by its own close box. */
	const IntervalTimer<TimerWindow> refresh_interval = {std::chrono::milliseconds(250), [this](auto) {
		this->SetDirty();
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_hub_tabs_widgets = {
	NWidget(WWT_PANEL, Colours::Grey, WID_MHUB_BACKGROUND),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_PANEL, Colours::Grey, WID_MHUB_CAPTION), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), EndContainer(),
			NWidget(NWID_SELECTION, Colours::Invalid, WID_MHUB_SEL_EXPAND),
				NWidget(WWT_PUSHBTN, Colours::Grey, WID_MHUB_EXPAND), SetMinimalSize(40, 40), SetToolTip(STR_MOBILE_HUB_EXPAND_TOOLTIP),
			EndContainer(),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MHUB_CLOSE), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_CROSS, STR_MOBILE_HUB_CLOSE_TOOLTIP),
		EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_TEXTBTN, Colours::Grey, WID_MHUB_TAB_FIRST + 0), SetMinimalSize(56, 40), SetFill(1, 0), SetResize(1, 0),
			NWidget(WWT_TEXTBTN, Colours::Grey, WID_MHUB_TAB_FIRST + 1), SetMinimalSize(56, 40), SetFill(1, 0), SetResize(1, 0),
			NWidget(WWT_TEXTBTN, Colours::Grey, WID_MHUB_TAB_FIRST + 2), SetMinimalSize(56, 40), SetFill(1, 0), SetResize(1, 0),
			NWidget(WWT_TEXTBTN, Colours::Grey, WID_MHUB_TAB_FIRST + 3), SetMinimalSize(56, 40), SetFill(1, 0), SetResize(1, 0),
			NWidget(NWID_SELECTION, Colours::Invalid, WID_MHUB_SEL_TAB5),
				NWidget(WWT_TEXTBTN, Colours::Grey, WID_MHUB_TAB_FIRST + 4), SetMinimalSize(56, 40), SetFill(1, 0), SetResize(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _mobile_hub_tabs_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileHubTabs, WindowClass::None,
	{WindowDefaultFlag::NoFocus},
	_nested_mobile_hub_tabs_widgets
);

/**
 * Open a hub sheet on its last shown tab, or close it when it is already open.
 * @param hub The hub.
 */
static void ToggleMobileHub(MobileHub hub)
{
	CloseMobileFlyouts();

	const MobileHubTabsWindow *open = dynamic_cast<MobileHubTabsWindow *>(FindWindowById(WindowClass::MobileHubTabs, 0));
	bool was_open = open != nullptr && open->hub == hub;
	CloseMobileSheet();
	if (was_open) return;

	CloseConstructionToolbars();
	MobileHubTabsWindow *w = new MobileHubTabsWindow(_mobile_hub_tabs_desc, hub);
	w->OpenTab(_last_hub_tab[to_underlying(hub)]);
	SetWindowDirty(WindowClass::MobileDock, 0);
}

/**
 * Show the hub of the company on a given tab.
 * @param hub The hub.
 * @param tab The tab.
 */
static void ShowMobileHubTab(MobileHub hub, uint8_t tab)
{
	_last_hub_tab[to_underlying(hub)] = tab;
	MobileHubTabsWindow *open = dynamic_cast<MobileHubTabsWindow *>(FindWindowById(WindowClass::MobileHubTabs, 0));
	if (open != nullptr && open->hub == hub) {
		open->OpenTab(tab);
		return;
	}
	ToggleMobileHub(hub);
}

void MobileStatusWindow::OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count)
{
	switch (widget) {
		case WID_MS_PAUSE:
			if (Command<Commands::Pause>::Post(PauseMode::Normal, _pause_mode.None())) SndConfirmBeep();
			break;

		case WID_MS_FFWD:
			ChangeGameSpeed(_game_speed == 100);
			SndClickBeep();
			break;

		case WID_MS_INFO:
			if (_local_company != COMPANY_SPECTATOR) ShowMobileHubTab(MobileHub::Company, 0);
			break;
	}
}

/* --- Build flyout --- */

/** The flyout picking a construction category. */
struct MobileBuildMenuWindow : Window {
	static constexpr int RAILTYPE_ROW_HEIGHT = 40; ///< Height of a rail (or road) type row, in GUI pixels at 1x.

	/** A rail or road type in the list below the categories. */
	struct TypeRow {
		int type; ///< The RailType or RoadType.
		SpriteID sprite; ///< Its icon.
		StringID name; ///< Its name.
		Money cost; ///< Its cost per piece.
		bool available; ///< Whether the company can build it.
	};

	Window *toolbar; ///< The construction toolbar this was opened from, if any.
	std::vector<TypeRow> types; ///< Types listed: rail types for the rail toolbar, road or tram types for the road toolbar.
	bool road = false; ///< The types are road (or tram) types.

	MobileBuildMenuWindow(WindowDesc &desc, Window *toolbar = nullptr) : Window(desc), toolbar(toolbar)
	{
		const Company *company = Company::GetIfValid(_local_company);
		if (toolbar != nullptr && toolbar->window_number == TransportType::Rail) {
			RailTypes used = GetRailTypes(true);
			used.Reset(_railtypes_hidden_mask);
			for (RailType rt : _sorted_railtypes) {
				if (!used.Test(rt)) continue;
				const RailTypeInfo *rti = GetRailTypeInfo(rt);
				this->types.push_back({rt, rti->gui_sprites.build_x_rail, rti->strings.menu_text, RailBuildCost(rt), company != nullptr && company->avail_railtypes.Test(rt)});
			}
		} else if (toolbar != nullptr && toolbar->window_number == TransportType::Road) {
			/* Road types, or tram types, as the toolbar builds. */
			this->road = true;
			RoadTramType rtt = GetRoadTramType(GetCurrentRoadType());
			RoadTypes used = GetRoadTypes(true);
			used.Reset(_roadtypes_hidden_mask);
			for (RoadType rt : _sorted_roadtypes) {
				if (!used.Test(rt) || GetRoadTramType(rt) != rtt) continue;
				const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
				this->types.push_back({rt, rti->gui_sprites.build_x_road, rti->strings.menu_text, RoadBuildCost(rt), company != nullptr && company->avail_roadtypes.Test(rt)});
			}
		}

		this->CreateNestedTree();
		this->GetWidget<NWidgetStacked>(WID_MB_SEL_RAILTYPES)->SetDisplayedPlane(this->types.empty() ? SZSP_NONE : 0);
		this->FinishInitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
		for (BuildCategory c = BuildCategory::Rail; c != BuildCategory::End; c = static_cast<BuildCategory>(to_underlying(c) + 1)) {
			this->SetWidgetDisabledState(WID_MB_RAIL + to_underlying(c), !IsBuildCategoryAvailable(c));
		}
		if (toolbar != nullptr) this->LowerWidget(WID_MB_RAIL + to_underlying(_last_build_category == BuildCategory::End ? BuildCategory::Rail : _last_build_category));
	}

	/**
	 * Is a listed type the one the toolbar builds now?
	 * @param row The type.
	 * @return \c true for the current type.
	 */
	bool IsCurrent(const TypeRow &row) const
	{
		return this->road ? row.type == GetCurrentRoadType() : row.type == GetCurrentRailType();
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_MB_RAILTYPES) return;
		/* As wide as the longest row, so NewGRF type names aren't cut; at most the visible map width less 16. */
		uint longest = 0;
		for (const TypeRow &row : this->types) {
			uint width = GetSpriteSize(row.sprite).width + GetStringBoundingBox(row.name).width +
					GetStringBoundingBox(GetString(STR_JUST_CURRENCY_SHORT, row.cost)).width + 2 * WidgetDimensions::scaled.hsep_wide;
			longest = std::max(longest, width);
		}
		longest += WidgetDimensions::scaled.framerect.Horizontal() + padding.width;
		Rect map = GetMobileVisibleMapRect();
		uint cap = std::max<int>(ScaleGUITrad(176), map.Width() - ScaleGUITrad(16));
		size.width = std::max<uint>(size.width, std::min(std::max<uint>(longest, ScaleGUITrad(176)), cap));
		size.height = ScaleGUITrad(RAILTYPE_ROW_HEIGHT) * std::max<size_t>(1, this->types.size());
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		static const StringID labels[] = {STR_MOBILE_BUILD_RAIL, STR_MOBILE_BUILD_ROAD, STR_MOBILE_BUILD_TRAM, STR_MOBILE_BUILD_WATER, STR_MOBILE_BUILD_AIRPORT, STR_MOBILE_BUILD_LANDSCAPE};
		if (widget == WID_MB_RAILTYPES) {
			int row_height = ScaleGUITrad(RAILTYPE_ROW_HEIGHT);
			Rect row = r.WithHeight(row_height).Shrink(WidgetDimensions::scaled.framerect.left, 0);
			for (const TypeRow &type : this->types) {
				if (this->IsCurrent(type)) GfxFillRect(row, GetColourGradient(Colours::Grey, Shade::Lighter));
				Dimension icon = GetSpriteSize(type.sprite);
				DrawSprite(type.sprite, type.available ? PAL_NONE : PALETTE_TO_GREY, row.left, CentreBounds(row.top, row.bottom, icon.height));
				Rect text = row.Indent(icon.width + WidgetDimensions::scaled.hsep_wide, false);
				int y = CentreBounds(row.top, row.bottom, GetCharacterHeight(FontSize::Normal));
				TextColour colour = type.available ? TextColour::Black : TextColour::Grey;
				std::string cost = GetString(STR_JUST_CURRENCY_SHORT, type.cost);
				int cost_width = DrawString(text.left, text.right, y, cost, colour, AlignmentH::End);
				DrawString(text.left, cost_width - WidgetDimensions::scaled.hsep_wide, y, type.name, colour);
				row = row.Translate(0, row_height);
			}
			return;
		}
		if (widget < WID_MB_RAIL || widget > WID_MB_LANDSCAPE) return;
		DrawMobileIconButton(r, GetBuildCategorySprite(static_cast<BuildCategory>(widget - WID_MB_RAIL)), labels[widget - WID_MB_RAIL], this->IsWidgetLowered(widget), this->IsWidgetDisabled(widget));
	}

	void OnClick(Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget == WID_MB_RAILTYPES) {
			const NWidgetBase *list = this->GetWidget<NWidgetBase>(WID_MB_RAILTYPES);
			size_t index = (pt.y - list->pos_y) / ScaleGUITrad(RAILTYPE_ROW_HEIGHT);
			if (index >= this->types.size() || !this->types[index].available) return;
			int type = this->types[index].type;
			bool road = this->road;
			this->Close();
			/* Switch the open toolbar to the type; the tool and any selection in review stay. */
			if (road) {
				RoadType rt = static_cast<RoadType>(type);
				if (RoadTypeIsRoad(rt)) {
					_last_built_roadtype = rt;
				} else {
					_last_built_tramtype = rt;
				}
				ShowBuildRoadToolbar(rt);
			} else {
				_last_built_railtype = static_cast<RailType>(type);
				ShowBuildRailToolbar(static_cast<RailType>(type));
			}
			return;
		}
		if (widget < WID_MB_RAIL || widget > WID_MB_LANDSCAPE) return;
		BuildCategory category = static_cast<BuildCategory>(widget - WID_MB_RAIL);
		/* The category of the open toolbar just closes the flyout. */
		if (this->toolbar != nullptr) {
			bool same = false;
			if (this->toolbar->window_number == TransportType::Rail) same = category == BuildCategory::Rail;
			if (this->toolbar->window_number == TransportType::Road) same = category == (RoadTypeIsRoad(GetCurrentRoadType()) ? BuildCategory::Road : BuildCategory::Tram);
			if (same) {
				this->Close();
				return;
			}
		}
		OpenBuildCategory(category);
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_build_menu_widgets = {
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_VERTICAL), SetPadding(4), SetPIP(0, 4, 0),
			NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
				NWidget(WWT_PUSHBTN, Colours::Grey, WID_MB_RAIL), SetMinimalSize(56, 48), SetToolTip(STR_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TRACK),
				NWidget(WWT_PUSHBTN, Colours::Grey, WID_MB_ROAD), SetMinimalSize(56, 48), SetToolTip(STR_TOOLBAR_TOOLTIP_BUILD_ROADS),
				NWidget(WWT_PUSHBTN, Colours::Grey, WID_MB_TRAM), SetMinimalSize(56, 48), SetToolTip(STR_TOOLBAR_TOOLTIP_BUILD_TRAMWAYS),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
				NWidget(WWT_PUSHBTN, Colours::Grey, WID_MB_WATER), SetMinimalSize(56, 48), SetToolTip(STR_TOOLBAR_TOOLTIP_BUILD_SHIP_DOCKS),
				NWidget(WWT_PUSHBTN, Colours::Grey, WID_MB_AIR), SetMinimalSize(56, 48), SetToolTip(STR_TOOLBAR_TOOLTIP_BUILD_AIRPORTS),
				NWidget(WWT_PUSHBTN, Colours::Grey, WID_MB_LANDSCAPE), SetMinimalSize(56, 48), SetToolTip(STR_TOOLBAR_TOOLTIP_LANDSCAPING),
			EndContainer(),
			NWidget(NWID_SELECTION, Colours::Invalid, WID_MB_SEL_RAILTYPES),
				NWidget(WWT_INSET, Colours::Grey, WID_MB_RAILTYPES), SetFill(1, 0), SetToolTip(STR_MOBILE_BUILD_RAILTYPE_TOOLTIP), EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _mobile_build_menu_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileBuildMenu, WindowClass::None,
	{},
	_nested_mobile_build_menu_widgets
);

/* --- Menu flyout --- */

/** Entries of the Map tools dropdown. */
enum class MapToolsEntry : uint8_t {
	ExtraViewport,
	SignList,
	LinkGraphLegend,
};

/** The game menu flyout. */
struct MobileMenuWindow : Window {
	MobileMenuWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
		/* Per-AI settings are not sent from the server to the clients, see the desktop toolbar. */
		this->SetWidgetDisabledState(WID_MM_SCRIPTS, _networking && !_network_server);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget == WID_MM_MAP_TOOLS) {
			DropDownList list;
			list.push_back(MakeDropDownListStringItem(STR_MAP_MENU_EXTRA_VIEWPORT, to_underlying(MapToolsEntry::ExtraViewport)));
			list.push_back(MakeDropDownListStringItem(STR_MAP_MENU_SIGN_LIST, to_underlying(MapToolsEntry::SignList)));
			list.push_back(MakeDropDownListStringItem(STR_MAP_MENU_LINGRAPH_LEGEND, to_underlying(MapToolsEntry::LinkGraphLegend)));
			ShowDropDownList(this, std::move(list), -1, widget);
			return;
		}

		/* Close the flyout first, so the window that opens gets the focus. */
		this->Close();
		switch (widget) {
			case WID_MM_MESSAGES: ShowMessageHistory(); break;
			case WID_MM_GAME_OPTIONS: ShowGameOptions(); break;
			case WID_MM_SETTINGS: ShowMobileSettings(); break;
			case WID_MM_SCRIPTS: ShowAIConfigWindow(); break;
			case WID_MM_NEWGRF: ShowNewGRFSettings(!_networking && _settings_client.gui.UserIsAllowedToChangeNewGRFs(), true, true, _grfconfig); break;
			case WID_MM_TRANSPARENCY: ShowTransparencyToolbar(); break;
			case WID_MM_SAVE: ShowSaveLoadDialog(AbstractFileType::Savegame, SaveLoadOperation::Save); break;
			case WID_MM_LOAD: ShowSaveLoadDialog(AbstractFileType::Savegame, SaveLoadOperation::Load); break;
			case WID_MM_MUSIC: ShowMusicWindow(); break;
			case WID_MM_HELP: ShowHelpWindow(); break;
			case WID_MM_ABOUT: ShowAboutWindow(); break;
			case WID_MM_QUIT: AskExitToGameMenu(); break;
			case WID_MM_QUIT_GAME: AskExitGame(); break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		if (widget != WID_MM_MAP_TOOLS) return;
		this->Close();
		switch (static_cast<MapToolsEntry>(index)) {
			case MapToolsEntry::ExtraViewport: ShowExtraViewportWindow(); break;
			case MapToolsEntry::SignList: ShowSignList(); break;
			case MapToolsEntry::LinkGraphLegend: ShowLinkGraphLegend(); break;
		}
	}
};

/**
 * Make one row of the menu flyout.
 * @param widget The widget of the row.
 * @param string The text of the row.
 * @param colour The colour of the row.
 */
#define MOBILE_MENU_ROW(widget, string, colour) \
	NWidget(WWT_PUSHTXTBTN, colour, widget), SetMinimalSize(180, 40), SetFill(1, 0), SetStringTip(string), SetAlignment({AlignmentH::Start, AlignmentV::Middle})

/* The round 4 design, section 7: game options, AI/game scripts and NewGRFs moved into Settings; pause and speed stay in the pill. */
static constexpr std::initializer_list<NWidgetPart> _nested_mobile_menu_one_column_widgets = {
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_VERTICAL), SetPadding(4),
			MOBILE_MENU_ROW(WID_MM_SAVE, STR_FILE_MENU_SAVE_GAME, Colours::Grey),
			MOBILE_MENU_ROW(WID_MM_LOAD, STR_FILE_MENU_LOAD_GAME, Colours::Grey),
			MOBILE_MENU_ROW(WID_MM_SETTINGS, STR_MOBILE_MENU_SETTINGS, Colours::Grey),
			MOBILE_MENU_ROW(WID_MM_MESSAGES, STR_MOBILE_MENU_MESSAGES, Colours::Grey),
			MOBILE_MENU_ROW(WID_MM_MAP_TOOLS, STR_MOBILE_MENU_MAP_TOOLS, Colours::Grey),
			MOBILE_MENU_ROW(WID_MM_TRANSPARENCY, STR_SETTINGS_MENU_TRANSPARENCY_OPTIONS, Colours::Grey),
			MOBILE_MENU_ROW(WID_MM_HELP, STR_ABOUT_MENU_HELP, Colours::Grey),
			MOBILE_MENU_ROW(WID_MM_QUIT, STR_MOBILE_MENU_QUIT, Colours::Red),
			MOBILE_MENU_ROW(WID_MM_QUIT_GAME, STR_MOBILE_MENU_QUIT_GAME, Colours::Red),
		EndContainer(),
	EndContainer(),
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_menu_two_column_widgets = {
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_HORIZONTAL), SetPadding(4), SetPIP(0, 4, 0),
			NWidget(NWID_VERTICAL),
				MOBILE_MENU_ROW(WID_MM_SAVE, STR_FILE_MENU_SAVE_GAME, Colours::Grey),
				MOBILE_MENU_ROW(WID_MM_LOAD, STR_FILE_MENU_LOAD_GAME, Colours::Grey),
				MOBILE_MENU_ROW(WID_MM_MAP_TOOLS, STR_MOBILE_MENU_MAP_TOOLS, Colours::Grey),
				MOBILE_MENU_ROW(WID_MM_TRANSPARENCY, STR_SETTINGS_MENU_TRANSPARENCY_OPTIONS, Colours::Grey),
				NWidget(NWID_SPACER), SetMinimalSize(0, 40), SetFill(0, 1),
			EndContainer(),
			NWidget(NWID_VERTICAL),
				MOBILE_MENU_ROW(WID_MM_SETTINGS, STR_MOBILE_MENU_SETTINGS, Colours::Grey),
				MOBILE_MENU_ROW(WID_MM_MESSAGES, STR_MOBILE_MENU_MESSAGES, Colours::Grey),
				MOBILE_MENU_ROW(WID_MM_HELP, STR_ABOUT_MENU_HELP, Colours::Grey),
				MOBILE_MENU_ROW(WID_MM_QUIT, STR_MOBILE_MENU_QUIT, Colours::Red),
				MOBILE_MENU_ROW(WID_MM_QUIT_GAME, STR_MOBILE_MENU_QUIT_GAME, Colours::Red),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

#undef MOBILE_MENU_ROW

/** Menu flyout for portrait: one column, short enough to fit below the status pill. */
static WindowDesc _mobile_menu_portrait_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileMenu, WindowClass::None,
	{},
	_nested_mobile_menu_one_column_widgets
);

/** Menu flyout for landscape: two columns, so it fits without scrolling (variant B). */
static WindowDesc _mobile_menu_landscape_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileMenu, WindowClass::None,
	{},
	_nested_mobile_menu_two_column_widgets
);

/* --- Hub dock --- */

static void OnMobileBuildClick()
{
	if (FindWindowById(WindowClass::MobileBuildMenu, 0) != nullptr) {
		CloseMobileFlyouts();
		return;
	}
	if (IsConstructionToolbarOpen()) {
		CloseConstructionToolbars();
		return;
	}

	if (_last_build_category == BuildCategory::End || _settings_client.gui.mobile_build_picker || !IsBuildCategoryAvailable(_last_build_category)) {
		CloseMobileFlyouts();
		CloseMobileSheet();
		new MobileBuildMenuWindow(_mobile_build_menu_desc);
		RelocateMobileWindows();
		return;
	}
	OpenBuildCategory(_last_build_category);
}

static void OnMobileMenuClick()
{
	bool was_open = FindWindowById(WindowClass::MobileMenu, 0) != nullptr;
	CloseMobileFlyouts();
	if (was_open) return;

	CloseMobileSheet();
	new MobileMenuWindow(IsLandscape() ? _mobile_menu_landscape_desc : _mobile_menu_portrait_desc);
	RelocateMobileWindows();
}

/** Stops the selected construction tool of toolbars without a Cancel of their own (road, landscaping, ...). */
struct MobileToolCancelWindow : Window {
	MobileToolCancelWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget == WID_MTC_CANCEL) ResetObjectToPlace();
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_tool_cancel_widgets = {
	NWidget(WWT_PUSHTXTBTN, Colours::Red, WID_MTC_CANCEL), SetMinimalSize(96, 40), SetStringTip(STR_MOBILE_TOOL_CANCEL, STR_MOBILE_TOOL_CANCEL_TOOLTIP),
};

static WindowDesc _mobile_tool_cancel_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileToolCancel, WindowClass::None,
	{WindowDefaultFlag::NoFocus, WindowDefaultFlag::NoClose},
	_nested_mobile_tool_cancel_widgets
);

/**
 * Should the floating Cancel show: a construction tool is selected, from a toolbar without a Cancel of its own.
 * @return \c true when it should show.
 */
static bool WantToolCancel()
{
	if ((_thd.place_mode & HT_DRAG_MASK) == HT_NONE) return false;
	const Window *w = _thd.GetCallbackWnd();
	/* Not for the construction toolbar (it has its own Cancel), nor while adding stops (the stops bar has Done). */
	return w != nullptr && w != GetMobileConstructionToolbar() && w->window_class != WindowClass::MainWindow && w->window_class != WindowClass::VehicleView;
}

/** The hub dock: a left rail in landscape and a bottom bar in portrait, with the same widgets. */
struct MobileDockWindow : Window {
	const bool landscape; ///< Which of the two layouts this is.

	MobileDockWindow(WindowDesc &desc, bool landscape) : Window(desc), landscape(landscape)
	{
		this->InitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
		this->UpdateButtonStates();
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		Rect gaps = GetMobileEdgeGaps();
		switch (widget) {
			case WID_MD_GAP_LEFT: size = {static_cast<uint>(gaps.left), 0}; break;
			case WID_MD_GAP_TOP: size = {0, static_cast<uint>(gaps.top)}; break;
			case WID_MD_GAP_BOTTOM: size = {0, static_cast<uint>(gaps.bottom)}; break;
			default: break;
		}
	}

	/** Lower the button of the hub or flyout that is open, and disable Build for spectators. */
	void UpdateButtonStates()
	{
		const MobileHubTabsWindow *hub = dynamic_cast<MobileHubTabsWindow *>(FindWindowById(WindowClass::MobileHubTabs, 0));
		bool changed = false;
		auto set_lowered = [&](WidgetID widget, bool lowered) {
			if (this->IsWidgetLowered(widget) == lowered) return;
			this->SetWidgetLoweredState(widget, lowered);
			changed = true;
		};
		set_lowered(WID_MD_MENU, FindWindowById(WindowClass::MobileMenu, 0) != nullptr);
		set_lowered(WID_MD_WORLD, hub != nullptr && hub->hub == MobileHub::World);
		set_lowered(WID_MD_VEHICLES, hub != nullptr && hub->hub == MobileHub::Vehicles);
		set_lowered(WID_MD_COMPANY, hub != nullptr && hub->hub == MobileHub::Company);
		set_lowered(WID_MD_BUILD, FindWindowById(WindowClass::MobileBuildMenu, 0) != nullptr || IsConstructionToolbarOpen());

		/* The floating Cancel follows the selected tool. */
		Window *cancel = FindWindowById(WindowClass::MobileToolCancel, 0);
		if (WantToolCancel() != (cancel != nullptr)) {
			if (cancel != nullptr) {
				cancel->Close();
			} else {
				new MobileToolCancelWindow(_mobile_tool_cancel_desc);
			}
			RelocateMobileWindows();
		}

		bool spectator = !Company::IsValidID(_local_company);
		if (this->IsWidgetDisabled(WID_MD_BUILD) != spectator) {
			this->SetWidgetDisabledState(WID_MD_BUILD, spectator);
			changed = true;
		}
		if (changed) this->SetDirty();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		SpriteID sprite;
		StringID label;
		switch (widget) {
			case WID_MD_MENU: sprite = SPR_IMG_SETTINGS; label = STR_MOBILE_DOCK_MENU; break;
			case WID_MD_COMPANY: sprite = SPR_IMG_COMPANY_FINANCE; label = STR_MOBILE_DOCK_COMPANY; break;
			case WID_MD_WORLD: sprite = SPR_IMG_TOWN; label = STR_MOBILE_DOCK_WORLD; break;
			case WID_MD_VEHICLES: sprite = SPR_IMG_TRAINLIST; label = STR_MOBILE_DOCK_VEHICLES; break;
			case WID_MD_BUILD: sprite = GetBuildCategorySprite(_last_build_category); label = STR_MOBILE_DOCK_BUILD; break;
			default: return;
		}
		/* The menu button in the landscape rail is icon only; its label is in the tooltip. */
		if (widget == WID_MD_MENU && this->landscape) {
			Dimension d = GetSpriteSize(sprite);
			int offset = this->IsWidgetLowered(widget) ? ScaleGUITrad(1) : 0;
			DrawSprite(sprite, PAL_NONE, CentreBounds(r.left, r.right, d.width) + offset, CentreBounds(r.top, r.bottom, d.height) + offset);
			return;
		}
		/* Portrait buttons are about 68 pixels wide, room for the normal font; the 40 wide landscape rail keeps the small one. */
		DrawMobileIconButton(r, sprite, label, this->IsWidgetLowered(widget), this->IsWidgetDisabled(widget), this->landscape ? FontSize::Small : FontSize::Normal);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MD_MENU: OnMobileMenuClick(); break;
			case WID_MD_COMPANY: if (Company::IsValidID(_local_company)) ToggleMobileHub(MobileHub::Company); break;
			case WID_MD_WORLD: ToggleMobileHub(MobileHub::World); break;
			case WID_MD_VEHICLES: if (Company::IsValidID(_local_company)) ToggleMobileHub(MobileHub::Vehicles); break;
			case WID_MD_BUILD: OnMobileBuildClick(); break;
		}
		this->UpdateButtonStates();
	}

	const IntervalTimer<TimerWindow> refresh_interval = {std::chrono::milliseconds(30), [this](auto) {
		this->UpdateButtonStates();
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_dock_landscape_widgets = {
	NWidget(WWT_PANEL, Colours::Grey),
		/* The dock reaches the screen edges; its buttons stay inside the safe area. */
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_EMPTY, Colours::Invalid, WID_MD_GAP_LEFT), SetFill(0, 1), SetResize(0, 1),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_EMPTY, Colours::Invalid, WID_MD_GAP_TOP), SetFill(1, 0),
				NWidget(NWID_VERTICAL), SetPadding(4), SetPIP(0, 4, 0),
					NWidget(WWT_PANEL, Colours::Grey, WID_MD_MENU), SetMinimalSize(40, 40), SetToolTip(STR_MOBILE_DOCK_MENU_TOOLTIP), EndContainer(),
					NWidget(NWID_SPACER), SetFill(0, 1), SetResize(0, 1),
					NWidget(WWT_PANEL, Colours::Grey, WID_MD_COMPANY), SetMinimalSize(40, 44), SetToolTip(STR_MOBILE_DOCK_COMPANY_TOOLTIP), EndContainer(),
					NWidget(WWT_PANEL, Colours::Grey, WID_MD_WORLD), SetMinimalSize(40, 44), SetToolTip(STR_MOBILE_DOCK_WORLD_TOOLTIP), EndContainer(),
					NWidget(WWT_PANEL, Colours::Grey, WID_MD_VEHICLES), SetMinimalSize(40, 44), SetToolTip(STR_MOBILE_DOCK_VEHICLES_TOOLTIP), EndContainer(),
					NWidget(WWT_PANEL, Colours::Green, WID_MD_BUILD), SetMinimalSize(40, 52), SetToolTip(STR_MOBILE_DOCK_BUILD_TOOLTIP), EndContainer(),
				EndContainer(),
				NWidget(WWT_EMPTY, Colours::Invalid, WID_MD_GAP_BOTTOM), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_dock_portrait_widgets = {
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPadding(4),
			NWidget(WWT_PANEL, Colours::Grey, WID_MD_WORLD), SetMinimalSize(64, 44), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_DOCK_WORLD_TOOLTIP), EndContainer(),
			NWidget(WWT_PANEL, Colours::Grey, WID_MD_VEHICLES), SetMinimalSize(64, 44), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_DOCK_VEHICLES_TOOLTIP), EndContainer(),
			NWidget(WWT_PANEL, Colours::Green, WID_MD_BUILD), SetMinimalSize(64, 44), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_DOCK_BUILD_TOOLTIP), EndContainer(),
			NWidget(WWT_PANEL, Colours::Grey, WID_MD_COMPANY), SetMinimalSize(64, 44), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_DOCK_COMPANY_TOOLTIP), EndContainer(),
			NWidget(WWT_PANEL, Colours::Grey, WID_MD_MENU), SetMinimalSize(64, 44), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_DOCK_MENU_TOOLTIP), EndContainer(),
		EndContainer(),
		/* The dock reaches the screen edge; its buttons stay inside the safe area. */
		NWidget(WWT_EMPTY, Colours::Invalid, WID_MD_GAP_BOTTOM), SetFill(1, 0), SetResize(1, 0),
	EndContainer(),
};

static WindowDesc _mobile_dock_landscape_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileDock, WindowClass::None,
	{WindowDefaultFlag::NoFocus, WindowDefaultFlag::NoClose},
	_nested_mobile_dock_landscape_widgets
);

static WindowDesc _mobile_dock_portrait_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileDock, WindowClass::None,
	{WindowDefaultFlag::NoFocus, WindowDefaultFlag::NoClose},
	_nested_mobile_dock_portrait_widgets
);

/* --- Minimap button --- */

/** The button opening the map of the world in the sheet slot. */
struct MobileMinimapWindow : Window {
	MobileMinimapWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget != WID_MMB_BUTTON) return;
		CloseMobileFlyouts();
		OpenSheetContent(WindowClass::SmallMap, [] { ShowSmallMap(); });
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_minimap_widgets = {
	NWidget(WWT_PUSHIMGBTN, Colours::Grey, WID_MMB_BUTTON), SetMinimalSize(40, 40), SetSpriteTip(SPR_IMG_SMALLMAP, STR_MOBILE_MINIMAP_TOOLTIP),
};

static WindowDesc _mobile_minimap_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileMinimap, WindowClass::None,
	{WindowDefaultFlag::NoFocus, WindowDefaultFlag::NoClose},
	_nested_mobile_minimap_widgets
);

/* --- News banner --- */

/** The news banner: the headline of the desktop status bar's ticker in its own window, shown for as long as the ticker runs. */
struct MobileNewsWindow : Window {
	static constexpr int TICKER_STOP = 1640; ///< Scrolling is finished when the counter reaches this value, as on the desktop status bar.
	int ticker_scroll = 0; ///< Position of the scrolling text.

	MobileNewsWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MN_TEXT) return;
		const NewsItem *ni = GetStatusbarNews();
		if (ni == nullptr) return;
		/* A static headline, truncated with an ellipsis: scrolling text is hard to read on a phone and starts empty. */
		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
		std::string headline = StrMakeValid(ni->GetStatusText(), StringValidationSetting::ReplaceTabCrNlWithSpace);
		DrawString(tr.left, tr.right, CentreBounds(r.top, r.bottom, GetCharacterHeight(FontSize::Normal)), headline, TextColour::Black);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MN_TEXT: ShowStatusbarNewsMessage(); break;
			case WID_MN_DISMISS: this->Close(); break;
		}
	}

	/** Restart the ticker for a new item. */
	void Restart()
	{
		this->ticker_scroll = 0;
		this->SetDirty();
	}

	const IntervalTimer<TimerWindow> ticker_scroll_interval = {std::chrono::milliseconds(15), [this](uint count) {
		if (_pause_mode.Any()) return;
		this->ticker_scroll += count;
		if (this->ticker_scroll >= TICKER_STOP) {
			this->Close();
			return;
		}
		this->SetWidgetDirty(WID_MN_TEXT);
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_news_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHBTN, Colours::Yellow, WID_MN_TEXT), SetMinimalSize(120, 32), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_NEWS_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Yellow, WID_MN_DISMISS), SetMinimalSize(40, 32), SetStringTip(STR_MOBILE_CROSS, STR_MOBILE_NEWS_DISMISS_TOOLTIP),
	EndContainer(),
};

static WindowDesc _mobile_news_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileNews, WindowClass::None,
	{WindowDefaultFlag::NoFocus},
	_nested_mobile_news_widgets
);

/** Show the news banner for the current ticker item, restarting it when it is already shown. */
void ShowMobileNewsBanner()
{
	if (MobileNewsWindow *w = dynamic_cast<MobileNewsWindow *>(FindWindowById(WindowClass::MobileNews, 0)); w != nullptr) {
		w->Restart();
		return;
	}
	new MobileNewsWindow(_mobile_news_desc);
	RelocateMobileWindows();
}

/** Hide the news banner. */
void CloseMobileNewsBanner()
{
	CloseWindowById(WindowClass::MobileNews, 0);
}

/**
 * Is the news banner showing a ticker item?
 * @return \c true while the banner is open.
 */
bool IsMobileNewsBannerShown()
{
	return FindWindowById(WindowClass::MobileNews, 0) != nullptr;
}

/* --- Layout pass --- */

/** Create the windows of the mobile shell, in place of the main toolbar and the status bar. */
void ShowMobileVitalWindows()
{
	/* ShowVitalWindows() can be called more than once, e.g. after world generation. */
	if (FindWindowById(WindowClass::MobileStatus, 0) == nullptr) new MobileStatusWindow(_mobile_status_desc);
	if (FindWindowById(WindowClass::MobileMinimap, 0) == nullptr) new MobileMinimapWindow(_mobile_minimap_desc);
	RelocateMobileWindows();
}

/** Height of the sheet filler's panel; the layout pass resizes it. */
static constexpr std::initializer_list<NWidgetPart> _nested_mobile_sheet_filler_widgets = {
	NWidget(WWT_PANEL, Colours::Grey, 0), SetMinimalSize(1, 1), SetResize(1, 1), EndContainer(),
};

/** Fills the strip between a sheet's content and the end of the slot, so the sheet looks attached to the edge. */
struct MobileSheetFillerWindow : Window {
	MobileSheetFillerWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
	}

	/** The sheet's content can be closed by its own close box; lay out again when that happens. */
	const IntervalTimer<TimerWindow> check_interval = {std::chrono::milliseconds(250), [](auto) {
		if (!IsSheetOpen()) RelocateMobileWindows();
	}};
};

static WindowDesc _mobile_sheet_filler_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileSheetFiller, WindowClass::None,
	{WindowDefaultFlag::NoFocus},
	_nested_mobile_sheet_filler_widgets
);

/**
 * Fill the part of the sheet slot below the content.
 * @param slot The sheet slot.
 * @param content_bottom First row below the content.
 * @param colour Colour of the content.
 */
static void PlaceSheetFiller(const Rect &slot, int content_bottom, Colours colour)
{
	int gap = slot.bottom + 1 - content_bottom;
	Window *filler = FindWindowById(WindowClass::MobileSheetFiller, 0);
	if (gap <= 0) {
		if (filler != nullptr) filler->Close();
		return;
	}
	if (filler == nullptr) filler = new MobileSheetFillerWindow(_mobile_sheet_filler_desc);
	filler->GetWidget<NWidgetCore>(0)->colour = colour;
	FitWindowTo(filler, slot.Width(), gap);
	MoveWindowTo(filler, slot.left, content_bottom);
	filler->SetDirty();
}

/**
 * Get the colour of a window's outermost panel, to match the filler to it.
 * @param w The window.
 * @return Its colour.
 */
static Colours GetWindowColour(const Window *w)
{
	/* The widget in the bottom left corner is usually the window's background or its list. */
	const NWidgetCore *core = w->nested_root->GetWidgetFromPos(1, w->height - 2);
	return (core != nullptr && core->colour != Colours::Invalid) ? core->colour : Colours::Grey;
}

/**
 * Place all windows of the mobile shell for the current screen size and orientation.
 * Creates the dock in the right orientation when needed.
 */
void RelocateMobileWindows()
{
	if (!IsMobileLayout()) return;

	static bool busy = false; ///< Re-creating windows calls back into the layout pass; finish the outer pass only.
	if (busy) return;
	AutoRestoreBackup busy_guard(busy, true);

	bool landscape = IsLandscape();
	const int margin = ScaleGUITrad(MOBILE_MARGIN);
	UpdateSafeAreaTarget();
	const Rect safe = GetSafeArea();

	/* The dock and the rail toolbar: open the other layout when the orientation changed. */
	MobileDockWindow *dock = dynamic_cast<MobileDockWindow *>(FindWindowById(WindowClass::MobileDock, 0));
	if (dock != nullptr && dock->landscape != landscape) {
		dock->Close();
		dock = nullptr;
		CloseMobileFlyouts();
	}
	if (dock == nullptr) dock = new MobileDockWindow(landscape ? _mobile_dock_landscape_desc : _mobile_dock_portrait_desc, landscape);
	Window *toolbar = GetMobileConstructionToolbar();
	if (toolbar != nullptr && (IsMobileRailToolbarLandscape(toolbar) || IsMobileRoadToolbarLandscape(toolbar)) != landscape) {
		if (toolbar->window_number == TransportType::Rail) {
			RecreateMobileRailToolbar();
		} else {
			RecreateMobileRoadToolbar();
		}
		toolbar = GetMobileConstructionToolbar();
	}
	/* The bar shown while adding stops to a train takes the toolbar's place. */
	if (toolbar == nullptr) toolbar = FindWindowById(WindowClass::MobileStopsBar, 0);

	/* The dock and the toolbars reach the screen edges (their spacers fill the insets): resize them when the insets change. */
	static Rect last_gaps{};
	Rect gaps = GetMobileEdgeGaps();
	if (gaps.left != last_gaps.left || gaps.top != last_gaps.top || gaps.right != last_gaps.right || gaps.bottom != last_gaps.bottom) {
		last_gaps = gaps;
		dock->ReInit();
		if (toolbar != nullptr) toolbar->ReInit();
	}

	if (landscape) {
		FitWindowTo(dock, dock->width, _screen.height);
		MoveWindowTo(dock, 0, 0);
	} else {
		FitWindowTo(dock, safe.Width(), dock->height);
		/* In portrait the rail toolbar replaces the dock. */
		MoveWindowTo(dock, safe.left, toolbar != nullptr ? _screen.height : _screen.height - dock->height);
	}
	if (!landscape && toolbar != nullptr) {
		FitWindowTo(toolbar, safe.Width(), toolbar->height);
		MoveWindowTo(toolbar, safe.left, _screen.height - toolbar->height);
	}
	int map_left = landscape ? dock->left + dock->width : safe.left;

	/* The sheet slot: the tab window at the top, the content and its overlay below. */
	int map_right = safe.right + 1;
	if (IsSheetOpen()) {
		Rect slot = GetSheetSlot();
		if (landscape) map_right = slot.left;

		int content_top = slot.top;
		Colours colour = Colours::Grey;
		MobileHubTabsWindow *tabs = dynamic_cast<MobileHubTabsWindow *>(FindWindowById(WindowClass::MobileHubTabs, 0));
		if (tabs != nullptr) {
			tabs->UpdateOrientation(true);
			FitWindowTo(tabs, slot.Width(), tabs->height);
			MoveWindowTo(tabs, slot.left, slot.top);
			content_top += tabs->height;
			colour = _hubs[to_underlying(tabs->hub)].colour;
		}
		int content_bottom = content_top;
		for (Window *w : {GetSheetContent(), GetSheetOverlay()}) {
			if (w == nullptr) continue;
			FitWindowTo(w, slot.Width(), slot.bottom + 1 - content_top);
			MoveWindowTo(w, slot.left, content_top);
			content_bottom = std::max(content_bottom, w->top + w->height);
			if (tabs == nullptr) colour = GetWindowColour(w);
		}
		PlaceSheetFiller(slot, content_bottom, colour);
	} else {
		CloseWindowById(WindowClass::MobileSheetFiller, 0);
	}

	if (landscape && toolbar != nullptr) {
		/* Its tools don't fit beside a sheet: let it switch to status, Cancel and Build only (it asks IsMobileSheetOpen()). */
		UpdateMobileRailToolbarPlanes(toolbar);
		UpdateMobileRoadToolbarPlanes(toolbar);
		/* Along the bottom of the map, between the dock and the sheet. */
		FitWindowTo(toolbar, map_right - map_left, toolbar->height);
		MoveWindowTo(toolbar, map_left, _screen.height - toolbar->height);
	}

	/* The floating chrome at the top: against the cutout's shape (and 8 from the rounded corners) where the platform says
	 * where it is; otherwise below the safe area's top. See the round 4 design, section 2.4. */
	int corner = ScaleGUITrad(8);
	if (Window *status = FindWindowById(WindowClass::MobileStatus, 0); status != nullptr) {
		int x = landscape ? map_left + margin : std::max(map_left + margin, corner);
		MoveWindowTo(status, x, GetTopChromeY(x, x + status->width - 1, safe));
	}

	if (Window *minimap = FindWindowById(WindowClass::MobileMinimap, 0); minimap != nullptr) {
		int x = landscape ? map_right - minimap->width - margin : std::min(map_right - minimap->width - margin, _screen.width - corner - minimap->width);
		MoveWindowTo(minimap, x, GetTopChromeY(x, x + minimap->width - 1, safe));
	}

	for (WindowClass cls : {WindowClass::MobileNews, WindowClass::MobileError}) {
		Window *banner = FindWindowById(cls, 0);
		if (banner == nullptr) continue;
		if (landscape) {
			int width = std::min(ScaleGUITrad(MOBILE_NEWS_WIDTH), map_right - map_left - ScaleGUITrad(16));
			FitWindowTo(banner, width, banner->height);
			MoveWindowTo(banner, CentreBounds(map_left, map_right, banner->width), safe.top + margin);
		} else {
			FitWindowTo(banner, safe.Width() - 2 * margin, banner->height);
			MoveWindowTo(banner, safe.left + margin, safe.top + ScaleGUITrad(MOBILE_NEWS_TOP_PORTRAIT));
		}
	}

	/* The floating Cancel: bottom right of the visible map. */
	if (Window *cancel = FindWindowById(WindowClass::MobileToolCancel, 0); cancel != nullptr) {
		Rect map = GetMobileVisibleMapRect();
		MoveWindowTo(cancel, map.right + 1 - cancel->width - margin, map.bottom + 1 - cancel->height - margin);
	}

	/* The flyouts open next to their button. */
	if (Window *menu = FindWindowById(WindowClass::MobileMenu, 0); menu != nullptr) {
		const NWidgetBase *button = dock->GetWidget<NWidgetBase>(WID_MD_MENU);
		if (landscape) {
			MoveWindowTo(menu, map_left, std::min<int>(dock->top + button->pos_y, safe.bottom + 1 - menu->height));
		} else {
			MoveWindowTo(menu, safe.right + 1 - menu->width - margin, dock->top - menu->height);
		}
	}
	if (MobileBuildMenuWindow *build = dynamic_cast<MobileBuildMenuWindow *>(FindWindowById(WindowClass::MobileBuildMenu, 0)); build != nullptr) {
		if (build->toolbar != nullptr && build->toolbar == toolbar) {
			/* Above the toolbar's category button, left edges aligned. */
			const NWidgetBase *button = toolbar->GetWidget<NWidgetBase>(toolbar->window_number == TransportType::Road ? static_cast<WidgetID>(WID_ROT_CATEGORY) : static_cast<WidgetID>(WID_RAT_CATEGORY));
			int x = toolbar->left + button->pos_x;
			MoveWindowTo(build, Clamp(x, safe.left, std::max(safe.left, safe.right + 1 - build->width)), std::max(safe.top, toolbar->top + (int)button->pos_y - build->height));
		} else {
			const NWidgetBase *button = dock->GetWidget<NWidgetBase>(WID_MD_BUILD);
			if (landscape) {
				MoveWindowTo(build, map_left, std::max<int>(safe.top, dock->top + button->pos_y + (int)button->current_y - build->height));
			} else {
				int x = dock->left + button->pos_x + ((int)button->current_x - build->width) / 2;
				MoveWindowTo(build, Clamp(x, safe.left, safe.right + 1 - build->width), dock->top - build->height);
			}
		}
	}

	/* A selection in review that a sheet now covers is scrolled back into view. */
	MobileBuildRevealPending();

	MarkWholeScreenDirty();
}

/**
 * Get the part of the map not covered by the mobile layout: without the dock, the status pill row,
 * the construction toolbar and the sheet slot.
 * @return The visible map area, in screen pixels.
 */
Rect GetMobileVisibleMapRect()
{
	Rect r = GetSafeArea();
	if (!IsMobileLayout()) return r;

	if (const Window *dock = FindWindowById(WindowClass::MobileDock, 0); dock != nullptr && dock->top < _screen.height) {
		if (IsLandscape()) {
			r.left = std::max(r.left, dock->left + dock->width);
		} else {
			r.bottom = std::min(r.bottom, dock->top - 1);
		}
	}
	if (const Window *status = FindWindowById(WindowClass::MobileStatus, 0); status != nullptr) {
		r.top = std::max(r.top, status->top + status->height + ScaleGUITrad(MOBILE_MARGIN));
	}
	if (const Window *toolbar = GetMobileBottomBar(); toolbar != nullptr) {
		r.bottom = std::min(r.bottom, toolbar->top - 1);
	}
	if (IsSheetOpen()) {
		Rect slot = GetSheetSlot();
		if (IsLandscape()) {
			r.right = std::min(r.right, slot.left - 1);
		} else {
			r.bottom = std::min(r.bottom, slot.top - 1);
		}
	}
	if (r.right < r.left) r.right = r.left;
	if (r.bottom < r.top) r.bottom = r.top;
	return r;
}

/**
 * How far the centre of the visible map is from the centre of the screen.
 * @return The offset in screen pixels; zero outside the mobile layout.
 */
Point GetMobileMapCentreOffset()
{
	if (!IsMobileLayout()) return {0, 0};
	Rect r = GetMobileVisibleMapRect();
	return {(r.left + r.right) / 2 - _screen.width / 2, (r.top + r.bottom) / 2 - _screen.height / 2};
}

/**
 * Show a window in the sheet slot, replacing the sheet's content; e.g. the bridge list.
 * @param w The window.
 */
void MobileShowInSheet(Window *w)
{
	if (w == nullptr || !IsMobileLayout()) return;
	CloseWindowById(WindowClass::MobileHubTabs, 0);
	if (Window *old = GetSheetContent(); old != nullptr && old != w) old->Close();
	_sheet.content_cls = w->window_class;
	_sheet.content_num = w->window_number;
	RelocateMobileWindows();
}

/** Close whatever fills the sheet slot (e.g. from the construction toolbar's "Show tools"). */
void CloseMobileSheetPublic()
{
	CloseMobileSheet();
	RelocateMobileWindows();
}

/**
 * Is the sheet slot in use?
 * @return \c true when a hub, a picker or an info window fills it.
 */
bool IsMobileSheetOpen()
{
	return IsMobileLayout() && IsSheetOpen();
}

/**
 * Show an info window (a town, a station) in the sheet slot: over a hub's list when a hub is open, so closing it
 * returns to the list; otherwise as the sheet's content.
 * @param w The window.
 */
void MobileShowInfoWindow(Window *w)
{
	if (w == nullptr || !IsMobileLayout()) return;
	if (FindWindowById(WindowClass::MobileHubTabs, 0) != nullptr && GetSheetContent() != w) {
		MobileDockOverlayInSheet(w);
		return;
	}
	MobileShowInSheet(w);
}

/**
 * Open the Build flyout from a construction toolbar's category button; for rail it also lists the rail types.
 * @param toolbar The toolbar.
 * @param widget The category button.
 */
void ShowMobileBuildFlyoutForToolbar(Window *toolbar, [[maybe_unused]] WidgetID widget)
{
	if (FindWindowById(WindowClass::MobileBuildMenu, 0) != nullptr) {
		CloseMobileFlyouts();
		return;
	}
	CloseMobileFlyouts();
	new MobileBuildMenuWindow(_mobile_build_menu_desc, toolbar);
	RelocateMobileWindows();
}

/* --- Error banner --- */

/** Errors outside construction toolbars: a red banner where the news banner goes. */
struct MobileErrorWindow : Window {
	std::string text; ///< The error.
	bool resume = false; ///< A yellow "paused while you were away" banner; tapping it unpauses.

	MobileErrorWindow(WindowDesc &desc, std::string &&text, bool resume = false) : Window(desc), text(std::move(text)), resume(resume)
	{
		this->CreateNestedTree();
		if (resume) {
			this->GetWidget<NWidgetCore>(WID_MERR_TEXT)->colour = Colours::Yellow;
			this->GetWidget<NWidgetCore>(WID_MERR_DISMISS)->colour = Colours::Yellow;
		}
		this->FinishInitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget == WID_MERR_TEXT) size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal) * 2 + padding.height);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MERR_TEXT) return;
		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
		DrawStringMultiLineWithClipping(tr, this->text, this->resume ? TextColour::Black : TextColour::White, AlignmentH::Centre);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget == WID_MERR_TEXT && this->resume && _pause_mode.Test(PauseMode::Normal)) Command<Commands::Pause>::Post(PauseMode::Normal, false);
		if (widget == WID_MERR_TEXT || widget == WID_MERR_DISMISS) this->Close();
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		this->Window::Close();
		/* At the main menu in portrait the panel sat below the banner. */
		if (_game_mode == GameMode::Menu) RelocateMobileMainMenu();
	}

	/**
	 * Show another error.
	 * @param text The error.
	 */
	void Replace(std::string &&text)
	{
		this->text = std::move(text);
		this->timeout.Reset();
		this->SetDirty();
	}

	/** Hide after the same time the desktop error window stays. */
	TimeoutTimer<TimerWindow> timeout = {std::chrono::seconds(std::max<int>(1, _settings_client.gui.errmsg_duration)), [this]() {
		this->Close();
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_error_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHBTN, Colours::Red, WID_MERR_TEXT), SetMinimalSize(120, 32), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_MOBILE_ERROR_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Red, WID_MERR_DISMISS), SetMinimalSize(40, 32), SetStringTip(STR_MOBILE_CROSS, STR_MOBILE_ERROR_TOOLTIP),
	EndContainer(),
};

static WindowDesc _mobile_error_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileError, WindowClass::None,
	{WindowDefaultFlag::NoFocus},
	_nested_mobile_error_widgets
);

/**
 * Show an error the mobile way: in the construction toolbar's status line when one is open, otherwise in a red banner.
 * @param summary The summary ("Can't build ...").
 * @param detail The reason; may be empty.
 * @return \c true when the error was shown; \c false to show it in an error window as usual.
 */
bool ShowMobileError(const std::string &summary, const std::string &detail)
{
	if (!IsMobileLayout()) return false;
	std::string text = detail.empty() ? summary : (summary.empty() ? detail : summary + " " + detail);
	if (MobileBuildHandleError(detail.empty() ? summary : detail)) return true;

	if (MobileErrorWindow *w = dynamic_cast<MobileErrorWindow *>(FindWindowById(WindowClass::MobileError, 0)); w != nullptr) {
		w->Replace(std::move(text));
		return true;
	}
	new MobileErrorWindow(_mobile_error_desc, std::move(text));
	RelocateMobileWindows();
	return true;
}

/**
 * A warning at the main menu (e.g. missing sprites in the base graphics): the red banner, centred at the top.
 * @param text The warning.
 */
void ShowMobileMenuBanner(std::string &&text)
{
	CloseWindowById(WindowClass::MobileError, 0);
	Window *w = new MobileErrorWindow(_mobile_error_desc, std::move(text));
	PlaceMobileMenuBanner(w);
}

/**
 * Place the main menu's banner: centred at the top of the safe area, at most 480 wide.
 * @param w The banner.
 */
void PlaceMobileMenuBanner(Window *w)
{
	Rect safe = VideoDriver::GetInstance() != nullptr ? VideoDriver::GetInstance()->GetSafeArea() : Rect{0, 0, _screen.width - 1, _screen.height - 1};
	int left = safe.left;
	/* Landscape: beside the menu panel, which sits on the left. */
	if (const Window *menu = FindWindowById(WindowClass::SelectGame, 0); menu != nullptr && _screen.width > _screen.height) left = menu->left + menu->width;
	int margin = ScaleGUITrad(MOBILE_MARGIN);
	int width = std::min(ScaleGUITrad(480), safe.right + 1 - left - 2 * margin);
	FitWindowTo(w, width, w->height);
	MoveWindowTo(w, CentreBounds(left, safe.right + 1, w->width), safe.top + margin);
}

/** Tell the player the game waited for them: a yellow banner; tapping it continues the game. */
void ShowMobileResumeBanner()
{
	if (!IsMobileLayout() || _game_mode != GameMode::Normal || !_pause_mode.Test(PauseMode::Normal)) return;
	CloseWindowById(WindowClass::MobileError, 0);
	new MobileErrorWindow(_mobile_error_desc, GetString(STR_MOBILE_RESUME_BANNER), true);
	RelocateMobileWindows();
}

/**
 * Close the flyouts when a tap lands outside them; the tap is consumed so it does not reach the map.
 * @param w The window under the tap.
 * @return \c true when the tap was consumed.
 */
bool MobileConsumeOutsideTap(const Window *w)
{
	if (!IsMobileMenus()) return false;
	/* A tap outside a confirmation cancels it. */
	if (Window *query = FindWindowByClass(WindowClass::ConfirmPopupQuery); query != nullptr && w != query && (w == nullptr || w->window_class != WindowClass::DropdownMenu)) {
		query->Close();
		return true;
	}
	if (!IsMobileLayout()) return false;
	if (w == nullptr || w->window_class == WindowClass::MobileMenu || w->window_class == WindowClass::MobileBuildMenu || w->window_class == WindowClass::MobileRailMore || w->window_class == WindowClass::DropdownMenu) return false;
	if (!IsMobileFlyoutOpen()) return false;

	CloseMobileFlyouts();
	SetWindowDirty(WindowClass::MobileDock, 0);
	return true;
}

/**
 * Handle the Android back button: close the topmost flyout, then the sheet, then leave build mode.
 * @return \c true when something was closed.
 */
bool MobileHandleBack()
{
	if (!IsMobileMenus()) return false;

	/* An open confirmation is cancelled; an open dropdown closes without changing anything. */
	if (Window *query = FindWindowByClass(WindowClass::ConfirmPopupQuery); query != nullptr) {
		query->Close();
		return true;
	}
	if (Window *dropdown = FindWindowByClass(WindowClass::DropdownMenu); dropdown != nullptr) {
		dropdown->Close();
		return true;
	}
	/* Full-screen pages: Back goes back a level, or closes them. */
	if (Window *settings = FindWindowById(WindowClass::MobileSettings, 0); settings != nullptr) {
		settings->OnClick({}, WID_MSET_BACK, 1);
		return true;
	}
	if (Window *new_game = FindWindowByClass(WindowClass::GenerateLandscape); new_game != nullptr && _game_mode == GameMode::Menu) {
		new_game->Close();
		return true;
	}
	if (Window *saveload = FindWindowById(WindowClass::SaveLoad, 0); saveload != nullptr && _game_mode == GameMode::Menu) {
		saveload->Close();
		return true;
	}
	if (!IsMobileLayout()) return false;

	if (IsMobileFlyoutOpen()) {
		CloseMobileFlyouts();
		return true;
	}
	/* In build mode: cancel the drag, then the selection in review, then drop the tool. */
	if (MobileBuildBack()) return true;
	if (Window *overlay = GetSheetOverlay(); overlay != nullptr) {
		overlay->Close();
		_sheet.overlay_cls = WindowClass::Invalid;
		return true;
	}
	if (IsSheetOpen()) {
		CloseMobileSheet();
		return true;
	}
	if (IsConstructionToolbarOpen()) {
		CloseConstructionToolbars();
		return true;
	}
	return false;
}

/**
 * Show a window on top of the sheet content, e.g. a town opened from the town list.
 * Closing it, or the sheet, returns to the content.
 * @param w The window to dock.
 */
void MobileDockOverlayInSheet(Window *w)
{
	if (w == nullptr || !IsMobileLayout() || !IsSheetOpen()) return;
	if (Window *old = GetSheetOverlay(); old != nullptr && old != w) old->Close();
	_sheet.overlay_cls = w->window_class;
	_sheet.overlay_num = w->window_number;
	RelocateMobileWindows();
}

/**
 * Position for windows that align to the main toolbar on desktop, such as construction toolbars.
 * @param window_width Width of the window.
 * @return Top-left corner, centred over the map below the status pill.
 */
Point GetMobileToolbarAlignedWindowPosition(int window_width)
{
	const Window *dock = FindWindowById(WindowClass::MobileDock, 0);
	Rect safe = GetSafeArea();
	int map_left = (dock != nullptr && IsLandscape()) ? dock->left + dock->width : safe.left;
	return {std::max(map_left, CentreBounds(map_left, safe.right + 1, window_width)), safe.top + ScaleGUITrad(MOBILE_MAP_STRIP)};
}

/** Show or hide Android's navigation bar after the setting changed. */
void ApplyMobileSystemBars()
{
#ifdef __ANDROID__
	AndroidApplySystemBars(_settings_client.gui.mobile_show_navigation_bar);
#endif
}

/** Switch between the desktop and the mobile layout after the setting changed. */
void ApplyMobileLayoutSetting()
{
	if (_game_mode == GameMode::Menu) return;

	CloseMobileFlyouts();
	CloseMobileSheet();
	for (WindowClass cls : {WindowClass::MainToolbar, WindowClass::Statusbar, WindowClass::MobileStatus, WindowClass::MobileDock,
			WindowClass::MobileMinimap, WindowClass::MobileNews}) {
		CloseWindowById(cls, 0, true);
	}
	ShowVitalWindows();
	RelocateAllWindows(_screen.width, _screen.height);
	MarkWholeScreenDirty();
}
