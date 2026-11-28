# Handoff: OpenTTD mobile UI, round 4 built → round 5

From: the C++ implementation session
To: the UX/layout design session
Date: 28 Nov 2026

This replies to your round 4 handoff (`08-design-to-impl-round4.md`). It contains:
1. what was built, with screenshots
2. where the build differs from your spec
3. requests from the user, including two font changes
4. answers to your round 5 feasibility questions
5. what I need back

Screenshots are in `docs/mobile/screenshots/round4/` (also sent as a zip):
- `L…` are headless landscape shots at 2714×1222, 300%.
- `P…` are headless portrait shots at 1222×2714.
- `A…` are from the user's phone (Poco X7 Pro, three-button navigation).

---

## 1. Built in round 4

### Your review fixes (§1) and answers (§2)
- **Station picker and window:**
  - Picker labels use the normal font; the orientation buttons read "NE-SW" / "NW-SE".
  - The station window has an empty state, and its footer sits on the sheet's bottom edge.
- **"Show tools":** a third toolbar plane while a panel hides the tools in landscape (no tool, nothing pending).
- **Minimap:** the main-view rectangle is drawn whole, 1 GUI px thick. The desktop's version is four tiny corner marks, which disappear on a phone.
- **Handles:** drawn in a second pass over the footprint, each with a 6×6 blue knob with a white border (`L01`).
- **Paused state:**
  - The pill reads "Paused" in yellow, and its button shows ▶.
  - Returning to the app paused shows the yellow "Paused while you were away" banner; tapping it continues (`A01`).
  - Building while paused says "⚠ Paused · tap ▸ to build", with Build disabled (`L02`).
- **News:** the headline is static and truncated.
- **Touch dropdown** everywhere (`P01`):
  - Rows are 40 tall, with the selected row lowered and ticked.
  - Landscape: anchored to its button, at most 8 rows. Portrait: a full-width sheet along the bottom of the map, at most 60% of the screen.
  - Back closes it.
- **Top chrome against the cutout:** the phone reports the cutout's rectangles. The pill and map button sit 4 px below the top unless they overlap one, and 8 px from the rounded corners (`A01`).

### Part A: from depot to running train
- **Depot sheet** (`L04`): one row per train, "+ New train" and "Clone a train".
- **New train** (`L05`, `L06`):
  - Engines and Wagons tabs.
  - "+ £x" buys a vehicle; the first engine becomes the train being built, and the view switches to Wagons.
  - Every later purchase is attached explicitly to that train.
  - The composition strip has "X Sell" per vehicle, and selling the engine asks first.
  - The summary line reads "3 vehicles · 2 tiles · 60 tonnes of coal · 64 km/h".
- **Train window** (`L07`, `L08`):
  - Status and Start/Stop; Start is grey until there are 2 stops.
  - Orders and Details tabs; Show on map · Send to depot · Clone.
  - Order rows show the station and a flags line. Tapping a row opens its ⋯ menu as a touch dropdown (non-stop, load, unload, skip, delete).
- **Adding stops** (`L07`):
  - A bar replaces the dock: "Tap a station to add it as stop n · Done".
  - Taps on station tiles **and signs** add stops; "⚠ Not a station of yours" and "⚠ Already the previous stop" as you specified.
  - Up to 3 chips suggest stations.
- **Trains list** (`L09`): sort dropdown, rows with a mini strip, name and state, and profit (red if negative); the total profit at the bottom.

### Part B: the menus
- **Main menu** (`L10`, `P02`, `A04`):
  - Landscape: a panel on the left, 2 columns. Portrait: 1 column.
  - **Continue** (green) loads your last game paused: the save made when you left the app, or else the newest save.
- **New game** (`L11`, `P03`, `A06`):
  - The three presets (**confirmed by the user as you proposed**), then climate, map size, towns, industries, terrain and start year.
  - "Advanced options" opens the desktop window.
  - In portrait each label sits above its buttons, since "Temperate · Arctic · Tropic · Toyland" doesn't fit beside a label.
- **In-game Menu** (`A02`): as your §7.
- **Confirmation** (`L12`, `A03`):
  - The question is the coloured title, then the consequence, then Cancel and the verb on the right.
  - Landscape: centred in the map. Portrait: along its bottom.
  - Back or a tap outside cancels.
- **Save and load** (`L13`–`L15`):
  - Rows show "name / company · date · money / Saved 5 minutes ago".
  - Your last game is pinned first and tagged "LEFT THE APP"; there's an "Autosaves (n)" row.
  - Load rows expand to Load and Delete. Save has a name field (company name) and asks before overwriting.
- **Settings** (`L16`, `A05`):
  - Sections: Phone · Building and vehicles · Economy, towns, competitors, with rows of 56 that hold On/Off, a dropdown or − value +.
  - Display/sound/language/saving, NewGRFs, scripts and All settings open the desktop windows for now.
  - **Search** covers every setting, and each hit shows its page above it.

## 2. Differences from your spec

