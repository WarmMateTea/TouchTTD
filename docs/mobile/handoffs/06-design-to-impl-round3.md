# Handoff: OpenTTD mobile UI, round 3 (stations and depots) → implementation

From: the UX/layout design session
To: the C++ implementation session
Date: 25 Nov 2026

This replies to your "round 2 built → round 3" handoff. It contains:
- decisions on your round 2 notes
- answers to your questions
- the round 3 spec: station placement with review, depots, and mobile layouts for the station window, town window and minimap
- small fixes from my review of the round 1 screenshots
- a note on the "modern look" pilots
- what I need back

Sizes are GUI px at 1×. The design floor is still 775×349 (350%) and the default 904×407 (300%).

> **Not received yet:** the round 2 screenshots (`L01`–`L11`, `P01`–`P05`) and the phone photos (`A01`–`A03`). Only the document arrived. The round 1 screenshots did arrive, and my notes on them are in §9.

---

## 1. Decisions on your round 2 notes (your §2)

1. **Handles are blue tile outlines.** Accepted. Blue is distinct from the white selection, and it needs no new drawing. The 40×40 hit area stays. (The round white knobs appear only in one of the modern-look pilots; see §10.)
2. **No "Building…" state.** Fine for single player. For multiplayer, when the command is sent and no result has come back:
   - The status line says "Building…".
   - Cancel and Build are disabled, and the selection stays drawn.
   - After 5 s with no reply, the status line says "Still waiting for the server…". Nothing else changes.
   - On the result, continue to DONE, or back to PENDING with the error.
3. **Rotation while PENDING drops the selection.** Acceptable for now; low priority.
4. **Stricter Undo check.** Accepted. Missing a possible Undo is better than deleting the player's track.
5. **The bridge list hides the span.** Yes, scroll the map. This becomes a general rule, **"reveal the pending selection":** whenever a sheet opens or the layout changes while a selection is pending, and the selection isn't fully inside the visible map rect, scroll so its centre sits at the centre of the visible map rect. If it can't fit, show the end nearest the finger's last position. This applies to the bridge list, the station picker and any later picker.
6. **Category flyout truncates rail type names.** Widen it; don't shorten names, since NewGRF names vary.
   - Width = the longest row (icon + name + cost + padding).
   - Cap: the visible map width minus 16.
   - Truncate with "…" only beyond the cap.
7. **Dock and the gesture strip (your §4, `A02`).** Chrome sits **flush with the screen edges**:
   - Window backgrounds extend into the insets (gesture strip, camera cutout); buttons and text stay inside the safe area.
   - **Bottom edge:** keep buttons clear of the **tappable-element inset**, not the full system-gesture inset. On gesture navigation, Android reserves only the edge *swipe*; a tap just above the handle reaches the app.
     - 153 px sounds like `systemGestures`. Please try `tappableElement` (or `mandatorySystemGestures`); it should be much smaller.
     - Keep at least 4 GUI px between the buttons and the screen edge.
   - **Landscape:** the hub rail's background extends into a side cutout, and its buttons start after the inset.
   - See the portrait station board on the canvas: the bar reaches the edge, and the buttons sit above the handle.

## 2. Answers to your round 3 questions (your §5)

1. **Station review:** confirmed, "tap or drag → frozen footprint → adjust in the picker or with the handles → Build". The picker goes in the **sheet slot** (§3.2).
2. **"Join station":** inside the picker, **before** Build, as a dropdown row that appears only when there's a choice to make (§3.4). There is no extra step after Build.
3. **Design items:** covered in §1.

---

## 3. Stations

### 3.1 Flow

1. **Tap Station** in the rail toolbar.
   - The **station picker** opens in the sheet slot.
   - The toolbar switches to its picker plane: status · ✕ · Build only, the same as during bridge picking. Landscape: the bar spans the map area beside the panel. Portrait: the tool row hides, the picker sits above the one-row toolbar.
   - The status line reads "Tap to place · drag for any size".
2. **Tap a tile** → PENDING footprint at the picker's size and orientation.
   - The footprint is **centred on the tapped tile**, as near as the size allows. A finger tap shouldn't land in a corner of the station.
   - Desktop anchors the footprint at its north tile, so this is a small new offset [custom].
3. **Or drag an area** → PENDING rectangle, using the drag-and-drop mechanism (your Q3).
   - The rectangle's sides become tracks × length, and the picker's buttons update to match.
   - If a side is longer than 7, no button is lowered and the label reads "Platform length: 9".
4. **While PENDING:**
   - The footprint is outlined white, with **blue corner handles**.
   - The **catchment area** shows if that toggle is on.
   - The picker shows **Accepts / Supplies** for the footprint.
   - The status line shows "Station 2×5 · £3,210", or the error in red with Build disabled. Typical errors: "Too close to another station", "Adjoins more than one existing station", "Station too spread out".
