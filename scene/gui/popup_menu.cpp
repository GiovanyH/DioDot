/*************************************************************************/
/*  popup_menu.cpp                                                       */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2019 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2019 Godot Engine contributors (cf. AUTHORS.md)    */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "popup_menu.h"
#include "os/input.h"
#include "os/keyboard.h"
#include "print_string.h"
#include "translation.h"

String PopupMenu::_get_accel_text(int p_item) const {

	ERR_FAIL_INDEX_V(p_item, items.size(), String());

	if (items[p_item].shortcut.is_valid())
		return items[p_item].shortcut->get_as_text();
	else if (items[p_item].accel)
		return keycode_get_string(items[p_item].accel);
	return String();
}

Size2 PopupMenu::get_minimum_size() const {

	int vseparation = get_constant("vseparation");
	int hseparation = get_constant("hseparation");

	Size2 minsize = get_stylebox("panel")->get_minimum_size();
	Ref<Font> font = get_font("font");

	float max_w = 0;
	int font_h = font->get_height();
	int check_w = MAX(get_icon("checked")->get_width(), get_icon("radio_checked")->get_width());
	int accel_max_w = 0;

	for (int i = 0; i < items.size(); i++) {

		Size2 size;
		if (!items[i].icon.is_null()) {

			Size2 icon_size = items[i].icon->get_size();
			size.height = MAX(icon_size.height, font_h);
			size.width += icon_size.width;
			size.width += hseparation;
		} else {

			size.height = font_h;
		}

		size.width += items[i].h_ofs;

		if (items[i].checkable_type) {

			size.width += check_w + hseparation;
		}

		String text = items[i].shortcut.is_valid() ? String(tr(items[i].shortcut->get_name())) : items[i].xl_text;
		size.width += font->get_string_size(text).width;
		if (i > 0)
			size.height += vseparation;

		if (items[i].accel || (items[i].shortcut.is_valid() && items[i].shortcut->is_valid())) {

			int accel_w = hseparation * 2;
			accel_w += font->get_string_size(_get_accel_text(i)).width;
			accel_max_w = MAX(accel_w, accel_max_w);
		}

		if (items[i].submenu != "") {

			size.width += get_icon("submenu")->get_width();
		}

		minsize.height += size.height;
		max_w = MAX(max_w, size.width);
	}

	minsize.width += max_w + accel_max_w;

	return minsize;
}

int PopupMenu::_get_mouse_over(const Point2 &p_over) const {

	if (p_over.x < 0 || p_over.x >= get_size().width)
		return -1;

	Ref<StyleBox> style = get_stylebox("panel");

	Point2 ofs = style->get_offset();

	if (ofs.y > p_over.y)
		return -1;

	Ref<Font> font = get_font("font");
	int vseparation = get_constant("vseparation");
	float font_h = font->get_height();

	for (int i = 0; i < items.size(); i++) {

		ofs.y += vseparation;
		float h;

		if (!items[i].icon.is_null()) {

			Size2 icon_size = items[i].icon->get_size();
			h = MAX(icon_size.height, font_h);
		} else {

			h = font_h;
		}

		ofs.y += h;
		if (p_over.y < ofs.y) {
			return i;
		}
	}

	return -1;
}

void PopupMenu::_activate_submenu(int over) {

	Node *n = get_node(items[over].submenu);
	ERR_EXPLAIN("item subnode does not exist: " + items[over].submenu);
	ERR_FAIL_COND(!n);
	Popup *pm = Object::cast_to<Popup>(n);
	ERR_EXPLAIN("item subnode is not a Popup: " + items[over].submenu);
	ERR_FAIL_COND(!pm);
	if (pm->is_visible_in_tree())
		return; //already visible!

	Point2 p = get_global_position();
	Rect2 pr(p, get_size());
	Ref<StyleBox> style = get_stylebox("panel");

	Point2 pos = p + Point2(get_size().width, items[over]._ofs_cache - style->get_offset().y);
	Size2 size = pm->get_size();
	// fix pos
	if (pos.x + size.width > get_viewport_rect().size.width)
		pos.x = p.x - size.width;

	pm->set_position(pos);
	pm->popup();

	PopupMenu *pum = Object::cast_to<PopupMenu>(pm);
	if (pum) {

		pr.position -= pum->get_global_position();
		pum->clear_autohide_areas();
		pum->add_autohide_area(Rect2(pr.position.x, pr.position.y, pr.size.x, items[over]._ofs_cache));
		if (over < items.size() - 1) {
			int from = items[over + 1]._ofs_cache;
			pum->add_autohide_area(Rect2(pr.position.x, pr.position.y + from, pr.size.x, pr.size.y - from));
		}
	}
}

void PopupMenu::_submenu_timeout() {

	if (mouse_over == submenu_over)
		_activate_submenu(mouse_over);

	submenu_over = -1;
}

void PopupMenu::_scroll(float p_factor, const Point2 &p_over) {

	const float global_y = get_global_position().y;

	int vseparation = get_constant("vseparation");
	Ref<Font> font = get_font("font");

	float dy = (vseparation + font->get_height()) * 3 * p_factor;
	if (dy > 0 && global_y < 0)
		dy = MIN(dy, -global_y - 1);
	else if (dy < 0 && global_y + get_size().y > get_viewport_rect().size.y)
		dy = -MIN(-dy, global_y + get_size().y - get_viewport_rect().size.y - 1);
	set_position(get_position() + Vector2(0, dy));

	Ref<InputEventMouseMotion> ie;
	ie.instance();
	ie->set_position(p_over - Vector2(0, dy));
	_gui_input(ie);
}

