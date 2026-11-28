# Round 5 implementation log

Design input: [`handoffs/10-design-to-impl-round5.md`](handoffs/10-design-to-impl-round5.md), every remaining group in 13 batches, built in its order. The user asked for this to run through the night; an hourly reminder (session cron, :05) resumes the work from this log and `git log`.

## Progress

| Batch | Status |
|---|---|
| 1. Fonts, main menu, cells, fixes, leftovers | done, headless (order drag needs the phone: touch only) |
| 2. Settings with Game Options folded in | done, headless |
| 3. Signals | done, headless |
| 4. Road and tram | done, headless (tram untested: no tram types in the test game) |
| 5. Other vehicles and refit | done, headless with road vehicles (ships, aircraft need docks and airports: batch 9) |
| 6. Finances | done, headless |
| 7. Industries | next |

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

## Batch 3: Signals (§5)

### Steps
- **Picker** (`rail_gui.cpp`, `_nested_mobile_signal_builder_widgets`, `BuildSignalWindow` with `mobile`): in the sheet slot (`MobileShowInSheet()`), title and ✕ (drops the tool), Electric / Semaphore, the type buttons in three columns (sprite and short name; path signals only unless the "show signal types" setting says all), spacing − "every N tiles" +, "Change existing signals" On/Off, and **"Remove signals" On/Off** (see below). The type buttons reuse the `WID_BS_ELECTRIC_*` widgets; the variant comes from the radio. New widgets `WID_BS_TITLE`, `WID_BS_CLOSE`, `WID_BS_MOBILE_ELECTRIC`, `WID_BS_MOBILE_SEMAPHORE`, `WID_BS_REMOVE`, appended to the enum.
- **The toolbar shows its picker plane** (status · ✕ · Build) while the signal picker is open, as with stations.
- **Review flow** (`mobile_build.cpp`): `DDSP_BUILD_SIGNALS` is always reviewed. A tap stores where in the tile it landed (`PendingSelection::fract`, from `_tile_fract_coords`), so the replay puts the signal on the same track and side. Status texts: "1 signal · £x", "5 signals, every 2 tiles · £x", "Turn signal · £x" (a tap on an existing signal; new `MobileBuildKind::Change`, green **Change**), "Turn signal 2 times" (each further tap on the pending signal adds a turn, `PendingSelection::cycles`, replayed that many times), "Convert 2 signals" (Change on, red **Convert**), "Remove 3 signals" (red **Remove**), and "Done · £x" after building.
- **Counts come from the cost**: every signal costs the same (`_price[Price::BuildSignals]`, plus `ClearSignals` when converting; `ClearSignals` alone when removing), so the estimate's cost divided by it is the count, end signal included. Counting from the spacing missed the end signal.
- **Spacing and type changes update the pending row live** (`MobileBuildReestimate()` after every picker click).
- **Dragging with Change on converts the stretch** in the mobile layout: `HandleAutoSignalPlacement()` passes `convert` to `BuildSignalLong` (the desktop ignores drags with its convert button on).
- Hints: "Tap a track for one signal, or drag along it" / "Tap or drag over signals to remove them".
- **Ghost signals**: the fallback from the handoff (the selection highlights the tiles; the count is in the status line). Real ghost sprites need a drawing pass of their own (backlog).
- Screenshots `L09`–`L14`, `P04`.

### Findings
- **The toolbar's Remove is hidden while a picker fills the sheet** (the tools row goes to the picker plane), so the handoff's "Remove mode" row had no way in. The picker got a "Remove signals" toggle that drives the toolbar's remove mode (`ToggleRailButton_Remove()`); Change and Remove switch each other off.
- `_tile_fract_coords` (`viewport_func.h`) is where in its tile the cursor is, 0–15 each way; `GenericPlaceSignals()` picks the track and side from it.
- Changing only the text of existing strings rebuilds in seconds; adding string IDs regenerates `table/strings.h` and rebuilds most files.

## Batch 4: Road and tram (§6)

