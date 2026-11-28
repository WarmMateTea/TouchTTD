/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file mobile_app.cpp App life cycle on phones.
 *
 * A phone may kill an app at any time once it's in the background. So when the player leaves the app during a game:
 * - the game is paused (setting gui.mobile_pause_in_background), so it waits for them;
 * - it's saved to autosave/background.sav, and the platform is told that this save resumes the game if the app is
 *   started afresh (on Android, OpenTTDActivity passes it with -g on the next start).
 * While a game runs, the platform is told so; Android then keeps a foreground service with a notification, which makes
 * the system much less likely to kill the app in the background.
 */

#include "stdafx.h"
#include "mobile_app.h"
#include "command_func.h"
#include "fileio_func.h"
#include "misc_cmd.h"
#include "openttd.h"
#include "settings_type.h"
#include "network/network.h"
#include "saveload/saveload_func.h"
#include "debug.h"
#include "fios.h"
#include <filesystem>
#include "mobile_gui.h"

#ifdef __ANDROID__
#	include "os/android/android_bridge.h"
#endif

#include "safeguards.h"

static bool _mobile_in_background = false; ///< The app is in the background (between leaving and coming back).
static bool _mobile_game_running = false; ///< Last game state told to the platform.
static bool _mobile_paused_on_leaving = false; ///< The game was paused when the player left the app.
static bool _mobile_show_resume_banner = false; ///< Show "paused while you were away" at the next tick.

/** Where the game is saved when the app goes to the background. */
std::string GetMobileBackgroundSavePath()
{
	return FioFindDirectory(Subdirectory::Autosave) + "background.sav";
}

/** Tell the platform which save resumes the game on a fresh start; empty for "start at the main menu". */
static void SetResumeSave(const std::string &path)
{
#ifdef __ANDROID__
	AndroidSetResumeSave(path);
#else
	(void)path;
#endif
}

/** The app is leaving the foreground: pause and save a running game. Called while the game state is locked. */
void MobileAppWillEnterBackground()
{
	if (_mobile_in_background) return;
	_mobile_in_background = true;
	if (_game_mode != GameMode::Normal) return;

	/* Pause first, so the save is paused too and a restored game waits for the player. */
	if (_settings_client.gui.mobile_pause_in_background && _pause_mode.None() && (!_networking || _network_server)) {
		Command<Commands::Pause>::Post(PauseMode::Normal, true);
		_mobile_paused_on_leaving = true;
	}

	std::string path = GetMobileBackgroundSavePath();
	Debug(Facility::Sl, Severity::Warning, "Leaving the app: saving to '{}'", path);
	/* Threaded: the main loop blocks while the app is in the background, the save thread carries on. */
	if (SaveOrLoad(path, SaveLoadOperation::Save, DetailedFileType::GameFile, Subdirectory::None, true) == SaveLoadResult::Ok) {
		SetResumeSave(path);
	}
}

/** The app is back in the foreground. */
void MobileAppDidEnterForeground()
{
	_mobile_in_background = false;
	/* Only a flag: this runs in SDL's event watch. The banner is shown from MobileAppTick(). */
	if (_mobile_paused_on_leaving) _mobile_show_resume_banner = true;
	_mobile_paused_on_leaving = false;
}

/**
 * The save the main menu's Continue loads: the save made when the player last left the app, or else the newest save.
 * @return The full path, or empty when there is no save.
 */
std::string GetMobileContinueSave()
{
	std::error_code ec;
	std::string background = GetMobileBackgroundSavePath();
	if (std::filesystem::is_regular_file(OTTD2FS(background), ec)) return background;

	std::string newest;
	std::filesystem::file_time_type newest_time{};
	for (Subdirectory dir : {Subdirectory::Save, Subdirectory::Autosave}) {
		for (const auto &entry : std::filesystem::directory_iterator(OTTD2FS(FioFindDirectory(dir)), ec)) {
			if (!entry.is_regular_file(ec) || entry.path().extension() != ".sav") continue;
			auto time = entry.last_write_time(ec);
			if (newest.empty() || time > newest_time) {
				newest = FS2OTTD(entry.path().native());
				newest_time = time;
			}
		}
	}
	return newest;
}

static bool _mobile_pause_after_load = false; ///< Pause the game the main menu's Continue loads.

/**
 * Continue the last game from the main menu, paused (as after coming back to the app).
 * @return \c false when there is nothing to continue.
 */
bool MobileContinueGame()
{
	std::string path = GetMobileContinueSave();
	if (path.empty()) return false;
	_file_to_saveload.name = path;
	auto [ft, _] = FiosGetSavegameListCallback(SaveLoadOperation::Load, path, ".sav");
	_file_to_saveload.SetMode(ft, SaveLoadOperation::Load);
	_switch_mode = SwitchMode::LoadGame;
	_mobile_pause_after_load = true;
	return true;
}

/** Called every draw tick: keep the platform informed about whether a game runs. */
void MobileAppTick()
{
	if (_mobile_show_resume_banner) {
		_mobile_show_resume_banner = false;
		ShowMobileResumeBanner();
	}

	bool running = _game_mode == GameMode::Normal && !_exit_game;
	if (running == _mobile_game_running) return;
	_mobile_game_running = running;

	/* Leaving a game on purpose (to the main menu, or quitting), as opposed to the app being closed from outside:
	 * the background save doesn't resume anything any more. */
	if (!running && !_mobile_in_background) SetResumeSave({});
	/* Continue from the main menu waits for the player, as a game resumed after the app was closed does. */
	if (running && _mobile_pause_after_load) {
		_mobile_pause_after_load = false;
		if (_pause_mode.None() && (!_networking || _network_server)) Command<Commands::Pause>::Post(PauseMode::Normal, true);
	}
	/* A game that starts paused was most likely resumed after the app was closed: say why it waits. */
	if (running) ShowMobileResumeBanner();

#ifdef __ANDROID__
	AndroidSetGameRunning(running);
#endif
}