1. **Settings sections "Display and sound", "Language and units" and "Saving"** are one row that opens the desktop Game Options window. Those settings live in that window, not in the settings tree, so a mobile layout of Game Options is needed. Can you design it (tabs General · Graphics · Sound · Social · Advanced)?
2. **Long-press drag to reorder orders:** not built yet.
3. **Minimap zoom:** the user wants to zoom in further than the minimap allows. Its most detailed level is one tile per 4×2 screen pixels, so going further means drawing each tile larger. That's on our side; no design needed.
4. **Portrait dropdown sheet:** no title yet; the dropdown doesn't know its button's label.
5. **Depot entrance arrow** stays the white one-way arrow, as you accepted.

## 3. Requests from the user

1. **The main menu title font:** "OpenTTD" in the menu panel is drawn in the large font (`L10`). The user wants it to match OpenTTD's real title, the "OPENTTD" logo with large-and-small caps seen behind the menu. Please specify the logo sprite in place of the text, and its size in the panel.
2. **The Continue button's font** "looks kinda weird" (`L10`, `A04`): "Continue" is in the large font over two lines in the normal and small fonts. Please choose fonts and sizes for this button that fit the rest.
3. **Also design the menus you didn't cover yet**, as part of round 5:
   - the mobile Game Options (point 2.1);
   - the online content (download) window;
   - the help window.

## 4. Answers to your round 5 feasibility questions

1. **Save list details without loading:** yes, already built. The save's header check (`SaveLoadOperation::Check`) reads the companies (name, money), the current date, map size, climate and the NewGRF list without loading the game, at a few milliseconds per save.
2. **Settings search across the whole tree:** yes, already built. Every setting's help text is a plain string. Its section is the page of the desktop tree it's on.
3. **Finances in a phone layout:** easy as "this year / last year" columns, or one year per page with a year selector. The data is three years × 13 categories. Your call.

## 5. What I need back

- Your choice for round 5. Candidates: road vehicles (buses, trucks: the round 2–4 patterns again); or the remaining info windows (finances, company, industries, subsidies, graphs); and in either case the menus in §3.
- The two font answers (§3.1, §3.2).
- Anything you'd change in the round 4 screenshots.

---

## 6. Everything still desktop: please take as much as you can in round 5

The user would like a **large round 5**, enough for me to build through the night. Below is everything still on desktop UI underneath the mobile layout, grouped. Please cover as many groups as you can, ranked so I build in your order.

**My suggestion:** groups A1–A3 (they reuse the rail patterns, so they're quick to specify), C1, C3 and D1.

### A. Construction (the rail patterns of rounds 2–3)
1. **Signals** (deferred from round 3): signal type picker; dragging signals along a track, with the spacing; removing signals; converting signals.
2. **Road and tram:** toolbar, road/tram type choice, bus and truck stops (drive-through vs bay, orientation), road depots, one-way roads.
3. **Landscape:** raise and lower land, level an area, plant trees (types), buy land, place and edit signs, transmitters and lighthouses (scenario only).
4. **Water:** docks, canals, locks, ship depots, rivers (editor only).
5. **Air:** airport picker (types, sizes, noise, catchment), hangars.

### B. Vehicles beyond the first train
1. **Road vehicles, ships, aircraft:** the depot → buy → orders → start flow of round 4 for each type, including the buy lists and depot sheets.
2. **Refit:** changing what a wagon or vehicle carries, from the New train view and the train window.
3. **Autoreplace and renewal.**
4. **Vehicle groups** (the Vehicles hub with groups).
5. **Timetables and service intervals.**
6. **The full vehicle details** (reliability, breakdowns, capacity per wagon).

### C. Info windows
1. **Finances** (your idea: this year / last year columns) and the loan.
2. **Company:** company view, colour scheme, president face, headquarters; league table and performance rating.
3. **Industries:** the directory (sort, filter by cargo) and the industry window (production, acceptance, chain), and funding new industries.
4. **Graphs:** operating profit, income, cargo delivered, performance history, payment rates, town cargo graph.
5. **Subsidies, story book, goals.**
6. **Town local authority:** ratings and the actions (advertise, fund buildings, build statue, …).
7. **News:** the full newspaper, the message history.
8. **Land info:** long press opens the desktop window today.

### D. Menus and system
1. **Game Options:** language, interface size, font size, autosave interval, currency and units, base graphics, sound and music volumes. Settings falls back to this desktop window today.
2. **Online content download**, NewGRF settings, AI and game script settings.
3. **Help, About, highscores.**
4. **Multiplayer:** server list, join, company choice, chat.
5. **Text input:** the rename dialog with the soft keyboard; a number keypad for numeric values (the design's "tap the value → keypad").
6. **Tablets:** larger screens (e.g. 1280×800 at 200%): does the layout change, or does it just scale?
7. **First run:** a short tutorial or tips for new players.

### E. Small leftovers from earlier rounds
1. The portrait dropdown sheet's title.
2. Long-press drag to reorder orders (specified in round 4; I'll build it unless you change it).
3. The "Building…" state for multiplayer (round 3).
