# Handoff: OpenTTD mobile UI, round 3 built → round 4 (trains, orders) **and the menus**

From: the C++ implementation session
To: the UX/layout design session
Date: 26 Nov 2026

This replies to your round 3 handoff (`06-design-to-impl-round3.md`). It contains:
1. what was built, with screenshots
2. where the build differs from your spec, and why
3. open questions for you
4. answers to your round 4 feasibility questions
5. **the request for round 4: trains and orders, plus the menus**

Screenshots are in `docs/mobile/screenshots/round3/` (also sent as a zip):
- `L…` are headless landscape shots at 2714×1222, 300% (the design default 904×407).
- `P…` are headless portrait shots at 1222×2714.
- `A…` are crops from the user's phone (Poco X7 Pro, three-button navigation).

The round 2 screenshots you were missing are in `docs/mobile/screenshots/round2/`. The phone photos `A01`–`A15` are in `screenshots/android/` and `screenshots/device-polish/`.

---

## 1. Built in round 3

### Stations (your §3)
- **Picker in the sheet slot**, as in §3.2: title and ✕, two orientation buttons, Tracks and Platform length rows of 1–7, the catchment toggle, the join dropdown when there's a choice, and Accepts/Supplies. Before placement it says "Accepts and supplies appear once the station is placed." (`L01`, `P04`)
- **Tap** gives a footprint of the picker's size, centred on the tapped tile. **Drag** gives any rectangle, and the picker follows it; above 7 the label reads "Platform length: 9".
- **Pending:** white footprint, catchment (`L02`), "Station 3×5 · £4,215", errors in red. Picker changes resize the footprint in place.
- **Moving:** dragging inside the footprint moves it, with the live label (`L04`). Dragging a corner resizes it.
- **Join:** the dropdown shows "Join <name>" and "New station". The default is what the desktop does without Ctrl. The status line says "Station 3×5 · joins Fronford North · £3,915" (`L03`). Build passes the choice, so the desktop join window never opens.
- **Build** shows "Built station · £x" with **Undo · £y** (`L05`, `A19`). The tool and the picker stay open.
- **Leaving:** the picker's ✕ drops the tool. Back drops a pending footprint first, then the tool.

### Depots (your §4)
- **Tap** gives a pending tile. If exactly one neighbouring track end points at it, the depot faces that track: "Depot faces the track · £540" (`L07`). Otherwise it uses the last direction: "Depot · £540 · check the entrance" (`L06`).
- **Turn** (40×40, just before ✕, only while a depot is pending) rotates the entrance clockwise.
- **Move** by dragging inside the tile. **Build** and **Undo** as for stations (`L08`, `A20`). There is no depot picker in the mobile layout.

### Waypoints (your §5)
Tap or drag along a straight track, then "Waypoint 1 tile · £450", then Build or Undo (`L14`). There is no picker; NewGRF waypoint types aren't handled yet.

### Info windows in the sheet slot (your §6)
- **Town** (`L11`, `A22`): label and value rows 28 tall, then Show on map · Local authority · Cargo graph. No viewport.
- **Station** (`L12`): Waiting · Accepts · Ratings tabs, rows 32 tall in Waiting, then Show on map · Trains · Catchment. Grouping and sorting are hidden, as in your v1.
- **Minimap** (`L09`, `L10`, `A21`): mode dropdown, Names and Legend toggles, map or legend, and the hint line.
  - A tap moves the main view there.
  - One finger pans the minimap; verified on the phone.
  - Two fingers pan and pinch-zoom the minimap. That's built, but untested on the device because adb can't do two fingers.
- **Opening:** a town or station opens over a hub's list when a hub is open (✕ returns to the list); otherwise it fills the sheet.

### Gestures (your §7)
All rows of your table are built.

### Fixes from your §1 and §9
- **Toolbar:** the disabled Build is grey (`L01`). Errors are capitalised and start with the warning sprite ("⚠ Flat land required").
- **Text and fonts:** titles and town rows are inset by 8. Portrait dock labels use the normal font (`P01`). The bridge speed line is now dark.
- **More flyout:** equal 56×48 cells, above the whole toolbar (`L13`).
- **Rail type list:** as wide as its longest row, capped at the visible map width − 16.
- **Edge-to-edge chrome (§1.7):** the dock and the rail toolbars reach the screen edges. Their buttons stay clear of the **tappable-element** inset: the full bar height for three-button navigation, nothing for gesture navigation (the buttons then sit 4 above the edge). See `A16` and `A17`. HyperOS painted the three-button bar solid black; it's transparent now, so the dock shows through under the buttons.
- **Reveal the pending selection (§1.5):** when the visible map changes and a pending selection isn't fully inside it, the map scrolls to centre it.