### Steps
1. **Toolbar** (`road_gui.cpp`): `BuildRoadToolbarWindow` got the rail toolbar's mobile machinery: `mobile`/`landscape`, landscape bar and portrait two rows (`_nested_mobile_road_{landscape,portrait}_widgets`), status · Turn · ✕ · Build/Undo/Show tools, Exit, `UpdateMobileWidgets()`, `UpdateMobileHint()`, `GetToolKind()`. One layout serves road and tram; `OnInit()` sets the type's sprites and the tram tooltips. New widgets `WID_ROT_CATEGORY` … `WID_ROT_SHOW_TOOLS`, appended.
2. **More flyout** (`MobileRoadMoreWindow`, reusing `WindowClass::MobileRailMore` and the `WID_RMORE_*` buttons; new `WID_RMORE_SEL_ROW2` hides the second row for four tools or fewer): road — one-way, convert, road NE-SW, road NW-SE (portrait: bridge and tunnel first); tram — the same without one-way.
3. **Category** opens the Build flyout with **road or tram types** (`MobileBuildMenuWindow` now lists `TypeRow`s for rail or road); the flyout anchors to `WID_ROT_CATEGORY` for the road toolbar.
4. **Mobile layout of the shell**: `GetMobileConstructionToolbar()` (rail or road) replaces `GetMobileRailToolbar()` in the layout pass, rotation (`RecreateMobileRoadToolbar()`) and plane updates (`UpdateMobileRoadToolbarPlanes()`).
5. **Review flow** (`mobile_build.cpp`): selections know their toolbar (`PendingSelection::IsRoad()`); remove mode is per toolbar (`IsRemoveModeActive()`/`SetRemoveMode()`, new road exports `IsRoadRemoveModeActive()`, `SetRoadRemoveMode()`, `IsRoadOneWayModeActive()`, `SetRoadOneWayMode()`); depots (`PostRoadDepot()`, facing the only adjacent road of the kind being built) and tunnels (`PostRoadTunnel()`) as rail. Reviewed: autoroad/road X/Y (setting, or always when removing or one-way), stops, stop removal, road conversion, demolish, bridges.
6. **One-way roads**: a tool of its own in the mobile layout (the desktop's toggle). It's autoroad with the one-way flag over existing road: pending "Change one-way · £0" → **Change**; each further tap on the pending stretch adds a turn ("Change one-way 2 times"), replayed that many times (`PendingSelection::one_way`, `cycles`).
7. **Stops** behave as station footprints (`PendingSelection::IsRoadStop()`): a tap is one tile, a drag an area; catchment of bus or lorry stops; join candidates and the join dropdown as for stations (`PlaceRoadStop()` posts with the mobile join choice); Undo removes the stops (`RemoveRoadStop`, keeping the road). On a tapped tile a drive-through stop takes the road's axis and a bay faces the only adjacent road (`GetMobileRoadStopDirection()`). Status "1 bus stop · £150", "…, joins X", "Done · £x".
8. **Stop picker** (`_nested_mobile_road_stop_picker_widgets`, `BuildRoadStationWindow` with `mobile`): in the sheet; title Bus stop / Lorry stop / Tram stop / Freight tram stop; Drive-through / Bay with previews; Facing (four previews) or "Follows the road"; Show catchment area; join dropdown; accepts/supplies once placed. Trams and drive-through-only NewGRF stops hide the bay choice. New widgets `WID_BROS_TITLE` … `WID_BROS_JOIN`, appended.
9. Screenshots `L15`–`L20`, `P05`.

### Findings
- **A road stop's callback builds road to connect a bay** (`CcRoadStop` → `ConnectRoadToStructure()`), and that follow-up fails with "Already built" when the road is there. The capture took it for the result; road stops now use `CommandResultCapture::first_only`, as depots do.
- `ShowBuildRoadToolbar()` keeps the open toolbar only for the same road/tram kind; switching kind closes and reopens.
- The desktop's one-way button is disabled unless a road tool is selected (`UpdateOptionWidgetStatus()`); the mobile layout re-enables it after every click because it's a tool there.
- A widget ID from one toolbar's enum used on the other toolbar silently picks an unrelated widget (the flyout first anchored to a road button through `WID_RAT_CATEGORY`).

### Not done
- Road waypoints have no mobile tool (not in the handoff's toolbar table).
- The "Type" dropdown for NewGRF road stop classes (as for rail stations, backlog).

## Batch 5: Other vehicles, refit (§7)

### Steps
1. **One set of windows for every vehicle type** (`mobile_vehicle_gui.cpp`), rather than three copies:
   - `MobileDepotWindow` takes the depot's `VehicleType`: lists the primary vehicles in a road depot, ship depot or hangar (trains still with free wagon chains); "+ New road vehicle / ship / aircraft"; Clone with the type's cursor.
   - `MobileTrainWindow` works on any `Vehicle` (`GetTrain()` now returns `const Vehicle *`): stops are any station the vehicle can use (`CanVehicleUseStation()`), and buoys for ships (go-to-waypoint orders); the details strip is one cell for a non-train (`DrawVehicleStrip()`), summary `GetVehicleSummary()`; "+ Add vehicles" is for trains only.
   - `MobileTrainListWindow` lists any type, with descs for `TrainList`, `RoadVehicleList`, `ShipList`, `AircraftList`, so the Vehicles hub's Road/Ships/Aircraft tabs get the trains-list layout.
   - Hooks: `ShowDepotWindow()`, `ShowVehicleViewWindow()` and `ShowVehicleListWindowLocal()` use the mobile windows for every type.
2. **Buy view for road vehicles, ships and aircraft** (`MobileNewVehicleWindow`): one list (as the desktop filters it: the depot's road type, the hangar's airport), "+ £x" buys a complete vehicle with `CcBuildPrimaryVehicle`, whose vehicle window then opens **adding stops** (`_mobile_open_adding_stops`).
3. **Refit** (`MobileRefitWindow`, new widgets `WID_MRF_*`): an overlay in the sheet over the window it was opened from (‹ returns); for a train of several vehicles "Whole train" / "Only this vehicle" (preset to only this, since it opens from a vehicle cell); the cargoes the vehicle (or train) can take, with a colour swatch and "30 bags of mail → 40 …" from a `QueryCost` refit; "Cost £x" and Refit (disabled until another cargo is picked); "Stop in a depot to refit" otherwise. `ShowVehicleRefitWindow()` opens it in the mobile layout except for order refits.
4. **Depots and bays beside a road**: both now also face the one road that *runs past* them, not only road that ends at them (the depot's and stop's callbacks connect it). Before, a depot beside a straight road said "check the entrance".
5. Texts made generic: "No vehicles here yet", "Clone", "No vehicles yet", "Depot faces the road".
6. Screenshots `L21`–`L25`: road depot sheet, New road vehicle, a bus adding stops from the suggestions, the bus running, refit while running.

### Findings
- **Orders for road vehicles, ships and aircraft must have the stop location "far end"**: `CmdInsertOrder()` refuses near end and middle for anything but trains, and `Order`'s default isn't far end. The desktop sets it the same way.
- `CanVehicleUseStation()` covers bus vs lorry stops, articulated road vehicles on bays, helicopters vs planes; no need to check facilities by hand.
- Stock temperate vehicles hardly refit (engines and most wagons carry one cargo); the refit list was checked for layout and the "not in a depot" state only.

### Not done
- Subtypes of a cargo in the refit list (NewGRF), and refitting from the New train strip before the train exists is the same window.
- Ships and aircraft tested only through the shared code (no docks or airports in the mobile layout yet).

## Batch 6: Finances and the loan (§8)

### Steps
- **New file** `src/mobile_company_gui.cpp` (+ `.h`, in `src/CMakeLists.txt`) for the company's windows; `MobileFinancesWindow` (class `Finances`, so the Company hub's Finances tab and the status pill's money open it; `ShowCompanyFinances()` hooks it in the mobile layout). New widgets `WID_MFIN_*`.
- **Table**: "<year> so far" against a year picked in a dropdown (last year by default, or the year before); group rows Income · Running costs (vehicles, property maintenance, loan interest) · Construction · New vehicles · Other, a tap opens a group's non-zero categories; Profit, shaded. Costs red with "-", zero as "-". The table scrolls by pixel (rows of 32 and 24).
- **Loan**: "Loan £x of £max · interest y%" and the bank balance; **Borrow ▾** (£10,000 · £50,000 · Maximum) and **Repay ▾** (£10,000 · £50,000 · Everything) as touch dropdowns, impossible entries disabled with the reason as their text ("Maximum reached", "not enough money", "No loan to repay"); `IncreaseLoan`/`DecreaseLoan` with `LoanCommand::Amount` or `Max`, no confirmation.
- Screenshot `L26`.

### Findings
- **`Company::yearly_expenses` keeps costs positive and income negative** (`[0]` is this year, `[1]` last year, `[2]` the year before); the table negates them.
- The desktop's category names (`STR_FINANCES_SECTION_*`) carry `{GOLD}`; drawing them black needs `ExtendedTextColourFlag::Forced` (and `NoShade`, or they keep a shadow).
- `Money` is `OverflowSafeInt`: a `?:` with a literal `0` on one side doesn't compile; write `Money{0}`.
