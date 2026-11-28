# Handoff: OpenTTD mobile UI, round 5 (the big one) → implementation

From: the UX/layout design session
To: the C++ implementation session
Date: 27 Nov 2026

This replies to `09-impl-to-design-round5.md`. It covers **every group in your §6**, ranked so you can build top to bottom through the night. Most groups reuse patterns from rounds 2–4, so they're specified by difference.

Canvas boards exist only for the new patterns. They're under the "Round 5" title:
- Signals
- Finances
- Industry window
- Settings › Display and sound
- the main menu board, updated for the font answers

Sizes are GUI px at 1×. Floor 775×349, default 904×407.

---

## 0. Build order

Stop wherever the night ends; each batch stands alone.

| # | Batch | Your groups | Section |
|---|---|---|---|
| 1 | Round 4 fixes, the two font answers, the global text rule, leftovers | E1–E3 | §1–§3 |
| 2 | Settings with Game Options folded in | D1 | §4 |
| 3 | Signals | A1 | §5 |
| 4 | Road and tram | A2 | §6 |
| 5 | Road vehicles, ships, aircraft; vehicle cell menu; refit | B1, B2 | §7 |
| 6 | Finances and the loan | C1 | §8 |
| 7 | Industries: directory, window, funding | C3 | §9 |
| 8 | Town local authority, subsidies, story book, goals, news, message history, land info | C5–C8 | §10 |
| 9 | Landscape, water, air | A3–A5 | §11 |
| 10 | Vehicle details, service interval, timetable (basic), autoreplace, groups | B3–B6 | §12 |
| 11 | Company, colours, league, graphs | C2, C4 | §13 |
| 12 | Online content, help/about/high scores, text input and number keypad | D2, D3, D5 | §14 |
| 13 | Tablets, first-run tips, multiplayer | D6, D7, D4 | §15 |

Ranked [custom] list: §16.

---

## 1. Global rule: text size

**All readable text in the mobile layout uses the normal font.** That covers names, values, help text, flags lines, summaries, status lines and empty states.

The small font is allowed only for things nobody needs to read in play: the version string, and the dark section label above a search hit.

Consequences:
- Two-line rows become **48** tall (they were 40 with a small second line).
- Order rows, settings help text, the New train summary and the trains-list state lines all switch to the normal font.

## 2. The font answers (your §3.1, §3.2)

### 2.1 Main menu title

- Draw the **title logo sprites**, the same "OPENTTD" logo drawn behind the menu, in place of the "OpenTTD" text.
- **Size:** the logo's native size at the current sprite zoom, left-aligned in the header with 10 padding. If it's wider than the panel's inner width, draw it one zoom level smaller.
- **No version text in the header.** It stays at the screen bottom, where it already is.

### 2.2 Continue button

```
PUSHBTN Continue   GREEN   fill, min h 48
  HORIZONTAL (padding 8, spacing 8)
    [play sprite ▶, 16×16, centred vertically]
    VERTICAL (spacing 3)
      line 1, normal font, white:        "Continue · <company name>"
      line 2, normal font, pale green:   "<in-game date> · <money> · <time ago>"
```

- Use one font (normal), with colour carrying the hierarchy.
- **Never show the file name.** `A04` shows "background", the internal name of the background save. Use the company name from the header. If there is none, line 1 is just "Continue".

The main menu board shows both.

## 3. Fixes from the round 4 screenshots, and leftovers

1. **Main menu (`L10`, `A04`):**
   - The startup warning ("base graphics set is missing 143 sprites") goes to the **top banner**, not inside the panel.
   - In landscape the panel's **height fits its content**; today the bottom half is empty.
   - **Quit** shows a "?" icon. Use no icon, or an exit sprite if the base set has one.
2. **New train strip (`L05`):** the "X Sell" buttons are far below 40 tall (my spec said 40×18, which was wrong).
   - Each vehicle in the strip becomes a **cell of at least 40×40**, sprite centred. Tap → touch dropdown: **Sell (+£x)** · **Refit…** (§7.3) · **Details**.
   - Remove the separate Sell buttons.
   - Selling the engine still asks first.
   - The summary line is in the normal font.
   - The footer has a gap below it; put it on the sheet's bottom edge.
