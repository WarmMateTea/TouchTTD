# Handoff: OpenTTD mobile UI, round 4 (trains, orders) and the menus → implementation

From: the UX/layout design session
To: the C++ implementation session
Date: 26 Nov 2026

This replies to your "round 3 built → round 4" handoff (`07-impl-to-design-round4.md`). It contains:
1. my review of the round 3 screenshots, with fixes
2. answers to your open questions (your §3) and the "paused" question
3. **Part A:** buying a train, the depot, the train window and orders
4. **Part B:** the menus (main menu, new game, in-game Menu, settings, save/load, confirmations)
5. the ranked [custom] list
6. what I need back

Sizes are GUI px at 1×. Floor 775×349 (350%), default 904×407 (300%). The canvas boards for this round are under "Round 4" and "Round 4 — Menus".

---

## 1. Review of the round 3 screenshots

The build matches the design closely. Fixes:

1. **Portrait toolbar width (`P04`):** with the station picker open, row A ends about two-thirds across and the map shows beside it. It must span the full width, like the dock.
2. **Picker labels on the phone (`A18`):** "Tracks" and "Platform length" are tiny in the sprite font. Use the normal font for labels in all pickers.
3. **Station window, empty "Waiting" tab (`L12`):**
   - Show an empty state, "Nothing waiting yet", instead of a blank list.
   - The 3-button footer has an empty strip under it. The filler belongs **above** the footer, so the footer sits on the sheet's bottom edge.
4. **Toolbar while an info sheet is open in build mode (`L12`):**
   - With no tool and no pending selection, "Pick a tool" with a disabled ✕ and Build is clutter.
   - Show a third plane instead: `TEXTBTN "Show tools"` (closes the sheet; the tools come back) plus the status line "Tools are hidden while a panel is open".
5. **Minimap (`L09`):** I can't see the white rectangle marking the main view. Please check it's drawn, with the same colour as desktop and a 1 GUI px line.
6. **Orientation labels:** "Along NE-SW" / "Along NW-SE" is fine. Shorten to **"NE–SW" / "NW–SE"**; the station sprite carries the meaning, and no arrow sprites are needed.
7. **Depot entrance arrow** (white one-way arrow): fine. **"Turn" as a text button:** fine.
8. **Toolbar switching to its picker plane** when any sheet is open beside it (your §2.5): agreed. Item 4 above covers the case where there is no tool.

## 2. Answers to your questions

### 2.1 Handle marker for rectangles (your §3.1)

Two changes, both cheap:
1. **Draw handles in a second pass**, after all footprint tiles. A blue corner outline then wins over the white neighbouring edges. This applies to every pending selection: track ends, station corners, demolish and convert corners.
2. **Add a knob at the centre of each handle tile:** a filled 6×6 GUI px square, blue (the same blue as the outline) with a 1 px white border, drawn over everything.
   - Size: 6 px at 1×, which is 18 device px at 300%. Big enough to see, but it doesn't define the 40×40 hit area.
   - It's one tiny new sprite, or a fill-rect in the selection overlay.

Pilot A's round white knobs stay a skin detail for later.

### 2.2 Dropdowns (your §3.2)

Yes: a **touch dropdown** for every dropdown in the mobile layout.
- **Rows:** 40 tall. Text left-aligned, padding 8. The selected row is lowered, with a ✓ at the right.
- **Landscape:** a list anchored to the dropdown button. It opens below the button if it fits, otherwise above. Width = max(button width, longest row), capped at the visible map width. At most 8 rows show, then it drag-scrolls.
- **Portrait:** a **bottom sheet above the dock**, full width, titled with the dropdown's label (e.g. "Sort by"), at most 60% of the screen height, drag-scrolls.
- **Closing:** tap a row to choose and close. Tap outside, ✕ or back closes without changing.
- **Disabled rows:** drawn disabled, as on desktop.

This replaces the desktop list everywhere in the mobile layout: join station, minimap mode, all sort orders, rail type (already custom), load/unload options in orders (§4.5).

### 2.3 News ticker (your §3.3)

- **Static headline**, truncated with "…", no scrolling.
- **Tap** opens the item; **✕** dismisses it. It stays for the existing ticker duration.
- If a new item arrives while one is shown, the new one replaces it. The message history keeps everything.

### 2.4 Top edge (your §3.4, and the user)

