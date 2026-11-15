# Handoff: OpenTTD mobile UI, round 2 (laying track) → implementation

From: the UX/layout design session
To: the C++ implementation session
Date: 14 Nov 2026

This replies to your "round 1 built → round 2" handoff. It covers:
- decisions on your round 1 notes
- answers to your round 2 questions
- the complete round 2 spec: rail toolbar, track laying with review before building, bridges, tunnels, errors and undo
- what I need back for round 3

It stands on its own. Sizes are GUI px at 1×, as before. The design floor is still 775×349 (350%) and the default 904×407 (300%).

> **One thing to know first.** The whole round 2 flow (drag → lift → review → Build) works with a mouse as well as a finger. You can build and test it on desktop with the mobile layout switched on, before the gesture layer exists. Only the two-finger and handle-precision parts need a touch device.

---

## 1. Decisions on your round 1 notes (your §2)

1. **No bold font.** Accepted. The money line stays yellow, red when negative. Town names keep their desktop colours.
2. **Menu button shows a gear.** Accepted for now; the menu contains Settings, so the gear is a fair stand-in. A ☰ sprite goes on the [custom] list as low-priority artwork.
3. **Hub title drawn as a panel, not a caption widget.** Accepted.
4. **Map strip below list sheets.** Fill it. Extend the window to the bottom of the slot and give the extra height to a filler panel in the window colour, not to the list. A sheet should always look attached to the dock (portrait) or the screen edge (landscape).
5. **Slot tabs open desktop windows that don't fit.** Agreed, they need mobile layouts. The schedule is in §12.
6. **Graphs opens the operating-profit graph directly.** Fine until the graphs round.
7. **Newspaper.** Interim rule until it's designed (round 5): open it centred in the visible map area, never covering the dock in portrait, and scale it down only if it doesn't fit. Tap or ✕ closes it.
8. **Construction toolbars.** This round (§4).
9. **Scroll-to-town centres on the screen.** This becomes more important now: the "visible map rect" (round 1 custom #8) is also used by the round 2 label clamp, edge scroll and handle placement. Please build it with round 2.

## 2. Answers to your round 2 questions (your §5)

### Q1. Click on lift
Confirmed for **all touch input**:
- **Touch-down:** the button draws lowered immediately, so the lift latency never feels like lag.
- **Lift:** the click fires if the finger is still inside the button rectangle expanded by **6 GUI px** on each side. Leaving that area raises the button and cancels the click.
- **Inside a scrollable list:** moving more than **6 GUI px** turns the touch into a scroll and cancels the row's tap.
- **Map:** the slop stays 6 GUI px.
- **Long-press (450 ms) on a button:** shows the tooltip and never clicks.
- **Exception, auto-repeat widgets** (scrollbar arrows, +/− spinners): they fire on touch-down and repeat while held, as on desktop, and show no tooltip.

### Q2. Bridges
The bridge choice **replaces** ✓:
- Drag the span → lift → the bridge list opens at once with the span still highlighted.
- Each row shows the total cost, and **tapping a row builds that bridge**. That tap is the confirmation.
- ✕ or back cancels. See §7.

### Q3. Live errors
Yes. While dragging, the label turns red with the reason. After lift, the reason is in the status line and Build is disabled.

### Q4. The other design items
Covered in §1 and §12.

---

## 3. Round 2 in one paragraph

Picking a tool opens a **rail toolbar**: a bottom bar in landscape, and in portrait a two-row bar that replaces the dock. The toolbar ends in a **status line, Cancel and Build**.
- **Drag** on the map and the track outline follows your finger, with a **live length, cost or error label** above it.
- **Lift** and nothing is built yet. The selection stays on screen with a **handle at each end**, and the status line shows the cost.
- **Adjust:** drag a handle to move that end. You can pan and zoom with two fingers, or with one finger anywhere off the handles.
- **Finish:** tap **Build** (green) or **Cancel**.
- **Undo:** after building, an **Undo** button showing its own cost is offered for a few seconds.
- **Destructive tools** (remove, demolish, convert) always ask, with a red button.
- **Bridges** use the bridge list as the confirmation step. **Tunnels** are tap → review → Build.

---

## 4. Windows

### 4.1 Rail toolbar, landscape

```
WINDOW mobile_rail_toolbar_landscape   GREY   x = dock_w, y = screen_h − 48, w = visible map width, h = 48
HORIZONTAL (padding 3,4, spacing 4)
  SELECTION WID_RT_SEL_TOOLS                    plane 0 = tools, plane 1 = empty (bridge picking)
    HORIZONTAL (spacing 4)
      IMGBTN  WID_RT_CATEGORY  40×40   sprite: current rail type's toolbar icon   → Build flyout + rail types (§4.4)
      SPACER  4
      IMGBTN  WID_RT_AUTORAIL  40×40   tool, toggle
      IMGBTN  WID_RT_REMOVE    40×40   modifier toggle (desktop's remove button)
      IMGBTN  WID_RT_DEMOLISH  40×40   tool, toggle
      SPACER  4
      IMGBTN  WID_RT_STATION   40×40   tool   (behaviour: round 3; interim §7)
      IMGBTN  WID_RT_DEPOT     40×40   tool   (round 3)
      IMGBTN  WID_RT_SIGNALS   40×40   tool   (round 3)
      IMGBTN  WID_RT_BRIDGE    40×40   tool
      IMGBTN  WID_RT_TUNNEL    40×40   tool
      SPACER  4
      IMGBTN  WID_RT_MORE      40×40   → More flyout (§4.5); shows the chosen tool's icon, lowered, while a More tool is active
  INSET   WID_RT_STATUS    fill(1,0)  min 120×40   status line, normal font, up to 2 lines then "…"
  IMGBTN  WID_RT_CANCEL    40×40   ✕
  SELECTION WID_RT_SEL_CONFIRM                  plane 0 = Build/Remove/…, plane 1 = Undo, plane 2 = empty (bridge picking)
    TEXTBTN WID_RT_CONFIRM 56×40   ✓ + label; colour GREEN for building, RED for remove/demolish
    TEXTBTN WID_RT_UNDO    min 100×40   "Undo · £x"
```

- **Width check at the floor:** 775 − 48 (rail) = 727. The tools take about 452 and the end cluster about 100, leaving about 150 for the status line. At 300% the status line gets about 280.
- **Build hub button** (on the rail): stays lowered while the toolbar is open; tapping it leaves build mode (§9).
- **While the bridge list is open** (it uses the sheet slot), the bar shrinks to the map area left of the slot and shows only the status line and ✕.

### 4.2 Rail toolbar, portrait

```
WINDOW mobile_rail_toolbar_portrait   GREY   x = 0, y = screen_h − 92, w = screen_w, h = 92
                                             the dock is hidden while this is open and restored on exit
VERTICAL (padding 4, spacing 4)
  HORIZONTAL (spacing 4)                         row A
    TEXTBTN WID_RT_EXIT      56×40   build icon + "Exit", GREEN, lowered = stands in for the Build hub
    INSET   WID_RT_STATUS    fill(1,0) min 120×40
    IMGBTN  WID_RT_CANCEL    40×40
    SELECTION WID_RT_SEL_CONFIRM   (as landscape)
  SELECTION WID_RT_SEL_TOOLS                     row B; plane 1 = empty while bridge picking
    HORIZONTAL (spacing 0)   8 × IMGBTN, fill(1,0), min 40×40, equal widths
      CATEGORY · AUTORAIL · REMOVE · DEMOLISH · STATION · DEPOT · SIGNALS · MORE
```

- **Width check at the floor (349):**
  - Row B: 8 × 40 = 320, plus padding, fits.
  - Row A: 56 + 40 + 56 + spacing leaves the status line about 177.
- **Bridge and Tunnel move into More** in portrait.
- **Rotation:** two WindowDescs with the same widget IDs, swapped by the layout pass, exactly like the dock. Tool, pending selection and status survive the swap.

### 4.3 Status line, Cancel and Confirm by state

| State | Status line | Cancel | Confirm plane |
|---|---|---|---|
| Tool selected, nothing pending | Tool hint: "Drag to lay track", "Drag to remove track", "Drag over an area to demolish", "Drag from one bridge end to the other", "Tap a slope to start a tunnel" | disabled | Build, disabled |
| Dragging | "Lift to review" | disabled | Build, disabled |
| Pending, valid | "9 tiles · £1,395" | enabled | **Build** (green) |
| Pending, estimate failed | "⚠ Land sloped in wrong direction" in red; long-press shows the full text | enabled | Build, disabled |
| Pending, remove / demolish / convert | "Remove 9 tiles · £270" | enabled | **Remove** / **Demolish** / **Convert** (red) |
| Building (command sent, waiting; matters in multiplayer) | "Building…" | disabled | disabled |
| Done | "Built 9 tiles · £1,395" | hidden | **Undo · £270**, for 8 s or until the next touch on the map |
| Bridge picking | "Pick a bridge for 6 tiles" | enabled | (empty) |

Notes:
- "Tiles" means the number of tiles in the selection.
- The cost comes from the test-mode estimate (your Q1 answer).
- If cash is short, the estimate's own error text ("Not enough cash…") is the error.

### 4.4 Build flyout with rail types

This is the round 1 Build flyout, opened from **WID_RT_CATEGORY**. It gains a rail-type list when the current category is Rail:

```
VERTICAL (padding 4, spacing 4)
  (round 1 3×2 category grid, Rail lowered)
  2 px dark divider
  VERTICAL (spacing 0)   one row per rail type the game offers, custom-drawn, min 176×40:
     [rail-type icon] [name, left] ··· [cost per piece, right]   unavailable types drawn disabled
```

- **Rail type row** → switches the rail type, keeps the current tool, closes the flyout. If a selection is pending, it is re-estimated with the new type and kept.
- **Another category** → that category's toolbar replaces this one, and the pending selection is discarded.
- **Anchoring:** above the category button, left-aligned to it. At 350% it may cover the status pill; that's fine, since it sits on top while open.

### 4.5 More flyout

```
WINDOW mobile_rail_more   GREY   anchored above WID_RT_MORE, right edge aligned to it
MATRIX, 4 per row, IMGTEXTBTN 56×48:
  landscape:  Waypoint · Convert · Track N–S · Track E–W
              Track NE–SW · Track NW–SE
  portrait:   Bridge · Tunnel · Waypoint · Convert
              Track N–S · Track NE–SW · Track E–W · Track NW–SE
```

- **A tool** → selects it and closes the flyout. The More button then shows that tool's icon, lowered.
- **Tap outside** → closes the flyout, without changing the tool.

### 4.6 Bridge list, mobile layout

```
WINDOW build_bridge (mobile WindowDesc)   desktop colour   in the sheet slot (side panel / half sheet)
VERTICAL
  HORIZONTAL (h 40)
    PANEL   WID_BBS_TITLE   fill(1,0)   "Choose a bridge · N tiles"   (drawn panel, not a caption: no window drag)
    IMGBTN  WID_BBS_CLOSE   40×40       ✕ = cancel
  DROPDOWN  WID_BBS_SORT    fill(1,0) h 40   Max speed ↓ · Max speed ↑ · Cost ↓ · Cost ↑ · Name
  HORIZONTAL
    PANEL       WID_BBS_LIST  fill(1,1) resize(0,48)   rows 48:
                [bridge sprite] [name / max speed, 2 lines] ··· [total cost for this span, right]
    VSCROLLBAR  WID_BBS_SCROLLBAR 12
  PANEL  h 24   "Tap a bridge to build it"
```

- **Row tap** → builds that bridge over the pending span. Desktop does the same on click, so this reuses existing behaviour.
- **Handles** on the span stay usable while the list is open. Moving one closes and reopens the list for the new span (simplest), or refreshes it in place if that's easy.

### 4.7 Where errors go (mobile layout only)

| When | Shown as |
|---|---|
| Any construction toolbar is open and the error comes from its estimate or command | **Status line**, red. No popup window. |
| Anywhere else | **Red error banner**, built like the news banner but red, in the same position. It auto-hides on the existing error timer; tap or ✕ dismisses it. |
| "Estimated cost" popup (Shift-click on desktop) | Never shown in the mobile layout; the status line replaces it. |

### 4.8 New setting

**"Confirm before building"**:
- Location: Settings › Interface, mobile layout only. Default **on**.
- **When off:** track builds on lift and tunnels build on tap, as on desktop. The live label still shows while dragging, and lifting over a window still cancels.
- **Never affected:** remove, demolish and convert always ask, and bridges always go through the bridge list.

---

## 5. Pending selection: state machine

| State | Enter when | Map shows | Leave |
|---|---|---|---|
| **IDLE** | Tool selected, nothing pending | Nothing (no hover on touch) | 1 finger down on map → PRESSED |
| **PRESSED** | Finger down, not yet moved past the slop | Tile under the finger highlighted | Moves > 6 px → DRAGGING · lifts → *tap* (per tool, §7; track tools → PENDING with 1 tile) · 2nd finger → pan/zoom, back to IDLE after |
| **DRAGGING** | Moved past the slop | Selection from start tile to finger tile (tool's own shape), label 48 px above the finger | Lift over map → PENDING (or BUILDING if "Confirm before building" is off and the tool isn't destructive) · lift over any window → IDLE · back → IDLE · 2nd finger → selection frozen, gesture becomes pan/zoom, PENDING once all fingers are up |
| **PENDING** | Lifted | Frozen selection + 2 handles; label hidden; cost or error in the status line | Touch inside a handle → ADJUSTING · 1-finger drag elsewhere → pan (selection stays) · tap elsewhere on map → ignored · Build → BUILDING · Cancel / back / other tool / another category / hub / Exit → IDLE (or leave build mode) · Remove toggle or rail type change → re-estimate, stay PENDING |
| **ADJUSTING** | Dragging a handle | As DRAGGING, other end fixed | Lift → PENDING (lifting over a window leaves the end where it was) |
| **BUILDING** | Build tapped | Frozen selection | Success → DONE · failure → PENDING with the error in the status line |
| **DONE** | Command succeeded | New track; no selection | Undo → removal command (§8) · any touch on map, 8 s passing, or another tool → IDLE |

**Implementation notes** (from your Q2 answer):
- PENDING is "`VpHandlePlaceSizingDrag()` stops at release and keeps start tile, end tile and drawing style".
- Build calls the toolbar's existing `OnPlaceMouseUp()` path.
- ADJUSTING edits one of the two stored tiles and re-runs the same drawing and estimate.

**Estimate timing:**
- Run the test-mode estimate whenever the selection's **end tile changes**, not on every pixel of movement.
- Run it again on entering PENDING, after ADJUSTING, on a rail type change and on a Remove toggle.
- An estimate may be stale by the time Build is tapped. That's fine: a failure at BUILDING returns to PENDING with the real error.

**Live label (DRAGGING and ADJUSTING):**
- Content: "{n} tiles · £{cost}", or the error text on a red background.
- Placement: centred horizontally on the finger, its bottom 48 GUI px above the touch point, kept inside the visible map rect.
- It can reuse the desktop measurement tooltip ("Length: 5") drawing.

**Handles:**
- Appearance: both ends of a pending selection get a thick yellow tile outline plus a small square grip at the tile centre [custom drawing].
- Hit area: a square of max(tile size on screen, 40×40 GUI px) centred on the tile.
- When the two hit areas overlap (1–2 tile selections), the nearer centre wins.
- Area tools (demolish, convert): the handles are the two corners the drag started and ended on.

**Edge auto-scroll (DRAGGING and ADJUSTING):**
- When the finger is within **32 GUI px** of the visible map rect's edge, the map scrolls toward that edge.
- Speed ramps from 0 at the inner edge of the band to about half a screen per second at the edge.
- The selection end keeps tracking the finger's map position.

---

## 6. Gesture spec, part 2 (a build tool is selected)

This replaces round 1's "preliminary" row.

| Gesture | Context | Result | Feedback |
|---|---|---|---|
| Tap | IDLE | Track tools: 1-tile PENDING, piece chosen by tap position within the tile (as desktop). Tunnel: PENDING tunnel. Other tools: §7. | Selection + handles; status shows cost |
| One-finger drag | IDLE | DRAGGING | Outline follows the finger; live label |
| One-finger drag | PENDING, starting on a handle | ADJUSTING | That end follows; live label |
| One-finger drag | PENDING, starting elsewhere | Pan | Map moves; selection stays |
| Tap | PENDING, on the map off the handles | Ignored | (optional: status line flashes once) |
| Two-finger drag / pinch / two-finger tap | Any build state | Pan / zoom / zoom out, as round 1 | During DRAGGING: selection freezes first |
| Double-tap | Map, build mode | Off (taps place things) | – |
| Long-press | Map, build mode | Off. A finger held still then moved is a drag; held still then lifted is a tap. | – |
| Long-press | Toolbar button | Tooltip, no click | Tooltip above the finger |
| Lift | DRAGGING, finger over a window | Cancel; nothing pending | Outline disappears |
| Android back | DRAGGING → PENDING → IDLE with tool → toolbar | Cancel drag → discard pending → drop tool → close toolbar | – |

---

## 7. Per-tool behaviour

| Tool | Input | After lift | Confirm |
|---|---|---|---|
| **Autorail** | Drag (autorail shape); tap = 1 piece | PENDING with handles | Build (setting applies) |
| **Track N–S / E–W / NE–SW / NW–SE** (More) | Drag along the fixed axis; tap = 1 piece | PENDING | Build (setting applies) |
| **Remove** (modifier) | Changes the active tool to its remove version, as on desktop | Red selection | **Remove**, always asks |
| **Demolish** | Area drag; tap = 1 tile | PENDING rectangle, corner handles | **Demolish**, always asks |
| **Convert rail** (More) | Area drag | PENDING rectangle | **Convert**, always asks |
| **Bridge** | Drag from one end to the other | PENDING span; bridge list opens in the slot | Tap a row (that is the confirmation) |
| **Tunnel** | Tap a tile at the entrance | PENDING showing entrance + computed exit + cost | Build (setting applies) |
| **Station, Depot, Signals, Waypoint** | Round 3 | Interim: keep desktop behaviour. Open the existing picker (in the sheet slot if it fits, otherwise centred over the map); tap places immediately. | Round 3 |

**Curves:** autorail locks to one axis or diagonal per drag (your §4). A curve is two drags. Start the second drag on the end tile of the first; nothing special is needed.

---

## 8. Undo

OpenTTD has no real undo, so "Undo" sends the **removal command for exactly what was just built**:
- **Track:** the same start, end and track shape, in remove mode.
- **Bridge or tunnel:** demolish one head tile, which removes the whole structure.

Its cost is estimated when the build succeeds and shown on the button: "Undo · £270".

- **Offered:** for 8 s, or until the next touch on the map or tool change. One level only, and no redo.
- **Safety rule:** offer Undo for track only if **none** of the selection's tiles already had track before the build. Otherwise removing it would also delete track the player had before. Check the tiles in the UI before sending the build command; if any already had track, skip the Undo offer.
- **Multiplayer:** if someone else changed the tiles in between, the removal fails and shows its error in the status line. That's acceptable.

---

## 9. Layout and hub interaction while building

- **Visible map rect** = screen minus the dock (landscape rail), status pill row, toolbar, and the sheet slot if one is open. Used for:
  - label clamping
  - edge auto-scroll
  - scroll-to-tile
- **Landscape:** the hub rail stays visible.
  - Tapping **Build** (lowered) leaves build mode.
  - Tapping **another hub** leaves build mode first; any pending selection is discarded, since it cost nothing.
  - The bridge list is the only sheet allowed while building.
- **Portrait:** the dock is hidden, so hubs can't be reached while building. **Exit** leaves build mode.
- **Rotation mid-drag:** cancel the drag. **Rotation while PENDING:** keep the selection; the toolbar swaps layout.
- **The news banner and status pill** are unchanged.

---

## 10. [custom] list for round 2, ranked

1. **Pending selection state**: freeze on lift, Build/Cancel, BUILDING/DONE states. Critical. It is input-agnostic, so build and test it with a mouse first.
2. **Live estimate on end-tile change** + label above the finger + status line text. Critical.
3. **Handles**: drawing, 40×40 hit areas, ADJUSTING. High.
4. **Rail toolbar windows**, landscape and portrait, with their SELECTION planes, and hiding/restoring the dock in portrait. High, but mostly stock widgets.
5. **Visible map rect** (round 1 #8) + **edge auto-scroll** that uses it. High.
6. **Error routing**: status line while building, red banner elsewhere. Medium.
7. **Bridge list flow**: opens on lift, a row builds over the pending span. Medium.
8. **Tunnel pending**: compute the exit on tap, show both ends + cost. Medium.
9. **Undo-by-removal** with the pre-existing-track check. Medium.
10. **Lift over a window cancels the drag.** Low.
11. **"Confirm before building" setting.** Low.
12. **Click-on-lift + auto-repeat exception** (part of the gesture layer). Covered in §2 Q1.
13. Optional: the status line flashes when the map is tapped during PENDING.
14. Optional artwork: a ☰ menu sprite.

## 11. Suggested build order

1. Rail toolbar windows (§4.1–4.3), with stock widgets, opened from the Build hub. Desktop mouse is fine.
2. PENDING / BUILDING / DONE with Build and Cancel, plus the status line and live estimate. Still mouse.
3. Handles and ADJUSTING, the live label, edge auto-scroll, and the visible map rect.
4. Remove, Demolish, Convert, Tunnel, Bridge list (§7).
5. Undo, error routing, the setting.
6. Then the gesture layer on Android (your plan): tap-on-lift, pan, pinch, two-finger, handle hit areas, and real-finger tuning.

## 12. Schedule for the slot windows that need mobile layouts

| Round | Windows |
|---|---|
| 3: stations and depots | Station picker, depot picker, station window, town window, **minimap** (it's step 1 of the flow) |
| 4: buying a train and orders | Depot window, new vehicles list, vehicle window, orders, train list (Vehicles › Trains), a simple timetable view |
| 5 | Finances, company, industry directory, subsidies, newspaper |
| 6 | League table, graphs (with a picker), story book, goals |

---

## 13. What I need back before round 3

**Screenshots:**
- **The built round 1 shell** (`L01`–`L12`, `P01`–`P05`). Your handoff lists them, but only the desktop references arrived here. I'd like to review the build against the design.
- **The round 2 toolbar,** once it runs, in landscape and portrait, for a quick review before round 3.

**Feasibility questions for round 3:**
1. **Station placement with review.** Can the station footprint use the same PENDING mechanism, so that a tap places a footprint and changing orientation, tracks or length in the picker updates it in place? Can the catchment grid and the "Accepts / Supplies" lines be computed for a pending footprint, not just for the tile under the mouse?
2. **Depot orientation.** Is it feasible to pre-select the depot's orientation so it faces an adjacent track end, when there is exactly one? The player could still change it in the picker.
3. **Station drag-and-drop** (the "Drag & Drop" button in the desktop picker): does it use the same area selection as demolish? If so, a station can be "drag a rectangle, review, Build" like the other area tools.

## 14. Design canvas

https://claude.ai/code/artifact/2e606486-b785-45a9-80d7-c0f735a6e636. The round 2 boards are under the "Round 2" title:

| Board | Shows |
|---|---|
| R2 · Rail toolbar | landscape, tool selected |
| R2 · Dragging track | live label, error label, edge band |
| R2 · After lift | toggle between Ready / Error / Remove mode / Built |
| R2 · Bridge | span pending with the bridge list |
| R2 · Category button | category and rail-type flyout |
| R2 · Portrait | toggle between Pending / More open |
| R2 · Spec boards | the key tables from this document |

The canvas is private to the user's account; this document has everything needed to build.