3. **Suggested stops (`L07`):** they're cut off ("+ Bondinghead …"), and an empty third button shows.
   - Show suggestions as **full-width rows**, 32 tall, normal font: "+ Bondinghead South".
   - At most 3, none when there are none.
   - Only in adding-stops mode.
4. **Order rows (`L08`):** the flags line in the normal font, rows 48.
5. **Settings (`L16`):**
   - Help text in the normal font: at most 2 lines with "…"; tap the row to show all.
   - Values ("12 tiles") in the normal font.
   - The section label above a search hit: small font, but **black, not grey**.
   - The search box 32 tall (it's about 10 now).
6. **Portrait dropdown title (E1):** use the label of the widget that opened the dropdown: its string, or its tooltip string if it has no text. With neither, no title.
7. **Long-press reorder of orders (E2):** build it as specified in round 4.
8. **"Building…" (E3):** with multiplayer (§15.3).

---

## 4. Batch 2: Settings, with Game Options folded in (D1)

**No mobile Game Options window, no tabs.** Its controls become rows in the mobile Settings window's sections. Rows bind to the same handlers the Game Options window uses, so one Settings window covers everything (board "Settings › Display and sound").

| Section | Rows (control) | Source |
|---|---|---|
| **Phone** | as built | settings |
| **Display and sound** | Interface size (dropdown, 250–400% in 25% steps on phones; full range on tablets) · Text size (dropdown, only if the build has a separate font zoom; otherwise omit) · Base graphics (dropdown; the help line shows missing-sprite status + a "Check for updates" link → Online content filtered to base graphics) · Base sounds (dropdown) · Base music (dropdown) · Music volume (− value + in 10% steps) · Sound effects volume (− value +) | Game Options: Graphics, Sound |
| **Language and units** | Language (dropdown) · Currency (dropdown) · Units: speed, power, weight, volume, force, height (dropdowns) | Game Options: General + the `locale.*` settings |
| **Saving** | Autosave interval (dropdown: Off · 10 · 30 · 60 · 120 min) · Autosaves to keep (− value +) | Game Options: General + settings |
| **Building and vehicles** | as built | settings |
| **Economy, towns, competitors** | as built | settings |
| **Add-ons** | Graphics add-ons (NewGRF) › · AI and game scripts › · Online content › | windows |
| **All settings** | as built | settings |

- **Hidden on phones:** fullscreen, resolution, refresh rate, video driver, bevels, the "Social" tab (Discord integration).
- **Town name style** is a new-game choice; it moves to New game › Advanced.
- **Sliders** become **− value +** steppers (10% steps; hold to repeat).

## 5. Batch 3: Signals (A1)

Board "Signals". The picker sits in the sheet slot, and the toolbar is in its picker plane (status · ✕ · Build), as with stations.

```
WINDOW build_signal (mobile)   GREY   sheet slot
VERTICAL (padding 0,4,4, spacing 4)
  HORIZONTAL (h 40): PANEL "Signals" fill · IMGBTN close 40×40 (= drop the tool)
  HORIZONTAL radio: TEXTBTN "Electric" fill h 32 · TEXTBTN "Semaphore" fill h 32
  LABEL "Type"
  MATRIX 3 columns, IMGTEXTBTN fill h 40 (signal sprite + short name):
     Path · One-way path · Block · Entry · Exit · Combo
     (follow the "show signal types" setting: path-only = the first two)
  LABEL "Spacing when dragging"
  HORIZONTAL: TEXTBTN − 48×36 · LABEL "every N tiles" fill · TEXTBTN + 48×36     (the desktop drag density, 1–20)
  TEXTBTN toggle "Change existing signals" ··· On/Off   fill h 32   (desktop's convert button)
```

| Input | Result (always PENDING first) | Status · Build label |
|---|---|---|
| Tap a track tile without a signal | One signal; side and direction from the tap position, as desktop | "1 signal · £x" · **Build** |
| Tap an existing signal, Change **off** | Cycle its direction (both → one way → other way), as desktop's click; each further tap on the pending signal cycles again | "Turn signal · £x" · **Change** |
| Tap an existing signal, Change **on** | Convert it to the selected style and type | "Convert 1 signal · £x" · **Convert** |
| Drag along a track | One signal every N tiles, end to end (autosignal); the spacing stepper updates the pending row live; knobs move the ends | "5 signals, every 2 tiles · £x" · **Build** |
| Drag with Change on | Convert every signal on the dragged stretch | "Convert 5 signals · £x" · **Convert** |
| Remove mode on | Tap or drag removes signals, red | "Remove 5 signals · +£x" · **Remove** (always asks) |

- **Ghost signals** at the pending positions [custom]. Fallback: highlight each signal's tile, plus the count in the status line.
- Desktop's Ctrl variants (fill to the next junction) are not in v1.

## 6. Batch 4: Road and tram (A2)

### 6.1 Toolbars

The same layout and behaviour as the rail toolbar (rounds 2–3). Landscape bar and portrait two rows, with status · ✕ · Build at the end.

| Toolbar | Main tools | More ▸ |
|---|---|---|
| **Road** | Category/road type · Autoroad · Remove · Demolish · Bus stop · Lorry stop · Depot · Bridge · Tunnel | One-way roads · Convert road type · Road along X · Road along Y |
| **Tram** | Category/tram type · Autotram · Remove · Demolish · Passenger tram stop · Freight tram stop · Depot · Bridge · Tunnel | Convert tram type · Track along X · Track along Y |

- **Portrait row B** holds 8 buttons. Bridge and Tunnel move into More, as for rail.
- **Category button:** the Build flyout with **road types** (or tram types), like the rail types.
- **Autoroad:** drag → pending → Build. Half-tile ends come from the tap position, as desktop.
- **One-way roads:** tap or drag over existing road → pending "Change one-way · £0" → **Change**. Each further tap on the pending stretch cycles the direction (both → A → B).
- **Road depot:** exactly like the rail depot. It faces the only adjacent road; Turn appears while pending.

### 6.2 Road stop picker

```
WINDOW build_road_stop (mobile)   GREY   sheet slot
VERTICAL (padding 0,4,4, spacing 4)
  HORIZONTAL (h 40): PANEL "Bus stop" | "Lorry stop" | "Tram stop" fill · IMGBTN close
  HORIZONTAL radio: IMGTEXTBTN "Drive-through" fill h 48 · IMGTEXTBTN "Bay" fill h 48
  SELECTION facing:
    Bay: LABEL "Facing" + HORIZONTAL 4 × IMGBTN (stop sprite per direction) fill h 48 (radio)
    Drive-through: LABEL "Follows the road"
  TEXTBTN toggle "Show catchment area"
  SELECTION join dropdown (as stations) · SELECTION type dropdown (NewGRF road stops only)
  PANEL Accepts / Supplies
```

- **Tap** → a pending stop on that tile.
  - Drive-through takes the road's axis.
  - A bay **faces the only adjacent road** when there is one (the depot rule); otherwise the picker's facing.
- **Drag** → an area of stops, as desktop allows.
- Join, errors, Undo and the toolbar behave as for stations.

## 7. Batch 5: Other vehicles, refit (B1, B2)

### 7.1 Road vehicles, ships, aircraft

The round 4 flow, with these differences:

| | Road vehicles | Ships | Aircraft |
|---|---|---|---|
| Depot | road depot | ship depot | hangar (part of an airport) |
| Buy view title | "New road vehicle" | "New ship" | "New aircraft" |
| Tabs | none (one list) | none | none |
| After "+ £x" | the **vehicle window opens** on Orders with adding-stops on | same | same |
| Stops you tap | bus stops / lorry stops (matching the vehicle) | docks; buoys too | airports (and heliports for helicopters) |

- **No composition strip** in the buy view: one tap buys a complete vehicle (articulated parts included).
- **Buying several:** buy again from the depot, or Clone from the vehicle window.
- **Vehicles hub tabs** Road / Ships / Aircraft use the trains-list layout (`L09`).

### 7.2 Vehicle cell menu (from §3.2)

Tap any vehicle cell in a strip (New train, Details tab) → touch dropdown:
- **Sell (+£x)**
- **Refit…**
- **Details**: scrolls the Details tab to that vehicle

### 7.3 Refit

```
WINDOW vehicle_refit (mobile)   GREY   sheet slot, over the vehicle window (‹ returns)
VERTICAL
  HORIZONTAL (h 40): IMGBTN ‹ · PANEL "Refit <vehicle or wagon name>" fill · IMGBTN close
  [train only] HORIZONTAL radio h 32: TEXTBTN "Whole train" · TEXTBTN "Only this wagon"   (preset by where it was opened)
  PANEL list fill resize(0,40): [cargo colour] cargo name (+ subtype) ··· capacity "30 → 40 bags"
  HORIZONTAL (h 48): LABEL "Cost £x" fill · TEXTBTN "Refit" GREEN (disabled until a different cargo is picked)
```

- **Not refittable here** (wrong place, not stopped): the list is disabled and the cost line says why, e.g. "Stop in a depot to refit".

## 8. Batch 6: Finances and the loan (C1)

Board "Finances". I chose **this year so far vs last year**, with any of the 3 years selectable for the second column.

```
WINDOW company_finances (mobile)   GREY   sheet slot (Company hub › Finances; the status pill's money opens it)
VERTICAL
  HORIZONTAL (h 24): SPACER fill · LABEL "<year> so far" w 75 right · DROPDOWN compare-year w 75 right (default: last year)
  PANEL table fill(1,1), drag-scroll:
     group row h 32, tap toggles:  ▸/▾ Income · Running costs · Construction · New vehicles · Other    value · value
     detail rows h 24, indented, only non-zero: the desktop's categories under each group
         Running costs = vehicle running costs + property maintenance + loan interest
     Profit row h 32, bold, shaded
     negative values: red with "−"
  PANEL loan, 2 lines: "Loan £x of £max · interest y%" / "Bank balance £x"
  HORIZONTAL (h 40, spacing 4): TEXTBTN "Borrow ▾" fill · TEXTBTN "Repay ▾" fill
```

- **Borrow ▾** opens a touch dropdown: £10,000 · £50,000 · Maximum (£x), using the game's loan step and currency.
- **Repay ▾** opens: £10,000 · £50,000 · Everything (£x).
- Rows that aren't possible are disabled with the reason as their text, e.g. "Maximum reached".
- Borrowing and repaying are reversible, so no confirmation.

## 9. Batch 7: Industries (C3)

### 9.1 Directory (World › Industries)

```
WINDOW industry_directory (mobile)   BROWN
VERTICAL
  HORIZONTAL (h 40, padding 4, spacing 4): DROPDOWN "Produces: any" fill · DROPDOWN "Accepts: any" fill · DROPDOWN sort 80 (Name · Type · Production · Transported)
  PANEL list resize(0,48): [type colour] name / "Coal 144 t · 0% transported" (first two cargos, then "…") ··· IMGBTN (i) 40×40
  HORIZONTAL (h 40): TEXTBTN "+ Fund new industry" fill
```

Row tap → the map scrolls there; (i) → the industry window (the towns pattern).

### 9.2 Industry window

Board "Industry window".

```
WINDOW industry_view (mobile)   BROWN   sheet slot
VERTICAL
  HORIZONTAL (h 40): [IMGBTN ‹ when opened over the directory] · PANEL name fill · IMGBTN close
  PANEL info fill:
     "Produces": rows h 40: [cargo colour] cargo / "<n> last month" ··· "<p>% transported" (red at 0%)
     "Accepts":  rows h 32: cargo (+ "waiting" amount where the desktop shows it)  ·  "Nothing" if none
     hint, only when nothing is transported: "Nobody serves this <industry> yet. Build a station within reach…"
  HORIZONTAL (h 40): Show on map · Cargo chain · Production graph
```

- **Opens from:** a tap on the industry on the map, or (i) in the directory.
- **Cargo chain** and **Production graph** open the desktop windows for now.

### 9.3 Fund new industry

```
WINDOW build_industry (mobile)   BROWN   sheet slot
VERTICAL
  HORIZONTAL (h 40): PANEL "Fund new industry" fill · IMGBTN close
  PANEL list resize(0,48): [colour] type / "Produces coal · £x" ; tapping a row expands it:
        desktop description text (produces / accepts)
        HORIZONTAL: TEXTBTN "Place it · £x" fill GREEN · TEXTBTN "Prospect · £x" fill    (whichever the game allows)
```

- **Place it** → placement mode with the toolbar's picker plane.
  - Tap → pending industry footprint → "Coal Mine · £x" → **Fund**.
  - Errors in the status line.
- **Prospect** → confirmation: "Prospect for a coal mine for £x? It may find nothing." with **Prospect** (verb). Then send.

## 10. Batch 8: Towns, subsidies, story, goals, news, land info (C5–C8)

### 10.1 Local authority

Opens from the Town window's button.

```
WINDOW town_authority (mobile)   BROWN   sheet slot, over the Town window (‹)
VERTICAL
  HORIZONTAL (h 40): IMGBTN ‹ · PANEL "Local authority · <town>" fill · IMGBTN close
  PANEL ratings: rows h 32: [company colour] company ··· rating word ("Good")
  LABEL "Actions"
  PANEL list resize(0,48): action / cost; tap expands: description + TEXTBTN "<action> · £x" GREEN
        unavailable actions: disabled, with the reason
```

**Bribe** always asks (confirmation, red verb "Bribe").

### 10.2 Subsidies (World tab)

- **Two sections:** "Offered" and "Awarded".
- **Rows 48:** "Coal from <source> to <destination>" / "Offer ends Jun 1954" (or the awarded company and its bonus).
- **Two buttons per row**, "From" and "To", 40×40 each, scroll the map to each end. That's desktop's click and Ctrl-click.

### 10.3 Story book (World tab)

- **Title row:** ‹ previous page · page dropdown (titles) fill · › next page.
- **Content:** the desktop page renderer, filling the area, with drag-scroll.
- Story buttons are at least 40 tall.

### 10.4 Goals (World tab)

- **Rows 48:** goal text / progress.
- **Tap** → scrolls to its location, if it has one.
- **Sections:** global goals, then company goals.

### 10.5 News

- **Newspaper:** opens from the banner or the history, as a sheet (half sheet in portrait).
  - **Title row:** date · close.
  - **Content:** headline, the picture if any, the text.
  - **Show on map** if the item has a location.
- **Message history:** Menu › Messages, as a sheet.
  - **Rows 48:** date / headline, newest first.
  - **Tap** → the newspaper sheet; ‹ returns to the history.

### 10.6 Land info (long-press on the map)

- A sheet "Land info · <tile type>", with label ··· value rows 32 (owner, type, cost to clear, road owner, build date, NewGRF name…).
- Landscape: the side panel. Portrait: a half sheet.
- **Long-press another tile** while it's open → the sheet updates in place.

## 11. Batch 9: Landscape, water, air (A3–A5)

### 11.1 Landscape toolbar

| Main tools | More ▸ |
|---|---|
| Raise · Lower · Level · Demolish · Trees · Buy land · Sign | Transmitter · Lighthouse (scenario editor only) |

- **Raise / Lower:** tap → that whole tile, pending; drag → an area, pending. Status "Raise 12 tiles · £x" → Build.
- **Level:** drag an area → it is levelled to the height of the tile where the drag started. Status "Level 12 tiles · £x".
- **Trees:** a picker in the slot.
  - Contents: a grid of tree-type sprites (48×48) plus "Random type".
  - Tap → one tree; drag → an area (count in the status).
- **Buy land:** tap or drag → "Buy 4 tiles · £x" → Build.
- **Sign:** tap a tile → the text dialog (§14.3) → placed.
  - Tapping an existing sign (in any mode) opens the dialog with its text and **Delete**.

### 11.2 Water toolbar

| Main tools | More ▸ |
|---|---|
| Canal · Lock · Dock · Ship depot · Buoy · Aqueduct · Demolish | River (scenario editor only) |

- **Canal:** drag, like track.
- **Lock:** tap a slope.
- **Dock:** tap a coastal slope. Pending with catchment, Accepts/Supplies, and join, like a one-tile station.
- **Ship depot:** pending 2 tiles, with Turn.
- **Buoy:** tap.
- **Aqueduct:** drag, like a bridge, but with no list.

### 11.3 Airports

```
WINDOW build_airport (mobile)   GREY   sheet slot
VERTICAL (padding 0,4,4, spacing 4)
  HORIZONTAL (h 40): PANEL "Airport" fill · IMGBTN close
  DROPDOWN class fill h 32: Small · Large · Hubs · Helicopter
  PANEL list resize(0,48): [airport sprite] name / "<w>×<h> · noise <n>" ; unavailable greyed
  [if the airport has several layouts] HORIZONTAL: TEXTBTN ‹ · LABEL "Layout 1 of 2" fill · TEXTBTN ›
  TEXTBTN toggle "Show catchment area"
  LABEL noise line: "Noise 3 · <town> allows 4 more"
  PANEL Accepts / Supplies
```

- **Placement:** tap → footprint centred on the tap, pending → Build. That's the station pattern, including join.
- **Hangars** are part of the airport.

## 12. Batch 10: Vehicle details, timetable, autoreplace, groups (B3–B6)

- **Details tab** (completes round 4 §4.4):
  - The strip, with cells as in §3.2.
  - "+ Add vehicles".
  - A **Service interval** row: "Service every 150 days", − value + (hold to repeat; tap the value → keypad, §14.4). Its unit follows the company setting (days or %).
  - Then the desktop details text, in the normal font.
  - Tapping a vehicle cell → Details scrolls to that vehicle's block: reliability, breakdowns, max speed, capacity, age.
- **Timetable tab** (third tab, basic):
  - Rows 48 per order: "Travel 12 days · wait 3 days".
  - **Autofill** toggle (desktop autofill).
  - Tap a row → the keypad to set its wait time.
  - The rest (start dates, lateness tools) stays desktop for now: a "More timetable options ›" row opens the desktop window.
- **Autoreplace:** a "Replace vehicles ›" row at the bottom of each Vehicles hub list opens a sheet:
  - Rows 48: "<old model>" → a dropdown of new models (same type and rail/road type).
  - Toggles: "Renew old vehicles" (the company autorenew setting) and, for trains, "Remove wagons to keep the length".
- **Groups:**
  - The Vehicles hub list gets a group dropdown at its top: All · Ungrouped · <groups…> · "+ New group".
  - The vehicle window's ⋯ (title row) gets "Add to group…" (touch dropdown).
  - **Rename and delete a group:** the dropdown row's long-press → tooltip only, so use a "Manage groups ›" row in the dropdown instead. It opens a list with rename and delete (delete asks).

## 13. Batch 11: Company, league, graphs (C2, C4)

### 13.1 Companies tab (Company hub)

- **Rows 48:** [colour] company / "Value £x".
- **Tap** → the company window.

```
WINDOW company (mobile)   company colour   sheet slot
VERTICAL
  HORIZONTAL (h 40): PANEL company name fill · [own: IMGBTN rename] · IMGBTN close
  HORIZONTAL (h 64): [president face sprite] · VERTICAL( president name / "Founded 1950" / "Value £x" )
  PANEL info: vehicles per type · "Infrastructure ›" (desktop window for now) · share owners
  [own company] HORIZONTAL (h 40): TEXTBTN "Colours" · TEXTBTN "Face" · TEXTBTN "Headquarters"
```

- **Colours:** a sheet with "Main colour" and "Second colour" as touch dropdowns, with colour squares in each row.
- **Face:** desktop window for now.
- **Headquarters:** placement mode. Tap → pending 2×2 → "Build headquarters · £x" (or "Move headquarters"). Moving it asks.

### 13.2 League tab

- **Rows 48:** rank · [colour] company / performance title ("Engineer").
- A "Performance rating ›" row opens the desktop detailed-rating text in a sheet.

### 13.3 Graphs tab

```
VERTICAL
  DROPDOWN graph fill h 32: Operating profit · Income · Cargo delivered · Performance · Company value · Cargo payment rates
  PANEL graph fill(1,1)   (the desktop graph widget, drawn to the panel size)
  PANEL legend: company chips h 32 (tap toggles a company on/off), wraps to 2 rows max, then scrolls
```

- **Payment rates** shows cargo chips instead of companies.
- The town cargo graph (Town window › Cargo graph) uses the same layout without the dropdown.
- In portrait the graph is short. Fine for v1; rotating gives a bigger graph.

## 14. Batch 12: Online content, help, text input (D2, D3, D5)

### 14.1 Online content

```
WINDOW network_content_list (mobile)   GREY   full screen (safe area)
VERTICAL
  HORIZONTAL (h 48): IMGBTN ‹ · PANEL "Online content" w 150 · EDITBOX search fill h 32
  HORIZONTAL (h 40, padding 4, spacing 4): DROPDOWN type fill (All · Base graphics · Base sounds · Base music · NewGRF · AI · Game scripts · Scenarios · Heightmaps) · TEXTBTN toggle "Updates only"
  PANEL list resize(0,48): [state icon: installed ✓ · update available ↻ · selected ● · empty] name / "<type> · <version> · <size>"
        tap a row → expands: description, dependencies, TEXTBTN "Select" | "Unselect"
  HORIZONTAL (h 48): LABEL "3 selected · 12 MB" fill · TEXTBTN "Download" GREEN (disabled when nothing is selected)
```

- **Download progress:** the existing progress window in a mobile layout, centred: bar + "x of y MB" + **Cancel**.
- **Selecting an item** that needs dependencies selects them too, as desktop does. A banner says "Also selected: <n> required items".
- **"Check for updates"** in Settings › Base graphics opens this window with type = Base graphics and Updates only on.
- **NewGRF settings, AI and game script settings:** still desktop windows. They're a later round, and rarely used on a phone.

### 14.2 Help, About, high scores

- **Menu › Help and manuals** opens a sheet with rows 40: Readme › · Changelog › · Known bugs › · Licence › · High scores › · About ›.
- **Text files** use a **text viewer**: full screen, with ‹ and title, the text wrapped in the normal font and drag-scrolled. It's the desktop text file window with a mobile layout.
- **About:** a sheet with the logo sprite, the version, the copyright lines and "Licence ›".
- **High scores:** rows 48: rank · company / score · title.
- Website links (manual, wiki) aren't shown on phones in v1. Your call if opening the browser is easy.

### 14.3 Text input (rename, sign text, save name, new group…)

```
WINDOW query_string (mobile)   GREY   anchored directly above the soft keyboard, full width − 16 (portrait) / centred w 360 (landscape)
VERTICAL (padding 6, spacing 6)
  LABEL title, bold ("Rename train")
  EDITBOX fill h 40 (text preselected so typing replaces it)
  HORIZONTAL right-aligned: TEXTBTN "Cancel" min 90×40 · TEXTBTN <verb> min 100×40 GREEN ("Rename", "Place sign", "Save")
```

- **The keyboard opens at once.** Enter = the verb. Back = Cancel.
- If the keyboard covers the window, it moves up (you already track the keyboard area from round 3's work).

### 14.4 Number keypad

For numeric values: start year, settings values, service intervals, timetable times.

- **Preferred:** Android's numeric soft keyboard, if SDL lets you request a numeric input type.
- **Otherwise** [custom]:

```
WINDOW numpad (mobile)   GREY   bottom-anchored, full width (portrait) / centred w 320 (landscape)
VERTICAL (padding 6, spacing 4)
  LABEL title ("Start year") · LABEL value display, large, right-aligned · LABEL "1920 – 2050" (the range)
  MATRIX 3 × 4 of TEXTBTN 48×48: 1 2 3 / 4 5 6 / 7 8 9 / ⌫ 0 OK(GREEN)
```

- **Out of range:** OK is disabled and the range label turns red.
- Back = Cancel.

## 15. Batch 13: Tablets, first run, multiplayer (D6, D7, D4)

### 15.1 Tablets

- **Same layout, different numbers.** Choose the default GUI scale so the short side is about 400–450 GUI px. For example, 1280×800 → 200% → 640×400.
- **When the short side is at least 520 GUI px** (a big tablet at a low scale), portrait uses the landscape layout: side panel, rail on the left. The bottom sheet would be too wide.
- **The sheet slot** grows to 360 wide when the screen is at least 1100 GUI px wide.
- **Nothing else changes.** Tap targets stay at least 40.

### 15.2 First-run tips

- **On the first game only:** four tips in the news banner position, styled blue:
  1. "Drag to move the map, pinch to zoom."
  2. "Build › Rail lays track. Nothing is built until you tap Build."
  3. "Tap a depot to buy a train."
  4. "In a train, tap stations on the map to give it stops."
- **Each tip** has **Next** (or **Got it** on the last) and **Skip tips**.
- **Settings › Phone › "Show tips"** (On by default; turned off by Skip).
- **No overlays or arrows** in v1.

### 15.3 Multiplayer (last)

- **Server list:** full screen.
  - **Rows 48:** server name / "players · map size · ping"; locked servers show a lock.
  - **Search** field and **Refresh**.
  - **Add server** opens the text dialog.
  - **Join** asks for the password when needed.
- **Choosing a company:** a list sheet (rows 48: colour, company, players), plus "New company" and "Spectate".
- **Chat:** while online, a **chat button** (40×40) sits right of the status pill.
  - Tap → a bottom sheet: messages (normal font, newest at the bottom) + an EDITBOX + **Send**.
  - Unread count as a small badge on the button.
- **"Building…"** (round 3) is required here.

---

## 16. [custom] list, ranked

1. Global normal-font rule; row heights 48 for two-line rows (§1). Low effort, high impact.
2. Logo sprite + Continue layout (§2); main menu fixes; New train cell menu (§3). Low.
3. Settings rows bound to Game Options controls (§4). Medium.
4. Signal pending states: one, a row with spacing, turn, convert, remove; ghost drawing or a tile-highlight fallback (§5). Medium–high.
5. Road/tram toolbars and the road stop picker, reusing rail code paths (§6). Medium.
6. Buy-and-open-orders for single-unit vehicles; the refit sheet (§7). Medium.
7. Finances table with groups and the compare-year dropdown; Borrow/Repay dropdowns (§8). Medium.
8. Industry directory filters, industry window, fund with placement and prospect (§9). Medium.
9. Local authority, subsidies, story, goals, newspaper sheet, history, land info sheets (§10). Medium (many, small).
10. Landscape, water and airport tools on the pending pattern; trees and airport pickers (§11). Medium–high.
11. Details completion, timetable basic, autoreplace sheet, groups (§12). Medium.
12. Company window, colours, HQ placement, league, graphs in the slot (§13). Medium.
13. Online content (mobile layout + filters); text viewer; About; high scores (§14.1–14.2). Medium.
14. Text dialog above the keyboard; number keypad (or the Android numeric keyboard) (§14.3–14.4). Medium.
15. Tablet thresholds (§15.1). Low.
16. First-run tips (§15.2). Low.
17. Multiplayer screens + chat + "Building…" (§15.3). High; last.

## 17. What I need back

- Screenshots per batch that you finish, including the phone.
- Anything that fought you, so I can adjust the spec rather than you bending the code.
- **For round 6 I suggest a polish round:** review everything on the device together, then the skins (Classic / Glass / Flat / Slate), which the user wants after the revamp.