void PopupMenu::_gui_input(const Ref<InputEvent> &p_event) {

	Ref<InputEventKey> k = p_event;

	if (k.is_valid()) {

		if (!k->is_pressed())
			return;

		switch (k->get_scancode()) {

			case KEY_DOWN: {

				int search_from = mouse_over + 1;
				if (search_from >= items.size())
					search_from = 0;

				for (int i = search_from; i < items.size(); i++) {

					if (i < 0 || i >= items.size())
						continue;

					if (!items[i].separator && !items[i].disabled) {

						mouse_over = i;
						update();
						break;
					}
				}
			} break;
			case KEY_UP: {

				int search_from = mouse_over - 1;
				if (search_from < 0)
					search_from = items.size() - 1;

				for (int i = search_from; i >= 0; i--) {

					if (i < 0 || i >= items.size())
						continue;

					if (!items[i].separator && !items[i].disabled) {

						mouse_over = i;
						update();
						break;
					}
				}
			} break;

			case KEY_LEFT: {

				Node *n = get_parent();
				if (!n)
					break;

				PopupMenu *pm = Object::cast_to<PopupMenu>(n);
				if (!pm)
					break;

				hide();
			} break;

			case KEY_RIGHT: {

				if (mouse_over >= 0 && mouse_over < items.size() && !items[mouse_over].separator && items[mouse_over].submenu != "" && submenu_over != mouse_over)
					_activate_submenu(mouse_over);
			} break;

			case KEY_ENTER:
			case KEY_KP_ENTER: {

				if (mouse_over >= 0 && mouse_over < items.size() && !items[mouse_over].separator) {

					if (items[mouse_over].submenu != "" && submenu_over != mouse_over) {
						_activate_submenu(mouse_over);
						break;
					}

					activate_item(mouse_over);
				}
			} break;
		}
	}

	Ref<InputEventMouseButton> b = p_event;

	if (b.is_valid()) {

		if (b->is_pressed())
			return;

		switch (b->get_button_index()) {

			case BUTTON_WHEEL_DOWN: {

				if (get_global_position().y + get_size().y > get_viewport_rect().size.y) {
					_scroll(-b->get_factor(), b->get_position());
				}
			} break;
			case BUTTON_WHEEL_UP: {

				if (get_global_position().y < 0) {
					_scroll(b->get_factor(), b->get_position());
				}
			} break;
			case BUTTON_LEFT: {

				int over = _get_mouse_over(b->get_position());

				if (invalidated_click) {
					invalidated_click = false;
					break;
				}
				if (over < 0) {
					hide();
					break; //non-activable
				}

				if (items[over].separator || items[over].disabled)
					break;

				if (items[over].submenu != "") {

					_activate_submenu(over);
					return;
				}
				activate_item(over);

			} break;
		}

		//update();
	}

	Ref<InputEventMouseMotion> m = p_event;

	if (m.is_valid()) {

		if (invalidated_click) {
			moved += m->get_relative();
			if (moved.length() > 4)
				invalidated_click = false;
		}

		for (List<Rect2>::Element *E = autohide_areas.front(); E; E = E->next()) {

			if (!Rect2(Point2(), get_size()).has_point(m->get_position()) && E->get().has_point(m->get_position())) {
				call_deferred("hide");
				return;
			}
		}

		int over = _get_mouse_over(m->get_position());
		int id = (over < 0 || items[over].separator || items[over].disabled) ? -1 : (items[over].ID >= 0 ? items[over].ID : over);

		if (id < 0) {
			mouse_over = -1;
			update();
			return;
		}

		if (items[over].submenu != "" && submenu_over != over) {
			submenu_over = over;
			submenu_timer->start();
		}

		if (over != mouse_over) {
			mouse_over = over;
			update();
		}
	}

	Ref<InputEventPanGesture> pan_gesture = p_event;
	if (pan_gesture.is_valid()) {
		if (get_global_position().y + get_size().y > get_viewport_rect().size.y || get_global_position().y < 0) {
			_scroll(-pan_gesture->get_delta().y, pan_gesture->get_position());
		}
	}
}

bool PopupMenu::has_point(const Point2 &p_point) const {

	if (parent_rect.has_point(p_point))
		return true;
	for (const List<Rect2>::Element *E = autohide_areas.front(); E; E = E->next()) {

		if (E->get().has_point(p_point))
			return true;
	}

	return Control::has_point(p_point);
}