5. **Adjust:**
   - **Picker changes** (orientation, tracks, length) resize the footprint in place. It stays centred on the tapped tile, or anchored at the first corner after a drag.
   - **Dragging a corner handle** resizes it as an area.
   - **Dragging inside the footprint** moves it (new gesture, §7).
   - Every change re-runs the estimate.
6. **Build** → DONE: "Built station · £3,210" with **Undo · £x**, which demolishes the footprint.
   - The **station tool stays selected and the picker stays open**, so the second station is one more tap away.
7. **Leaving:**
   - **Picker ✕** drops the station tool and returns the toolbar to its tool row.
   - **Back:** discards a pending footprint first, then closes the picker.

### 3.2 Station picker, mobile layout

```
WINDOW build_station (mobile WindowDesc)   desktop colour   in the sheet slot
VERTICAL (padding 0,4,4, spacing 4)
  HORIZONTAL (h 40)
    PANEL   WID_BRAS_TITLE   fill(1,0)   "Station"   (drawn panel, not a caption)
    IMGBTN  WID_BRAS_CLOSE   40×40       ✕ = drop the station tool
  HORIZONTAL (spacing 4)
    2 × IMGTEXTBTN orientation, fill(1,0), min 120×40   station sprite + "Along ↗" / "Along ↘"   (radio)
  LABEL "Tracks"            small font
  HORIZONTAL (spacing 0)    7 × TEXTBTN 40×40 "1"…"7"   (radio)
  LABEL "Platform length"   small font
  HORIZONTAL (spacing 0)    7 × TEXTBTN 40×40 "1"…"7"   (radio)
  TEXTBTN  toggle "Show catchment area" ··· On/Off       fill(1,0) h 32
  SELECTION WID_BRAS_SEL_JOIN   plane 0: DROPDOWN "Join Fronford North" fill h 32   | plane 1: empty
  SELECTION WID_BRAS_SEL_TYPE   plane 0: DROPDOWN "Type: Default"     fill h 32    | plane 1: empty (base game)
  PANEL    WID_BRAS_COVERAGE fill(1,1)   Accepts / Supplies text (the desktop text, wrapped)
                                         before placement: "Accepts and supplies appear once the station is placed."
```

- **Height at the landscape floor** (349): about 330 without the optional rows, so it fits. With the Join and/or Type rows, the picker's content **scrolls** (drag-scroll).
- **Width:** 7 × 40 = 280, which fits the 300 panel and the 349 portrait width.
- **Station class/type** (NewGRF stations):
  - The row shows only when there is more than one class or type.
  - It opens the class/type list. Interim: the desktop picker's list window; mobile design later.
  - The base game hides the row.

### 3.3 Toolbar while a picker is open

- **Landscape:** the same plane as bridge picking, `WID_RT_SEL_TOOLS` = empty, but with the Confirm plane present: `INSET status · ✕ · Build`.
- **Portrait:** row B (tools) is hidden. Row A stays: Exit · status · ✕ · Build.

### 3.4 Joining an existing station

- **When the row shows:** the join dropdown (`WID_BRAS_SEL_JOIN`) appears only when the pending footprint can join something: adjacent to an existing station of this company, or within reach under the distant-join setting.
- **Entries:** "Join <name>" per candidate, then "New station".
- **Default:** whatever the desktop would do without Ctrl. For example, adjacent to exactly one station → join it.
- **Status line:** includes the choice, e.g. "Station 2×5 · joins Fronford North · £3,210".
- **Changing the choice** re-runs the estimate.
- **Build** sends the command with the chosen station, so the desktop's "join station" popup never appears in the mobile layout.

## 4. Depots

- **No picker window** in the mobile layout.
- **Tap a tile** → PENDING single tile.
- **Orientation (your Q2):**
  - If exactly one neighbouring tile has a track end pointing at this tile, the entrance faces it. Status: "Depot faces the track · £540".
  - Otherwise the last-used direction is used, and the status adds "· check the entrance".
- **Drawing:** a **ghost depot** on the tile plus a **yellow entrance arrow** [custom]. If the ghost is costly, the arrow alone is enough.
- **Rotate:** a **Rotate** button (40×40) appears in the toolbar, just before ✕, **only while a depot is pending**. Each tap turns the entrance clockwise and re-runs the estimate.
  - Landscape width check at the floor: tools 452 + Rotate 44 + ✕ 44 + Build 60 leaves the status line about 120.
- **Drag inside the pending tile** → move it (§7).
- **Build** → DONE + Undo (demolish).

## 5. Waypoints and signals

