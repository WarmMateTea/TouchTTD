/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file settings_gui.h Functions for setting GUIs. */

#ifndef SETTING_GUI_H
#define SETTING_GUI_H

#include "gfx_type.h"
#include "dropdown_type.h"

/** Width of setting buttons */
#define SETTING_BUTTON_WIDTH  ((int)NWidgetScrollbar::GetHorizontalDimension().width * 2)
/** Height of setting buttons */
#define SETTING_BUTTON_HEIGHT ((int)NWidgetScrollbar::GetHorizontalDimension().height)

void DrawArrowButtons(int x, int y, Colours button_colour, uint8_t state, bool clickable_left, bool clickable_right);
void DrawUpDownButtons(int x, int y, Colours button_colour, uint8_t state, bool clickable_up, bool clickable_down);
void DrawDropDownButton(int x, int y, Colours button_colour, bool state, bool clickable);
void DrawBoolButton(int x, int y, Colours button_colour, Colours background, bool state, bool clickable);

template <class T>
DropDownList BuildSetDropDownList(int *selected_index);

/** Base sets that the Game Options can change. */
enum class BaseSetType : uint8_t { Graphics, Sounds, Music };

DropDownList BuildCurrencyDropDownList(const struct GameSettings &opt, int *selected_index);
std::string GetCurrencyLabel(const struct GameSettings &opt);
void ChangeCurrency(struct GameSettings &opt, int index);
DropDownList BuildAutosaveDropDownList(int *selected_index);
std::string GetAutosaveLabel();
void ChangeAutosaveInterval(int index);
DropDownList BuildLanguageDropDownList(int *selected_index);
void ChangeLanguage(int index);
DropDownList BuildBaseSetDropDownList(BaseSetType type, int *selected_index);
std::string GetBaseSetLabel(BaseSetType type);
bool ChangeBaseSet(BaseSetType type, int index);
void ChangeVolume(bool music, uint8_t vol);
void ChangeGUIScale(int scale);

/* Actually implemented in music_gui.cpp */
void ChangeMusicSet(int index);

#endif /* SETTING_GUI_H */