void PopupMenu::_notification(int p_what) {

	switch (p_what) {

		case NOTIFICATION_TRANSLATION_CHANGED: {

			for (int i = 0; i < items.size(); i++) {
				items[i].xl_text = tr(items[i].text);
			}

			minimum_size_changed();
			update();

		} break;
		case NOTIFICATION_DRAW: {

			RID ci = get_canvas_item();
			Size2 size = get_size();

			Ref<StyleBox> style = get_stylebox("panel");
			Ref<StyleBox> hover = get_stylebox("hover");
			Ref<Font> font = get_font("font");
			// In Item::checkable_type enum order (less the non-checkable member)
			Ref<Texture> check[] = { get_icon("checked"), get_icon("radio_checked") };
			Ref<Texture> uncheck[] = { get_icon("unchecked"), get_icon("radio_unchecked") };
			Ref<Texture> submenu = get_icon("submenu");
			Ref<StyleBox> separator = get_stylebox("separator");

			style->draw(ci, Rect2(Point2(), get_size()));
			Point2 ofs = style->get_offset();
			int vseparation = get_constant("vseparation");
			int hseparation = get_constant("hseparation");
			Color font_color = get_color("font_color");
			Color font_color_disabled = get_color("font_color_disabled");
			Color font_color_accel = get_color("font_color_accel");
			Color font_color_hover = get_color("font_color_hover");
			float font_h = font->get_height();

			for (int i = 0; i < items.size(); i++) {

				if (i > 0)
					ofs.y += vseparation;
				Point2 item_ofs = ofs;
				float h;
				Size2 icon_size;

				item_ofs.x += items[i].h_ofs;
				if (!items[i].icon.is_null()) {

					icon_size = items[i].icon->get_size();
					h = MAX(icon_size.height, font_h);
				} else {

					h = font_h;
				}

				if (i == mouse_over) {

					hover->draw(ci, Rect2(item_ofs + Point2(-hseparation, -vseparation / 2), Size2(get_size().width - style->get_minimum_size().width + hseparation * 2, h + vseparation)));
				}

				if (items[i].separator) {

					int sep_h = separator->get_center_size().height + separator->get_minimum_size().height;
					separator->draw(ci, Rect2(item_ofs + Point2(0, Math::floor((h - sep_h) / 2.0)), Size2(get_size().width - style->get_minimum_size().width, sep_h)));
				}

				if (items[i].checkable_type) {
					Texture *icon = (items[i].checked ? check[items[i].checkable_type - 1] : uncheck[items[i].checkable_type - 1]).ptr();
					icon->draw(ci, item_ofs + Point2(0, Math::floor((h - icon->get_height()) / 2.0)));
					item_ofs.x += icon->get_width() + hseparation;
				}

				if (!items[i].icon.is_null()) {
					items[i].icon->draw(ci, item_ofs + Point2(0, Math::floor((h - icon_size.height) / 2.0)));
					item_ofs.x += items[i].icon->get_width();
					item_ofs.x += hseparation;
				}

				if (items[i].submenu != "") {
					submenu->draw(ci, Point2(size.width - style->get_margin(MARGIN_RIGHT) - submenu->get_width(), item_ofs.y + Math::floor(h - submenu->get_height()) / 2));
				}

				item_ofs.y += font->get_ascent();
				String text = items[i].shortcut.is_valid() ? String(tr(items[i].shortcut->get_name())) : items[i].xl_text;
				if (!items[i].separator) {

					font->draw(ci, item_ofs + Point2(0, Math::floor((h - font_h) / 2.0)), text, items[i].disabled ? font_color_disabled : (i == mouse_over ? font_color_hover : font_color));
				}

				if (items[i].accel || (items[i].shortcut.is_valid() && items[i].shortcut->is_valid())) {
					//accelerator
					String text = _get_accel_text(i);
					item_ofs.x = size.width - style->get_margin(MARGIN_RIGHT) - font->get_string_size(text).width;
					font->draw(ci, item_ofs + Point2(0, Math::floor((h - font_h) / 2.0)), text, i == mouse_over ? font_color_hover : font_color_accel);
				}

				items[i]._ofs_cache = ofs.y;

				ofs.y += h;
			}

		} break;
		case NOTIFICATION_MOUSE_ENTER: {

			grab_focus();
		} break;
		case NOTIFICATION_MOUSE_EXIT: {

			if (mouse_over >= 0 && (items[mouse_over].submenu == "" || submenu_over != -1)) {
				mouse_over = -1;
				update();
			}
		} break;
		case NOTIFICATION_POPUP_HIDE: {

			if (mouse_over >= 0) {
				mouse_over = -1;
				update();
			}
		} break;
	}
}

void PopupMenu::add_icon_item(const Ref<Texture> &p_icon, const String &p_label, int p_ID, uint32_t p_accel) {

	Item item;
	item.icon = p_icon;
	item.text = p_label;
	item.xl_text = tr(p_label);
	item.accel = p_accel;
	item.ID = p_ID;
	items.push_back(item);
	update();
}
void PopupMenu::add_item(const String &p_label, int p_ID, uint32_t p_accel) {

	Item item;
	item.text = p_label;
	item.xl_text = tr(p_label);
	item.accel = p_accel;
	item.ID = p_ID;
	items.push_back(item);
	update();
}

void PopupMenu::add_submenu_item(const String &p_label, const String &p_submenu, int p_ID) {

	Item item;
	item.text = p_label;
	item.xl_text = tr(p_label);
	item.ID = p_ID;
	item.submenu = p_submenu;
	items.push_back(item);
	update();
}

void PopupMenu::add_icon_check_item(const Ref<Texture> &p_icon, const String &p_label, int p_ID, uint32_t p_accel) {

	Item item;
	item.icon = p_icon;
	item.text = p_label;
	item.xl_text = tr(p_label);
	item.accel = p_accel;
	item.ID = p_ID;
	item.checkable_type = Item::CHECKABLE_TYPE_CHECK_BOX;
	items.push_back(item);
	update();
}

void PopupMenu::add_check_item(const String &p_label, int p_ID, uint32_t p_accel) {

	Item item;
	item.text = p_label;
	item.xl_text = tr(p_label);
	item.accel = p_accel;
	item.ID = p_ID;
	item.checkable_type = Item::CHECKABLE_TYPE_CHECK_BOX;
	items.push_back(item);
	update();
}

