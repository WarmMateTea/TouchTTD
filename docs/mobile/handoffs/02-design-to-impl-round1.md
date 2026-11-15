# Handoff: OpenTTD mobile UI, round 1 (app shell) → implementation

From: the UX/layout design session
To: the C++ implementation session
Date: 14 Nov 2026

This document has everything decided so far. It stands on its own; the design canvas (link at the end) has the same content drawn as mockups.

**What I need back** (section 9): screenshots of a few desktop windows, and answers to four feasibility questions. Both feed round 2 (laying track).

---

## 1. Status of the design rounds

| Round | Scope | Status |
|---|---|---|
| 1 | App shell: hub dock, status pill, news banner, minimap button, hub sheets, "find a town" | **Done, approved.** Ready to build. |
| 2 | Rail toolbar, track laying by drag, cost preview, confirm/cancel, demolish/undo | Next. Needs the screenshots in §9. |
| 3 | Station and depot placement | Later |
| 4 | Buying a train, orders, starting it | Later |

---

## 2. Global decisions

- **Primary orientation: landscape.** Reasons: the isometric map is wide, track lines are long drags, and a side panel still leaves a usable map beside it.
- **Portrait is first-class, not a fallback.** Every step of every flow must be completable in portrait. Anything that only works in landscape counts as a design bug.
- **GUI scale (measured on the user's phone):** the phone is 2714×1222 device px with a pixel ratio of 3.25.
  - **Default is 300%**, which gives a 904×407 GUI px screen in landscape (407×904 in portrait).
  - **The design floor is 350%**, or 775×349 GUI px. Every layout must still fit at that size.
  - 250% was rated "readable, but a strain", so it is not a supported default.
- **Units:** all sizes below are GUI px at 1×, the same units as `NWidgetPart` sizes.
- **Minimum tap target: 40×40** (about 5.9 mm at 300% on the user's phone). This holds for anything frequent or destructive.
  - **Dense exception: 32.** Only for non-destructive items repeated in a list or picker, and only when 40 does not fit at 350%. Nothing goes below 32.
  - Device test data: 40 px got 10/10 hits at 250%, 300% and 350%. Today's 22 px toolbar buttons also got 10/10 but were about 12% slower. The test was seated and focused, so 40 is deliberately conservative.
- **The main viewport stays full-screen.** All chrome floats over it as docked windows. Windows in the mobile layout have no draggable caption and no resize box.
- **One hub at a time.** Opening a hub (Build, Vehicles, World, Company, Menu) closes whichever hub sheet or flyout was open. Hub buttons are toggles: lowered while their sheet or flyout is open, and tapping a lowered hub closes it.
- **Android back** closes the topmost flyout, then the topmost sheet. With nothing open it does nothing; the game never quits by accident. In build mode (round 2) it will cancel the drag, then drop the tool, then close the toolbar.
- **Desktop is untouched.** The mobile layout is an additional set of WindowDescs and windows, not a replacement.

---

## 3. Window inventory (new for the mobile layout)

```
WC_MOBILE_STATUS     status pill          top-left                              always
WC_MOBILE_DOCK       hub dock             left rail (landscape) / bottom bar (portrait)   always
WC_MOBILE_MINIMAP    1-button window      top-right of the visible map area     always
WC_MOBILE_NEWS       news banner          top-centre (L) / under status (P)     while a news item is live
WC_MOBILE_BUILDMENU  Build flyout         next to the Build button              on demand
WC_MOBILE_MENU       Menu flyout          next to the Menu button               on demand
WC_MOBILE_HUBTABS    hub caption + tabs   top of the sheet slot                 while a hub sheet is open
(content)            existing windows     sheet slot, below the tabs            Town directory etc., with a mobile WindowDesc
```

The window class names are suggestions; use whatever fits the codebase.

---

## 4. Per-window spec

### 4.1 Status pill

```
WINDOW mobile_status   colour GREY   no caption · not draggable · fixed size
  position: landscape x = dock_w + 4, y = 4   |   portrait x = 4, y = 4   (plus safe-area insets)
HORIZONTAL (padding 0, spacing 0)
  IMGBTN   WID_MS_PAUSE   min 40×40   toolbar "Pause" sprite         toggle, lowered while paused
  IMGBTN   WID_MS_FFWD    min 40×40   toolbar "Fast forward" sprite  toggle
  PUSHBTN  WID_MS_INFO    min 108×40  custom-drawn, 2 lines, left-aligned, padding 7:
                                       line 1  current date (normal font)
                                       line 2  company money (bold, red when negative)
```

- **PAUSE**
  - Tap → toggles pause, the same command the desktop toolbar's Pause sends.
  - Long-press → tooltip.
- **FFWD**
  - Tap → toggles fast-forward (local, as on desktop).
  - Long-press → tooltip.
- **INFO**
  - Tap → opens the Company hub on its Finances tab.
  - Long-press → tooltip with cash, loan and max loan.
- Nothing stretches. On rotation only its position changes.

### 4.2 Hub dock

The dock uses **two WindowDescs with the same widget IDs and the same OnClick**. On an orientation change the layout pass (§4.8) closes one and opens the other. That is simpler than rebuilding one nested tree.

Landscape: left rail, 48 wide, full height.

```
WINDOW mobile_dock_landscape   GREY   x = 0, y = 0, h = screen_h
VERTICAL (padding 4, spacing 4)
  IMGBTN      WID_MD_MENU      40×40                 icon: menu
  SPACER      fill(0,1)
  IMGTEXTBTN  WID_MD_COMPANY   40×44   "Company"     icon: toolbar "Company finances"
  IMGTEXTBTN  WID_MD_WORLD     40×44   "World"       icon: toolbar "Towns"
  IMGTEXTBTN  WID_MD_VEHICLES  40×44   "Vehicles"    icon: toolbar "Trains"
  IMGTEXTBTN  WID_MD_BUILD     40×52   "Build"       icon: last-used build category · colour GREEN
```

Portrait: bottom bar, 52 tall, full width.

```
WINDOW mobile_dock_portrait   GREY   x = 0, y = screen_h − 52, w = screen_w
HORIZONTAL (padding 4, spacing 0)       every child fill(1,0), min 64×44, equal widths
  IMGTEXTBTN  WID_MD_WORLD
  IMGTEXTBTN  WID_MD_VEHICLES
  IMGTEXTBTN  WID_MD_BUILD     colour GREEN · centred, reachable by either thumb
  IMGTEXTBTN  WID_MD_COMPANY
  IMGTEXTBTN  WID_MD_MENU      (label "Menu")
```

- **Order:** the most frequent action sits nearest the thumb, so Build is at the bottom of the rail and Menu, the least used, is at the top.
- **[custom] icon above label.** The stock image+text button draws image and text side by side. Fallback: icon only, with the label in the long-press tooltip.
- **BUILD**
  - Tap, with no toolbar open → reopens the last-used construction toolbar (the default).
  - If no toolbar has been used yet this game, or the setting below is off → opens the Build flyout.
  - While a toolbar is open, Build stays lowered; tapping it closes the toolbar and leaves build mode.
  - Its icon shows the category it will reopen.
- **New setting** (Settings › Interface, mobile layout only): "Build button opens: last-used toolbar | category picker". The default is last-used toolbar. It is a normal client setting and changes nothing on desktop.
- **Changing category from inside a toolbar:** each construction toolbar's first button is a category button that opens the Build flyout. Its full spec comes in round 2.
- **VEHICLES / WORLD / COMPANY** tap → opens that hub sheet on the tab it showed last.
- **MENU** tap → opens the Menu flyout.

### 4.3 Build flyout

```
WINDOW mobile_build_menu   GREY   landscape: right of the rail, bottom edge level with Build
                                  portrait: above the dock, centred on Build
VERTICAL (padding 4, spacing 4)
  HORIZONTAL (spacing 4): [IMGTEXTBTN Rail 56×48] [IMGTEXTBTN Road 56×48] [IMGTEXTBTN Tram 56×48]
  HORIZONTAL (spacing 4): [IMGTEXTBTN Water 56×48] [IMGTEXTBTN Airport 56×48] [IMGTEXTBTN Landscape 56×48]
```

- **Opened from:**
  - Build, when no toolbar has been used yet
  - Build, when the setting is "category picker"
  - the category button inside any construction toolbar
- **Any category button** → closes the flyout and any open construction toolbar, then opens that category's toolbar with its last-used rail or road type. The rail-type switch lives inside the toolbar (round 2).
- **Out of scope for now:** Road, Tram, Water, Airport and Landscape open their existing desktop toolbars.
- **Tap outside** → closes the flyout. The tap is consumed and does not reach the map.
- **Disabled categories** (no road types available yet, etc.) are drawn disabled, as on desktop.

### 4.4 Menu flyout: two variants, choice still open

**Rows:**

| Row | Note |
|---|---|
| Messages and news history | |
| Game options | |
| Settings | |
| AI / game script settings | slot only |
| NewGRF settings | slot only |
| Transparency options | |
| Map tools ▸ | extra viewport · sign list · link-graph legend |
| Save game | |
| Load game | |
| Music and sound | |
| Help and manuals | |
| About | |
| Quit to main menu | |

The 13 rows are TEXTBTN min 180×40, text left-aligned.

**Variant A, scrolling list.** This is the portrait layout either way, since 13 × 40 = 520 fits below the status pill.

```
WINDOW mobile_menu   GREY   landscape: right of the rail, top edge level with Menu
                            portrait: above the dock, right-aligned
VERTICAL (padding 4)
  HORIZONTAL
    PANEL (scrolling, rows 40)  fill(0,1)   the 13 rows
    VSCROLLBAR 12
```

In landscape the 520 px of rows don't fit the ~349 available, so the list must drag-scroll. That needs [custom] item #4 in §7.

**Variant B, two columns** (landscape only):

```
HORIZONTAL (padding 4, spacing 4)
  VERTICAL (spacing 0)   Messages · Game options · Settings · AI/GS settings · NewGRF settings ·
                         Transparency · Map tools ▸                         7 × TEXTBTN 180×40
  (1 px dark divider)
  VERTICAL (spacing 0)   Save game · Load game · Music and sound · Help and manuals · About ·
                         SPACER 40 · Quit to main menu (colour RED)          6 × TEXTBTN 180×40
  → 372×296: fits without scrolling
```

Variant B needs no scroll code, so it can ship first. The user will choose between A and B after trying them on the device.

### 4.5 Hub sheets (World · Vehicles · Company)

A sheet is **two windows in one "sheet slot"**: a small tab window, plus the real content window opened with a mobile WindowDesc. Switching tabs closes the content window and opens the next one in the slot. This reuses the Town directory, Industry directory and the rest instead of rebuilding them inside one giant NWID_SELECTION.

Slot geometry:

```
landscape:  x = screen_w − 300,  y = 0,  w = 300,  h = screen_h               ("side panel")
portrait:   x = 0,  w = screen_w,  bottom = dock top
            half:  h = 400   (or screen_h / 2)                                  ("bottom sheet")
            full:  h = screen_h − 52 (dock) − 48  → keeps a 48 px strip of map visible
```

Tab window:

```
WINDOW mobile_hub_tabs   same colour as the content windows it hosts (World = BROWN, like the Town directory)
VERTICAL
  HORIZONTAL (h 40)
    CAPTION   WID_MH_CAPTION   fill(1,0)    "World" | "Vehicles" | "Company"
    SELECTION WID_MH_SEL_EXPAND             plane 0 (portrait): IMGBTN WID_MH_EXPAND 40×40 ▲/▼
                                            plane 1 (landscape): empty
    IMGBTN    WID_MH_CLOSE     40×40   ✕
  HORIZONTAL (h 40)    TEXTBTN × N, fill(1,0), min 56×40, exactly one lowered
     World:    Towns · Industries · Subsidies · Story · Goals
     Vehicles: Trains · Road · Ships · Aircraft
     Company:  Finances · Stations · Companies · League · Graphs
```

- **EXPAND** (portrait only) → toggles the sheet between half and full height. The icon flips ▲/▼.
- **CLOSE** → closes the tab and content windows and raises the hub button.
- **Tab** → swaps the content window. The sheet remembers the last tab per hub, so reopening a hub takes 1 tap.
- **Rotation** → the slot re-docks (side panel ↔ half sheet). The content window, active tab and list scroll position are kept.

### 4.6 Content: Town directory, mobile layout

This is the only content window designed in round 1. It is step 1 of the flow: finding towns.

```
WINDOW town_directory (mobile WindowDesc)   BROWN   no caption: the tab window provides it
VERTICAL
  HORIZONTAL (padding 4, spacing 4)
    EDITBOX   WID_TD_FILTER     fill(1,0)  min 120×32 inside a 40-tall row   "Filter towns…"
    DROPDOWN  WID_TD_SORT       min 100×32                                   Name ↑ · Name ↓ · Population ↓ ·
                                                                              Population ↑ · Rating ↓ · Rating ↑
  HORIZONTAL
    PANEL       WID_TD_LIST       fill(1,1)  resize(0,40)   rows 40 tall, custom-drawn:
                                  [ name, bold, left ] ··· [ population, right ] [ (i) 40×40 ]
    VSCROLLBAR  WID_TD_SCROLLBAR  12 wide
  PANEL  WID_TD_WORLD_POPULATION   h 24   "World population: N"
```

- **Row tap** (anywhere except (i)) → scrolls the main view to the town, as a desktop click on the row does. The row stays highlighted and the sheet stays open.
- **(i) tap** → opens the Town window in the same slot, on top of the list. Closing it returns to the list.
- **Filter tap** → focuses the box and raises the soft keyboard [custom]. The list filters as you type, as on desktop.
- **Sort tap** → opens a dropdown. The desktop's separate sort-direction button is folded into the entries (Name ↑/↓ and so on). Dropdown rows must be at least 40 tall [custom].
- **List drag** (one finger, vertical) → scrolls [custom].
- **Stretching:** the list stretches both ways and the filter box stretches horizontally. Nothing else stretches.
- **Portrait "scroll to town"** centres the town in the visible map strip above the sheet, not the screen centre [custom].

### 4.7 News banner and minimap button

```
WINDOW mobile_news   news-yellow panel   landscape: x centred in map area, y = 4, w = min(310, map_w − 16)
                                         portrait:  x = 4, y = 48, w = screen_w − 8
HORIZONTAL
  PUSHBTN  WID_MN_TEXT     fill(1,0) min 120×32   ticker text, clipped (reuse the status-bar ticker drawing)
  IMGBTN   WID_MN_DISMISS  40×32  ✕

WINDOW mobile_minimap_btn   GREY   top-right of the visible map area (moves left of the side panel when one is open)
  IMGBTN  WID_MM_BUTTON  40×40   toolbar "Map" sprite
```

- **Banner text** tap → opens that news item, or the message history if the item has expired.
- **✕** → hides the banner. The banner also auto-hides on the desktop ticker's timer.
- **Minimap** tap → opens the map-of-the-world window in the sheet slot.
  - The minimap is not a hub. Its button is never lowered and does not close other sheets; the minimap just replaces the current sheet's content.

### 4.8 Layout pass (runs on every resize or rotation)

```
landscape = screen_w > screen_h
insets    = platform safe-area insets (cutout, gesture bar)                       [custom]
if orientation changed: close the dock window, open the other dock WindowDesc
place status pill → place minimap button → place news banner
if a sheet is open:          re-dock the tab window + content window into the slot
if a flyout is open:         close it (cheaper than re-anchoring)
if a build toolbar is open:  re-dock it (round 2)
```

---

## 5. Navigation map: every old toolbar button

Taps are counted from the idle map. "1–2" means 1 tap when the hub already shows that tab.

Status codes:
- **R1**: designed in this round.
- **flow**: designed in a later round of this flow.
- **slot**: a place is reserved; for now it opens the existing desktop window.

| Old toolbar button | New home | Taps | Status |
|---|---|---|---|
| Pause | Status pill › Pause | 1 | R1 |
| Fast-forward | Status pill › FF | 1 | R1 |
| Settings (game options, settings, AI/GS, NewGRF, transparency) | Menu › item | 2 | slot |
| Save / Load | Menu › Save game / Load game | 2 | slot |
| Minimap (map of the world) | Minimap button | 1 | R1 |
| Minimap extras (extra viewport, sign list, link-graph legend) | Menu › Map tools › item | 3 | slot |
| Towns | World › Towns | 1–2 | R1 |
| Subsidies | World › Subsidies | 1–2 | slot |
| Stations | Company › Stations | 1–2 | slot |
| Finances | Tap the money in the status pill (or Company › Finances) | 1 | slot |
| Companies | Company › Companies | 1–2 | slot |
| Story book | World › Story | 1–2 | slot |
| Goals | World › Goals | 1–2 | slot |
| Graphs | Company › Graphs › pick a graph | 2–3 | slot |
| League table | Company › League | 1–2 | slot |
| Industries (and fund industry) | World › Industries (the fund button is inside) | 1–2 | slot |
| Trains | Vehicles › Trains | 1–2 | flow |
| Road vehicles / Ships / Aircraft | Vehicles › Road / Ships / Aircraft | 1–2 | slot |
| Zoom in / out | Pinch · double-tap empty ground · two-finger tap (optional on-screen buttons via a setting) | 0 | R1 |
| Build rail | Build (reopens the last toolbar) · first time or setting off: Build › Rail | 1 / 2 | flow |
| Build road / tram / water / airport | Build › category | 2 | slot |
| Landscaping | Build › Landscape | 2 | slot |
| Music and sound | Menu › Music and sound | 2 | slot |
| Messages | Tap the news banner while it shows · or Menu › Messages | 1 / 2 | R1 |
| Help | Menu › Help and manuals / About | 2 | slot |

Out-of-scope areas and where they will live:
- Graphs and the league table: Company hub.
- AI/game script and NewGRF settings: Menu.
- Multiplayer (online players, chat): a Menu item for now, and later a chat button beside the status pill.
- Main menu and new-game setup: a separate screen set that does not use this shell.

---

## 6. Gesture spec, part 1 (map navigation, buttons, lists)

**Constants:**

| Constant | Value |
|---|---|
| Touch slop (below this, movement still counts as a tap) | 6 GUI px |
| Long-press delay | 450 ms |
| Double-tap window | 250 ms, at most 12 GUI px apart |
| Pinch step ratio | 1.5 |

| Gesture | Context | Result | Feedback |
|---|---|---|---|
| Tap | Map, no tool | Same as a desktop left click on that spot: opens the vehicle, station, town, industry or depot window. Signs and vehicles get a +6 px hit margin [custom]. | The window opens. Nothing else. |
| Double-tap | Map, no tool | Zoom in one step around the tap point. **Only armed when the first tap hit nothing clickable**, so taps on signs and vehicles are never delayed. | Instant zoom step. |
| Long-press | Map, no tool | **Inspect:** opens Land area information for that tile (the desktop "?" query tool). | Haptic tick at 450 ms and the tile is highlighted; the window opens on lift. |
| One-finger drag | Map, no tool | **Pan**, 1:1 with the finger. | The map follows the finger. v1 stops dead on lift; fling inertia is optional [custom]. |
| Two-finger drag | Map, any mode | Pan by the movement of the midpoint between the fingers. | The map follows. |
| Pinch | Map, any mode | Zoom in discrete steps: each time the finger distance passes ×1.5 or ÷1.5 of its baseline, zoom one level around the pinch midpoint and reset the baseline. Clamped to the allowed zoom levels. Reuse the scroll-wheel "zoom at cursor" path. | One instant zoom step per threshold. No smooth scaling in v1. |
| Two-finger tap | Map, any mode | Zoom out one step around the midpoint. | Instant zoom step. |
| Any gesture | Map, build tool active | **Preliminary; final version in round 2.** One finger builds (drag lays track with a preview). Two fingers pan and zoom. Long-press is off, so pausing before a drag never turns into an inspect. | Round 2. |
| Tap | Any button | Click, fired on lift while the finger is still inside the button, as on desktop. Sliding off cancels the press. | The button lowers while pressed. |
| Long-press | Any button or row with a tooltip | **Tooltip**, shown while the finger is down. Reuse the existing "tooltip while the right mouse button is held" mode. On lift the tooltip closes and the button is **not** activated. | The tooltip appears above the finger, never under it. |
| One-finger drag | Inside a list or matrix | Scroll the list [custom]. A drag that starts on a row and moves past the slop cancels that row's tap. | The list follows the finger and the scrollbar thumb moves. |
| Tap | Edit box | Focus and raise the soft keyboard [custom]. The sheet scrolls so the box stays visible. | Caret appears and the keyboard slides up. |
| Any drag | Window caption | Nothing: mobile windows are docked and never move. (Optional later: drag the portrait sheet's caption to switch between half and full height [custom].) | None. |
| Tap | Outside a flyout | Closes the flyout. The tap is consumed and does **not** reach the map. | The flyout disappears. |
| Android back | Anywhere | Closes the topmost flyout, then the topmost sheet. In build mode (round 2): cancel the drag, then drop the tool, then close the toolbar. | The window closes. |

**Conflicts resolved:**
- One-finger drag **pans** when no tool is active and **builds** when one is. Two fingers always pan and zoom.
- Long-press means **inspect on the map** and **tooltip on UI**. In build mode it is off.

---

## 7. [custom] list, ranked by how much the design depends on it

1. **Touch gesture layer** (critical). A recogniser for tap, double-tap, long-press, one- and two-finger drag, pinch and two-finger tap, with slop and timers. It routes each gesture to the map or to the window under the finger. Everything else rests on this.
2. **Build-mode drag semantics** (critical): preview, confirm and cancel. Spec comes in round 2.
3. **Layout pass** (high): orientation detection, swapping the two dock WindowDescs, re-docking everything, and platform safe-area insets.
4. **Drag-to-scroll in lists and matrices** (high). Every list in the game needs it.
5. **Soft keyboard on edit-box focus** (high). Needed for filters and naming.
6. **Dropdown rows at least 40 tall** in the mobile layout (medium). Used by sort dropdowns, menus and the rail-type picker.
7. **Map hit margin** for signs and vehicles (medium).
8. **"Visible map rect"** (medium-low), so scroll-to-tile centres in the uncovered area beside a panel or above a sheet.
9. **Long-press tooltip that suppresses the click** (low; mostly reuses the existing hold-to-show tooltip).
10. **Icon-above-label button drawing** (low; the fallback is icon only).
11. **News banner** as its own window, reusing the ticker drawing (low).
12. **Android back mapping** (low).
13. **Fling inertia** on the map and lists (optional).
14. **Dragging the sheet caption** to resize (optional; the ▲ button covers it).

---

## 8. Suggested build order

A suggestion only; reorder freely. Each step gives something you can run and try.

1. **Status pill and the two dock windows,** placed by a minimal layout pass with no insets yet. Buttons can open the existing desktop windows for now. This is mostly stock widgets.
2. **Gesture layer v1:** tap, one-finger pan, pinch-to-zoom and two-finger tap. This makes the map usable on a phone.
3. **Hub sheet slot and tab window,** plus the mobile Town directory, including row tap → scroll to town.
4. **Long-press:** tooltips on buttons and land info on the map.
5. **Drag-to-scroll in lists and the soft keyboard.**
6. **Menu variant B and the Build flyout.**
7. **Safe-area insets, the news banner and the Android back button.**

---

## 9. What I need back before round 2

### 9a. Screenshots

How to take them:
- Desktop build, **GUI scale 200%** (Game Options › Graphics › Interface size), default base graphics, default UI colours.
- Full-window screenshots are fine; crop if you like.
- Name the files as below.

Round 2 (track laying), needed first:

| File | What to capture |
|---|---|
| `r2-rail-toolbar.png` | The rail construction toolbar, with one tool selected (e.g. autorail). |
| `r2-railtype-dropdown.png` | The Build rail button's rail-type dropdown, open (Railway / Electrified / Monorail / Maglev, whatever the game offers). |
| `r2-drag-in-progress.png` | Autorail mid-drag over 8–10 tiles, mouse button still held, so the tile highlight is visible, including a curve or a slope if possible. |
| `r2-cost-estimate.png` | The cost readout: Shift+drag with autorail, released, so the "Estimated cost" message is showing. |
| `r2-demolish.png` | The demolish tool dragging over a few track tiles. |
| `r2-signal-picker.png` | The signal selection window (signal tool in the rail toolbar, Ctrl+click or the window that opens). |
| `r2-bridge-picker.png` | The bridge selection list that appears after dragging a bridge span. |
| `r2-error.png` | Any build error message, e.g. "Land sloped in wrong direction" or "Too close to edge of map". |

Rounds 3–4, whenever convenient:

| File | What to capture |
|---|---|
| `r3-station-picker.png` | The rail station picker: orientation, platforms, length, station class list. |
| `r3-depot-picker.png` | The train depot orientation picker. |
| `r3-station-placing.png` | The station footprint highlight while hovering to place it, with the catchment area shown. |
| `r4-depot-window.png` | The depot window with one train inside. |
| `r4-build-vehicle.png` | The "Build new rail vehicles" list with one engine selected, details visible. |
| `r4-orders.png` | The orders window for a train with two orders. |
| `r4-vehicle-window.png` | The train's main window (start/stop bar). |

### 9b. Feasibility questions

These answers decide how round 2 works:

1. **Cost preview.** Can the UI get a cost estimate for a drag without executing it? Shift-click on desktop seems to use the command's estimate-only mode. Can it run on every finger move during a drag, or is it too slow for long autorail lines? The round 2 design will show a live cost label while dragging.
2. **Keeping the preview after lift.** Desktop builds on mouse release. Mobile will be "drag → lift → preview stays → tap ✓ to build or ✕ to cancel". Can the tile selection highlight (the thing that draws the dragged track outline) stay drawn and adjustable after the pointer is released, or is it tied to the button being held?
3. **Soft keyboard.** On the Android build, does SDL's text input bring up the keyboard when an edit box is focused, or does that need new code?
4. **Tooltip on hold.** Can the existing "show tooltip while right-click is held" mode be triggered from a long-press without also firing the left click?

---

## 10. Design canvas

The round 1 mockups, widget-tree boards and the Menu A/B toggle are here: https://claude.ai/code/artifact/2e606486-b785-45a9-80d7-c0f735a6e636

The canvas is private to the user's account. Everything an implementer needs is already in this document.
