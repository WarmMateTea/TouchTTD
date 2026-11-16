# Round 2 implementation log: laying track

Running journal of building the design's round 2 (`handoffs/04-design-to-impl-round2.md`): rail toolbar, review before building, bridges, tunnels, errors, undo. Also the round 1 follow-ups the designer decided on (§1 there).

The phone's VPN is down for this stretch, so everything is built and tested with a mouse on the server (the design says the whole flow works with a mouse). Touch-only parts (gesture layer, handle precision) wait for the phone.

## Step 1: Reading the placement code

How a desktop construction tool works, end to end:

1. **Picking a tool:** a toolbar button calls `HandlePlacePushButton()` → `SetObjectToPlace()`, which stores the tool's highlight style in `_thd` (`TileHighlightData`, `tilehighlight_type.h`).
2. **Pressing on the map:** `HandleViewportClicked()` → `PlaceObject()` → the toolbar's `OnPlaceObject()`. Dragging tools call `VpStartPlaceSizing()`; one-tap tools (depot, tunnel) post their command directly.
3. **Dragging:** every frame `VpHandlePlaceSizingDrag()` calls `OnPlaceDrag()` → `VpSelectTilesWithMethod()`, which updates `_thd.selstart/selend/next_drawstyle`. `UpdateTileSelection()` turns that into what is drawn.
4. **Releasing:** `VpHandlePlaceSizingDrag()` resets the tool to its idle highlight and calls `OnPlaceMouseUp()`. The toolbar reads `_thd` (e.g. `HandleAutodirPlacement()` takes the track direction from `_thd.drawstyle`) and posts the commands.

**Findings**
- **`_thd.freeze` already exists** (the station picker uses it): it stops `UpdateTileSelection()` from following the cursor. Freezing after release *is* the review state.
- **Replay instead of re-implementing:** because the toolbar reads everything from `_thd`, calling `OnPlaceMouseUp()` again with the frozen `_thd` builds exactly what desktop would have built on release. No per-tool command code was copied.
- **Estimates:** a command posted while Shift is held only runs its test and shows a popup (`CommandHelperBase::InternalPostBefore`). With no side effects and no callbacks, that's the basis for the live estimate.

## Step 2: Command result capture (`command_func.h`, `command.cpp`)

`CommandResultCapture` is an RAII object. While it exists:
- every command posted by the local player passes through `InternalPostResult()`, which adds up its cost and keeps the first error;
- with `estimate_only = true`, commands are only tested (the Shift path), and nothing is shown;
- with `estimate_only = false`, commands execute and keep their cost animation, but a failure doesn't open an error window; the capture's owner shows it.

**Gotcha found the hard way:** captures can't nest (`assert(active == nullptr)`). Building a bridge crashed because the bridge window still had its capture open when it reported the result, and the result estimated the undo with a second capture. Close the scope before calling anything that may capture.

## Step 3: The review controller (`src/mobile_build.cpp`)

It's the state machine of the design's §5: Idle → Dragging → Pending → (Build) → Done, plus BridgePicking. Hooks in existing code:

| Hook | Where | What it does |
|---|---|---|
| `MobileBuildOnSizingUpdate()` | `VpHandlePlaceSizingDrag()`, every frame while dragging | Live estimate when the end tile changes, label above the finger, edge scroll |
| `MobileBuildReviewWanted()` / `MobileBuildOnRelease()` | `VpHandlePlaceSizingDrag()`, on release | Freeze the selection instead of building (track if the setting is on; remove, demolish, convert always; bridges open the list) |
| `MobileBuildOnViewportClick()` | top of `HandleViewportClicked()` | While pending: a press on a handle starts adjusting, elsewhere it pans |
| `MobileBuildOnPlacementReset()` | `SetObjectToPlace()` | Any tool change drops what is in review |
| `MobileBuildTunnelTap()` | rail toolbar `OnPlaceObject()` | Tunnel tap → review, with the exit found the way desktop's hover does |
| `IsMobileBuildHandleTile()` | `DrawTileSelection()` | Handles drawn as blue tile outlines, over the selection |
| `ShowMobileError()` | `ShowErrorMessage()` | Errors to the status line (toolbar open) or a red banner |

**Undo:** the same selection replayed with the rail toolbar's remove mode on (`SetRailRemoveMode()`). Bridges and tunnels are undone by clearing one head (`LandscapeClear`). Undo is only offered when no tile of the selection's rectangle had track before; that's more conservative than the design asked, because it checks the rectangle rather than the exact track path.

## Step 4: The mobile rail toolbar (`rail_gui.cpp`)

The same `BuildRailToolbarWindow` with two more `WindowDesc`s (landscape, portrait), as with the Town directory in round 1. The tools moved to the More flyout still exist in the toolbar, inside a never-shown `NWID_SELECTION` (`WID_RAT_SEL_HIDDEN`). The existing code that lowers, raises and disables them keeps working, and the More flyout "clicks" them through `toolbar->OnClick()`.

