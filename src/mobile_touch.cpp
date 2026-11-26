/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file mobile_touch.cpp Turns touch screen gestures into the mouse input the game understands.
 *
 * The video driver feeds finger events in; this translates them (see the round 1 design, section 6):
 *  - a tap clicks when the finger lifts, so sliding off or starting a drag cancels it;
 *  - a one-finger drag presses where the finger went down and follows it (panning the map, or dragging
 *    a construction); over a list it scrolls the list instead;
 *  - a long press shows the tooltip of the button under the finger, or the land information of the tile;
 *  - two fingers pan the map and zoom it in steps when pinched; a two-finger tap zooms out.
 * The mouse pointer leaves the window when the last finger lifts, so nothing "hovers" after a tap.
 */

#include "stdafx.h"
#include "mobile_touch.h"
#include "mobile_gui.h"
#include "gfx_func.h"
#include "gui.h"
#include "window_gui.h"
#include "window_func.h"
#include "viewport_func.h"
#include "tilehighlight_func.h"
#include "zoom_func.h"
#include "strings_func.h"
#include "tile_map.h"
#include "openttd.h"

#include "widgets/smallmap_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** Constants from the design, in GUI pixels at 1x and milliseconds. */
static constexpr int TOUCH_SLOP = 6; ///< Movement below this is still a tap.
static constexpr std::chrono::milliseconds LONG_PRESS_TIME{450}; ///< Holding still this long is a long press.
static constexpr std::chrono::milliseconds TWO_FINGER_TAP_TIME{300}; ///< Two fingers down and up within this is a two-finger tap.
static constexpr float PINCH_STEP = 1.5f; ///< Finger distance ratio for one zoom step.

/** What the fingers are doing. */
enum class TouchMode : uint8_t {
	None, ///< No finger down.
	Pressed, ///< One finger down, not moved past the slop yet.
	Dragging, ///< One finger dragging with the (emulated) left button down.
	ListScroll, ///< One finger dragging over a scrollable list.
	MapPan, ///< One finger dragging over the minimap, which pans it.
	LongPress, ///< One finger held still; a tooltip or land information is shown.
	Multi, ///< Two or more fingers: pan and zoom.
	Ignore, ///< The gesture was finished or cancelled; wait until all fingers lift.
};

/** A finger on the screen. */
struct Finger {
	Point pos; ///< Current position, in screen pixels.
};

static struct {
	TouchMode mode = TouchMode::None;
	std::map<int64_t, Finger> fingers; ///< The fingers down.
	Point start{}; ///< Where the gesture started.
	std::chrono::steady_clock::time_point start_time{}; ///< When the gesture started.
	bool long_press_checked = false; ///< The press was held long enough, but there was nothing to show for it.
	bool active = false; ///< The last input came from touch.

	/* List scrolling. */
	WindowClass list_cls = WindowClass::Invalid; ///< Window of the list.
	WindowNumber list_num{}; ///< Number of that window.
	WidgetID list_scrollbar = INVALID_WIDGET; ///< Scrollbar of the list.
	int list_start_pos = 0; ///< Scroll position when the drag started.
	int list_step = 1; ///< Pixels per row.

	/* Panning or zooming the minimap rather than the main map. */
	bool on_minimap = false; ///< The one- or two-finger gesture started on the minimap.
	Point last{}; ///< Finger position at the last minimap pan step.

	/* Two fingers. */
	Point mid{}; ///< Midpoint of the fingers.
	float baseline = 0; ///< Finger distance at the last zoom step.
	bool moved = false; ///< Whether the two-finger gesture panned or zoomed.
} _touch;

/**
 * Did the last input come from the touch screen?
 * @return \c true when it did.
 */
bool IsTouchInputActive()
{
	return _touch.active;
}

static void MoveCursor(Point pt)
{
	_cursor.in_window = true;
	_cursor.UpdateCursorPosition(pt.x, pt.y);
	HandleMouseEvents();
}

static void PressLeft(Point pt)
{
	MoveCursor(pt);
	_input_pressed.Set(ControlInput::Left);
	HandleMouseEvents();
}