- **Bars at the bottom/side edge:** the dock and toolbars keep the rule from round 3 (background into the inset, buttons clear of it).
- **Floating top chrome** (status pill, minimap button, news banner):
  - Place it against the **cutout's actual rectangles** (Android `DisplayCutout.getBoundingRects()`), not the whole inset band.
  - With a centred punch-hole (Poco X7 Pro), the pill (left) and the minimap button (right) can sit **4 GUI px below the screen top**, as long as they don't overlap a cutout rect.
  - Rounded corners: keep the pill and minimap button at least 8 GUI px horizontally from the screen's corner (`RoundedCorner` radius, or 8 when unknown).
  - The **news banner** is centred, so it must clear the punch-hole: put its top 4 GUI px below the cutout rect's bottom.
  - In **landscape** the cutout is at a side. The rail extends into it as now, and the pill keeps 4 from the top.
- The top chrome floats, so its background does **not** extend into the inset (unlike the dock).

### 2.5 Paused state (your §1, "should the status pill make paused more obvious?")

Yes.
- **While paused:**
  - The **Pause button** stays lowered and shows a ▶ (play) sprite instead of ‖, so it reads as "tap to play".
  - The **date line** in the pill reads **"Paused"** in yellow instead of the date. The money line is unchanged.
- **After returning to the app paused** (your background-save feature): the news banner shows "Paused while you were away · tap ▶ to continue". It auto-hides as usual; tapping it unpauses.
- **Build while paused:** the status line already says "⚠ Not allowed while paused". Make it "⚠ Paused · tap ▶ to build", and keep Build disabled until unpaused. No extra button is needed; ▶ is in the pill.

---

# Part A: from depot to running train

## 3. Flow

1. **Tap the depot** on the map → the **depot sheet** opens in the sheet slot (landscape side panel, portrait half sheet). Board "R4 · Depot and train — portrait".
2. Tap **+ New train** → the **New train** view replaces the depot sheet's content. ‹ goes back to the depot.
3. **Engines** tab: tap **+ £x** on an engine → it's bought and becomes the train being built.
   - The view switches to the **Wagons** tab by itself.
   - Tap **+ £x** on wagons to attach them, one per tap (board "R4 · New train").
4. **Next: orders** → the **train window** opens with its Orders tab, and **adding-stops mode** is on.
   - The status bar asks you to tap stations. Each tap on a station tile or sign adds the next stop.
   - Suggested stations are offered as chips.
   - **Done** ends the mode (board "R4 · Train — orders").
5. **Start** → the train leaves. The train window shows its live status ("Heading to Wedingbury Station"), and Start becomes a red **Stop**.

Every money-spending tap shows its price on the button itself. Every purchase can be reversed with **✕ Sell** in the composition strip, which refunds the vehicle's value. That meets the "preview the cost, can be undone" rule without a confirm step for each wagon.

## 4. Windows

### 4.1 Depot sheet (mobile layout of the depot window)

```
WINDOW depot (mobile WindowDesc)   GREY   sheet slot
VERTICAL
  HORIZONTAL (h 40): PANEL title (depot name) fill · [portrait: IMGBTN expand 40×40] · IMGBTN close 40×40
  PANEL list fill(1,1) resize(0,56)   one row per vehicle chain in the depot, rows 56:
      [composition strip, sprites at 1× scale, clipped with "…" if longer than the row]
      [name, bold] / [state: "Stopped · no orders" red · "Stopped" · "Waiting for free path"]
      [IMGBTN › 40×52] → opens that train's window
      empty state: "No trains here yet"
      free wagon chains (no engine) show as "Wagons without an engine" with › too
  HORIZONTAL (h 48, padding 4, spacing 4)
      TEXTBTN "+ New train"   fill(1,0) GREEN
      TEXTBTN "Clone a train" fill(1,0)   → as desktop: then tap a train on the map or in a list
```

- **Opening:** a tap on a depot tile, or a train's "Send to depot" + tap on the depot.
- **Drag-and-drop between chains:** not in v1. Moving vehicles is done in the train's Details tab (§4.4).

### 4.2 New train view