- **Waypoint** (More): like a one-tile station on existing track. Tap the track tile → PENDING → Build. No picker unless NewGRF waypoint types exist (then the same Type row as stations).
- **Signals:** not needed for the one-train flow. Keep the interim desktop behaviour; the design comes with a later round (with signal dragging along a track).

## 6. Info windows in the sheet slot

### 6.1 Station window

Opens from a tap on a station sign or tile, or from Company › Stations.

```
WINDOW station_view (mobile WindowDesc)   GREY   sheet slot
VERTICAL
  HORIZONTAL (h 40): PANEL title (station name) fill · IMGBTN rename 40×40 · IMGBTN close 40×40
  HORIZONTAL (h 40): 3 × TEXTBTN tab, fill: Waiting · Accepts · Ratings
  SELECTION content
    Waiting:  PANEL list fill(1,1) resize(0,40)
              rows: [cargo icon] name ··· amount [▸/▾]
              tap a row → expand its destinations (indented rows, 28 tall); reuse the desktop tree
              group-by and sort: hidden in mobile v1 (desktop default grouping)
    Accepts:  text panel (desktop accepts text)
    Ratings:  rows: cargo ··· rating %
  HORIZONTAL (h 40): 3 × TEXTBTN fill: Show on map · Trains · Catchment (toggle)
```

- **Show on map** → scrolls the main view to the station; the sheet stays open.
- **Trains** → the train list filtered to this station (layout in round 4; interim desktop).
- **Rename** → the existing text query, with the soft keyboard.

### 6.2 Town window

```
WINDOW town_view (mobile WindowDesc)   BROWN   sheet slot
VERTICAL
  HORIZONTAL (h 40): PANEL title (town name) fill · IMGBTN rename 40×40 · IMGBTN close 40×40
  PANEL info fill(1,1): label ··· value rows, 28 tall
    Population ··· 2,140 · 41 houses
    Passengers last month ··· 120 of 410
    Mail last month ··· 30 of 160
    (cargo the town needs to grow, if any)
    Growth ··· every 30 days / not growing
    Your rating ··· Good   (only if the company has one)
  HORIZONTAL (h 40): 3 × TEXTBTN fill: Show on map · Local authority · Cargo graph
```

- **No embedded viewport.** The main map is right there, and "Show on map" scrolls to the town.
- **Local authority** and **Cargo graph** open their desktop windows for now (later rounds).

### 6.3 Minimap

```
WINDOW smallmap (mobile WindowDesc)   BROWN   sheet slot (landscape side panel · portrait full height)
VERTICAL
  HORIZONTAL (h 40): PANEL title "Map" fill · IMGBTN close 40×40
  HORIZONTAL (h 40, padding 4, spacing 4)
    DROPDOWN mode fill(1,0): Contours · Vehicles · Industries · Link graph · Routes · Vegetation · Owners
    TEXTBTN  toggle "Names"  min 60×32
    TEXTBTN  toggle "Legend" min 60×32
  SELECTION: plane 0 = PANEL map fill(1,1) · plane 1 = PANEL legend fill(1,1), scrolls
  LABEL h 24: "Tap to go there · drag to move the map · pinch to zoom"
```

- **Legend** swaps the map for the legend, since a 300-wide panel has no room for both.
- **Gestures on the minimap:**
  - Tap: the main view jumps there (the desktop click).
  - One-finger drag: pans the minimap.
  - Pinch: zooms the minimap, replacing the desktop zoom buttons.