void PopupMenu::add_radio_check_item(const String &p_label, int p_ID, uint32_t p_accel) {

	add_check_item(p_label, p_ID, p_accel);
	items[items.size() - 1].checkable_type = Item::CHECKABLE_TYPE_RADIO_BUTTON;
	update();
}

void PopupMenu::add_icon_radio_check_item(const Ref<Texture> &p_icon, const String &p_label, int p_ID, uint32_t p_accel) {

	add_icon_check_item(p_icon, p_label, p_ID, p_accel);
	items[items.size() - 1].checkable_type = Item::CHECKABLE_TYPE_RADIO_BUTTON;
	update();
}

void PopupMenu::add_icon_shortcut(const Ref<Texture> &p_icon, const Ref<ShortCut> &p_shortcut, int p_ID, bool p_global) {

	ERR_FAIL_COND(p_shortcut.is_null());

	_ref_shortcut(p_shortcut);

	Item item;
	item.ID = p_ID;
	item.icon = p_icon;
	item.shortcut = p_shortcut;
	item.shortcut_is_global = p_global;
	items.push_back(item);
	update();
}

void PopupMenu::add_shortcut(const Ref<ShortCut> &p_shortcut, int p_ID, bool p_global) {

	ERR_FAIL_COND(p_shortcut.is_null());

	_ref_shortcut(p_shortcut);

	Item item;
	item.ID = p_ID;
	item.shortcut = p_shortcut;
	item.shortcut_is_global = p_global;
	items.push_back(item);
	update();
}

void PopupMenu::add_icon_check_shortcut(const Ref<Texture> &p_icon, const Ref<ShortCut> &p_shortcut, int p_ID, bool p_global) {

	ERR_FAIL_COND(p_shortcut.is_null());

	_ref_shortcut(p_shortcut);

	Item item;
	item.ID = p_ID;
	item.shortcut = p_shortcut;
	item.checkable_type = Item::CHECKABLE_TYPE_CHECK_BOX;
	item.icon = p_icon;
	item.shortcut_is_global = p_global;
	items.push_back(item);
	update();
}

void PopupMenu::add_check_shortcut(const Ref<ShortCut> &p_shortcut, int p_ID, bool p_global) {

	ERR_FAIL_COND(p_shortcut.is_null());

	_ref_shortcut(p_shortcut);

	Item item;
	item.ID = p_ID;
	item.shortcut = p_shortcut;
	item.shortcut_is_global = p_global;
	item.checkable_type = Item::CHECKABLE_TYPE_CHECK_BOX;
	items.push_back(item);
	update();
}

void PopupMenu::add_radio_check_shortcut(const Ref<ShortCut> &p_shortcut, int p_ID, bool p_global) {

	add_check_shortcut(p_shortcut, p_ID, p_global);
	items[items.size() - 1].checkable_type = Item::CHECKABLE_TYPE_RADIO_BUTTON;
	update();
}

void PopupMenu::add_multistate_item(const String &p_label, int p_max_states, int p_default_state, int p_ID, uint32_t p_accel) {

	Item item;
	item.text = p_label;
	item.xl_text = tr(p_label);
	item.accel = p_accel;
	item.ID = p_ID;
	item.max_states = p_max_states;
	item.state = p_default_state;
	items.push_back(item);
	update();
}

void PopupMenu::set_item_text(int p_idx, const String &p_text) {

	ERR_FAIL_INDEX(p_idx, items.size());
	items[p_idx].text = p_text;
	items[p_idx].xl_text = tr(p_text);

	update();
}
void PopupMenu::set_item_icon(int p_idx, const Ref<Texture> &p_icon) {

	ERR_FAIL_INDEX(p_idx, items.size());
	items[p_idx].icon = p_icon;

	update();
}
void PopupMenu::set_item_checked(int p_idx, bool p_checked) {

	ERR_FAIL_INDEX(p_idx, items.size());

	items[p_idx].checked = p_checked;

	update();
}
void PopupMenu::set_item_id(int p_idx, int p_ID) {

	ERR_FAIL_INDEX(p_idx, items.size());
	items[p_idx].ID = p_ID;

	update();
}

void PopupMenu::set_item_accelerator(int p_idx, uint32_t p_accel) {

	ERR_FAIL_INDEX(p_idx, items.size());
	items[p_idx].accel = p_accel;

	update();
}

void PopupMenu::set_item_metadata(int p_idx, const Variant &p_meta) {

	ERR_FAIL_INDEX(p_idx, items.size());
	items[p_idx].metadata = p_meta;
	update();
}

void PopupMenu::set_item_disabled(int p_idx, bool p_disabled) {

	ERR_FAIL_INDEX(p_idx, items.size());
	items[p_idx].disabled = p_disabled;
	update();
}

void PopupMenu::set_item_submenu(int p_idx, const String &p_submenu) {

	ERR_FAIL_INDEX(p_idx, items.size());
	items[p_idx].submenu = p_submenu;
	update();
}

void PopupMenu::toggle_item_checked(int p_idx) {

	ERR_FAIL_INDEX(p_idx, items.size());
	items[p_idx].checked = !items[p_idx].checked;
	update();
}

String PopupMenu::get_item_text(int p_idx) const {

	ERR_FAIL_INDEX_V(p_idx, items.size(), "");
	return items[p_idx].text;
}