```
WINDOW build_vehicle (mobile WindowDesc, "for a train being built")   GREY   sheet slot
VERTICAL
  HORIZONTAL (h 40): IMGBTN back ‹ 40×40 · PANEL title "New train · <rail type>" fill · IMGBTN close 40×40
  PANEL composition (h ~70)
      HORIZONTAL strip: per vehicle  VERTICAL( sprite at 1× · TEXTBTN "✕ Sell" 40×18 )   drag-scrolls if wider
      LABEL summary: "<n> vehicles · <length> tiles · <capacity> · <max speed>"
      empty: "Buy an engine to start"
  HORIZONTAL (h 40): 2 × TEXTBTN tab fill: Engines · Wagons   (Wagons disabled until an engine exists)
  PANEL list fill(1,1) resize(0,48)   rows 48:
      [sprite] [name, bold / one key line] ··· [PUSHBTN "+ £price" min 72×40, GREEN]
      tap the row (not the button) → the row expands with the desktop's full stats text, collapse on second tap
      engines' key line: "<speed> · <power>"  ·  wagons': "<capacity>"
  HORIZONTAL (h 48, padding 4): LABEL "Spent £x" fill · TEXTBTN "Next: orders ›" min 130×40 GREEN (disabled until an engine exists)
```

- **List contents:** what the desktop list shows for this depot's rail type, with the same filters. The **sort** is a touch dropdown in the tab row's overflow (⋯) and defaults to cost. "Show hidden" is not exposed in v1.
- **Buying:**
  - **"+ £x" on an engine:**
    - With no train being built: buy it, and it becomes the train being built.
    - With a train being built: buy it and attach it to the end, for double heading.
  - **"+ £x" on a wagon:** buy it and attach it to the train being built. That's the attach-explicitly path from your §4.1; don't rely on the "exactly one stopped train" rule.
  - **Feedback:** the strip grows; the money in the pill drops; the button flashes once.
  - **Errors** ("Not enough cash", "Train too long") go to the panel's status line. Since the panel has no toolbar, show them as the red error banner (round 2 routing rule).
- **✕ Sell:** sells that vehicle at once and refunds its value. No confirmation: the vehicle is brand new and the refund is shown in the money line. Selling the engine sells the whole train: **this one asks** (§6.7).
- **Train being built:**
  - It's the train opened from "New train". It stays the target until the view closes.
  - Reopening "New train" with a stopped train selected, from the train's Details tab › "Add vehicles", targets that train.

### 4.3 Train window (vehicle view)

```
WINDOW vehicle_view (mobile WindowDesc)   GREY   sheet slot
VERTICAL
  HORIZONTAL (h 40): PANEL title (train name) fill · IMGBTN rename 40×40 · IMGBTN close 40×40
  HORIZONTAL (h 48, padding 4)
      PANEL status fill: line 1 state (bold; red when stopped, green when running)
                         line 2 detail: "<speed> · profit this year £x"  /  "Add at least 2 stops to start"
      SELECTION start/stop: TEXTBTN "▶ Start" 90×40 GREEN  |  TEXTBTN "■ Stop" 80×40 RED
         Start disabled with no orders; Stop when running
  HORIZONTAL (h 40): 2 × TEXTBTN tab fill: Orders · Details     (Timetable later)
  SELECTION content: Orders (§4.5) | Details (§4.4)
  HORIZONTAL (h 40): 3 × TEXTBTN fill: Show on map · Send to depot · Clone
```

- **Show on map** → the main view follows the train (desktop "centre main view", with follow). The sheet stays open.
- **Send to depot** → as desktop. It is disabled while the train is in a depot.
- **Opening:** tap a train on the map, a row in Vehicles › Trains, or a depot row.

### 4.4 Details tab

```
VERTICAL
  PANEL composition strip (as §4.2), each vehicle with ✕ Sell
  TEXTBTN "+ Add vehicles" fill h 40   → New train view targeting this train (only when stopped in a depot; else disabled: "Stop in a depot to change")
  PANEL text fill: the desktop details text (age, reliability, max speed, capacity, running cost, profit last year)
```

Reordering vehicles by drag is later.

### 4.5 Orders tab

```
VERTICAL
  PANEL list fill(1,1) resize(0,40)   rows 40:
      [number] [station name, bold / flags line: "Non-stop · load what is available"] [IMGBTN ⋯ 40×40]
      ▶ marks the current order; the current row is drawn lowered
      long-press a row (450 ms) → pick it up; drag up/down → move order (MoveOrder); lift → drop   [custom]
      empty: "No stops yet"
  TEXTBTN "+ Add stops" fill h 32   → adding-stops mode
```

- **⋯ on a row** opens a touch dropdown (§2.2) with these rows:
  - Go non-stop / Stop at intermediate stations
  - Load: Normal / Full load (any cargo) / Full load (all cargo) / Don't load
  - Unload: Normal / Unload all / Transfer / Don't unload
  - Skip to this stop
  - Delete stop (red)