### Also new: keeping a game alive on Android (the user's request)
While a game runs, a quiet "Game in progress" notification keeps Android from killing the app. Leaving the app pauses and saves the game. If Android kills it anyway, the next start continues from that save, paused (`A23`). A new setting, on by default: "Phones: pause the game when leaving the app".
- **UI consequence for you:** after returning, the game is paused. Building while paused shows "⚠ Not allowed while paused", and Build re-enables after unpausing (`A18`).
- Should the status pill make the paused state more obvious, for example "Paused · tap ▶"?

## 2. Differences from your spec

1. **Orientation labels are "Along NE-SW" / "Along NW-SE".** The phone's game font has no ↗ ↘ glyphs; headless Linux had them through a TrueType fallback. If you want arrows, they need to be sprites. Maybe the station tile alone is enough?
2. **Corner handles are hard to see on stations** (`L02`). A blue outline on a corner tile is drawn over by its white neighbours, because each tile draws its own outline and neighbours share edges. Round 2's track ends weren't affected because a line has no neighbours on the sides. See question 3.1.
3. **The depot entrance arrow is the white one-way-road arrow**, not yellow; recolouring that sprite does nothing. There's no ghost depot.
4. **Turn is a text button.** There's no rotate sprite in the base set.
5. **Landscape and info windows:** while a sheet is open beside a construction toolbar, the toolbar switches to its picker plane (status · ✕ · Build). The tools don't fit beside the 300-wide panel at the floor. Closing the sheet brings the tools back (`L12`).
6. **Station picker Type row (NewGRF classes and types):** not built yet. The base game doesn't need it.
7. **"Building…" state:** not built yet, as agreed; it comes with the multiplayer work.
8. **Rotation while pending** still drops the selection, as agreed.

## 3. Open questions

1. **Handle marker for rectangles.** What should a draggable corner look like when it's inside the white footprint? For example, a small filled knob sprite at the tile centre, or a different colour for the whole corner tile. Your pilot A had round white knobs.
2. **Dropdowns.** The join dropdown, minimap mode and sort orders open the desktop's list with small rows. Should there be a touch dropdown (rows ≥ 40, maybe a bottom sheet in portrait)?
3. **News ticker.** The banner scrolls its text in from the right, as on desktop, so for a moment it's an empty yellow bar. Should it show the headline statically, truncated, with a tap to open?
4. **Top edge (from the user):** the status pill and the minimap button sit a fair distance below the real top: the camera-cutout inset, and maybe the rounded corners. The user would like them to slide with the safe area like the bottom dock does, and possibly sit closer to the edge. Should the top chrome also reach the edge (background into the inset, content below it), like the dock?

## 4. Answers to your round 4 feasibility questions

1. **Buying a train.**
   - **What attaches a wagon:** when a wagon is bought in a depot, the game attaches it to the end of the train **if exactly one stopped train with an engine is in that depot**. Otherwise it joins an existing free chain of wagons of the same type, or starts a new chain (`CcBuildWagon()`, `CmdBuildRailVehicle()`).
   - **Your flow** ("buy engine → next wagons attach until Done") is feasible. With one train in the depot it already works that way, and with several the mobile flow can attach explicitly to "the train being built".
2. **Orders by tapping stations.**
   - **Tapping a station:** "Go to" mode puts the map in a placement mode, and a tap on a station tile gives the order, exactly as the desktop click does. Tapping a station *sign* needs a check in the viewport code, which is doable.
   - **Quick picks:** "last two stations built" and "stations within N tiles of the depot" are easy to compute (stations have a build date and a location).
3. **Depot window.** A horizontal strip of vehicle sprites with ✕ per vehicle is feasible. Selling a single wagon is one command, and so is moving a vehicle within the train, if you want reordering by drag later.

## 5. Request for round 4

The user asks for two things in round 4:

**A. The natural next phase:** buying a train, the depot window, and orders (your §11). The flow from a built depot and station to a running train should be complete by touch.

**B. The menus.** Please design these too, mobile-first, in the same visual language:
- **Main menu** (start screen): New game, Load game, Continue (resume the last game; the background-save machinery exists), Settings, Content download, Quit. `A01` showed the desktop menu, which is small but usable.
- **New game setup** (world generation): which options to show on a phone, presets, an "Advanced" section.
- **In-game Menu flyout** (dock › Menu): Save, Load, Settings, Game options, Exit to main menu, Quit, and the Pause/speed controls if they belong there.
- **Settings:** the desktop settings tree is huge. Suggest a mobile structure: a short "Phone" section with the mobile settings, then the rest, searchable.
- **Save and load:** a list of saves with date, company and money; autosaves and the background save; overwrite and delete.
- **Confirmation dialogs** (quit, overwrite, delete): one mobile pattern.

Same format as before: WindowDescs and NWidget trees with sizes at 1×, the floor 775×349 and default 904×407, and a ranked [custom] list.

## 6. What I need back
- The round 4 spec (trains, depot, orders) and the menus spec.
- Answers to §3.
- Anything you'd change in the round 3 screenshots.
