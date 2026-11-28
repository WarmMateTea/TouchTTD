# Round 5 implementation log

Design input: [`handoffs/10-design-to-impl-round5.md`](handoffs/10-design-to-impl-round5.md), every remaining group in 13 batches, built in its order. The user asked for this to run through the night; an hourly reminder (session cron, :05) resumes the work from this log and `git log`.

## Progress

| Batch | Status |
|---|---|
| 1. Fonts, main menu, cells, fixes, leftovers | done, headless (order drag needs the phone: touch only) |
| 2. Settings with Game Options folded in | done, headless |
| 3. Signals | next |

## Batch 1: fonts, main menu, cells, fixes, leftovers (§2, §3)

### Steps
- **Main menu logo (§2.1)** (`intro_gui.cpp`): the panel's header draws the same "OPENTTD" letter sprites as the title behind the menu (`DrawLogo()`, `GetLogoSize()`, widget `WID_SGI_VERSION` reused as the header), scaled to the header; no version text in it.
- **Continue (§2.2)**: one normal font, colour for the hierarchy: the play sprite, "Continue · <company>" in white over "<date> · <money> · <n min/h/days ago>" in cream. The company, date and money come from the save's header: `GetMobileSaveHeader()` (new, `mobile_saveload_gui.cpp`, shared with the save list). The file name is never shown.
- **Startup warning** ("missing 143 sprites") → `ShowMobileMenuBanner()` (new, `mobile_gui.cpp`): the red banner, beside the panel in landscape, above it in portrait (`PlaceMobileMenuBanner()`, called from `SelectGameWindow::PlaceMobile()`); closing it moves the panel back up (`MobileErrorWindow::Close()`).
- **Panel height fits its content** in landscape; Quit is a plain text button.
- **Vehicle cells (§3.2)** (`mobile_vehicle_gui.cpp`): `DrawTrainStrip()` draws cells of at least 40×40 with the sprite centred; a tap opens a touch dropdown Sell (+£x) · Refit… · Details (`ShowVehicleCellMenu()`), in New train and in the train window's Details tab. The separate Sell buttons are gone; selling the engine still asks.
- **Suggested stops (§3.3)**: full-width rows of 32 ("+ Bondinghead South"), only as many as there are, only while adding stops (`WID_MTR_CHIP_FIRST` is one panel).
- **Order rows (§3.4)**: 48 tall, flags line in the normal font.
- **Long-press reorder of orders (E2)**: new `Window` virtuals `OnLongPress()`, `OnLongPressMove()`, `OnLongPressEnd()` (`window_gui.h`); the gesture layer has a new mode `TouchMode::ItemDrag` (`mobile_touch.cpp`): a long press on a window that accepts it drags an item instead of showing the tooltip. The train window lifts the order, draws the drop line, and posts `MoveOrder` on release.
- **Settings (§3.5)**: help in the normal font, 2 lines with "...", tapping the row's text shows all of it; values in the normal font; the page label over a search hit is black; the search box is 32 tall.
- **Portrait dropdown title (§3.6, E1)** (`dropdown.cpp`): a title row (`WID_DM_TITLE`, in `WID_DM_TITLE_SEL`) over the portrait sheet, from the opening button's text, or its tooltip for a dropdown button (whose text is the current value). Windows whose dropdowns open from a list name them with a new virtual, `Window::GetMobileDropDownTitle()`: Settings (the setting's name), the train window (the order's destination, the vehicle model) and New train (the vehicle model).
- **Settings over the shell**: `WindowClass::MobileSettings` now has its own z-priority level between dropdowns and the shell windows (`GetWindowZPriority()`); round 4 had put it with the on-screen keyboard, which is *below* the shell, so the status pill covered its title row.
- Normal font everywhere the handoff's global text rule asks (save/load rows, New game, minimap legend, train window).
- **"Building…" for multiplayer (E3)**: moved to batch 13 with multiplayer, as the handoff says.

- **Dropdowns from colourless widgets**: the vehicle cell menu opens from the strip (a `WWT_EMPTY`, no colour), and the dropdown copies its button's colour: an assertion in `DrawFrameRect()`. `ShowDropDownListAt()` now uses grey for a widget without a colour.
- Screenshots: `screenshots/round5/` (`L01`–`L08` landscape, `P01`–`P03` portrait), headless.

### Findings
- **An edit box ignores `SetMinimalSize()` for its height**: `NWidgetLeaf::SetupSmallestSize()` *assigns* `size.height` from the font for `WWT_EDITBOX` instead of taking the maximum. Set the height in the window's `UpdateWidgetSize()`, which runs afterwards.
- **`GetWindowZPriority()` counts down through fall-throughs**: a class listed *earlier* in the switch is higher. The shell classes are above `OnScreenKeyboard`; anything that must cover the shell needs a case above them.
- A dropdown button (`WWT_DROPDOWN`) shows the current value as its text; its tooltip is the only label it has.
- The headless runner's 300% asks for 300 but the game picked 250% at 1222×2714 portrait (`_gui_scale`), so pixel coordinates from older screenshots can be off.

## Batch 2: Settings with Game Options folded in (§4)

### Steps
- **Shared Game Options functions** (`settings_gui.cpp`, declared in `settings_gui.h`): the Game Options window's dropdown and slider handlers became free functions, and the desktop window now calls them, so the two can't drift apart: `BuildCurrencyDropDownList()`/`GetCurrencyLabel()`/`ChangeCurrency()`, `BuildAutosaveDropDownList()`/`GetAutosaveLabel()`/`ChangeAutosaveInterval()`, `BuildLanguageDropDownList()`/`ChangeLanguage()`, `BuildBaseSetDropDownList()`/`GetBaseSetLabel()`/`ChangeBaseSet()` (new `enum class BaseSetType`), `ChangeVolume()`, `ChangeGUIScale()`.
- **Option rows** (`mobile_settings_gui.cpp`): `MobileOption` is a row that isn't a setting of the tree: a dropdown, a − value + stepper, or a link that opens a window. Sections list them by key (`"opt.language"`) next to setting names; `MobileSettingRow::option` marks them. Search covers them too, with their section as the page.
- **Sections:** Phone · Display and sound (interface size, base graphics + "Check for updates", base sounds, base music, music and effects volume) · Language and units (language, currency, the seven `locale.units_*` settings) · Saving (autosave interval, autosaves to keep, autosave on exit) · Building and vehicles · Economy, towns, competitors · Add-ons (NewGRF, AI and game scripts, Online content) · All settings. The section that opened the Game Options window is gone.
- **Interface size:** 250–400% in 25% steps when the screen's short side is under 1600 px (phones), else 100% to the maximum. Applied like the desktop slider on release (`_gui_scale_cfg`, `AdjustGUIZoom()`, `ReInitAllWindows()`).
- **Base graphics** can only change at the main menu (as on desktop); it then reloads the menu (`_switch_mode = SwitchMode::Menu`, what the desktop does when its window closes). Its help line adds the missing-sprites warning.
- **Volumes:** steppers in 10% steps of the 0–127 range.
- **Row dropdowns open at the row's control** (`ShowRowDropDown()` → `ShowDropDownListAt()` with the control's rectangle): opened from the whole list widget, the landscape dropdown was placed against the list's top edge with room for one row, and the list was drawn lowered.
- **Settings refits after a rotation or a new interface size**: `RelocateMobileSettings()` (called from `RelocateAllWindows()`) fills the safe area again, or reopens the other orientation's layout on the same section. Before, Settings kept its old pixel size.
- **Setting names with the value in the middle** ("stop by default at the {value} of the platform") showed a double space; it's now "at the ... of the platform".
- Not done: hold-to-repeat on steppers (backlog); text size (the build has no separate font zoom, so the row is omitted as the handoff allows); Town name style already sits in New game › Advanced options (the desktop window).

### Findings
- **Units, autosave on exit, volumes and the interface size are not all settings of the tree**: the units are (`locale.units_*`, dropdowns with `SettingFlag::GuiDropdown`), but language, currency, base sets and interface size are handled only by the Game Options window, and `gui.max_num_autosaves` has no title string (so it can't be listed as a setting row).

### Tips (batches 1–2)
- **The gesture layer only reads finger events** (`SDL_EVENT_FINGER_*`), so long presses and item drags can't be driven with `xdotool` on Xvfb; taps can, as mouse clicks.
- **Headless games left running eat the CPU**: eight forgotten instances made a full build take an hour (load average 25 on 4 cores). `tools/mobile/stop_headless.sh` now stops every headless OpenTTD using the private config, whatever its display, and checks that they're gone. Check with `ps -C openttd,Xvfb`.
- Changing `english.txt` or a widely included header rebuilds nearly everything (~20 min on this server when it's idle): group string changes.
