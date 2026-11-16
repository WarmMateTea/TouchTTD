# Handoff: OpenTTD mobile UI, round 2 built → round 3 design (stations and depots)

From: the C++ implementation session
To: the UX/layout design session
Date: 15 Nov 2026

This replies to your round 2 handoff. It covers what was built, where the build differs from the spec, answers to your three round 3 questions, and new findings. It stands on its own.

**Attached screenshots** (the user will upload them with this document):
- `round1/`: the built round 1 shell (`L01`–`L12`, `P01`–`P05`, `D01`–`D02`). You said only the desktop references reached you last time.
- `round2/`: the built round 2, from the real game at the reference phone size (2714×1222, 300%), shown at half size.
- `android/`: three photos-of-screen from the user's phone (`A01`–`A03`), from the first device test.

Everything below was tested with a mouse on the server, as you suggested. Touch-only parts (the gesture layer, handle precision with a finger) are the next step, on the phone.

---

## 1. Round 2 status

| Spec item | Status | Screenshot |
|---|---|---|
| §4.1 Rail toolbar, landscape: one bar over the map; category · autorail · remove · demolish · station · depot · signals · bridge · tunnel · More · status · ✕ · Build | ✅ | `L01` |
| §4.2 Rail toolbar, portrait: two rows, replaces the dock; Exit · status · ✕ · Build / 8 tools | ✅ | `P01` |
| §4.3 Status line and Cancel/Confirm per state (hint, "Lift to review", cost, error, Remove/Demolish/Convert in red, Built + Undo, Pick a bridge) | ✅ (no *Building…* state, see §2) | `L01`–`L09` |
| §4.4 Category flyout with rail types (icon, name, cost per piece; unavailable greyed; switching keeps tool and pending selection) | ✅ | `L11` |
| §4.5 More flyout (6 tools landscape, 8 portrait; More shows the chosen tool) | ✅ | `L10`, `P04` |
| §4.6 Bridge list in the sheet slot; rows show the total cost for the span; tap builds; ✕ cancels | ✅ | `L08` |
| §4.7 Errors: status line while a toolbar is open, red banner elsewhere, no estimate popups | ✅ | `L09` |
| §4.8 Setting "Confirm before building" (named "review before building"), default on | ✅ | – |
| §5 Pending state, live estimate on end-tile change, label 48 px above the finger, handles, ADJUSTING, edge auto-scroll | ✅ | `L02`–`L04` |
| §7 Autorail, fixed-direction tracks, Remove, Demolish, Convert, Bridge, Tunnel | ✅ | `L05`–`L09` |
| §8 Undo by removal, with its cost (refunds shown as "+£371") | ✅ | `L05` |
| §9 Visible map rect (label clamp, edge scroll, scroll-to-tile) | ✅ | – |
| Lift over a window cancels the drag | ✅ | – |
| Round 1 follow-ups: sheet filler (your note 4), newspaper centred (note 7), visible map rect (note 9) | ✅ | `P05` |
| §6 Gestures (tap on lift, two-finger pan/zoom, long-press) | ❌ next, on the phone | – |

## 2. Where the build differs

1. **Handles are blue tile outlines, not yellow with a grip.** The game's selection drawing only has white, red and blue tile outlines. A yellow outline and a grip square need new sprites or custom drawing. *Design: is blue fine, or do you want the yellow look?*
2. **No *Building…* state.** In single player a command finishes immediately, so the toolbar goes straight from Pending to Done. Multiplayer clients will need the state; it isn't designed in yet.
3. **Rotation while Pending drops the selection.** The toolbar is recreated in the other layout and keeps its tool, but the selection in review is discarded. Keeping it is possible but fiddly; it's on the list.
4. **The Undo safety check is stricter than asked.** It looks at the whole rectangle between the two ends, not just the tiles the track runs over, so Undo is sometimes not offered when it could be.
5. **Bridge span hidden by the list.** In landscape the bridge list opens in the side panel, which can cover the span you just dragged (see `L08`, where the span is behind the panel). *Design: should the map scroll so the span stays visible?* (That's easy; the visible map rect exists now.)
6. **Category flyout:** at 300% the rail type names are cut off ("Electrified railway c…") at the 176 px minimum width. *Design: widen it, or use shorter names?*

## 3. Answers to your round 3 questions

**Q1. Station placement with review: yes.**
In the non-drag mode, the station footprint is the tool's highlight, sized from the picker (tracks × length, orientation). It can be frozen on a tap exactly like a track selection. Changing orientation, tracks or length in the picker can resize the frozen footprint in place, anchored at the tapped tile.

The **catchment grid** and the picker's **"Accepts / Supplies"** lines are both computed from that highlight's position and size (`DrawStationCoverageAreaText` reads it). So a frozen footprint automatically shows the right catchment and cargo lines, which makes "tap, review, adjust in the picker, Build" a natural fit.

One more step to design for: building next to an existing station can open the **"join station"** question (which station to join, or a new one).

**Q2. Depot orientation facing the track: yes, feasible.**
The orientation is a single setting the picker writes. On a tap, the code can look at the four neighbouring tiles. If exactly one has track ending towards the tapped tile, it pre-selects that direction before placing (or before review). With zero or several candidates, it keeps the picker's choice.

**Q3. Station drag-and-drop: yes, the same area selection as demolish.**
It's the same rectangle mechanism, with a size limit (the game's station spread). The rectangle's width and height become the number of tracks and the platform length. So "drag a rectangle, review, Build" works like Demolish, with handles at the corners.

## 4. Findings from the first device test (for you to know)

- **System bars and the camera cutout.** On the phone, Android's status bar, navigation bar and camera cutout covered the edges, and the round 1 shell sat under them. The game now runs full screen, and the shell keeps inside the **safe area**. With the bars hidden, Android still reserves the 153 px gesture strip at the bottom, so the dock floats above a strip of map (`A02`). *Design: dock flush with the edge, with its buttons kept clear of the strip?*
- **Hover tooltips after taps** (`A03`). Android leaves the pointer where the finger lifted, so tooltips pop up as if hovering. The gesture layer removes hover on touch.

## 5. Questions for round 3 (from the implementation side)

1. Station review: confirm "tap → frozen footprint → adjust in the picker → Build", and where the picker goes (sheet slot?).
2. The "join station" question: as a step after Build, or inside the picker?
3. §2 items marked *Design*: handle colour, bridge span visibility, flyout width, dock and gesture strip.

## 6. Next on the implementation side

1. The gesture layer on the phone (tap on lift, pan, pinch, two-finger tap, long-press tooltips, no hover on touch).
2. Background survival on Android (persistent notification, autosave when backgrounded, optional pause), approved by the user.
3. Round 3 once you send it.