- Rows like "Go to depot" orders and conditional orders show and can be deleted. **Adding** them is out of scope for v1.

### 4.6 Adding-stops mode

- **Entered by:** "+ Add stops", or automatically from "Next: orders".
- **Status bar** (as the build toolbar's status line, in the map area): "Tap a station to add it as stop <n>" · **Done** (GREEN).
  - Landscape: bottom bar over the map area beside the sheet.
  - Portrait: replaces the dock, like the rail toolbar.
- **Map taps:** a tap on a station **tile or sign** of this company, matching the vehicle type, appends "Go to <station>". The default flags come from the "non-stop by default" setting. The tile flashes; the row appears in the list. Your "Go to" placement mode, extended to signs.
  - Tap on anything else: the status line says "⚠ Not a station of yours", and nothing is added.
  - The same station twice in a row: the status line says "⚠ Already the previous stop", and nothing is added.
- **Suggested chips** (in the Orders panel while the mode is on): up to 3 chips, 32 tall, "+ <station>". Order:
  1. the two most recently built stations of this type
  2. then the station nearest the depot the train is in

  Tapping a chip adds it like a map tap.
- **Undo while adding:** each added row has ⋯ › Delete. No separate undo.
- **Leaving:** Done, back, or closing the window ends the mode.
- **Start** stays disabled until there are 2 or more orders. The status detail reads "Add at least 2 stops to start" (1 stop: "Add one more stop").

### 4.7 Vehicles › Trains list (mobile)

```
WINDOW vehicle_list (mobile WindowDesc)   GREY   sheet slot (Vehicles hub, Trains tab)
VERTICAL
  HORIZONTAL (h 40, padding 4): DROPDOWN sort fill (Name · Profit this year · Profit last year · Age · Reliability)
  PANEL list fill(1,1) resize(0,48): [mini strip] [name / state] ··· [profit this year, red if negative] [›]
  LABEL h 24: "Profit this year: £x" (sum)
```

- **Row** → opens the train window.
- **Groups:** not in v1.
- The same layout works for road vehicles, ships and aircraft later.
- The station window's "Trains" button opens this list filtered to the station, with a title row "Trains calling at <station>".

---

# Part B: the menus

## 5. Main menu

Board "Menus · Main menu".

```
WINDOW main_menu (mobile WindowDesc)   BROWN   landscape: left panel, x = 12, y = 12, w = 380, h = screen_h − 24
                                               portrait: full width − 24, top 12, height to content
VERTICAL (padding 10, spacing 7)
  HORIZONTAL: [logo sprite "OpenTTD"] SPACER [LABEL version, small font]
  PUSHBTN Continue   fill h 64 GREEN  custom-drawn: "Continue" (large) / "<company> · <date> · <money>" / "Saved <ago>"
                     shown only if a background save or any save exists
  MATRIX 2 columns (landscape) / 1 column (portrait), spacing 5, IMGTEXTBTN fill h 40 ORANGE:
     New game · Load game · Settings · Online content · Multiplayer · Scenario editor · Help and manuals · Quit (RED)
```

- **Continue** loads the background save if there is one, otherwise the newest save. The game starts **paused**, as after a background resume.
- **Multiplayer** and **Scenario editor** open their desktop windows (out of scope).
- **Quit** quits at once. The main menu has no unsaved game, so there is nothing to confirm.
- **Background:** the title game keeps running behind, as on desktop.
- **Desktop messages** at startup (e.g. "Only a fallback sound set was found", `A01`): show them as the red/yellow banner at the top, not as a window over the menu.

## 6. New game

Board "Menus · New game". This is a full-screen window, with no map behind it.

```
WINDOW game_options_generate (mobile WindowDesc)   BROWN   full screen (safe area)
VERTICAL
  HORIZONTAL (h 48): IMGBTN back ‹ 48×48 · PANEL title "New game" fill · TEXTBTN "▶ Start game" min 120×40 GREEN
  landscape: HORIZONTAL (padding 8, spacing 8)  |  portrait: VERTICAL, scrolls; Start game moves to a bottom bar
    VERTICAL "Rules" w 220:
       3 × PUSHBTN preset fill h ≥40, custom-drawn title + one line: Classic · Relaxed · Sandbox (radio)
       LABEL "A preset only sets normal game settings; all of them can be changed later in Settings."
    VERTICAL options fill(1,0), rows 40, label column w 100:
       Climate     : 4 × TEXTBTN radio  Temperate · Sub-arctic · Sub-tropical · Toyland
       Map size    : 4 × TEXTBTN radio  Small 256² · Medium 512² · Large 1024² · Custom… (opens a W × H pair of touch dropdowns)
       Towns       : 3 × radio Few · Normal · Many
       Industries  : 3 × radio Few · Normal · Many
       Terrain     : 3 × radio Flat · Hilly · Mountains
       Start year  : TEXTBTN − 48×40 · LABEL year 80 · TEXTBTN + 48×40  (hold to repeat; tap the year → number keypad)
                     SPACER · TEXTBTN "Advanced options ›" → the desktop world-generation window (seed, generator, sea level, rivers, snow line, competitors…)
```

**Presets** only set existing settings; no new game mechanics. Exact values are a gameplay choice, so please confirm with the user:

| Preset | Settings |
|---|---|
| **Classic** | Defaults. |
| **Relaxed** | Vehicle breakdowns: none · disasters: off · inflation: off · economy: steady. |
| **Sandbox** | Max initial loan: highest · competitors: none · town growth: slow · disasters: off · breakdowns: none. |

## 7. In-game Menu flyout (dock › Menu)

Revised content (variant B in landscape, A in portrait, as built):

| Left column | Right column |
|---|---|
| Save game | Settings |
| Load game | Messages and news history |
| Map tools ▸ | Help and manuals |
| Transparency options | Exit to main menu (RED) |
|  | Quit game (RED) |

- **Game options, AI/game script settings and NewGRF settings** move into **Settings** as sections (§8). In-game they open read-only where the game forbids changes, as desktop does.
- **Pause and speed stay in the status pill**, not in the menu.
- **Exit to main menu / Quit game:** ask first (§10) if the game changed since the last save. The text says when it was last saved.

## 8. Settings

Board "Menus · Settings". A full-screen window.

```
WINDOW settings (mobile WindowDesc)   GREY   full screen (safe area)
VERTICAL
  HORIZONTAL (h 48): IMGBTN back ‹ 48×48 · PANEL title "Settings" w 150 · EDITBOX search fill h 32 ("Search all settings")
  landscape: HORIZONTAL                         portrait: one pane at a time (sections list → section page, ‹ goes back)
    VERTICAL sections w 220 (rows TEXTBTN fill h 32, ›):
       Phone · Display and sound · Language and units · Saving · Building and vehicles ·
       Economy, towns, competitors · Graphics and add-ons · All settings (full list)
    PANEL settings fill(1,1)   rows ≥48, drag-scroll:
       [name, bold / help text, small, ≤2 lines]  ···  [control]
       control: boolean → 2 × TEXTBTN Off/On 45×32 (radio) · list → touch dropdown 90×32 · number → − value + (hold to repeat; tap value → keypad)
```

### Section contents

| Section | Settings |
|---|---|
| **Phone** | Touch-friendly layout · Review before building · Build button opens the category picker · Pause when leaving the app · Interface size · (later: Interface style: Classic / Glass / Flat / Slate) |
| **Display and sound** | Font size · music volume · effects volume · base graphics/sounds/music sets · show/hide signs and names |
| **Language and units** | Language · currency · units (speed, power, weight…) · date format |
| **Saving** | Autosave interval · autosaves to keep · save on exit |
| **Building and vehicles / Economy, towns, competitors** | Curated subsets of the desktop tree, mapped from its categories (Construction, Vehicles, Limitations, Disasters, Economy/Accounting, Towns, Industries, Competitors/AI). These are the ~20 most-used settings each. Everything is still in "All settings". |
| **Graphics and add-ons** | Opens NewGRF settings and AI/game script settings (desktop windows for now) |
| **All settings (full list)** | The desktop settings tree in this layout: category rows drill down, setting rows as above. The desktop's "Show: Basic / Advanced / Expert" filter becomes a touch dropdown in the section header. |

- **Search** covers every setting's name and help text. Results show their section, small, above each row. Typing raises the soft keyboard; results update as you type.
- **In-game restrictions:** greyed rows with "Can only be changed before the game starts" under the name (desktop rules).

## 9. Save and load

Board "Menus · Save / Load / confirmation".

```
WINDOW saveload (mobile WindowDesc)   GREY   in game: sheet slot (landscape side panel · portrait full sheet)
                                             from the main menu: full screen
VERTICAL
  HORIZONTAL (h 40): PANEL title "Save game" | "Load game" fill · IMGBTN close 40×40
  [Save only] HORIZONTAL (h 48, padding 4, spacing 4): EDITBOX name fill (prefilled "<company name>") · TEXTBTN "Save" 75×40 GREEN
  PANEL list fill(1,1) resize(0,48) rows 48, 3 lines:
      line 1 bold: save name  [+ tag "LEFT THE APP" for the background save, GREEN]
      line 2: "<company> · <in-game date> · <money>"
      line 3: "Saved <time ago>"   ("Saved automatically …" for autosaves and the background save)
      Load: tap a row → it expands with HORIZONTAL( TEXTBTN "Load" fill GREEN · TEXTBTN "Delete" 80 )
      Save: tap a row → copies its name into the field (Save then asks to overwrite)
      pinned first (Load): the background save, if any
      last row: "Autosaves (<n>) ›" → the same list filtered to autosaves
  HORIZONTAL (h 32): DROPDOWN sort fill: Newest first · Name
```

- **Lines 2–3** come from the save's header (company name, date and money are in the savegame info the desktop load window already reads). If a field is missing, drop it rather than showing "?".
- **Saving over an existing name** asks (§10). Saving a new name doesn't.
- **Load while a game is running** asks if the current game changed since its last save.
- **Delete** always asks.

## 10. Confirmation pattern

One pattern for every "are you sure": quit, exit to main menu, overwrite, delete, sell a train, load over an unsaved game.

```
WINDOW confirm (modal)   GREY   landscape: centred in the visible map area, w = min(320, visible width − 16)
                                portrait: bottom-anchored above the dock, full width − 16
VERTICAL
  PANEL title, h ≥32, colour RED for destructive / GREY otherwise, text bold: a question naming the thing
        ("Overwrite "Fronford Rail"?", "Delete "Coal empire"?", "Quit without saving?")
  PANEL body, padding 10: one or two sentences on the consequence ("The save from 5 minutes ago will be replaced. This can't be undone.")
  HORIZONTAL (padding 6, spacing 4), right-aligned:
     TEXTBTN "Cancel"  min 100×40
     TEXTBTN <verb>    min 110×40, RED if destructive   ("Overwrite", "Delete", "Quit", "Sell train")
```

- Labels are **verbs, never Yes/No**. The destructive button is always on the right.
- **Back, ✕ or a tap outside = Cancel.** Nothing happens until the verb button is tapped.
- **Quit / Exit with unsaved changes** gets three buttons: "Cancel" · "Save and quit" (GREEN) · "Quit" (RED).
- This reuses the desktop query window with a mobile WindowDesc.

---

## 11. [custom] list for round 4, ranked

1. **Explicit "train being built"** target for wagon purchases, plus the New train view with the composition strip and per-vehicle ✕ Sell. High.
2. **Adding-stops mode:** station taps (tile and sign) append orders, plus suggestion chips and the status bar. High.
3. **Touch dropdown** (§2.2), used everywhere. High.
4. **Train window, Orders and Details tabs** (mobile layout); order ⋯ options through the touch dropdown. Medium.
5. **Long-press-drag to reorder orders.** Medium (can ship after the rest).
6. **Settings window:** sections, curated lists, search over the whole tree, the row control types. Medium–high (the search and the mapping).
7. **Save/load list** with save-header details, expandable rows, autosave folder row. Medium.
8. **Main menu** with Continue (background save). Medium.
9. **New game:** presets, short options, Advanced → desktop. Medium.
10. **Confirmation pattern** (mobile WindowDesc for the query window, with three buttons for unsaved quit). Low–medium.
11. **Handles:** second-pass drawing + knob. Low.
12. **Paused state** in the pill + resume banner. Low.
13. **Top chrome against the cutout rects.** Low–medium.
14. **Static news headline.** Low.
15. **Round 3 fixes** (§1). Low.

## 12. What I need back

- Screenshots of round 4, both orientations, plus the phone.
- **Presets:** the user's OK on the exact values (§6), or changes.
- **Feasibility questions for round 5** (likely: road vehicles, or the remaining info windows: finances, company, industries, subsidies, graphs; the user decides):
  1. Can the save list read the company name, in-game date and money without loading the full save? I'm assuming the savegame header has them.
  2. Can a settings search run across the whole tree and show the parent category for each hit? Is the setting help text available as plain text?
  3. Is there a cheap way to give the Finances window a phone layout? It's a wide table: years × categories. I'm thinking one year per page, swiped, or just "this year / last year" columns.
