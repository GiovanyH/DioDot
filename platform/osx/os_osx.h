/*************************************************************************/
/*  os_osx.h                                                             */
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

#ifndef OS_OSX_H
#define OS_OSX_H

#include "crash_handler_osx.h"
#include "drivers/coreaudio/audio_driver_coreaudio.h"
#include "drivers/unix/os_unix.h"
#include "joypad_osx.h"
#include "main/input_default.h"
#include "os/input.h"
#include "power_osx.h"
#include "servers/audio_server.h"
#include "servers/visual/rasterizer.h"
#include "servers/visual/visual_server_wrap_mt.h"
#include "servers/visual_server.h"
#include <AppKit/AppKit.h>
#include <AppKit/NSCursor.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreVideo/CoreVideo.h>

#undef CursorShape
/**
	@author Juan Linietsky <reduzio@gmail.com>
*/

class OS_OSX : public OS_Unix {
public:
	struct KeyEvent {
		unsigned int osx_state;
		bool pressed;
		bool echo;
		uint32_t scancode;
		uint32_t unicode;
	};

	Vector<KeyEvent> key_event_buffer;
	int key_event_pos;

	bool force_quit;
	//  rasterizer seems to no longer be given to visual server, its using GLES3 directly?
	//Rasterizer *rasterizer;
	VisualServer *visual_server;

	List<String> args;
	MainLoop *main_loop;

	IP_Unix *ip_unix;

	AudioDriverCoreAudio audio_driver;

	InputDefault *input;
	JoypadOSX *joypad_osx;

	/* objc */

	CGEventSourceRef eventSource;

	void process_events();
	void process_key_events();

	void *framework;
	//          pthread_key_t   current;
	bool mouse_grab;
	Point2 mouse_pos;

	id delegate;
	id window_delegate;
	id window_object;
	id window_view;
	id autoreleasePool;
	id cursor;
	NSOpenGLPixelFormat *pixelFormat;
	NSOpenGLContext *context;

	bool waiting_for_vsync;
	NSCondition *vsync_condition;
	CVDisplayLinkRef displayLink;

	CursorShape cursor_shape;
	NSCursor *cursors[CURSOR_MAX] = { NULL };
	MouseMode mouse_mode;

	String title;
	bool minimized;
	bool maximized;
	bool zoomed;

	Size2 window_size;
	Rect2 restore_rect;

	String open_with_filename;

	Point2 im_position;
	ImeCallback im_callback;
	void *im_target;

	power_osx *power_manager;

	CrashHandler crash_handler;

	float _mouse_scale(float p_scale) {
		if (_display_scale() > 1.0)
			return p_scale;
		else
			return 1.0;
	}

	float _display_scale() const;
	float _display_scale(id screen) const;

	void _update_window();

protected:
	virtual int get_video_driver_count() const;
	virtual const char *get_video_driver_name(int p_driver) const;

	virtual void initialize_core();
	virtual Error initialize(const VideoMode &p_desired, int p_video_driver, int p_audio_driver);
	virtual void finalize();

	virtual void set_main_loop(MainLoop *p_main_loop);
	virtual void delete_main_loop();

public:
	static OS_OSX *singleton;

	void wm_minimized(bool p_minimized);

	virtual String get_name();

	virtual void alert(const String &p_alert, const String &p_title = "ALERT!");

	virtual Error open_dynamic_library(const String p_path, void *&p_library_handle, bool p_also_set_library_path = false);

	virtual void set_cursor_shape(CursorShape p_shape);
	virtual void set_custom_mouse_cursor(const RES &p_cursor, CursorShape p_shape, const Vector2 &p_hotspot);

	virtual void set_mouse_show(bool p_show);
	virtual void set_mouse_grab(bool p_grab);
	virtual bool is_mouse_grab_enabled() const;
	virtual void warp_mouse_position(const Point2 &p_to);
	virtual Point2 get_mouse_position() const;
	virtual int get_mouse_button_state() const;
	virtual void set_window_title(const String &p_title);

	virtual Size2 get_window_size() const;
	virtual Size2 get_real_window_size() const;

	virtual void set_icon(const Ref<Image> &p_icon);

	virtual MainLoop *get_main_loop() const;

	virtual String get_config_path() const;
	virtual String get_data_path() const;
	virtual String get_cache_path() const;
	virtual String get_godot_dir_name() const;

	virtual String get_system_dir(SystemDir p_dir) const;

	virtual bool can_draw() const;

	virtual void set_clipboard(const String &p_text);
	virtual String get_clipboard() const;

	virtual void release_rendering_thread();
	virtual void make_rendering_thread();
	virtual void swap_buffers();

	Error shell_open(String p_uri);
	void push_input(const Ref<InputEvent> &p_event);

	String get_locale() const;

	virtual void set_video_mode(const VideoMode &p_video_mode, int p_screen = 0);
	virtual VideoMode get_video_mode(int p_screen = 0) const;
	virtual void get_fullscreen_mode_list(List<VideoMode> *p_list, int p_screen = 0) const;

	virtual String get_executable_path() const;

	virtual LatinKeyboardVariant get_latin_keyboard_variant() const;

	virtual void move_window_to_foreground();

	virtual int get_screen_count() const;
	virtual int get_current_screen() const;
	virtual void set_current_screen(int p_screen);
	virtual Point2 get_screen_position(int p_screen = -1) const;
	virtual Size2 get_screen_size(int p_screen = -1) const;
	virtual int get_screen_dpi(int p_screen = -1) const;

	virtual Point2 get_window_position() const;
	virtual void set_window_position(const Point2 &p_position);
	virtual void set_window_size(const Size2 p_size);
	virtual void set_window_fullscreen(bool p_enabled);
	virtual bool is_window_fullscreen() const;
	virtual void set_window_resizable(bool p_enabled);
	virtual bool is_window_resizable() const;
	virtual void set_window_minimized(bool p_enabled);
	virtual bool is_window_minimized() const;
	virtual void set_window_maximized(bool p_enabled);
	virtual bool is_window_maximized() const;
	virtual void set_window_always_on_top(bool p_enabled);
	virtual bool is_window_always_on_top() const;
	virtual void request_attention();
	virtual String get_joy_guid(int p_device) const;

	virtual void set_borderless_window(bool p_borderless);
	virtual bool get_borderless_window();
	virtual void set_ime_position(const Point2 &p_pos);
	virtual void set_ime_intermediate_text_callback(ImeCallback p_callback, void *p_inp);

	virtual OS::PowerState get_power_state();
	virtual int get_power_seconds_left();
	virtual int get_power_percent_left();

	virtual bool _check_internal_feature_support(const String &p_feature);

	virtual void _set_use_vsync(bool p_enable);
	//virtual bool is_vsync_enabled() const;

	void run();

	void set_mouse_mode(MouseMode p_mode);
	MouseMode get_mouse_mode() const;

	void disable_crash_handler();
	bool is_disable_crash_handler() const;

	virtual Error move_to_trash(const String &p_path);

	void force_process_input();

	OS_OSX();

private:
	Point2 get_native_screen_position(int p_screen) const;
	Point2 get_native_window_position() const;
	void set_native_window_position(const Point2 &p_position);
	Point2 get_screens_origin() const;
};

#endif