int PopupMenu::get_item_idx_from_text(const String &text) const {

	for (int idx = 0; idx < items.size(); idx++) {
		if (items[idx].text == text)
			return idx;
	}

	return -1;
}

Ref<Texture> PopupMenu::get_item_icon(int p_idx) const {

	ERR_FAIL_INDEX_V(p_idx, items.size(), Ref<Texture>());
	return items[p_idx].icon;
}

uint32_t PopupMenu::get_item_accelerator(int p_idx) const {

	ERR_FAIL_INDEX_V(p_idx, items.size(), 0);
	return items[p_idx].accel;
}

Variant PopupMenu::get_item_metadata(int p_idx) const {

	ERR_FAIL_INDEX_V(p_idx, items.size(), Variant());
	return items[p_idx].metadata;
}

bool PopupMenu::is_item_disabled(int p_idx) const {

	ERR_FAIL_INDEX_V(p_idx, items.size(), false);
	return items[p_idx].disabled;
}

bool PopupMenu::is_item_checked(int p_idx) const {

	ERR_FAIL_INDEX_V(p_idx, items.size(), false);
	return items[p_idx].checked;
}

int PopupMenu::get_item_id(int p_idx) const {

	ERR_FAIL_INDEX_V(p_idx, items.size(), 0);
	return items[p_idx].ID;
}

int PopupMenu::get_item_index(int p_ID) const {

	for (int i = 0; i < items.size(); i++) {

		if (items[i].ID == p_ID)
			return i;
	}

	return -1;
}

String PopupMenu::get_item_submenu(int p_idx) const {

	ERR_FAIL_INDEX_V(p_idx, items.size(), "");
	return items[p_idx].submenu;
}

String PopupMenu::get_item_tooltip(int p_idx) const {

	ERR_FAIL_INDEX_V(p_idx, items.size(), "");
	return items[p_idx].tooltip;
}

Ref<ShortCut> PopupMenu::get_item_shortcut(int p_idx) const {

	ERR_FAIL_INDEX_V(p_idx, items.size(), Ref<ShortCut>());
	return items[p_idx].shortcut;
}

int PopupMenu::get_item_state(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), -1);
	return items[p_idx].state;
}

void PopupMenu::set_item_as_separator(int p_idx, bool p_separator) {

	ERR_FAIL_INDEX(p_idx, items.size());
	items[p_idx].separator = p_separator;
	update();
}

bool PopupMenu::is_item_separator(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), false);
	return items[p_idx].separator;
}

void PopupMenu::set_item_as_checkable(int p_idx, bool p_checkable) {

	ERR_FAIL_INDEX(p_idx, items.size());
	items[p_idx].checkable_type = p_checkable ? Item::CHECKABLE_TYPE_CHECK_BOX : Item::CHECKABLE_TYPE_NONE;
	update();
}

void PopupMenu::set_item_as_radio_checkable(int p_idx, bool p_radio_checkable) {

	ERR_FAIL_INDEX(p_idx, items.size());
	items[p_idx].checkable_type = p_radio_checkable ? Item::CHECKABLE_TYPE_RADIO_BUTTON : Item::CHECKABLE_TYPE_NONE;
	update();
}

void PopupMenu::set_item_tooltip(int p_idx, const String &p_tooltip) {

	ERR_FAIL_INDEX(p_idx, items.size());
	items[p_idx].tooltip = p_tooltip;
	update();
}

void PopupMenu::set_item_shortcut(int p_idx, const Ref<ShortCut> &p_shortcut, bool p_global) {
	ERR_FAIL_INDEX(p_idx, items.size());
	if (items[p_idx].shortcut.is_valid()) {
		_unref_shortcut(items[p_idx].shortcut);
	}
	items[p_idx].shortcut = p_shortcut;
	items[p_idx].shortcut_is_global = p_global;

	if (items[p_idx].shortcut.is_valid()) {
		_ref_shortcut(items[p_idx].shortcut);
	}

	update();
}

void PopupMenu::set_item_h_offset(int p_idx, int p_offset) {

	ERR_FAIL_INDEX(p_idx, items.size());
	items[p_idx].h_ofs = p_offset;
	update();
}

void PopupMenu::set_item_multistate(int p_idx, int p_state) {

	ERR_FAIL_INDEX(p_idx, items.size());
	items[p_idx].state = p_state;
	update();
}

void PopupMenu::toggle_item_multistate(int p_idx) {

	ERR_FAIL_INDEX(p_idx, items.size());
	if (0 >= items[p_idx].max_states) {
		return;
	}

	++items[p_idx].state;
	if (items[p_idx].max_states <= items[p_idx].state)
		items[p_idx].state = 0;

	update();
}

bool PopupMenu::is_item_checkable(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), false);
	return items[p_idx].checkable_type;
}

bool PopupMenu::is_item_radio_checkable(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), false);
	return items[p_idx].checkable_type == Item::CHECKABLE_TYPE_RADIO_BUTTON;
}

int PopupMenu::get_item_count() const {

	return items.size();
}