static void ReleaseLeft(Point pt)
{
	_cursor.UpdateCursorPosition(pt.x, pt.y);
	_input_pressed.Reset(ControlInput::Left);
	_input_clicked.Reset(ControlInput::Left);
	HandleMouseEvents();
}

/** Nothing hovers after a touch: take the pointer out of the window and hide its sprite. */
static void LeaveWindow()
{
	_cursor.in_window = false;
	_cursor.dirty = true;
}

static int Distance(Point a, Point b)
{
	return std::max(abs(a.x - b.x), abs(a.y - b.y));
}

/**
 * Midpoint and spread of the fingers down.
 * @param[out] mid Their midpoint.
 * @return The distance between the first two.
 */
static float GetFingerSpread(Point &mid)
{
	auto it = _touch.fingers.begin();
	Point a = it->second.pos;
	Point b = std::next(it)->second.pos;
	mid = {(a.x + b.x) / 2, (a.y + b.y) / 2};
	float dx = a.x - b.x;
	float dy = a.y - b.y;
	return std::sqrt(dx * dx + dy * dy);
}

/**
 * Start a one-finger drag: scroll the list under the finger, or press the emulated left button where the drag started.
 * @param current Where the finger is now.
 */
/**
 * The minimap window, when a point is on its map.
 * @param pt The point.
 * @return The minimap window, or \c nullptr.
 */
static Window *GetMinimapAt(Point pt)
{
	Window *w = FindWindowFromPt(pt.x, pt.y);
	if (w == nullptr || w->window_class != WindowClass::SmallMap) return nullptr;
	const NWidgetCore *nw = w->nested_root->GetWidgetFromPos(pt.x - w->left, pt.y - w->top);
	return (nw != nullptr && nw->GetIndex() == WID_SM_MAP) ? w : nullptr;
}

/**
 * Pan the minimap by the movement of a finger: the map follows the finger.
 * @param from Previous finger position.
 * @param to New finger position.
 */
static void PanMinimap(Point from, Point to)
{
	Window *w = FindWindowById(WindowClass::SmallMap, 0);
	if (w == nullptr || (from.x == to.x && from.y == to.y)) return;
	w->OnScroll({from.x - to.x, from.y - to.y});
}

/**
 * Zoom the minimap one step around a point, as its mouse wheel does.
 * @param in Zoom in rather than out.
 * @param around The point.
 */
static void ZoomMinimap(bool in, Point around)
{
	Window *w = FindWindowById(WindowClass::SmallMap, 0);
	if (w == nullptr) return;
	_cursor.UpdateCursorPosition(around.x, around.y);
	w->OnMouseWheel(in ? -1 : 1, WID_SM_MAP);
}

static void StartDrag(Point current)
{
	/* On the minimap a finger pans it (a tap still moves the main map there). */
	if (GetMinimapAt(_touch.start) != nullptr) {
		_touch.mode = TouchMode::MapPan;
		_touch.last = _touch.start;
		PanMinimap(_touch.last, current);
		_touch.last = current;
		return;
	}

	Window *w = FindWindowFromPt(_touch.start.x, _touch.start.y);
	if (w != nullptr && w->window_class != WindowClass::MainWindow) {
		const NWidgetCore *nw = w->nested_root->GetWidgetFromPos(_touch.start.x - w->left, _touch.start.y - w->top);
		if (nw != nullptr && nw->GetScrollbarIndex() != INVALID_WIDGET) {
			Scrollbar *sb = w->GetScrollbar(nw->GetScrollbarIndex());
			if (sb != nullptr) {
				_touch.mode = TouchMode::ListScroll;
				_touch.list_cls = w->window_class;
				_touch.list_num = w->window_number;
				_touch.list_scrollbar = nw->GetScrollbarIndex();
				_touch.list_start_pos = sb->GetPosition();
				_touch.list_step = std::max<int>(1, w->resize.step_height > 0 ? w->resize.step_height : GetCharacterHeight(FontSize::Normal));
				return;
			}
		}
	}

	_touch.mode = TouchMode::Dragging;
	PressLeft(_touch.start);
	MoveCursor(current);
}

/**
 * A finger touched the screen.
 * @param finger Identifier of the finger.
 * @param x Screen x.
 * @param y Screen y.
 */
