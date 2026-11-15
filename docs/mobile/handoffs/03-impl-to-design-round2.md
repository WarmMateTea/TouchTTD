# Handoff: OpenTTD mobile UI, round 1 built → round 2 design (track laying)

From: the C++ implementation session
To: the UX/layout design session
Date: 14 Nov 2026

This replies to your round 1 handoff. It covers what was built and how it differs from the spec, the screenshots you asked for (§9a), the answers to your feasibility questions (§9b), and new facts from the code that affect round 2. It stands on its own.

**Attached screenshots** (the user will upload them with this document):
- `round1/`: the built shell, taken from the real game at the reference phone size (2714×1222, 300%), shown at half size. Names start with `L` (landscape), `P` (portrait) or `D` (desktop).
- `round2-reference/`: your §9a list, desktop layout at 200%, full size: 8 for round 2 and 7 for rounds 3–4.

---

## 1. Round 1 status

The shell is built and running. It's a new client setting, **Settings › Interface › "Touch-friendly mobile layout"**, that can be switched at runtime. When it's off, desktop is pixel-for-pixel unchanged (`D01`, `D02`).

| Spec item | Status | Screenshot |
|---|---|---|
| §4.1 Status pill: pause, FF, date and money; tap money → Company › Finances | ✅ built | `L01`, `P01` |
| §4.2 Hub dock, landscape rail and portrait bar, two WindowDescs swapped by the layout pass | ✅ built, including **icon above label** (custom drawing, not the fallback) | `L01`, `P01` |
| §4.2 Build: reopen last toolbar / flyout first time / toggle closes toolbar / icon shows category | ✅ built | `L09`–`L11` |
| §4.2 New setting "Build button opens…" | ✅ built as a checkbox: "Mobile layout Build button always opens the category picker" | – |
| §4.3 Build flyout 3×2, disabled categories greyed | ✅ built (Tram is greyed on this map: no tram types) | `L09`, `P05` |
| §4.4 Menu flyout | ✅ **variant B in landscape**, single column (variant A without scrolling) in portrait. Variant A in landscape is not built: it needs drag-to-scroll (custom #4) | `L12`, `P04` |
| §4.5 Hub sheets: tab window + content in the sheet slot; side panel / half / full; ▲▼; ✕; last tab remembered | ✅ built | `L02`, `L06`, `L07`, `P02`, `P03` |
| §4.6 Mobile Town directory: filter, merged sort dropdown, 40 px rows, name / population / (i) | ✅ built | `L02`, `P02` |
| §4.6 Row tap scrolls the map and keeps the row highlighted; (i) opens the Town window on top of the list | ✅ built | `L03`, `L04` |
| §4.7 News banner; tap opens the item; ✕ hides it; auto-hides on the ticker timer | ✅ built | `L09`–`L12` (yellow bar) |
| §4.7 Minimap button opens the map in the sheet slot and moves left of the side panel | ✅ built | `L08` |
| §4.8 Layout pass on resize/rotation | ✅ built. Both orientations verified, and switching on at runtime. A *live* rotation was not tested (the virtual display can't rotate) | – |
| Android back | ✅ mapped to Escape: closes flyout → overlay → sheet → construction toolbar | `L05` |
| §6 Gesture layer (pinch, two-finger, double-tap, long-press) | ❌ not built. See §4: it needs a touch device to verify, and it comes with the Android build | – |
| Custom items #4 drag-scroll, #5 soft keyboard, #6 40 px dropdown rows, #7 hit margin, #8 visible map rect, #9 long-press tooltip, #11 insets, #13–14 | ❌ not built | – |

## 2. Where the build differs from the spec

These are deliberate. Each says why, and whether the design needs to decide something.

1. **No bold font.** OpenTTD has normal, small, large and mono fonts, but no bold. The money line in the status pill is **yellow** (red when negative) instead of bold. Town names in the list are orange (towns) or yellow (cities), as on desktop.
2. **The Menu button has no "menu" icon.** None exists in the base graphics, so it uses the settings gear. In the 40 px landscape rail it's icon-only, because "Menu" plus the icon doesn't fit; the label is in the tooltip. *Design: is a gear OK, or do you want a new ☰ sprite? A new sprite is artwork work.*
3. **The hub caption isn't a caption widget.** Every caption widget in OpenTTD starts a window drag when touched, so the hub title is a custom-drawn panel. It looks the same.
4. **The sheet doesn't always reach the bottom of the slot.** Existing windows can only grow in whole steps of their row height (40 px rows → 40 px steps). A list sheet can therefore end up to one row short of the slot, leaving a strip of map below it (`L02`). *Design: is that acceptable, or should the empty area be filled with the window colour? Filling it is easy.*
5. **"Slot" tabs open desktop windows that don't fit.** The vehicle list, finances, company, league, graphs and minimap have desktop minimum widths above the 300 px side panel, so they run off the right edge (`L06`–`L08`). They work, but each needs a mobile layout like the Town directory got. The town info window from (i) is the desktop Town window, docked (`L04`). *Design: please put these on the round list. The train list and vehicle windows are in round 4 anyway.*
6. **Graphs** opens the operating-profit graph directly. There's no graph picker yet.
7. **News:** in the mobile layout every news item, including "full newspaper" ones, goes to the banner. Tapping it opens the newspaper window, which is the desktop one, placed at the bottom of the screen; in portrait it covers the dock. *Design: how should the newspaper look on mobile? A sheet?*
8. **Construction toolbars** currently open desktop-sized at the top centre of the map area, below the status row (`L10`). That's round 2's subject.
9. **Scroll to town** centres on the screen, not on the uncovered map area (custom #8 not built).

## 3. Answers to §9b (feasibility)

**Q1. Cost preview while dragging: yes, and it's cheap.**
Shift-click "estimate" runs the command in *test mode* on the local machine. Nothing is sent to the server, so it's safe in multiplayer (`CommandHelperBase::InternalPostBefore` in `src/command.cpp`). A test run of a rail line costs about the same as checking each tile once, so running it on every finger move is fine even for long autorail lines. The result also says *why* a build would fail (slope, obstacle, money), so the live label can show an error as well as a price. One caveat: the estimate is only valid at that moment. A train or town building can still change the tiles before ✓.

**Q2. Keeping the preview after lift: yes, with new code in one place.**
Today, building happens the moment the button is released: `VpHandlePlaceSizingDrag()` in `src/viewport.cpp` resets the highlight to a single tile and calls the toolbar's `OnPlaceMouseUp()`, which sends the build command. For mobile, that function can stop at release and keep the selection (start tile, end tile, track shape) frozen on screen in a new "pending" state. ✓ then calls the same `OnPlaceMouseUp()`, and ✕ discards it. Adjusting the pending selection (dragging an end handle) is also possible, because the selection is just two tile positions plus a drawing style. This is the main new engine-side piece for round 2.

**Q3. Soft keyboard: probably already works, to be confirmed on the device.**
OpenTTD already calls SDL's "start text input" when an edit box gets focus and "stop" when it loses it (`src/video/sdl2_v.cpp`). On Android, SDL shows the system keyboard in response. What's missing is telling SDL *where* the edit box is, so it isn't covered by the keyboard (the "scroll so the box stays visible" part). That's small. Separately, OpenTTD has its own on-screen keyboard window, which should be disabled in the mobile layout.

**Q4. Tooltip on hold without clicking: not as-is. This is the most important finding for round 2.**
OpenTTD fires a click when the button goes **down**, not when it comes up (`MouseLoop` in `src/window.cpp`). So today a long-press would click the button first and show the tooltip second. The design's "tap fires on lift, sliding off cancels, long-press shows a tooltip instead" therefore needs the gesture layer to **delay the click until lift** for touch input. That's feasible: the gesture layer decides tap vs long-press vs drag, then feeds OpenTTD a press+release for a tap, or a tooltip for a long-press. The existing "show tooltip while right button held" mode can be reused for display. The consequence: **every tap gets the lift latency.** This is the normal behaviour on phones, but it's a change from desktop OpenTTD, and the design should assume it everywhere.

## 4. Other facts from the code that matter for round 2

- **Drag-building has these selection shapes** (`VPM_*` in `src/viewport_type.h`): straight X or Y line, autorail (fixes the track direction from the drag direction), a rectangular area (demolish, station drag-and-drop, landscaping), and a single tile. Autorail snaps to one axis or a diagonal. It doesn't lay curves within a single drag; a curve is two drags, or one drag that changes direction at a junction tile. `r2-drag-in-progress.png` shows the "Length: 5" readout that already exists.
- **Escape cancels a drag in progress** without building (seen while taking `r2-demolish.png`). The gesture layer can map ✕ / Android back onto that path.
- **Errors** appear as red message windows near the tile (`r2-error.png`). The cost estimate uses the same window (`r2-cost-estimate.png`). Both currently auto-close on a timer.
- **The rail toolbar** has 15 tools plus the railtype dropdown on the main toolbar button (`r2-rail-toolbar.png`, `r2-railtype-dropdown.png`). Signals open a separate picker window (`r2-signal-picker.png`). Bridges show a selection list after the drag (`r2-bridge-picker.png`). That's a second step after "confirm", which the confirm/cancel flow needs to account for.
- **Station placement** shows the footprint and an optional catchment grid that follows the cursor (`r3-station-placing.png`). On touch there's no hover, so the footprint can only appear once the finger is down (or after a first tap).
- **Interim touch behaviour.** Until the gesture layer exists, SDL turns touches into mouse events: tap = click, and one-finger drag pans the map *if* the "scroll with left mouse button" option is on (the test setup turns it on). There's no pinch.

## 5. Questions for round 2 (beyond your own scope)

1. Given §3 Q4: confirm "click on lift" globally for touch, and set the tap-vs-drag slop for *buttons* as well as the map (you specified 6 px for the map).
2. Bridges: the bridge list appears *after* the drag. With confirm/cancel, does the flow become drag → ✓ → bridge list → pick, or should the bridge choice come before ✓?
3. Should build errors show live while dragging (the cost label turns red with the reason), instead of after ✓?
4. Items in §2 marked *Design*: Menu icon, the empty strip under list sheets, newspaper on mobile, and the order of mobile layouts for the slot windows.

## 6. Suggested implementation order for round 2 (for the implementation side)

1. Gesture layer v1, on Android: tap-on-lift, pan, pinch, two-finger tap.
2. Pending-selection state for drag-building, with ✓ / ✕ (Q2), plus a live cost/error label (Q1).
3. Your round 2 rail toolbar layout.
4. Long-press tooltip (Q4), drag-to-scroll lists, 40 px dropdown rows.