bool PopupMenu::activate_item_by_event(const Ref<InputEvent> &p_event, bool p_for_global_only) {

	uint32_t code = 0;
	Ref<InputEventKey> k = p_event;

	if (k.is_valid()) {
		code = k->get_scancode();
		if (code == 0)
			code = k->get_unicode();
		if (k->get_control())
			code |= KEY_MASK_CTRL;
		if (k->get_alt())
			code |= KEY_MASK_ALT;
		if (k->get_metakey())
			code |= KEY_MASK_META;
		if (k->get_shift())
			code |= KEY_MASK_SHIFT;
	}

	int il = items.size();
	for (int i = 0; i < il; i++) {
		if (is_item_disabled(i))
			continue;

		if (items[i].shortcut.is_valid() && items[i].shortcut->is_shortcut(p_event) && (items[i].shortcut_is_global || !p_for_global_only)) {
			activate_item(i);
			return true;
		}

		if (code != 0 && items[i].accel == code) {
			activate_item(i);
			return true;
		}

		if (items[i].submenu != "") {
			Node *n = get_node(items[i].submenu);
			if (!n)
				continue;

			PopupMenu *pm = Object::cast_to<PopupMenu>(n);
			if (!pm)
				continue;

			if (pm->activate_item_by_event(p_event, p_for_global_only)) {
				return true;
			}
		}
	}
	return false;
}

void PopupMenu::activate_item(int p_item) {

	ERR_FAIL_INDEX(p_item, items.size());
	ERR_FAIL_COND(items[p_item].separator);
	int id = items[p_item].ID >= 0 ? items[p_item].ID : p_item;

	//hide all parent PopupMenus
	Node *next = get_parent();
	PopupMenu *pop = Object::cast_to<PopupMenu>(next);
	while (pop) {
		// We close all parents that are chained together,
		// with hide_on_item_selection enabled

		if (items[p_item].checkable_type) {
			if (!hide_on_checkable_item_selection || !pop->is_hide_on_checkable_item_selection())
				break;
		} else if (0 < items[p_item].max_states) {
			if (!hide_on_multistate_item_selection || !pop->is_hide_on_multistate_item_selection())
				break;
		} else if (!hide_on_item_selection || !pop->is_hide_on_item_selection())
			break;

		pop->hide();
		next = next->get_parent();
		pop = Object::cast_to<PopupMenu>(next);
	}

	// Hides popup by default; unless otherwise specified
	// by using set_hide_on_item_selection and set_hide_on_checkable_item_selection

	bool need_hide = true;

	if (items[p_item].checkable_type) {
		if (!hide_on_checkable_item_selection)
			need_hide = false;
	} else if (0 < items[p_item].max_states) {
		if (!hide_on_multistate_item_selection)
			need_hide = false;
	} else if (!hide_on_item_selection)
		need_hide = false;

	emit_signal("id_pressed", id);
	emit_signal("index_pressed", p_item);

	if (need_hide) {
		hide();
	}
}

void PopupMenu::remove_item(int p_idx) {

	ERR_FAIL_INDEX(p_idx, items.size());

	if (items[p_idx].shortcut.is_valid()) {
		_unref_shortcut(items[p_idx].shortcut);
	}

	items.remove(p_idx);
	update();
	minimum_size_changed();
}

void PopupMenu::add_separator() {

	Item sep;
	sep.separator = true;
	sep.ID = -1;
	items.push_back(sep);
	update();
}

void PopupMenu::clear() {

	for (int i = 0; i < items.size(); i++) {
		if (items[i].shortcut.is_valid()) {
			_unref_shortcut(items[i].shortcut);
		}
	}
	items.clear();
	mouse_over = -1;
	update();
}

Array PopupMenu::_get_items() const {

	Array items;
	for (int i = 0; i < get_item_count(); i++) {

		items.push_back(get_item_text(i));
		items.push_back(get_item_icon(i));
		// For compatibility, use false/true for no/checkbox and integers for other values
		int ct = this->items[i].checkable_type;
		items.push_back(Variant(ct <= Item::CHECKABLE_TYPE_CHECK_BOX ? is_item_checkable(i) : ct));
		items.push_back(is_item_checked(i));
		items.push_back(is_item_disabled(i));

		items.push_back(get_item_id(i));
		items.push_back(get_item_accelerator(i));
		items.push_back(get_item_metadata(i));
		items.push_back(get_item_submenu(i));
		items.push_back(is_item_separator(i));
	}

	return items;
}

void PopupMenu::_ref_shortcut(Ref<ShortCut> p_sc) {

	if (!shortcut_refcount.has(p_sc)) {
		shortcut_refcount[p_sc] = 1;
		p_sc->connect("changed", this, "update");
	} else {
		shortcut_refcount[p_sc] += 1;
	}
}

void PopupMenu::_unref_shortcut(Ref<ShortCut> p_sc) {

	ERR_FAIL_COND(!shortcut_refcount.has(p_sc));
	shortcut_refcount[p_sc]--;
	if (shortcut_refcount[p_sc] == 0) {
		p_sc->disconnect("changed", this, "update");
		shortcut_refcount.erase(p_sc);
	}
}

void PopupMenu::_set_items(const Array &p_items) {

	ERR_FAIL_COND(p_items.size() % 10);
	clear();

	for (int i = 0; i < p_items.size(); i += 10) {

		String text = p_items[i + 0];
		Ref<Texture> icon = p_items[i + 1];
		// For compatibility, use false/true for no/checkbox and integers for other values
		bool checkable = p_items[i + 2];
		bool radio_checkable = (int)p_items[i + 2] == Item::CHECKABLE_TYPE_RADIO_BUTTON;
		bool checked = p_items[i + 3];
		bool disabled = p_items[i + 4];

		int id = p_items[i + 5];
		int accel = p_items[i + 6];
		Variant meta = p_items[i + 7];
		String subm = p_items[i + 8];
		bool sep = p_items[i + 9];

		int idx = get_item_count();
		add_item(text, id);
		set_item_icon(idx, icon);
		if (checkable) {
			if (radio_checkable) {
				set_item_as_radio_checkable(idx, true);
			} else {
				set_item_as_checkable(idx, true);
			}
		}
		set_item_checked(idx, checked);
		set_item_disabled(idx, disabled);
		set_item_id(idx, id);
		set_item_metadata(idx, meta);
		set_item_as_separator(idx, sep);
		set_item_accelerator(idx, accel);
		set_item_submenu(idx, subm);
	}
}