- **Opening the minimap** centres it on the main view. The white rectangle (the main view's area) is drawn as on desktop.
- The desktop's other buttons are replaced by the mode dropdown and the two toggles.

---

## 7. Gesture additions

| Gesture | Context | Result | Feedback |
|---|---|---|---|
| One-finger drag starting **inside** the selection | PENDING station, depot or waypoint | **Move** the selection (whole tiles), live re-estimate | Footprint follows; label above the finger |
| One-finger drag on a corner handle | PENDING station | Resize as an area; the picker numbers update | As ADJUSTING |
| One-finger drag elsewhere | PENDING (any tool) | Pan, as round 2 | – |
| Tap | Minimap | Main view jumps there | Main view moves; the rectangle follows |
| One-finger drag / pinch | Minimap | Pan / zoom the minimap | – |

## 8. [custom] list for round 3, ranked

1. **Station PENDING**: tap footprint + area drag, frozen; the picker resizes it in place; catchment and Accepts/Supplies for the pending footprint. High.
2. **Mobile layouts** for the station picker, station window, town window and minimap. Mostly stock widgets; minimap gestures are the custom part. Medium.
3. **Join dropdown** in the picker (candidates, desktop default, status text). Medium.
4. **Move by dragging inside** a pending selection. Medium.
5. **Depot**: auto-orientation, the Rotate button, entrance arrow (ghost optional). Medium.
6. **Reveal the pending selection** when a sheet opens or the layout changes. Low.
7. **Footprint centred on the tapped tile.** Low.
8. **Category flyout auto-width.** Low.
9. **Insets**: flush chrome, tappable-element inset at the bottom. Low–medium.
10. **"Building…" state**: with multiplayer work.

## 9. Fixes from my review of the round 1 screenshots

1. **Left padding:** the hub title ("World") and the town names touch the panel's left edge (`L02`, `P02`). Add 6–8 GUI px to titles and list rows.
2. **Portrait Build flyout** (`P05`): centre it above the Build button (it's left-aligned now).
3. **Portrait dock labels** (`P01`): use the normal font. Each button is about 68 wide, so "Vehicles" and "Company" fit. Keep the small font on the 40-wide landscape rail.
4. **Full portrait sheet** (`P03`): fine as built.

### 9b. Fixes from my review of the round 2 screenshots

1. **Portrait More flyout (`P04`):** it covers ✕ and Build. Anchor it above the **whole** toolbar (above row A), so the status line, ✕ and Build stay visible.
2. **More flyout grid (`L10`):** every cell is the same 56×48, left-aligned. "Track NE–SW" and "Track NW–SE" are currently stretched to double width.
3. **Disabled Build (`L01`, `L09`):** draw it grey like the disabled ✕. The dark green reads as nearly active.
4. **Status-line errors (`L09`):** capitalise the first letter and use the warning sign: "⚠ Site unsuitable for tunnel entrance", not "! site unsuitable…".
5. **Bridge rows (`L08`):** the speed line is grey on olive, which is low contrast. Use the same dark text as the name line.

### 9c. Notes from the phone photos (`A01`–`A09`)

- **Bottom gap (`A02`, `A04`, `A07`):** the dock and the portrait toolbar still float above a strip of map. This is §1.7: extend the background to the edge and use the tappable-element inset.
- **Round 1 fixes still open:** the dock labels are still the small font (`A02`), and the town names still touch the left edge (`A03`). The Build flyout is centred now (`A06`). Thanks.
- **Tooltips on a plain tap (`A03`, `A05`):** expected until the gesture layer removes hover on touch.
- **Track flow on the device (`A07`–`A09`):** drag label, review handles, Build and Undo with refund all work by touch as designed.
- **Main menu (`A01`):** it's the desktop menu, small but usable. It's out of scope for now. The "fallback sound set" message is a content install, not a UI issue.

## 10. The "modern look" pilots (no work needed yet)

The user asked for modern-looking alternatives. The canvas has three pilots of the same screen with the same layout:
- **A · Glass HUD:** translucent, rounded, floating.
- **B · Flat light:** opaque, hairline borders, slight corners.
- **C · Pixel slate:** flat dark, square, game sprites and font.

The cost board on the canvas is titled "Modern pilots · what each costs". All three would be a **skin**: a client setting "Interface style: Classic / Modern" that changes only how frames, buttons, colours, fonts and icons are drawn. Every widget tree in rounds 1–3 stays the same.

**Decision (user, 25 Nov):** all three will be built as selectable skins **after the full mobile revamp is done**. The setting becomes "Interface style: Classic / Glass / Flat / Slate". No skin work before then.

**One request now, to keep that cheap later:** in the new mobile widgets, don't hard-code bevel colours or frame drawing. Go through the shared frame and colour functions, so a skin can swap them in one place.

## 11. What I need back before round 4 (buying a train, orders)

**Screenshots:** round 2 (`L01`–`L11`, `P01`–`P05`) and the phone photos (`A01`–`A03`), plus round 3 once it runs.

**Feasibility questions for round 4:**
1. **Buying a train.**
   - In the depot, buying a wagon attaches it to a train under some conditions. What exactly are they?
   - Can the mobile flow be: "buy engine → the next wagons you buy attach to it automatically, until you tap Done"? That would avoid drag-and-drop in the depot window.
2. **Orders by tapping stations.** With the orders window in the sheet slot, can "Go to" mode accept a tap on a station sign or tile on the main map, as the desktop click does?
   - Could the UI also offer quick picks: "the last two stations built" or "stations within N tiles of the depot"?
3. **Depot window.** Is it feasible to show a train's composition as a horizontal strip of vehicle sprites with ✕ per vehicle (sell), instead of the desktop drag-and-drop grid?

## 12. Canvas

https://claude.ai/code/artifact/2e606486-b785-45a9-80d7-c0f735a6e636

| Section | Boards |
|---|---|
| "Round 3" | Station (toggle: Before tap / Placed / Next to a station) · Depot · Station portrait · Station, Town and Minimap panels · Round 3 spec |
| "Modern look", to the right of round 1 | Pilots A, B and C · cost notes |