void TouchDown(int64_t finger, int x, int y)
{
	_touch.active = true;
	_touch.fingers[finger] = {{x, y}};

	if (_touch.fingers.size() == 1) {
		_touch.mode = TouchMode::Pressed;
		_touch.start = {x, y};
		_touch.start_time = std::chrono::steady_clock::now();
		_touch.long_press_checked = false;
		MoveCursor(_touch.start);
		return;
	}

	if (_touch.fingers.size() == 2) {
		/* A second finger ends the one-finger gesture: a drag is released where it was (a construction
		 * stays for review), a tap is cancelled. From now on the fingers pan and zoom. */
		switch (_touch.mode) {
			case TouchMode::Dragging: ReleaseLeft(_cursor.pos); break;
			case TouchMode::LongPress: _input_pressed.Reset(ControlInput::Right); HandleMouseEvents(); break;
			default: break;
		}
		_touch.mode = TouchMode::Multi;
		_touch.start_time = std::chrono::steady_clock::now();
		_touch.baseline = GetFingerSpread(_touch.mid);
		_touch.moved = false;
		/* Two fingers on the minimap pan and zoom the minimap instead of the main map. */
		_touch.on_minimap = GetMinimapAt(_touch.mid) != nullptr;
	}
}

/**
 * A finger moved.
 * @param finger Identifier of the finger.
 * @param x Screen x.
 * @param y Screen y.
 */
void TouchMove(int64_t finger, int x, int y)
{
	auto it = _touch.fingers.find(finger);
	if (it == _touch.fingers.end()) return;
	it->second.pos = {x, y};

	switch (_touch.mode) {
		case TouchMode::Pressed:
			if (Distance(_touch.start, {x, y}) > ScaleGUITrad(TOUCH_SLOP)) StartDrag({x, y});
			break;

		case TouchMode::Dragging:
			MoveCursor({x, y});
			break;

		case TouchMode::MapPan:
			PanMinimap(_touch.last, {x, y});
			_touch.last = {x, y};
			break;

		case TouchMode::ListScroll: {
			Window *w = FindWindowById(_touch.list_cls, _touch.list_num);
			Scrollbar *sb = w != nullptr ? w->GetScrollbar(_touch.list_scrollbar) : nullptr;
			if (sb == nullptr) break;
			int rows = (_touch.start.y - y) / _touch.list_step;
			if (sb->SetPosition(std::max(0, _touch.list_start_pos + rows))) w->SetDirty();
			break;
		}

		case TouchMode::Multi: {
			if (_touch.fingers.size() < 2) break;
			Point mid;
			float spread = GetFingerSpread(mid);

			if (_touch.on_minimap) {
				if (Distance(mid, _touch.mid) > 0) _touch.moved = true;
				PanMinimap(_touch.mid, mid);
				_touch.mid = mid;
				if (_touch.baseline > 0 && (spread >= _touch.baseline * PINCH_STEP || spread <= _touch.baseline / PINCH_STEP)) {
					ZoomMinimap(spread > _touch.baseline, mid);
					_touch.baseline = spread;
					_touch.moved = true;
				}
				break;
			}

			/* Pan by the movement of the midpoint. */
			Window *main = GetMainWindow();
			if (main != nullptr && main->viewport != nullptr && (mid.x != _touch.mid.x || mid.y != _touch.mid.y)) {
				ViewportData &vp = *main->viewport;
				vp.dest_scrollpos_x -= ScaleByZoom(mid.x - _touch.mid.x, vp.zoom);
				vp.dest_scrollpos_y -= ScaleByZoom(mid.y - _touch.mid.y, vp.zoom);
				vp.scrollpos_x = vp.dest_scrollpos_x;
				vp.scrollpos_y = vp.dest_scrollpos_y;
				main->SetDirty();
				if (Distance(mid, _touch.mid) > 0) _touch.moved = true;
			}
			_touch.mid = mid;

			/* Zoom one step each time the fingers spread or pinch by the step ratio. */
			if (_touch.baseline > 0 && main != nullptr && (spread >= _touch.baseline * PINCH_STEP || spread <= _touch.baseline / PINCH_STEP)) {
				_cursor.UpdateCursorPosition(mid.x, mid.y);
				ZoomInOrOutToCursorWindow(spread > _touch.baseline, main);
				_touch.baseline = spread;
				_touch.moved = true;
			}
			break;
		}

		default:
			break;
	}
}