// Hide on item selection determines whether or not the popup will close after item selection
void PopupMenu::set_hide_on_item_selection(bool p_enabled) {

	hide_on_item_selection = p_enabled;
}

bool PopupMenu::is_hide_on_item_selection() const {

	return hide_on_item_selection;
}

void PopupMenu::set_hide_on_checkable_item_selection(bool p_enabled) {

	hide_on_checkable_item_selection = p_enabled;
}

bool PopupMenu::is_hide_on_checkable_item_selection() const {

	return hide_on_checkable_item_selection;
}

void PopupMenu::set_hide_on_multistate_item_selection(bool p_enabled) {

	hide_on_multistate_item_selection = p_enabled;
}

bool PopupMenu::is_hide_on_multistate_item_selection() const {

	return hide_on_multistate_item_selection;
}

String PopupMenu::get_tooltip(const Point2 &p_pos) const {

	int over = _get_mouse_over(p_pos);
	if (over < 0 || over >= items.size())
		return "";
	return items[over].tooltip;
}

void PopupMenu::set_parent_rect(const Rect2 &p_rect) {

	parent_rect = p_rect;
}

void PopupMenu::get_translatable_strings(List<String> *p_strings) const {

	for (int i = 0; i < items.size(); i++) {

		if (items[i].xl_text != "")
			p_strings->push_back(items[i].xl_text);
	}
}

void PopupMenu::add_autohide_area(const Rect2 &p_area) {

	autohide_areas.push_back(p_area);
}

void PopupMenu::clear_autohide_areas() {

	autohide_areas.clear();
}

