# Handoff: Touch layout design for an OpenTTD mobile UI

You are the **UX/layout designer** for this project. A separate Claude Code session is helping the user implement the UI in C++. Your job is to design what the screens look like and how touch interaction works, and to produce a spec that can be built with the toolkit described below. You don't write C++.

## The project

- **OpenTTD** is an open-source transport tycoon game (C++, GPLv2). Players build rail, road, sea and air networks, carry cargo between towns and industries, and run a company.
- **Goal:** a touch-first phone UI (Android first, iOS later). The game engine, graphics and map rendering stay as they are. Only the windows, toolbars and input change.
- **Why that works:** every player action goes through a "command" (build rail, build station, buy vehicle…). The new UI sends the same commands as the desktop UI, so multiplayer with desktop players keeps working. **You can redesign any UI freely, but you can't add game mechanics.**
- **The user** is learning C++ GUI work as they go. Designs that need fewer new building blocks get built sooner.

## What you're designing with (constraints)

The new UI is built with OpenTTD's own widget toolkit, not a native mobile toolkit. Design within these rules unless a deviation is really worth it. If it is, flag it as **[custom]** so the implementer knows it needs new code.

**Layout model.** Every window is a tree of:
- `NWID_HORIZONTAL` (row) and `NWID_VERTICAL` (column) containers, with padding and spacing between children
- `NWID_SPACER` (empty space), plus a min size and "fill/stretch" per axis on every element
- `NWID_SELECTION` (show one of several children: good for tabs and modes)
- `NWID_MATRIX` / `WWT_MATRIX` (grid or list of rows), `NWID_VSCROLLBAR` / `NWID_HSCROLLBAR`
- `NWID_VIEWPORT` (an embedded live view of the map)

**Available widgets:** panel, inset panel, push/toggle button (text, image, or image+text), boolean toggle, label/text, dropdown, text edit box, frame, caption bar, close/sticky/resize boxes, and custom-drawn panels where code paints anything (sprites, text, rectangles).

**Look:**
- Pixel-art, bevelled panels in a small set of company/UI colours (grey, brown, green, blue, etc.). There are no rounded corners, shadows, blur, gradients or opacity unless marked [custom].
- Icons come from the game's existing sprite set (all the current toolbar icons exist). New icons are possible but cost artwork.
- GUI scale runs from 100% to 500% in 25% steps. Assume a phone runs at about **200–300%**. Give sizes in **"1× GUI pixels"** (a desktop button is about 12–22 px tall at 1×), and say what scale you assumed.

**Windows:**
- Windows are rectangles that float over the map. Any of them can be placed full-width, docked to an edge, or sized to the screen. A "bottom sheet" is just a window docked at the bottom, so it's cheap.
- Animated slide-in/out transitions are [custom].

**Input today:** mouse only. There's left click, a click count (double-click), drag on the map for building, right-click/hover for tooltips, and scroll-wheel zoom.
- Pinch-zoom, two-finger pan, long-press and drag-with-confirm are **new** (a gesture layer will be added). Design them, but specify them exactly: gesture → result → feedback.
- Remember there's no hover on touch, so tooltips need a replacement.

## What exists today (desktop)

- **Main toolbar** (top edge, ~30 icon buttons, most open dropdown menus): Pause, Fast-forward, Settings, Save/Load, Minimap, Towns, Subsidies, Stations, Finances, Companies, Story, Goals, Graphs, League, Industries, Trains, Road vehicles, Ships, Aircraft, Zoom in, Zoom out, Build rail, Build road, Build tram, Build water, Build airport, Landscaping, Music/sound, Messages, Help.
- **Status bar** (bottom edge): date, news ticker, company money.
- **Rail construction toolbar** (opened from Build rail): rail in 4 directions, autorail (drag to lay track), demolish, depot, waypoint, station, signals, bridge, tunnel, remove-mode toggle, convert rail type. Building means picking a tool, then clicking or dragging on map tiles. Stations, depots and signals open extra picker windows (orientation, platform count/length, station class…).
- **Deep windows** you'll eventually have to tame: vehicle orders (`order_gui.cpp`, ~1800 lines), timetables, the vehicle purchase list (`build_vehicle_gui.cpp`, ~2000 lines, sortable/filterable list + details), vehicle details, finances, company settings.

If you want, the user can send you screenshots of any of these.

## Scope for this phase

Design **one flow end to end**: *build a rail line between two towns and get a train running on it*. That covers:
1. Navigating the map (pan/zoom, finding towns)
2. Opening rail construction and laying track (straight runs, curves, slopes; bridges/tunnels as a stretch)
3. Placing a station at each end (orientation, platform count/length)
4. Placing a depot
5. Buying a locomotive + wagons in the depot
6. Giving it orders (go to station A, go to station B) and starting it
7. Fixing mistakes: undo-by-demolish, cancel a drag, cost preview before confirming

Also design the **app shell** this flow sits in, i.e. what replaces the 30-button top toolbar and the status bar on a portrait **and** landscape phone (pick one as primary and justify it).

## Out of scope for now

Road/sea/air building, graphs, league tables, AI/game scripts, NewGRF settings, multiplayer lobby, main menu and new-game setup. Mention where they'd live in the shell, but don't design them.

## Deliverables

Produce these so the implementer can turn each screen into a widget tree with no guessing:

1. **Shell layout:** wireframes (ASCII or SVG/HTML mockups) for portrait and landscape, showing where navigation, money/date, and the map go.
2. **Navigation map:** every primary destination, and how many taps to reach each of the old toolbar functions (a table is fine).
3. **Per screen in the flow:**
   - A wireframe, and the same thing as a nested row/column outline using the layout model above, e.g.
     ```
     VERTICAL (padding 4)
       HORIZONTAL: [IMGBTN rail-NS 32×32] [IMGBTN autorail 32×32] SPACER(fill) [TEXTBTN "Done" 48×32]
       SELECTION (mode): …
     ```
   - Min tap-target size, which elements stretch, and what happens on rotation.
   - Each interactive element: tap result, and which game action it triggers (e.g. "places station" / "buys vehicle").
4. **Gesture spec:** a table of gesture × context (map idle, build tool active, mid-drag, inside a list) → result → visual feedback. Resolve the conflicts explicitly: one-finger drag pans *or* builds? Long-press means inspect *or* tooltip?
5. **Build-tool interaction:** step-by-step state machine for placing track by drag: start, preview with cost, adjust, confirm/cancel. The desktop version builds on mouse release with no confirm step.
6. **Open questions and [custom] list:** everything that needs new engine/UI code beyond the toolkit, ranked by how much the design depends on it.

Prefer a sequence of small rounds: shell first, then track laying, then station/depot, then vehicle purchase and orders. Check in with the user after each round.

## Principles

- Thumb reach: put frequent actions in the bottom third. The map is the main content; keep chrome thin and dismissible.
- Contextual over global: show tools for the current mode instead of every tool at once.
- Every destructive or expensive action previews its cost and can be cancelled.
- Desktop players and features must keep working. This is a new layout set, not a removal of the old one.