/**
 * A finger left the screen.
 * @param finger Identifier of the finger.
 * @param x Screen x.
 * @param y Screen y.
 */
void TouchUp(int64_t finger, int x, int y)
{
	if (_touch.fingers.erase(finger) == 0) return;

	switch (_touch.mode) {
		case TouchMode::Pressed:
			/* A tap: click on lift. */
			PressLeft({x, y});
			ReleaseLeft({x, y});
			break;

		case TouchMode::Dragging:
			ReleaseLeft({x, y});
			break;

		case TouchMode::LongPress:
			/* The tooltip shows while the right button is down; releasing it closes the tooltip, without a click. */
			_input_pressed.Reset(ControlInput::Right);
			HandleMouseEvents();
			break;

		case TouchMode::Multi:
			if (std::chrono::steady_clock::now() - _touch.start_time < TWO_FINGER_TAP_TIME && !_touch.moved) {
				/* Two-finger tap: zoom out around the fingers. */
				if (_touch.on_minimap) {
					ZoomMinimap(false, _touch.mid);
				} else if (Window *main = GetMainWindow(); main != nullptr) {
					_cursor.UpdateCursorPosition(_touch.mid.x, _touch.mid.y);
					ZoomInOrOutToCursorWindow(false, main);
				}
			}
			_touch.mode = TouchMode::Ignore;
			break;

		default:
			break;
	}

	if (_touch.fingers.empty()) {
		_touch.mode = TouchMode::None;
		LeaveWindow();
	} else if (_touch.mode != TouchMode::Multi) {
		_touch.mode = TouchMode::Ignore;
	}
}

/** All fingers were taken away by the system (e.g. a system gesture): cancel without clicking. */
void TouchCancel()
{
	if (_touch.mode == TouchMode::Dragging) ReleaseLeft(_cursor.pos);
	if (_touch.mode == TouchMode::LongPress) {
		_input_pressed.Reset(ControlInput::Right);
		HandleMouseEvents();
	}
	_touch.fingers.clear();
	_touch.mode = TouchMode::None;
	LeaveWindow();
}

/**
 * Show what a long press on a point shows: the tooltip of a button, or the land information of a tile.
 * @param pt The point.
 * @return \c true when something was shown.
 */
static bool ShowLongPress(Point pt)
{
	Window *w = FindWindowFromPt(pt.x, pt.y);
	if (w == nullptr) return false;

	if (w->window_class == WindowClass::MainWindow) {
		/* Building: holding still and then moving is a drag, so the long press is off. */
		if (_game_mode != GameMode::Normal || (_thd.place_mode & HT_DRAG_MASK) != HT_NONE) return false;
		Point tile = GetTileBelowCursor();
		if (tile.x == -1) return false;
		ShowLandInfo(TileVirtXY(tile.x, tile.y));
		return true;
	}

	NWidgetCore *nw = w->nested_root->GetWidgetFromPos(pt.x - w->left, pt.y - w->top);
	if (nw == nullptr) return false;
	Point wpt = {pt.x - w->left, pt.y - w->top};
	_input_pressed.Set(ControlInput::Right);
	if (nw->GetIndex() >= 0 && w->OnTooltip(wpt, nw->GetIndex(), TooltipCloseCondition::RightClick)) return true;
	if (nw->GetToolTip() == STR_NULL) {
		_input_pressed.Reset(ControlInput::Right);
		return false;
	}
	GuiShowTooltips(w, GetEncodedString(nw->GetToolTip()), TooltipCloseCondition::RightClick);
	return true;
}

/** Called every input loop: detect long presses. */
void TouchTick()
{
	if (_touch.mode != TouchMode::Pressed || _touch.long_press_checked) return;
	if (std::chrono::steady_clock::now() - _touch.start_time < LONG_PRESS_TIME) return;

	/* With nothing to show, it stays a press: lifting still taps and moving still drags. */
	_touch.long_press_checked = true;
	if (ShowLongPress(_touch.start)) _touch.mode = TouchMode::LongPress;
}