**Findings**
- **A hidden selection widget's children are still registered** (`NWidgetStacked::FillWidgetLookup` adds every plane), so `GetWidget()` finds them.
- **Vertical containers take the LCM of their children's horizontal resize steps,** and `std::lcm(x, 0) == 0`. One row that can't resize (the hidden selection) made the whole portrait toolbar unresizable. Moved into a horizontal row, where it doesn't matter.
- **`NWidgetCore::widget_data` is protected,** so the toolbar has `GetToolSprite()` for the More flyout's icons.
- **Unsigned arithmetic:** `button->current_x - build->width` is unsigned, so a flyout wider than its button got an x of −2 billion. Cast to `int` first.
- **The toolbar updates its buttons in `OnInvalidateData()`,** so the controller invalidates (`InvalidateWindowClassesData(..., true)`) instead of just marking it dirty.

## Step 5: Bridge list, flyouts, errors, round 1 follow-ups

- **Bridge list:** a mobile layout in `bridge_gui.cpp`, docked in the sheet slot with `MobileShowInSheet()`. Rows of 48 px show the total cost for the span. A tap builds under a capture and reports to the controller. Closing it cancels the span.
- **Category flyout:** the round 1 Build flyout, opened from the toolbar, with a rail type list (icon, name, cost per piece; unavailable types greyed).
- **Error banner:** `MobileErrorWindow`, red, where the news banner goes. It closes on the desktop error timer, a tap or ✕. Estimate popups (`ShowEstimatedCostOrIncome`) are off in the mobile layout.
- **Sheet filler** (design round 1 note 4): a window in the content's colour fills the strip below a sheet's content. It checks every 250 ms whether its sheet still exists, because desktop windows can close themselves.
- **Newspaper** (round 1 note 7): centred in the visible map rect and not slid up; also in `RelocateAllWindows()`.
- **Visible map rect** (round 1 custom #8): `GetMobileVisibleMapRect()`. `ScrollWindowTo()` offsets the main viewport by `GetMobileMapCentreOffset()`, so scroll-to-tile centres in the uncovered area.

## Step 6: Verification (screenshots in `screenshots/round2/`)

Driven with `xdotool` at the phone's size (2714×1222 and 1222×2714, 300%):
- `L01` toolbar, `L02` live label, `L03` pending with handles and cost, `L04` adjusting a handle (7 → 12 tiles), `L05` built + Undo.
- `L06` remove (red, refund shown as "+£371"), `L07` demolish, `L08` bridge list in the side panel, `L09` tunnel error in the status line.
- `L10` More flyout, `L11` category + rail types.
- `P01`–`P04` portrait toolbar, drag, pending, More (8 tools).
- `P05` newspaper centred.

Also checked without screenshots: Undo removes the track (income animation); lifting over the toolbar cancels; edge scrolling while dragging near the edge. `ctest`: 99/99.

**Not done in this round:**
- The *Building…* state: single-player commands finish at once. Multiplayer clients would need the command callback.
- Keeping a pending selection through a rotation: the toolbar is recreated in the other layout and keeps its tool, but the selection is dropped.
- The yellow handle look: handles use the existing blue tile outline (the selection palettes are red and blue).
- Station, depot, signals and waypoint keep their desktop placement (design: round 3).
- The gesture layer: next, on the phone.

## Step 7: The gesture layer (`src/mobile_touch.cpp`), tested on the phone

On Android, `SDL_HINT_TOUCH_MOUSE_EVENTS=0` stops SDL turning touches into mouse events. The SDL3 driver sends finger events to `TouchDown/Move/Up/Cancel()`, and calls `TouchTick()` each input loop to detect long presses. The layer then emulates the mouse the game expects:

| Gesture | Emulated as |
|---|---|
| Tap (< 6 GUI px movement) | Press + release at the lift point, so the click fires on lift |
| One-finger drag | Press where the finger went down, then follow it (map pan with left-drag scroll mode, or a construction drag) |
| One-finger drag over a widget with a scrollbar | Scroll that list by rows; no click |
| Long press 450 ms on a widget | Tooltip in "while right button held" mode; closes on lift, no click |
| Long press on the map (no tool) | Land area information |
| Two fingers | Pan by the midpoint; zoom one step per ×1.5 spread change around the midpoint |
| Two-finger tap (< 300 ms, no movement) | Zoom out |
| Last finger lifts | Pointer leaves the window, so nothing hovers afterwards |

**Verified on the Poco** with `adb shell input` (`A04`–`A09` in `screenshots/android/`):
- taps: New Game, Generate, World, the Build flyout;
- one-finger pan;
- list scrolling in the Town directory;
- long press: the tooltip, with no click;
- track: drag with the live label, review with handles, Build, then Undo offered.

No stray hover tooltips after taps.

**Not verifiable with adb:** pinch, two-finger pan and two-finger tap (`adb shell input` only does one finger). The user tests those by hand.

**Finding:** on the phone the toolbar sits higher than on the server (above the 153 px gesture strip of the safe area), so coordinates for `adb shell input tap` come from a screenshot of the phone, not from the server tests.

**Tip:** `adb shell input swipe X1 Y1 X2 Y2 MS` with `X1,Y1 == X2,Y2` and a long `MS` is a long press. Run it in the background and screenshot during the hold.