void PopupMenu::_bind_methods() {

	ClassDB::bind_method(D_METHOD("_gui_input"), &PopupMenu::_gui_input);
	ClassDB::bind_method(D_METHOD("add_icon_item", "texture", "label", "id", "accel"), &PopupMenu::add_icon_item, DEFVAL(-1), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("add_item", "label", "id", "accel"), &PopupMenu::add_item, DEFVAL(-1), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("add_icon_check_item", "texture", "label", "id", "accel"), &PopupMenu::add_icon_check_item, DEFVAL(-1), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("add_check_item", "label", "id", "accel"), &PopupMenu::add_check_item, DEFVAL(-1), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("add_radio_check_item", "label", "id", "accel"), &PopupMenu::add_radio_check_item, DEFVAL(-1), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("add_submenu_item", "label", "submenu", "id"), &PopupMenu::add_submenu_item, DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("add_icon_shortcut", "texture", "shortcut", "id", "global"), &PopupMenu::add_icon_shortcut, DEFVAL(-1), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("add_shortcut", "shortcut", "id", "global"), &PopupMenu::add_shortcut, DEFVAL(-1), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("add_icon_check_shortcut", "texture", "shortcut", "id", "global"), &PopupMenu::add_icon_check_shortcut, DEFVAL(-1), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("add_check_shortcut", "shortcut", "id", "global"), &PopupMenu::add_check_shortcut, DEFVAL(-1), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("add_radio_check_shortcut", "shortcut", "id", "global"), &PopupMenu::add_radio_check_shortcut, DEFVAL(-1), DEFVAL(false));

	ClassDB::bind_method(D_METHOD("set_item_text", "idx", "text"), &PopupMenu::set_item_text);
	ClassDB::bind_method(D_METHOD("set_item_icon", "idx", "icon"), &PopupMenu::set_item_icon);
	ClassDB::bind_method(D_METHOD("set_item_checked", "idx", "checked"), &PopupMenu::set_item_checked);
	ClassDB::bind_method(D_METHOD("set_item_id", "idx", "id"), &PopupMenu::set_item_id);
	ClassDB::bind_method(D_METHOD("set_item_accelerator", "idx", "accel"), &PopupMenu::set_item_accelerator);
	ClassDB::bind_method(D_METHOD("set_item_metadata", "idx", "metadata"), &PopupMenu::set_item_metadata);
	ClassDB::bind_method(D_METHOD("set_item_disabled", "idx", "disabled"), &PopupMenu::set_item_disabled);
	ClassDB::bind_method(D_METHOD("set_item_submenu", "idx", "submenu"), &PopupMenu::set_item_submenu);
	ClassDB::bind_method(D_METHOD("set_item_as_separator", "idx", "enable"), &PopupMenu::set_item_as_separator);
	ClassDB::bind_method(D_METHOD("set_item_as_checkable", "idx", "enable"), &PopupMenu::set_item_as_checkable);
	ClassDB::bind_method(D_METHOD("set_item_as_radio_checkable", "idx", "enable"), &PopupMenu::set_item_as_radio_checkable);
	ClassDB::bind_method(D_METHOD("set_item_tooltip", "idx", "tooltip"), &PopupMenu::set_item_tooltip);
	ClassDB::bind_method(D_METHOD("set_item_shortcut", "idx", "shortcut", "global"), &PopupMenu::set_item_shortcut, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("set_item_multistate", "idx", "state"), &PopupMenu::set_item_multistate);

	ClassDB::bind_method(D_METHOD("toggle_item_checked", "idx"), &PopupMenu::toggle_item_checked);
	ClassDB::bind_method(D_METHOD("toggle_item_multistate", "idx"), &PopupMenu::toggle_item_multistate);

	ClassDB::bind_method(D_METHOD("get_item_text", "idx"), &PopupMenu::get_item_text);
	ClassDB::bind_method(D_METHOD("get_item_icon", "idx"), &PopupMenu::get_item_icon);
	ClassDB::bind_method(D_METHOD("is_item_checked", "idx"), &PopupMenu::is_item_checked);
	ClassDB::bind_method(D_METHOD("get_item_id", "idx"), &PopupMenu::get_item_id);
	ClassDB::bind_method(D_METHOD("get_item_index", "id"), &PopupMenu::get_item_index);
	ClassDB::bind_method(D_METHOD("get_item_accelerator", "idx"), &PopupMenu::get_item_accelerator);
	ClassDB::bind_method(D_METHOD("get_item_metadata", "idx"), &PopupMenu::get_item_metadata);
	ClassDB::bind_method(D_METHOD("is_item_disabled", "idx"), &PopupMenu::is_item_disabled);
	ClassDB::bind_method(D_METHOD("get_item_submenu", "idx"), &PopupMenu::get_item_submenu);
	ClassDB::bind_method(D_METHOD("is_item_separator", "idx"), &PopupMenu::is_item_separator);
	ClassDB::bind_method(D_METHOD("is_item_checkable", "idx"), &PopupMenu::is_item_checkable);
	ClassDB::bind_method(D_METHOD("is_item_radio_checkable", "idx"), &PopupMenu::is_item_radio_checkable);
	ClassDB::bind_method(D_METHOD("get_item_tooltip", "idx"), &PopupMenu::get_item_tooltip);
	ClassDB::bind_method(D_METHOD("get_item_shortcut", "idx"), &PopupMenu::get_item_shortcut);

	ClassDB::bind_method(D_METHOD("get_item_count"), &PopupMenu::get_item_count);

	ClassDB::bind_method(D_METHOD("remove_item", "idx"), &PopupMenu::remove_item);

	ClassDB::bind_method(D_METHOD("add_separator"), &PopupMenu::add_separator);
	ClassDB::bind_method(D_METHOD("clear"), &PopupMenu::clear);

	ClassDB::bind_method(D_METHOD("_set_items"), &PopupMenu::_set_items);
	ClassDB::bind_method(D_METHOD("_get_items"), &PopupMenu::_get_items);

	ClassDB::bind_method(D_METHOD("set_hide_on_item_selection", "enable"), &PopupMenu::set_hide_on_item_selection);
	ClassDB::bind_method(D_METHOD("is_hide_on_item_selection"), &PopupMenu::is_hide_on_item_selection);

	ClassDB::bind_method(D_METHOD("set_hide_on_checkable_item_selection", "enable"), &PopupMenu::set_hide_on_checkable_item_selection);
	ClassDB::bind_method(D_METHOD("is_hide_on_checkable_item_selection"), &PopupMenu::is_hide_on_checkable_item_selection);

	ClassDB::bind_method(D_METHOD("set_hide_on_state_item_selection", "enable"), &PopupMenu::set_hide_on_multistate_item_selection);
	ClassDB::bind_method(D_METHOD("is_hide_on_state_item_selection"), &PopupMenu::is_hide_on_multistate_item_selection);

	ClassDB::bind_method(D_METHOD("_submenu_timeout"), &PopupMenu::_submenu_timeout);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "items", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR | PROPERTY_USAGE_INTERNAL), "_set_items", "_get_items");
	ADD_PROPERTYNO(PropertyInfo(Variant::BOOL, "hide_on_item_selection"), "set_hide_on_item_selection", "is_hide_on_item_selection");
	ADD_PROPERTYNO(PropertyInfo(Variant::BOOL, "hide_on_checkable_item_selection"), "set_hide_on_checkable_item_selection", "is_hide_on_checkable_item_selection");
	ADD_PROPERTYNO(PropertyInfo(Variant::BOOL, "hide_on_state_item_selection"), "set_hide_on_state_item_selection", "is_hide_on_state_item_selection");

	ADD_SIGNAL(MethodInfo("id_pressed", PropertyInfo(Variant::INT, "ID")));
	ADD_SIGNAL(MethodInfo("index_pressed", PropertyInfo(Variant::INT, "index")));
}

void PopupMenu::set_invalidate_click_until_motion() {
	moved = Vector2();
	invalidated_click = true;
}

PopupMenu::PopupMenu() {

	mouse_over = -1;
	submenu_over = -1;

	set_focus_mode(FOCUS_ALL);
	set_as_toplevel(true);
	set_hide_on_item_selection(true);
	set_hide_on_checkable_item_selection(true);
	set_hide_on_multistate_item_selection(false);

	submenu_timer = memnew(Timer);
	submenu_timer->set_wait_time(0.3);
	submenu_timer->set_one_shot(true);
	submenu_timer->connect("timeout", this, "_submenu_timeout");
	add_child(submenu_timer);
}

PopupMenu::~PopupMenu() {
}
