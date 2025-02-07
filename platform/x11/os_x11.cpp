/*************************************************************************/
/*  os_x11.cpp                                                           */
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

#include "os_x11.h"
#include "drivers/gles3/rasterizer_gles3.h"
#include "errno.h"
#include "key_mapping_x11.h"
#include "os/dir_access.h"
#include "print_string.h"
#include "servers/visual/visual_server_raster.h"
#include "servers/visual/visual_server_wrap_mt.h"

#ifdef HAVE_MNTENT
#include <mntent.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "X11/Xutil.h"

#include "X11/Xatom.h"
#include "X11/extensions/Xinerama.h"
// ICCCM
#define WM_NormalState 1L // window normal state
#define WM_IconicState 3L // window minimized
// EWMH
#define _NET_WM_STATE_REMOVE 0L // remove/unset property
#define _NET_WM_STATE_ADD 1L // add/set property
#define _NET_WM_STATE_TOGGLE 2L // toggle property

#include "main/main.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

//stupid linux.h
#ifdef KEY_TAB
#undef KEY_TAB
#endif

#include <X11/Xatom.h>

#undef CursorShape

#include <X11/XKBlib.h>

int OS_X11::get_video_driver_count() const {
	return 1;
}

const char *OS_X11::get_video_driver_name(int p_driver) const {
	return "GLES3";
}

int OS_X11::get_audio_driver_count() const {
	return AudioDriverManager::get_driver_count();
}

const char *OS_X11::get_audio_driver_name(int p_driver) const {

	AudioDriver *driver = AudioDriverManager::get_driver(p_driver);
	ERR_FAIL_COND_V(!driver, "");
	return AudioDriverManager::get_driver(p_driver)->get_name();
}

void OS_X11::initialize_core() {

	crash_handler.initialize();

	OS_Unix::initialize_core();
}

Error OS_X11::initialize(const VideoMode &p_desired, int p_video_driver, int p_audio_driver) {

	long im_event_mask = 0;
	last_button_state = 0;

	xmbstring = NULL;
	x11_window = 0;
	last_click_ms = 0;
	args = OS::get_singleton()->get_cmdline_args();
	current_videomode = p_desired;
	main_loop = NULL;
	last_timestamp = 0;
	last_mouse_pos_valid = false;
	last_keyrelease_time = 0;
	xdnd_version = 0;

	if (get_render_thread_mode() == RENDER_SEPARATE_THREAD) {
		XInitThreads();
	}

	/** XLIB INITIALIZATION **/
	x11_display = XOpenDisplay(NULL);

	if (!x11_display) {
		ERR_PRINT("X11 Display is not available");
		return ERR_UNAVAILABLE;
	}

	char *modifiers = NULL;
	Bool xkb_dar = False;
	XAutoRepeatOn(x11_display);
	xkb_dar = XkbSetDetectableAutoRepeat(x11_display, True, NULL);

	// Try to support IME if detectable auto-repeat is supported
	if (xkb_dar == True) {

#ifdef X_HAVE_UTF8_STRING
		// Xutf8LookupString will be used later instead of XmbLookupString before
		// the multibyte sequences can be converted to unicode string.
		modifiers = XSetLocaleModifiers("");
#endif
	}

	if (modifiers == NULL) {
		if (is_stdout_verbose()) {
			WARN_PRINT("IME is disabled");
		}
		modifiers = XSetLocaleModifiers("@im=none");
		WARN_PRINT("Error setting locale modifiers");
	}

	const char *err;
	xrr_get_monitors = NULL;
	xrr_free_monitors = NULL;
	int xrandr_major = 0;
	int xrandr_minor = 0;
	int event_base, error_base;
	xrandr_ext_ok = XRRQueryExtension(x11_display, &event_base, &error_base);
	xrandr_handle = dlopen("libXrandr.so.2", RTLD_LAZY);
	if (!xrandr_handle) {
		err = dlerror();
		fprintf(stderr, "could not load libXrandr.so.2, Error: %s\n", err);
	} else {
		XRRQueryVersion(x11_display, &xrandr_major, &xrandr_minor);
		if (((xrandr_major << 8) | xrandr_minor) >= 0x0105) {
			xrr_get_monitors = (xrr_get_monitors_t)dlsym(xrandr_handle, "XRRGetMonitors");
			if (!xrr_get_monitors) {
				err = dlerror();
				fprintf(stderr, "could not find symbol XRRGetMonitors\nError: %s\n", err);
			} else {
				xrr_free_monitors = (xrr_free_monitors_t)dlsym(xrandr_handle, "XRRFreeMonitors");
				if (!xrr_free_monitors) {
					err = dlerror();
					fprintf(stderr, "could not find XRRFreeMonitors\nError: %s\n", err);
					xrr_get_monitors = NULL;
				}
			}
		}
	}

#ifdef TOUCH_ENABLED
	if (!XQueryExtension(x11_display, "XInputExtension", &touch.opcode, &event_base, &error_base)) {
		fprintf(stderr, "XInput extension not available");
	} else {
		// 2.2 is the first release with multitouch
		int xi_major = 2;
		int xi_minor = 2;
		if (XIQueryVersion(x11_display, &xi_major, &xi_minor) != Success) {
			fprintf(stderr, "XInput 2.2 not available (server supports %d.%d)\n", xi_major, xi_minor);
			touch.opcode = 0;
		} else {
			int dev_count;
			XIDeviceInfo *info = XIQueryDevice(x11_display, XIAllDevices, &dev_count);

			for (int i = 0; i < dev_count; i++) {
				XIDeviceInfo *dev = &info[i];
				if (!dev->enabled)
					continue;
				if (!(dev->use == XIMasterPointer || dev->use == XIFloatingSlave))
					continue;

				bool direct_touch = false;
				for (int j = 0; j < dev->num_classes; j++) {
					if (dev->classes[j]->type == XITouchClass && ((XITouchClassInfo *)dev->classes[j])->mode == XIDirectTouch) {
						direct_touch = true;
						break;
					}
				}
				if (direct_touch) {
					touch.devices.push_back(dev->deviceid);
					fprintf(stderr, "Using touch device: %s\n", dev->name);
				}
			}

			XIFreeDeviceInfo(info);

			if (is_stdout_verbose() && !touch.devices.size()) {
				fprintf(stderr, "No touch devices found\n");
			}
		}
	}
#endif

	xim = XOpenIM(x11_display, NULL, NULL, NULL);

	if (xim == NULL) {
		WARN_PRINT("XOpenIM failed");
		xim_style = 0L;
	} else {
		::XIMCallback im_destroy_callback;
		im_destroy_callback.client_data = (::XPointer)(this);
		im_destroy_callback.callback = (::XIMProc)(xim_destroy_callback);
		if (XSetIMValues(xim, XNDestroyCallback, &im_destroy_callback,
					NULL) != NULL) {
			WARN_PRINT("Error setting XIM destroy callback");
		}

		::XIMStyles *xim_styles = NULL;
		xim_style = 0L;
		char *imvalret = XGetIMValues(xim, XNQueryInputStyle, &xim_styles, NULL);
		if (imvalret != NULL || xim_styles == NULL) {
			fprintf(stderr, "Input method doesn't support any styles\n");
		}

		if (xim_styles) {
			xim_style = 0L;
			for (int i = 0; i < xim_styles->count_styles; i++) {

				if (xim_styles->supported_styles[i] ==
						(XIMPreeditNothing | XIMStatusNothing)) {

					xim_style = xim_styles->supported_styles[i];
					break;
				}
			}

			XFree(xim_styles);
		}
		XFree(imvalret);
	}

/*
	char* windowid = getenv("GODOT_WINDOWID");
	if (windowid) {

		//freopen("/home/punto/stdout", "w", stdout);
		//reopen("/home/punto/stderr", "w", stderr);
		x11_window = atol(windowid);

		XWindowAttributes xwa;
		XGetWindowAttributes(x11_display,x11_window,&xwa);

		current_videomode.width = xwa.width;
		current_videomode.height = xwa.height;
	};
	*/

// maybe contextgl wants to be in charge of creating the window
//print_line("def videomode "+itos(current_videomode.width)+","+itos(current_videomode.height));
#if defined(OPENGL_ENABLED)

	context_gl = memnew(ContextGL_X11(x11_display, x11_window, current_videomode, true));
	context_gl->initialize();

	RasterizerGLES3::register_config();

	RasterizerGLES3::make_current();

	context_gl->set_use_vsync(current_videomode.use_vsync);

#endif
	visual_server = memnew(VisualServerRaster);

	if (get_render_thread_mode() != RENDER_THREAD_UNSAFE) {

		visual_server = memnew(VisualServerWrapMT(visual_server, get_render_thread_mode() == RENDER_SEPARATE_THREAD));
	}
	if (current_videomode.maximized) {
		current_videomode.maximized = false;
		set_window_maximized(true);
		// borderless fullscreen window mode
	} else if (current_videomode.fullscreen) {
		current_videomode.fullscreen = false;
		set_window_fullscreen(true);
	} else if (current_videomode.borderless_window) {
		Hints hints;
		Atom property;
		hints.flags = 2;
		hints.decorations = 0;
		property = XInternAtom(x11_display, "_MOTIF_WM_HINTS", True);
		XChangeProperty(x11_display, x11_window, property, property, 32, PropModeReplace, (unsigned char *)&hints, 5);
	}

	// disable resizable window
	if (!current_videomode.resizable && !current_videomode.fullscreen) {
		XSizeHints *xsh;
		xsh = XAllocSizeHints();
		xsh->flags = PMinSize | PMaxSize;
		XWindowAttributes xwa;
		if (current_videomode.fullscreen) {
			XGetWindowAttributes(x11_display, DefaultRootWindow(x11_display), &xwa);
		} else {
			XGetWindowAttributes(x11_display, x11_window, &xwa);
		}
		xsh->min_width = xwa.width;
		xsh->max_width = xwa.width;
		xsh->min_height = xwa.height;
		xsh->max_height = xwa.height;
		XSetWMNormalHints(x11_display, x11_window, xsh);
		XFree(xsh);
	}

	if (current_videomode.always_on_top) {
		current_videomode.always_on_top = false;
		set_window_always_on_top(true);
	}

	AudioDriverManager::initialize(p_audio_driver);

	ERR_FAIL_COND_V(!visual_server, ERR_UNAVAILABLE);
	ERR_FAIL_COND_V(x11_window == 0, ERR_UNAVAILABLE);

	XSetWindowAttributes new_attr;

	new_attr.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask |
						  ButtonReleaseMask | EnterWindowMask |
						  LeaveWindowMask | PointerMotionMask |
						  Button1MotionMask |
						  Button2MotionMask | Button3MotionMask |
						  Button4MotionMask | Button5MotionMask |
						  ButtonMotionMask | KeymapStateMask |
						  ExposureMask | VisibilityChangeMask |
						  StructureNotifyMask |
						  SubstructureNotifyMask | SubstructureRedirectMask |
						  FocusChangeMask | PropertyChangeMask |
						  ColormapChangeMask | OwnerGrabButtonMask |
						  im_event_mask;

	XChangeWindowAttributes(x11_display, x11_window, CWEventMask, &new_attr);

#ifdef TOUCH_ENABLED
	if (touch.devices.size()) {

		// Must be alive after this block
		static unsigned char mask_data[XIMaskLen(XI_LASTEVENT)] = {};

		touch.event_mask.deviceid = XIAllDevices;
		touch.event_mask.mask_len = sizeof(mask_data);
		touch.event_mask.mask = mask_data;

		XISetMask(touch.event_mask.mask, XI_TouchBegin);
		XISetMask(touch.event_mask.mask, XI_TouchUpdate);
		XISetMask(touch.event_mask.mask, XI_TouchEnd);
		XISetMask(touch.event_mask.mask, XI_TouchOwnership);

		XISelectEvents(x11_display, x11_window, &touch.event_mask, 1);

		// Disabled by now since grabbing also blocks mouse events
		// (they are received as extended events instead of standard events)
		/*XIClearMask(touch.event_mask.mask, XI_TouchOwnership);

		// Grab touch devices to avoid OS gesture interference
		for (int i = 0; i < touch.devices.size(); ++i) {
			XIGrabDevice(x11_display, touch.devices[i], x11_window, CurrentTime, None, XIGrabModeAsync, XIGrabModeAsync, False, &touch.event_mask);
		}*/
	}
#endif

	/* set the titlebar name */
	XStoreName(x11_display, x11_window, "Godot");

	wm_delete = XInternAtom(x11_display, "WM_DELETE_WINDOW", true);
	XSetWMProtocols(x11_display, x11_window, &wm_delete, 1);

	if (xim && xim_style) {

		xic = XCreateIC(xim, XNInputStyle, xim_style, XNClientWindow, x11_window, XNFocusWindow, x11_window, (char *)NULL);
		if (XGetICValues(xic, XNFilterEvents, &im_event_mask, NULL) != NULL) {
			WARN_PRINT("XGetICValues couldn't obtain XNFilterEvents value");
			XDestroyIC(xic);
			xic = NULL;
		}
		if (xic) {
			XSetICFocus(xic);
		} else {
			WARN_PRINT("XCreateIC couldn't create xic");
		}
	} else {

		xic = NULL;
		WARN_PRINT("XCreateIC couldn't create xic");
	}

	cursor_size = XcursorGetDefaultSize(x11_display);
	cursor_theme = XcursorGetTheme(x11_display);

	if (!cursor_theme) {
		if (is_stdout_verbose()) {
			print_line("XcursorGetTheme could not get cursor theme");
		}
		cursor_theme = "default";
	}

	for (int i = 0; i < CURSOR_MAX; i++) {

		cursors[i] = None;
		img[i] = NULL;
	}

	current_cursor = CURSOR_ARROW;

	if (cursor_theme) {
		//print_line("cursor theme: "+String(cursor_theme));
		for (int i = 0; i < CURSOR_MAX; i++) {

			static const char *cursor_file[] = {
				"left_ptr",
				"xterm",
				"hand2",
				"cross",
				"watch",
				"left_ptr_watch",
				"fleur",
				"hand1",
				"X_cursor",
				"sb_v_double_arrow",
				"sb_h_double_arrow",
				"size_bdiag",
				"size_fdiag",
				"hand1",
				"sb_v_double_arrow",
				"sb_h_double_arrow",
				"question_arrow"
			};

			img[i] = XcursorLibraryLoadImage(cursor_file[i], cursor_theme, cursor_size);
			if (img[i]) {
				cursors[i] = XcursorImageLoadCursor(x11_display, img[i]);
				//print_line("found cursor: "+String(cursor_file[i])+" id "+itos(cursors[i]));
			} else {
				if (OS::is_stdout_verbose())
					print_line("failed cursor: " + String(cursor_file[i]));
			}
		}
	}

	{
		Pixmap cursormask;
		XGCValues xgc;
		GC gc;
		XColor col;
		Cursor cursor;

		cursormask = XCreatePixmap(x11_display, RootWindow(x11_display, DefaultScreen(x11_display)), 1, 1, 1);
		xgc.function = GXclear;
		gc = XCreateGC(x11_display, cursormask, GCFunction, &xgc);
		XFillRectangle(x11_display, cursormask, gc, 0, 0, 1, 1);
		col.pixel = 0;
		col.red = 0;
		col.flags = 4;
		cursor = XCreatePixmapCursor(x11_display,
				cursormask, cursormask,
				&col, &col, 0, 0);
		XFreePixmap(x11_display, cursormask);
		XFreeGC(x11_display, gc);

		if (cursor == None) {
			ERR_PRINT("FAILED CREATING CURSOR");
		}

		null_cursor = cursor;
	}
	set_cursor_shape(CURSOR_BUSY);

	//Set Xdnd (drag & drop) support
	Atom XdndAware = XInternAtom(x11_display, "XdndAware", False);
	Atom version = 5;
	XChangeProperty(x11_display, x11_window, XdndAware, XA_ATOM, 32, PropModeReplace, (unsigned char *)&version, 1);

	xdnd_enter = XInternAtom(x11_display, "XdndEnter", False);
	xdnd_position = XInternAtom(x11_display, "XdndPosition", False);
	xdnd_status = XInternAtom(x11_display, "XdndStatus", False);
	xdnd_action_copy = XInternAtom(x11_display, "XdndActionCopy", False);
	xdnd_drop = XInternAtom(x11_display, "XdndDrop", False);
	xdnd_finished = XInternAtom(x11_display, "XdndFinished", False);
	xdnd_selection = XInternAtom(x11_display, "XdndSelection", False);
	requested = None;

	visual_server->init();

	input = memnew(InputDefault);

	window_has_focus = true; // Set focus to true at init
#ifdef JOYDEV_ENABLED
	joypad = memnew(JoypadLinux(input));
#endif
	_ensure_user_data_dir();

	power_manager = memnew(PowerX11);

	XEvent xevent;
	while (XPending(x11_display) > 0) {
		XNextEvent(x11_display, &xevent);
		if (xevent.type == ConfigureNotify) {
			_window_changed(&xevent);
		}
	}

	return OK;
}

void OS_X11::xim_destroy_callback(::XIM im, ::XPointer client_data,
		::XPointer call_data) {

	WARN_PRINT("Input method stopped");
	OS_X11 *os = reinterpret_cast<OS_X11 *>(client_data);
	os->xim = NULL;
	os->xic = NULL;
}

void OS_X11::set_ime_position(const Point2 &p_pos) {

	if (!xic)
		return;

	::XPoint spot;
	spot.x = short(p_pos.x);
	spot.y = short(p_pos.y);
	XVaNestedList preedit_attr = XVaCreateNestedList(0, XNSpotLocation, &spot, NULL);
	XSetICValues(xic, XNPreeditAttributes, preedit_attr, NULL);
	XFree(preedit_attr);
}

void OS_X11::finalize() {

	if (main_loop)
		memdelete(main_loop);
	main_loop = NULL;

	/*
	if (debugger_connection_console) {
		memdelete(debugger_connection_console);
	}
	*/

#ifdef JOYDEV_ENABLED
	memdelete(joypad);
#endif
#ifdef TOUCH_ENABLED
	touch.devices.clear();
	touch.state.clear();
#endif
	memdelete(input);

	visual_server->finish();
	memdelete(visual_server);
	//memdelete(rasterizer);

	memdelete(power_manager);

	if (xrandr_handle)
		dlclose(xrandr_handle);

	XUnmapWindow(x11_display, x11_window);
	XDestroyWindow(x11_display, x11_window);

#if defined(OPENGL_ENABLED)
	memdelete(context_gl);
#endif
	for (int i = 0; i < CURSOR_MAX; i++) {
		if (cursors[i] != None)
			XFreeCursor(x11_display, cursors[i]);
		if (img[i] != NULL)
			XcursorImageDestroy(img[i]);
	};

	if (xic) {
		XDestroyIC(xic);
	}
	if (xim) {
		XCloseIM(xim);
	}

	XCloseDisplay(x11_display);
	if (xmbstring)
		memfree(xmbstring);

	args.clear();
}

void OS_X11::set_mouse_mode(MouseMode p_mode) {

	if (p_mode == mouse_mode)
		return;

	if (mouse_mode == MOUSE_MODE_CAPTURED || mouse_mode == MOUSE_MODE_CONFINED)
		XUngrabPointer(x11_display, CurrentTime);

	// The only modes that show a cursor are VISIBLE and CONFINED
	bool showCursor = (p_mode == MOUSE_MODE_VISIBLE || p_mode == MOUSE_MODE_CONFINED);

	if (showCursor) {
		XUndefineCursor(x11_display, x11_window); // show cursor
	} else {
		XDefineCursor(x11_display, x11_window, null_cursor); // hide cursor
	}

	mouse_mode = p_mode;

	if (mouse_mode == MOUSE_MODE_CAPTURED || mouse_mode == MOUSE_MODE_CONFINED) {

		while (true) {
			//flush pending motion events

			if (XPending(x11_display) > 0) {
				XEvent event;
				XPeekEvent(x11_display, &event);
				if (event.type == MotionNotify) {
					XNextEvent(x11_display, &event);
				} else {
					break;
				}
			} else {
				break;
			}
		}

		if (XGrabPointer(
					x11_display, x11_window, True,
					ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
					GrabModeAsync, GrabModeAsync, x11_window, None, CurrentTime) != GrabSuccess) {
			ERR_PRINT("NO GRAB");
		}

		center.x = current_videomode.width / 2;
		center.y = current_videomode.height / 2;
		XWarpPointer(x11_display, None, x11_window,
				0, 0, 0, 0, (int)center.x, (int)center.y);

		input->set_mouse_position(center);
	} else {
		do_mouse_warp = false;
	}

	XFlush(x11_display);
}

void OS_X11::warp_mouse_position(const Point2 &p_to) {

	if (mouse_mode == MOUSE_MODE_CAPTURED) {

		last_mouse_pos = p_to;
	} else {

		/*XWindowAttributes xwa;
		XGetWindowAttributes(x11_display, x11_window, &xwa);
		printf("%d %d\n", xwa.x, xwa.y); needed? */

		XWarpPointer(x11_display, None, x11_window,
				0, 0, 0, 0, (int)p_to.x, (int)p_to.y);
	}
}

OS::MouseMode OS_X11::get_mouse_mode() const {
	return mouse_mode;
}

int OS_X11::get_mouse_button_state() const {
	return last_button_state;
}

Point2 OS_X11::get_mouse_position() const {
	return last_mouse_pos;
}

void OS_X11::set_window_title(const String &p_title) {
	XStoreName(x11_display, x11_window, p_title.utf8().get_data());

	Atom _net_wm_name = XInternAtom(x11_display, "_NET_WM_NAME", false);
	Atom utf8_string = XInternAtom(x11_display, "UTF8_STRING", false);
	XChangeProperty(x11_display, x11_window, _net_wm_name, utf8_string, 8, PropModeReplace, (unsigned char *)p_title.utf8().get_data(), p_title.utf8().length());
}

void OS_X11::set_video_mode(const VideoMode &p_video_mode, int p_screen) {
}

OS::VideoMode OS_X11::get_video_mode(int p_screen) const {
	return current_videomode;
}

void OS_X11::get_fullscreen_mode_list(List<VideoMode> *p_list, int p_screen) const {
}

void OS_X11::set_wm_fullscreen(bool p_enabled) {
	if (p_enabled && !get_borderless_window()) {
		// remove decorations if the window is not already borderless
		Hints hints;
		Atom property;
		hints.flags = 2;
		hints.decorations = 0;
		property = XInternAtom(x11_display, "_MOTIF_WM_HINTS", True);
		XChangeProperty(x11_display, x11_window, property, property, 32, PropModeReplace, (unsigned char *)&hints, 5);
	}

	if (p_enabled && !is_window_resizable()) {
		// Set the window as resizable to prevent window managers to ignore the fullscreen state flag.
		XSizeHints *xsh;

		xsh = XAllocSizeHints();
		xsh->flags = 0L;
		XSetWMNormalHints(x11_display, x11_window, xsh);
		XFree(xsh);
	}

	// Using EWMH -- Extended Window Manager Hints
	XEvent xev;
	Atom wm_state = XInternAtom(x11_display, "_NET_WM_STATE", False);
	Atom wm_fullscreen = XInternAtom(x11_display, "_NET_WM_STATE_FULLSCREEN", False);

	memset(&xev, 0, sizeof(xev));
	xev.type = ClientMessage;
	xev.xclient.window = x11_window;
	xev.xclient.message_type = wm_state;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = p_enabled ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
	xev.xclient.data.l[1] = wm_fullscreen;
	xev.xclient.data.l[2] = 0;

	XSendEvent(x11_display, DefaultRootWindow(x11_display), False, SubstructureRedirectMask | SubstructureNotifyMask, &xev);

	// set bypass compositor hint
	Atom bypass_compositor = XInternAtom(x11_display, "_NET_WM_BYPASS_COMPOSITOR", False);
	unsigned long compositing_disable_on = p_enabled ? 1 : 0;
	XChangeProperty(x11_display, x11_window, bypass_compositor, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&compositing_disable_on, 1);

	XFlush(x11_display);

	if (!p_enabled && !is_window_resizable()) {
		// Reset the non-resizable flags if we un-set these before.
		Size2 size = get_window_size();
		XSizeHints *xsh;

		xsh = XAllocSizeHints();
		xsh->flags = PMinSize | PMaxSize;
		xsh->min_width = size.x;
		xsh->max_width = size.x;
		xsh->min_height = size.y;
		xsh->max_height = size.y;

		XSetWMNormalHints(x11_display, x11_window, xsh);
		XFree(xsh);
	}

	if (!p_enabled && !get_borderless_window()) {
		// put decorations back if the window wasn't suppoesed to be borderless
		Hints hints;
		Atom property;
		hints.flags = 2;
		hints.decorations = 1;
		property = XInternAtom(x11_display, "_MOTIF_WM_HINTS", True);
		XChangeProperty(x11_display, x11_window, property, property, 32, PropModeReplace, (unsigned char *)&hints, 5);
	}
}

void OS_X11::set_wm_above(bool p_enabled) {
	Atom wm_state = XInternAtom(x11_display, "_NET_WM_STATE", False);
	Atom wm_above = XInternAtom(x11_display, "_NET_WM_STATE_ABOVE", False);

	XClientMessageEvent xev;
	memset(&xev, 0, sizeof(xev));
	xev.type = ClientMessage;
	xev.window = x11_window;
	xev.message_type = wm_state;
	xev.format = 32;
	xev.data.l[0] = p_enabled ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
	xev.data.l[1] = wm_above;
	xev.data.l[3] = 1;
	XSendEvent(x11_display, DefaultRootWindow(x11_display), False, SubstructureRedirectMask | SubstructureNotifyMask, (XEvent *)&xev);
}

int OS_X11::get_screen_count() const {
	// Using Xinerama Extension
	int event_base, error_base;
	const Bool ext_okay = XineramaQueryExtension(x11_display, &event_base, &error_base);
	if (!ext_okay) return 0;

	int count;
	XineramaScreenInfo *xsi = XineramaQueryScreens(x11_display, &count);
	XFree(xsi);
	return count;
}

int OS_X11::get_current_screen() const {
	int x, y;
	Window child;
	XTranslateCoordinates(x11_display, x11_window, DefaultRootWindow(x11_display), 0, 0, &x, &y, &child);

	int count = get_screen_count();
	for (int i = 0; i < count; i++) {
		Point2i pos = get_screen_position(i);
		Size2i size = get_screen_size(i);
		if ((x >= pos.x && x < pos.x + size.width) && (y >= pos.y && y < pos.y + size.height))
			return i;
	}
	return 0;
}

void OS_X11::set_current_screen(int p_screen) {
	int count = get_screen_count();
	if (p_screen >= count) return;

	if (current_videomode.fullscreen) {
		Point2i position = get_screen_position(p_screen);
		Size2i size = get_screen_size(p_screen);

		XMoveResizeWindow(x11_display, x11_window, position.x, position.y, size.x, size.y);
	} else {
		if (p_screen != get_current_screen()) {
			Point2i position = get_screen_position(p_screen);
			XMoveWindow(x11_display, x11_window, position.x, position.y);
		}
	}
}

Point2 OS_X11::get_screen_position(int p_screen) const {
	if (p_screen == -1) {
		p_screen = get_current_screen();
	}

	// Using Xinerama Extension
	int event_base, error_base;
	const Bool ext_okay = XineramaQueryExtension(x11_display, &event_base, &error_base);
	if (!ext_okay) {
		return Point2i(0, 0);
	}

	int count;
	XineramaScreenInfo *xsi = XineramaQueryScreens(x11_display, &count);
	if (p_screen >= count) {
		return Point2i(0, 0);
	}

	Point2i position = Point2i(xsi[p_screen].x_org, xsi[p_screen].y_org);

	XFree(xsi);

	return position;
}

Size2 OS_X11::get_screen_size(int p_screen) const {
	if (p_screen == -1) {
		p_screen = get_current_screen();
	}

	// Using Xinerama Extension
	int event_base, error_base;
	const Bool ext_okay = XineramaQueryExtension(x11_display, &event_base, &error_base);
	if (!ext_okay) return Size2i(0, 0);

	int count;
	XineramaScreenInfo *xsi = XineramaQueryScreens(x11_display, &count);
	if (p_screen >= count) return Size2i(0, 0);

	Size2i size = Point2i(xsi[p_screen].width, xsi[p_screen].height);
	XFree(xsi);
	return size;
}

int OS_X11::get_screen_dpi(int p_screen) const {
	if (p_screen == -1) {
		p_screen = get_current_screen();
	}

	//invalid screen?
	ERR_FAIL_INDEX_V(p_screen, get_screen_count(), 0);

	//Get physical monitor Dimensions through XRandR and calculate dpi
	Size2 sc = get_screen_size(p_screen);
	if (xrandr_ext_ok) {
		int count = 0;
		if (xrr_get_monitors) {
			xrr_monitor_info *monitors = xrr_get_monitors(x11_display, x11_window, true, &count);
			if (p_screen < count) {
				double xdpi = sc.width / (double)monitors[p_screen].mwidth * 25.4;
				double ydpi = sc.height / (double)monitors[p_screen].mheight * 25.4;
				xrr_free_monitors(monitors);
				return (xdpi + ydpi) / 2;
			}
			xrr_free_monitors(monitors);
		} else if (p_screen == 0) {
			XRRScreenSize *sizes = XRRSizes(x11_display, 0, &count);
			if (sizes) {
				double xdpi = sc.width / (double)sizes[0].mwidth * 25.4;
				double ydpi = sc.height / (double)sizes[0].mheight * 25.4;
				return (xdpi + ydpi) / 2;
			}
		}
	}

	int width_mm = DisplayWidthMM(x11_display, p_screen);
	int height_mm = DisplayHeightMM(x11_display, p_screen);
	double xdpi = (width_mm ? sc.width / (double)width_mm * 25.4 : 0);
	double ydpi = (height_mm ? sc.height / (double)height_mm * 25.4 : 0);
	if (xdpi || xdpi)
		return (xdpi + ydpi) / (xdpi && ydpi ? 2 : 1);

	//could not get dpi
	return 96;
}

Point2 OS_X11::get_window_position() const {
	int x, y;
	Window child;
	XTranslateCoordinates(x11_display, x11_window, DefaultRootWindow(x11_display), 0, 0, &x, &y, &child);

	int screen = get_current_screen();
	Point2i screen_position = get_screen_position(screen);

	return Point2i(x - screen_position.x, y - screen_position.y);
}

void OS_X11::set_window_position(const Point2 &p_position) {
	XMoveWindow(x11_display, x11_window, p_position.x, p_position.y);
}

Size2 OS_X11::get_window_size() const {
	// Use current_videomode width and height instead of XGetWindowAttributes
	// since right after a XResizeWindow the attributes may not be updated yet
	return Size2i(current_videomode.width, current_videomode.height);
}

Size2 OS_X11::get_real_window_size() const {
	XWindowAttributes xwa;
	XSync(x11_display, False);
	XGetWindowAttributes(x11_display, x11_window, &xwa);
	int w = xwa.width;
	int h = xwa.height;
	Atom prop = XInternAtom(x11_display, "_NET_FRAME_EXTENTS", True);
	Atom type;
	int format;
	unsigned long len;
	unsigned long remaining;
	unsigned char *data = NULL;
	if (XGetWindowProperty(x11_display, x11_window, prop, 0, 4, False, AnyPropertyType, &type, &format, &len, &remaining, &data) == Success) {
		long *extents = (long *)data;
		w += extents[0] + extents[1]; // left, right
		h += extents[2] + extents[3]; // top, bottom
	}
	return Size2(w, h);
}

void OS_X11::set_window_size(const Size2 p_size) {

	if (current_videomode.width == p_size.width && current_videomode.height == p_size.height)
		return;

	XWindowAttributes xwa;
	XSync(x11_display, False);
	XGetWindowAttributes(x11_display, x11_window, &xwa);
	int old_w = xwa.width;
	int old_h = xwa.height;

	// If window resizable is disabled we need to update the attributes first
	if (is_window_resizable() == false) {
		XSizeHints *xsh;
		xsh = XAllocSizeHints();
		xsh->flags = PMinSize | PMaxSize;
		xsh->min_width = p_size.x;
		xsh->max_width = p_size.x;
		xsh->min_height = p_size.y;
		xsh->max_height = p_size.y;
		XSetWMNormalHints(x11_display, x11_window, xsh);
		XFree(xsh);
	}

	// Resize the window
	XResizeWindow(x11_display, x11_window, p_size.x, p_size.y);

	// Update our videomode width and height
	current_videomode.width = p_size.x;
	current_videomode.height = p_size.y;

	for (int timeout = 0; timeout < 50; ++timeout) {
		XSync(x11_display, False);
		XGetWindowAttributes(x11_display, x11_window, &xwa);

		if (old_w != xwa.width || old_h != xwa.height)
			break;

		usleep(10000);
	}
}

void OS_X11::set_window_fullscreen(bool p_enabled) {
	if (current_videomode.fullscreen == p_enabled)
		return;

	if (p_enabled && current_videomode.always_on_top) {
		// Fullscreen + Always-on-top requires a maximized window on some window managers (Metacity)
		set_window_maximized(true);
	}
	set_wm_fullscreen(p_enabled);
	if (!p_enabled && !current_videomode.always_on_top) {
		// Restore
		set_window_maximized(false);
	}

	current_videomode.fullscreen = p_enabled;
}

bool OS_X11::is_window_fullscreen() const {
	return current_videomode.fullscreen;
}

void OS_X11::set_window_resizable(bool p_enabled) {
	XSizeHints *xsh;
	Size2 size = get_window_size();

	xsh = XAllocSizeHints();
	xsh->flags = p_enabled ? 0L : PMinSize | PMaxSize;
	if (!p_enabled) {
		xsh->min_width = size.x;
		xsh->max_width = size.x;
		xsh->min_height = size.y;
		xsh->max_height = size.y;
	}
	XSetWMNormalHints(x11_display, x11_window, xsh);
	XFree(xsh);
	current_videomode.resizable = p_enabled;
}

bool OS_X11::is_window_resizable() const {
	return current_videomode.resizable;
}

void OS_X11::set_window_minimized(bool p_enabled) {
	// Using ICCCM -- Inter-Client Communication Conventions Manual
	XEvent xev;
	Atom wm_change = XInternAtom(x11_display, "WM_CHANGE_STATE", False);

	memset(&xev, 0, sizeof(xev));
	xev.type = ClientMessage;
	xev.xclient.window = x11_window;
	xev.xclient.message_type = wm_change;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = p_enabled ? WM_IconicState : WM_NormalState;

	XSendEvent(x11_display, DefaultRootWindow(x11_display), False, SubstructureRedirectMask | SubstructureNotifyMask, &xev);

	Atom wm_state = XInternAtom(x11_display, "_NET_WM_STATE", False);
	Atom wm_hidden = XInternAtom(x11_display, "_NET_WM_STATE_HIDDEN", False);

	memset(&xev, 0, sizeof(xev));
	xev.type = ClientMessage;
	xev.xclient.window = x11_window;
	xev.xclient.message_type = wm_state;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = _NET_WM_STATE_ADD;
	xev.xclient.data.l[1] = wm_hidden;

	XSendEvent(x11_display, DefaultRootWindow(x11_display), False, SubstructureRedirectMask | SubstructureNotifyMask, &xev);
}

bool OS_X11::is_window_minimized() const {
	// Using ICCCM -- Inter-Client Communication Conventions Manual
	Atom property = XInternAtom(x11_display, "WM_STATE", True);
	Atom type;
	int format;
	unsigned long len;
	unsigned long remaining;
	unsigned char *data = NULL;

	int result = XGetWindowProperty(
			x11_display,
			x11_window,
			property,
			0,
			32,
			False,
			AnyPropertyType,
			&type,
			&format,
			&len,
			&remaining,
			&data);

	if (result == Success) {
		long *state = (long *)data;
		if (state[0] == WM_IconicState)
			return true;
	}
	return false;
}

void OS_X11::set_window_maximized(bool p_enabled) {
	if (is_window_maximized() == p_enabled)
		return;

	// Using EWMH -- Extended Window Manager Hints
	XEvent xev;
	Atom wm_state = XInternAtom(x11_display, "_NET_WM_STATE", False);
	Atom wm_max_horz = XInternAtom(x11_display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
	Atom wm_max_vert = XInternAtom(x11_display, "_NET_WM_STATE_MAXIMIZED_VERT", False);

	memset(&xev, 0, sizeof(xev));
	xev.type = ClientMessage;
	xev.xclient.window = x11_window;
	xev.xclient.message_type = wm_state;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = p_enabled ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
	xev.xclient.data.l[1] = wm_max_horz;
	xev.xclient.data.l[2] = wm_max_vert;

	XSendEvent(x11_display, DefaultRootWindow(x11_display), False, SubstructureRedirectMask | SubstructureNotifyMask, &xev);

	if (is_window_maximize_allowed()) {
		while (p_enabled && !is_window_maximized()) {
			// Wait for effective resizing (so the GLX context is too).
		}
	}

	maximized = p_enabled;
}

bool OS_X11::is_window_maximize_allowed() {
	Atom property = XInternAtom(x11_display, "_NET_WM_ALLOWED_ACTIONS", False);
	Atom type;
	int format;
	unsigned long len;
	unsigned long remaining;
	unsigned char *data = NULL;

	int result = XGetWindowProperty(
			x11_display,
			x11_window,
			property,
			0,
			1024,
			False,
			XA_ATOM,
			&type,
			&format,
			&len,
			&remaining,
			&data);

	if (result == Success) {
		Atom *atoms = (Atom *)data;
		Atom wm_act_max_horz = XInternAtom(x11_display, "_NET_WM_ACTION_MAXIMIZE_HORZ", False);
		Atom wm_act_max_vert = XInternAtom(x11_display, "_NET_WM_ACTION_MAXIMIZE_VERT", False);
		bool found_wm_act_max_horz = false;
		bool found_wm_act_max_vert = false;

		for (unsigned int i = 0; i < len; i++) {
			if (atoms[i] == wm_act_max_horz)
				found_wm_act_max_horz = true;
			if (atoms[i] == wm_act_max_vert)
				found_wm_act_max_vert = true;

			if (found_wm_act_max_horz || found_wm_act_max_vert)
				return true;
		}
		XFree(atoms);
	}

	return false;
}

bool OS_X11::is_window_maximized() const {
	// Using EWMH -- Extended Window Manager Hints
	Atom property = XInternAtom(x11_display, "_NET_WM_STATE", False);
	Atom type;
	int format;
	unsigned long len;
	unsigned long remaining;
	unsigned char *data = NULL;
	bool retval = false;

	int result = XGetWindowProperty(
			x11_display,
			x11_window,
			property,
			0,
			1024,
			False,
			XA_ATOM,
			&type,
			&format,
			&len,
			&remaining,
			&data);

	if (result == Success) {
		Atom *atoms = (Atom *)data;
		Atom wm_max_horz = XInternAtom(x11_display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
		Atom wm_max_vert = XInternAtom(x11_display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
		bool found_wm_max_horz = false;
		bool found_wm_max_vert = false;

		for (unsigned int i = 0; i < len; i++) {
			if (atoms[i] == wm_max_horz)
				found_wm_max_horz = true;
			if (atoms[i] == wm_max_vert)
				found_wm_max_vert = true;

			if (found_wm_max_horz && found_wm_max_vert) {
				retval = true;
				break;
			}
		}
	}

	XFree(data);
	return retval;
}

void OS_X11::set_window_always_on_top(bool p_enabled) {
	if (is_window_always_on_top() == p_enabled)
		return;

	if (p_enabled && current_videomode.fullscreen) {
		// Fullscreen + Always-on-top requires a maximized window on some window managers (Metacity)
		set_window_maximized(true);
	}
	set_wm_above(p_enabled);
	if (!p_enabled && !current_videomode.fullscreen) {
		// Restore
		set_window_maximized(false);
	}

	current_videomode.always_on_top = p_enabled;
}

bool OS_X11::is_window_always_on_top() const {
	return current_videomode.always_on_top;
}

void OS_X11::set_borderless_window(bool p_borderless) {

	if (current_videomode.borderless_window == p_borderless)
		return;

	current_videomode.borderless_window = p_borderless;

	Hints hints;
	Atom property;
	hints.flags = 2;
	hints.decorations = current_videomode.borderless_window ? 0 : 1;
	property = XInternAtom(x11_display, "_MOTIF_WM_HINTS", True);
	XChangeProperty(x11_display, x11_window, property, property, 32, PropModeReplace, (unsigned char *)&hints, 5);
}

bool OS_X11::get_borderless_window() {
	return current_videomode.borderless_window;
}

void OS_X11::request_attention() {
	// Using EWMH -- Extended Window Manager Hints
	//
	// Sets the _NET_WM_STATE_DEMANDS_ATTENTION atom for WM_STATE
	// Will be unset by the window manager after user react on the request for attention

	XEvent xev;
	Atom wm_state = XInternAtom(x11_display, "_NET_WM_STATE", False);
	Atom wm_attention = XInternAtom(x11_display, "_NET_WM_STATE_DEMANDS_ATTENTION", False);

	memset(&xev, 0, sizeof(xev));
	xev.type = ClientMessage;
	xev.xclient.window = x11_window;
	xev.xclient.message_type = wm_state;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = _NET_WM_STATE_ADD;
	xev.xclient.data.l[1] = wm_attention;

	XSendEvent(x11_display, DefaultRootWindow(x11_display), False, SubstructureRedirectMask | SubstructureNotifyMask, &xev);
	XFlush(x11_display);
}

void OS_X11::get_key_modifier_state(unsigned int p_x11_state, Ref<InputEventWithModifiers> state) {

	state->set_shift((p_x11_state & ShiftMask));
	state->set_control((p_x11_state & ControlMask));
	state->set_alt((p_x11_state & Mod1Mask /*|| p_x11_state&Mod5Mask*/)); //altgr should not count as alt
	state->set_metakey((p_x11_state & Mod4Mask));
}

unsigned int OS_X11::get_mouse_button_state(unsigned int p_x11_state) {

	unsigned int state = 0;

	if (p_x11_state & Button1Mask) {

		state |= 1 << 0;
	}

	if (p_x11_state & Button3Mask) {

		state |= 1 << 1;
	}

	if (p_x11_state & Button2Mask) {

		state |= 1 << 2;
	}

	if (p_x11_state & Button4Mask) {

		state |= 1 << 3;
	}

	if (p_x11_state & Button5Mask) {

		state |= 1 << 4;
	}

	last_button_state = state;
	return state;
}

void OS_X11::handle_key_event(XKeyEvent *p_event, bool p_echo) {

	// X11 functions don't know what const is
	XKeyEvent *xkeyevent = p_event;

	// This code was pretty difficult to write.
	// The docs stink and every toolkit seems to
	// do it in a different way.

	/* Phase 1, obtain a proper keysym */

	// This was also very difficult to figure out.
	// You'd expect you could just use Keysym provided by
	// XKeycodeToKeysym to obtain internationalized
	// input.. WRONG!!
	// you must use XLookupString (???) which not only wastes
	// cycles generating an unnecessary string, but also
	// still works in half the cases. (won't handle deadkeys)
	// For more complex input methods (deadkeys and more advanced)
	// you have to use XmbLookupString (??).
	// So.. then you have to chosse which of both results
	// you want to keep.
	// This is a real bizarreness and cpu waster.

	KeySym keysym_keycode = 0; // keysym used to find a keycode
	KeySym keysym_unicode = 0; // keysym used to find unicode

	// XLookupString returns keysyms usable as nice scancodes/
	char str[256 + 1];
	XLookupString(xkeyevent, str, 256, &keysym_keycode, NULL);

	// Meanwhile, XLookupString returns keysyms useful for unicode.

	if (!xmbstring) {
		// keep a temporary buffer for the string
		xmbstring = (char *)memalloc(sizeof(char) * 8);
		xmblen = 8;
	}

	keysym_unicode = keysym_keycode;

	if (xkeyevent->type == KeyPress && xic) {

		Status status;
#ifdef X_HAVE_UTF8_STRING
		int utf8len = 8;
		char *utf8string = (char *)memalloc(sizeof(char) * utf8len);
		int utf8bytes = Xutf8LookupString(xic, xkeyevent, utf8string,
				utf8len - 1, &keysym_unicode, &status);
		if (status == XBufferOverflow) {
			utf8len = utf8bytes + 1;
			utf8string = (char *)memrealloc(utf8string, utf8len);
			utf8bytes = Xutf8LookupString(xic, xkeyevent, utf8string,
					utf8len - 1, &keysym_unicode, &status);
		}
		utf8string[utf8bytes] = '\0';

		if (status == XLookupChars) {
			bool keypress = xkeyevent->type == KeyPress;
			unsigned int keycode = KeyMappingX11::get_keycode(keysym_keycode);
			if (keycode >= 'a' && keycode <= 'z')
				keycode -= 'a' - 'A';

			String tmp;
			tmp.parse_utf8(utf8string, utf8bytes);
			for (int i = 0; i < tmp.length(); i++) {
				Ref<InputEventKey> k;
				k.instance();
				if (keycode == 0 && tmp[i] == 0) {
					continue;
				}

				get_key_modifier_state(xkeyevent->state, k);

				k->set_unicode(tmp[i]);

				k->set_pressed(keypress);

				k->set_scancode(keycode);

				k->set_echo(false);

				if (k->get_scancode() == KEY_BACKTAB) {
					//make it consistent across platforms.
					k->set_scancode(KEY_TAB);
					k->set_shift(true);
				}

				input->parse_input_event(k);
			}
			return;
		}
		memfree(utf8string);
#else
		do {

			int mnbytes = XmbLookupString(xic, xkeyevent, xmbstring, xmblen - 1, &keysym_unicode, &status);
			xmbstring[mnbytes] = '\0';

			if (status == XBufferOverflow) {
				xmblen = mnbytes + 1;
				xmbstring = (char *)memrealloc(xmbstring, xmblen);
			}
		} while (status == XBufferOverflow);
#endif
	}

	/* Phase 2, obtain a pigui keycode from the keysym */

	// KeyMappingX11 just translated the X11 keysym to a PIGUI
	// keysym, so it works in all platforms the same.

	unsigned int keycode = KeyMappingX11::get_keycode(keysym_keycode);

	/* Phase 3, obtain a unicode character from the keysym */

	// KeyMappingX11 also translates keysym to unicode.
	// It does a binary search on a table to translate
	// most properly.
	//print_line("keysym_unicode: "+rtos(keysym_unicode));
	unsigned int unicode = keysym_unicode > 0 ? KeyMappingX11::get_unicode_from_keysym(keysym_unicode) : 0;

	/* Phase 4, determine if event must be filtered */

	// This seems to be a side-effect of using XIM.
	// XEventFilter looks like a core X11 function,
	// but it's actually just used to see if we must
	// ignore a deadkey, or events XIM determines
	// must not reach the actual gui.
	// Guess it was a design problem of the extension

	bool keypress = xkeyevent->type == KeyPress;

	if (keycode == 0 && unicode == 0)
		return;

	/* Phase 5, determine modifier mask */

	// No problems here, except I had no way to
	// know Mod1 was ALT and Mod4 was META (applekey/winkey)
	// just tried Mods until i found them.

	//print_line("mod1: "+itos(xkeyevent->state&Mod1Mask)+" mod 5: "+itos(xkeyevent->state&Mod5Mask));

	Ref<InputEventKey> k;
	k.instance();

	get_key_modifier_state(xkeyevent->state, k);

	/* Phase 6, determine echo character */

	// Echo characters in X11 are a keyrelease and a keypress
	// one after the other with the (almot) same timestamp.
	// To detect them, i use XPeekEvent and check that their
	// difference in time is below a threshold.

	if (xkeyevent->type != KeyPress) {

		p_echo = false;

		// make sure there are events pending,
		// so this call won't block.
		if (XPending(x11_display) > 0) {
			XEvent peek_event;
			XPeekEvent(x11_display, &peek_event);

			// I'm using a threshold of 5 msecs,
			// since sometimes there seems to be a little
			// jitter. I'm still not convinced that all this approach
			// is correct, but the xorg developers are
			// not very helpful today.

			::Time tresh = ABS(peek_event.xkey.time - xkeyevent->time);
			if (peek_event.type == KeyPress && tresh < 5) {
				KeySym rk;
				XLookupString((XKeyEvent *)&peek_event, str, 256, &rk, NULL);
				if (rk == keysym_keycode) {
					XEvent event;
					XNextEvent(x11_display, &event); //erase next event
					handle_key_event((XKeyEvent *)&event, true);
					return; //ignore current, echo next
				}
			}

			// use the time from peek_event so it always works
		}

		// save the time to check for echo when keypress happens
	}

	/* Phase 7, send event to Window */

	k->set_pressed(keypress);

	if (keycode >= 'a' && keycode <= 'z')
		keycode -= 'a' - 'A';

	k->set_scancode(keycode);
	k->set_unicode(unicode);
	k->set_echo(p_echo);

	if (k->get_scancode() == KEY_BACKTAB) {
		//make it consistent across platforms.
		k->set_scancode(KEY_TAB);
		k->set_shift(true);
	}

	//don't set mod state if modifier keys are released by themselves
	//else event.is_action() will not work correctly here
	if (!k->is_pressed()) {
		if (k->get_scancode() == KEY_SHIFT)
			k->set_shift(false);
		else if (k->get_scancode() == KEY_CONTROL)
			k->set_control(false);
		else if (k->get_scancode() == KEY_ALT)
			k->set_alt(false);
		else if (k->get_scancode() == KEY_META)
			k->set_metakey(false);
	}

	bool last_is_pressed = Input::get_singleton()->is_key_pressed(k->get_scancode());
	if (k->is_pressed()) {
		if (last_is_pressed) {
			k->set_echo(true);
		}
	} else {
		//ignore
		if (last_is_pressed == false) {
			return;
		}
	}

	//printf("key: %x\n",k->get_scancode());
	input->parse_input_event(k);
}

struct Property {
	unsigned char *data;
	int format, nitems;
	Atom type;
};

static Property read_property(Display *p_display, Window p_window, Atom p_property) {

	Atom actual_type;
	int actual_format;
	unsigned long nitems;
	unsigned long bytes_after;
	unsigned char *ret = 0;

	int read_bytes = 1024;

	//Keep trying to read the property until there are no
	//bytes unread.
	do {
		if (ret != 0)
			XFree(ret);

		XGetWindowProperty(p_display, p_window, p_property, 0, read_bytes, False, AnyPropertyType,
				&actual_type, &actual_format, &nitems, &bytes_after,
				&ret);

		read_bytes *= 2;

	} while (bytes_after != 0);

	Property p = { ret, actual_format, (int)nitems, actual_type };

	return p;
}

static Atom pick_target_from_list(Display *p_display, Atom *p_list, int p_count) {

	static const char *target_type = "text/uri-list";

	for (int i = 0; i < p_count; i++) {

		Atom atom = p_list[i];

		if (atom != None && String(XGetAtomName(p_display, atom)) == target_type)
			return atom;
	}
	return None;
}

static Atom pick_target_from_atoms(Display *p_disp, Atom p_t1, Atom p_t2, Atom p_t3) {

	static const char *target_type = "text/uri-list";
	if (p_t1 != None && String(XGetAtomName(p_disp, p_t1)) == target_type)
		return p_t1;

	if (p_t2 != None && String(XGetAtomName(p_disp, p_t2)) == target_type)
		return p_t2;

	if (p_t3 != None && String(XGetAtomName(p_disp, p_t3)) == target_type)
		return p_t3;

	return None;
}

void OS_X11::_window_changed(XEvent *event) {

	if (xic) {
		//  Not portable.
		set_ime_position(Point2(0, 1));
	}
	if ((event->xconfigure.width == current_videomode.width) &&
			(event->xconfigure.height == current_videomode.height))
		return;

	current_videomode.width = event->xconfigure.width;
	current_videomode.height = event->xconfigure.height;
}

void OS_X11::process_xevents() {

	//printf("checking events %i\n", XPending(x11_display));

	do_mouse_warp = false;

	// Is the current mouse mode one where it needs to be grabbed.
	bool mouse_mode_grab = mouse_mode == MOUSE_MODE_CAPTURED || mouse_mode == MOUSE_MODE_CONFINED;

	while (XPending(x11_display) > 0) {
		XEvent event;
		XNextEvent(x11_display, &event);

		if (XFilterEvent(&event, None)) {
			continue;
		}

#ifdef TOUCH_ENABLED
		if (XGetEventData(x11_display, &event.xcookie)) {

			if (event.xcookie.type == GenericEvent && event.xcookie.extension == touch.opcode) {

				XIDeviceEvent *event_data = (XIDeviceEvent *)event.xcookie.data;
				int index = event_data->detail;
				Vector2 pos = Vector2(event_data->event_x, event_data->event_y);

				switch (event_data->evtype) {

					case XI_TouchBegin: // Fall-through
							// Disabled hand-in-hand with the grabbing
							//XIAllowTouchEvents(x11_display, event_data->deviceid, event_data->detail, x11_window, XIAcceptTouch);

					case XI_TouchEnd: {

						bool is_begin = event_data->evtype == XI_TouchBegin;

						Ref<InputEventScreenTouch> st;
						st.instance();
						st->set_index(index);
						st->set_position(pos);
						st->set_pressed(is_begin);

						if (is_begin) {
							if (touch.state.has(index)) // Defensive
								break;
							touch.state[index] = pos;
							if (touch.state.size() == 1) {
								// X11 may send a motion event when a touch gesture begins, that would result
								// in a spurious mouse motion event being sent to Godot; remember it to be able to filter it out
								touch.mouse_pos_to_filter = pos;
							}
							input->parse_input_event(st);
						} else {
							if (!touch.state.has(index)) // Defensive
								break;
							touch.state.erase(index);
							input->parse_input_event(st);
						}
					} break;

					case XI_TouchUpdate: {

						Map<int, Vector2>::Element *curr_pos_elem = touch.state.find(index);
						if (!curr_pos_elem) { // Defensive
							break;
						}

						if (curr_pos_elem->value() != pos) {

							Ref<InputEventScreenDrag> sd;
							sd.instance();
							sd->set_index(index);
							sd->set_position(pos);
							sd->set_relative(pos - curr_pos_elem->value());
							input->parse_input_event(sd);

							curr_pos_elem->value() = pos;
						}
					} break;
				}
			}
		}
		XFreeEventData(x11_display, &event.xcookie);
#endif

		switch (event.type) {
			case Expose:
				Main::force_redraw();
				break;

			case NoExpose:
				minimized = true;
				break;

			case VisibilityNotify: {
				XVisibilityEvent *visibility = (XVisibilityEvent *)&event;
				minimized = (visibility->state == VisibilityFullyObscured);
			} break;
			case LeaveNotify: {
				if (main_loop && !mouse_mode_grab)
					main_loop->notification(MainLoop::NOTIFICATION_WM_MOUSE_EXIT);
				if (input)
					input->set_mouse_in_window(false);

			} break;
			case EnterNotify: {
				if (main_loop && !mouse_mode_grab)
					main_loop->notification(MainLoop::NOTIFICATION_WM_MOUSE_ENTER);
				if (input)
					input->set_mouse_in_window(true);
			} break;
			case FocusIn:
				minimized = false;
				window_has_focus = true;
				main_loop->notification(MainLoop::NOTIFICATION_WM_FOCUS_IN);
				if (mouse_mode_grab) {
					// Show and update the cursor if confined and the window regained focus.
					if (mouse_mode == MOUSE_MODE_CONFINED)
						XUndefineCursor(x11_display, x11_window);
					else if (mouse_mode == MOUSE_MODE_CAPTURED) // or re-hide it in captured mode
						XDefineCursor(x11_display, x11_window, null_cursor);

					XGrabPointer(
							x11_display, x11_window, True,
							ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
							GrabModeAsync, GrabModeAsync, x11_window, None, CurrentTime);
				}
#ifdef TOUCH_ENABLED
				// Grab touch devices to avoid OS gesture interference
				/*for (int i = 0; i < touch.devices.size(); ++i) {
					XIGrabDevice(x11_display, touch.devices[i], x11_window, CurrentTime, None, XIGrabModeAsync, XIGrabModeAsync, False, &touch.event_mask);
				}*/
#endif
				if (xic) {
					XSetICFocus(xic);
				}
				break;

			case FocusOut:
				window_has_focus = false;
				main_loop->notification(MainLoop::NOTIFICATION_WM_FOCUS_OUT);
				if (mouse_mode_grab) {
					//dear X11, I try, I really try, but you never work, you do whathever you want.
					if (mouse_mode == MOUSE_MODE_CAPTURED) {
						// Show the cursor if we're in captured mode so it doesn't look weird.
						XUndefineCursor(x11_display, x11_window);
					}
					XUngrabPointer(x11_display, CurrentTime);
				}
#ifdef TOUCH_ENABLED
				// Ungrab touch devices so input works as usual while we are unfocused
				/*for (int i = 0; i < touch.devices.size(); ++i) {
					XIUngrabDevice(x11_display, touch.devices[i], CurrentTime);
				}*/

				// Release every pointer to avoid sticky points
				for (Map<int, Vector2>::Element *E = touch.state.front(); E; E = E->next()) {

					Ref<InputEventScreenTouch> st;
					st.instance();
					st->set_index(E->key());
					st->set_position(E->get());
					input->parse_input_event(st);
				}
				touch.state.clear();
#endif
				if (xic) {
					XUnsetICFocus(xic);
				}
				break;

			case ConfigureNotify:
				_window_changed(&event);
				break;
			case ButtonPress:
			case ButtonRelease: {

				/* exit in case of a mouse button press */
				last_timestamp = event.xbutton.time;
				if (mouse_mode == MOUSE_MODE_CAPTURED) {
					event.xbutton.x = last_mouse_pos.x;
					event.xbutton.y = last_mouse_pos.y;
				}

				Ref<InputEventMouseButton> mb;
				mb.instance();

				get_key_modifier_state(event.xbutton.state, mb);
				mb->set_button_mask(get_mouse_button_state(event.xbutton.state));
				mb->set_position(Vector2(event.xbutton.x, event.xbutton.y));
				mb->set_global_position(mb->get_position());
				mb->set_button_index(event.xbutton.button);
				if (mb->get_button_index() == 2)
					mb->set_button_index(3);
				else if (mb->get_button_index() == 3)
					mb->set_button_index(2);

				mb->set_pressed((event.type == ButtonPress));

				if (event.type == ButtonPress && event.xbutton.button == 1) {

					uint64_t diff = get_ticks_usec() / 1000 - last_click_ms;

					if (diff < 400 && Point2(last_click_pos).distance_to(Point2(event.xbutton.x, event.xbutton.y)) < 5) {

						last_click_ms = 0;
						last_click_pos = Point2(-100, -100);
						mb->set_doubleclick(true);

					} else {
						last_click_ms += diff;
						last_click_pos = Point2(event.xbutton.x, event.xbutton.y);
					}
				}

				input->parse_input_event(mb);

			} break;
			case MotionNotify: {

				// FUCK YOU X11 API YOU SERIOUSLY GROSS ME OUT
				// YOU ARE AS GROSS AS LOOKING AT A PUTRID PILE
				// OF POOP STICKING OUT OF A CLOGGED TOILET
				// HOW THE FUCK I AM SUPPOSED TO KNOW WHICH ONE
				// OF THE MOTION NOTIFY EVENTS IS THE ONE GENERATED
				// BY WARPING THE MOUSE POINTER?
				// YOU ARE FORCING ME TO FILTER ONE BY ONE TO FIND IT
				// PLEASE DO ME A FAVOR AND DIE DROWNED IN A FECAL
				// MOUNTAIN BECAUSE THAT'S WHERE YOU BELONG.

				while (true) {
					if (mouse_mode == MOUSE_MODE_CAPTURED && event.xmotion.x == current_videomode.width / 2 && event.xmotion.y == current_videomode.height / 2) {
						//this is likely the warp event since it was warped here
						center = Vector2(event.xmotion.x, event.xmotion.y);
						break;
					}

					if (XPending(x11_display) > 0) {
						XEvent tevent;
						XPeekEvent(x11_display, &tevent);
						if (tevent.type == MotionNotify) {
							XNextEvent(x11_display, &event);
						} else {
							break;
						}
					} else {
						break;
					}
				}

				last_timestamp = event.xmotion.time;

				// Motion is also simple.
				// A little hack is in order
				// to be able to send relative motion events.
				Point2i pos(event.xmotion.x, event.xmotion.y);

#ifdef TOUCH_ENABLED
				// Avoidance of spurious mouse motion (see handling of touch)
				bool filter = false;
				// Adding some tolerance to match better Point2i to Vector2
				if (touch.state.size() && Vector2(pos).distance_squared_to(touch.mouse_pos_to_filter) < 2) {
					filter = true;
				}
				// Invalidate to avoid filtering a possible legitimate similar event coming later
				touch.mouse_pos_to_filter = Vector2(1e10, 1e10);
				if (filter) {
					break;
				}
#endif

				if (mouse_mode == MOUSE_MODE_CAPTURED) {

					if (pos == Point2i(current_videomode.width / 2, current_videomode.height / 2)) {
						//this sucks, it's a hack, etc and is a little inaccurate, etc.
						//but nothing I can do, X11 sucks.

						center = pos;
						break;
					}

					Point2i new_center = pos;
					pos = last_mouse_pos + (pos - center);
					center = new_center;
					do_mouse_warp = window_has_focus; // warp the cursor if we're focused in
				}

				if (!last_mouse_pos_valid) {

					last_mouse_pos = pos;
					last_mouse_pos_valid = true;
				}

				Point2i rel = pos - last_mouse_pos;

				Ref<InputEventMouseMotion> mm;
				mm.instance();

				get_key_modifier_state(event.xmotion.state, mm);
				mm->set_button_mask(get_mouse_button_state(event.xmotion.state));
				mm->set_position(pos);
				mm->set_global_position(pos);
				input->set_mouse_position(pos);
				mm->set_speed(input->get_last_mouse_speed());
				mm->set_relative(rel);

				last_mouse_pos = pos;

				// printf("rel: %d,%d\n", rel.x, rel.y );
				// Don't propagate the motion event unless we have focus
				// this is so that the relative motion doesn't get messed up
				// after we regain focus.
				if (window_has_focus || !mouse_mode_grab)
					input->parse_input_event(mm);

			} break;
			case KeyPress:
			case KeyRelease: {

				last_timestamp = event.xkey.time;

				// key event is a little complex, so
				// it will be handled in its own function.
				handle_key_event((XKeyEvent *)&event);
			} break;
			case SelectionRequest: {

				XSelectionRequestEvent *req;
				XEvent e, respond;
				e = event;

				req = &(e.xselectionrequest);
				if (req->target == XInternAtom(x11_display, "UTF8_STRING", 0) ||
						req->target == XInternAtom(x11_display, "COMPOUND_TEXT", 0) ||
						req->target == XInternAtom(x11_display, "TEXT", 0) ||
						req->target == XA_STRING ||
						req->target == XInternAtom(x11_display, "text/plain;charset=utf-8", 0) ||
						req->target == XInternAtom(x11_display, "text/plain", 0)) {
					CharString clip = OS::get_clipboard().utf8();
					XChangeProperty(x11_display,
							req->requestor,
							req->property,
							req->target,
							8,
							PropModeReplace,
							(unsigned char *)clip.get_data(),
							clip.length());
					respond.xselection.property = req->property;
				} else if (req->target == XInternAtom(x11_display, "TARGETS", 0)) {

					Atom data[7];
					data[0] = XInternAtom(x11_display, "TARGETS", 0);
					data[1] = XInternAtom(x11_display, "UTF8_STRING", 0);
					data[2] = XInternAtom(x11_display, "COMPOUND_TEXT", 0);
					data[3] = XInternAtom(x11_display, "TEXT", 0);
					data[4] = XA_STRING;
					data[5] = XInternAtom(x11_display, "text/plain;charset=utf-8", 0);
					data[6] = XInternAtom(x11_display, "text/plain", 0);

					XChangeProperty(x11_display,
							req->requestor,
							req->property,
							XA_ATOM,
							32,
							PropModeReplace,
							(unsigned char *)&data,
							sizeof(data) / sizeof(data[0]));
					respond.xselection.property = req->property;

				} else {
					char *targetname = XGetAtomName(x11_display, req->target);
					printf("No Target '%s'\n", targetname);
					if (targetname)
						XFree(targetname);
					respond.xselection.property = None;
				}

				respond.xselection.type = SelectionNotify;
				respond.xselection.display = req->display;
				respond.xselection.requestor = req->requestor;
				respond.xselection.selection = req->selection;
				respond.xselection.target = req->target;
				respond.xselection.time = req->time;
				XSendEvent(x11_display, req->requestor, True, NoEventMask, &respond);
				XFlush(x11_display);
			} break;

			case SelectionNotify:

				if (event.xselection.target == requested) {

					Property p = read_property(x11_display, x11_window, XInternAtom(x11_display, "PRIMARY", 0));

					Vector<String> files = String((char *)p.data).split("\n", false);
					for (int i = 0; i < files.size(); i++) {
						files[i] = files[i].replace("file://", "").replace("%20", " ").strip_escapes();
					}
					main_loop->drop_files(files);

					//Reply that all is well.
					XClientMessageEvent m;
					memset(&m, 0, sizeof(m));
					m.type = ClientMessage;
					m.display = x11_display;
					m.window = xdnd_source_window;
					m.message_type = xdnd_finished;
					m.format = 32;
					m.data.l[0] = x11_window;
					m.data.l[1] = 1;
					m.data.l[2] = xdnd_action_copy; //We only ever copy.

					XSendEvent(x11_display, xdnd_source_window, False, NoEventMask, (XEvent *)&m);
				}
				break;

			case ClientMessage:

				if ((unsigned int)event.xclient.data.l[0] == (unsigned int)wm_delete)
					main_loop->notification(MainLoop::NOTIFICATION_WM_QUIT_REQUEST);

				else if ((unsigned int)event.xclient.message_type == (unsigned int)xdnd_enter) {

					//File(s) have been dragged over the window, check for supported target (text/uri-list)
					xdnd_version = (event.xclient.data.l[1] >> 24);
					Window source = event.xclient.data.l[0];
					bool more_than_3 = event.xclient.data.l[1] & 1;
					if (more_than_3) {
						Property p = read_property(x11_display, source, XInternAtom(x11_display, "XdndTypeList", False));
						requested = pick_target_from_list(x11_display, (Atom *)p.data, p.nitems);
					} else
						requested = pick_target_from_atoms(x11_display, event.xclient.data.l[2], event.xclient.data.l[3], event.xclient.data.l[4]);
				} else if ((unsigned int)event.xclient.message_type == (unsigned int)xdnd_position) {

					//xdnd position event, reply with an XDND status message
					//just depending on type of data for now
					XClientMessageEvent m;
					memset(&m, 0, sizeof(m));
					m.type = ClientMessage;
					m.display = event.xclient.display;
					m.window = event.xclient.data.l[0];
					m.message_type = xdnd_status;
					m.format = 32;
					m.data.l[0] = x11_window;
					m.data.l[1] = (requested != None);
					m.data.l[2] = 0; //empty rectangle
					m.data.l[3] = 0;
					m.data.l[4] = xdnd_action_copy;

					XSendEvent(x11_display, event.xclient.data.l[0], False, NoEventMask, (XEvent *)&m);
					XFlush(x11_display);
				} else if ((unsigned int)event.xclient.message_type == (unsigned int)xdnd_drop) {

					if (requested != None) {
						xdnd_source_window = event.xclient.data.l[0];
						if (xdnd_version >= 1)
							XConvertSelection(x11_display, xdnd_selection, requested, XInternAtom(x11_display, "PRIMARY", 0), x11_window, event.xclient.data.l[2]);
						else
							XConvertSelection(x11_display, xdnd_selection, requested, XInternAtom(x11_display, "PRIMARY", 0), x11_window, CurrentTime);
					} else {
						//Reply that we're not interested.
						XClientMessageEvent m;
						memset(&m, 0, sizeof(m));
						m.type = ClientMessage;
						m.display = event.xclient.display;
						m.window = event.xclient.data.l[0];
						m.message_type = xdnd_finished;
						m.format = 32;
						m.data.l[0] = x11_window;
						m.data.l[1] = 0;
						m.data.l[2] = None; //Failed.
						XSendEvent(x11_display, event.xclient.data.l[0], False, NoEventMask, (XEvent *)&m);
					}
				}
				break;
			default:
				break;
		}
	}

	XFlush(x11_display);

	if (do_mouse_warp) {

		XWarpPointer(x11_display, None, x11_window,
				0, 0, 0, 0, (int)current_videomode.width / 2, (int)current_videomode.height / 2);

		/*
		Window root, child;
		int root_x, root_y;
		int win_x, win_y;
		unsigned int mask;
		XQueryPointer( x11_display, x11_window, &root, &child, &root_x, &root_y, &win_x, &win_y, &mask );

		printf("Root: %d,%d\n", root_x, root_y);
		printf("Win: %d,%d\n", win_x, win_y);
		*/
	}
}

MainLoop *OS_X11::get_main_loop() const {

	return main_loop;
}

void OS_X11::delete_main_loop() {

	if (main_loop)
		memdelete(main_loop);
	main_loop = NULL;
}

void OS_X11::set_main_loop(MainLoop *p_main_loop) {

	main_loop = p_main_loop;
	input->set_main_loop(p_main_loop);
}

bool OS_X11::can_draw() const {

	return !minimized;
};

void OS_X11::set_clipboard(const String &p_text) {

	OS::set_clipboard(p_text);

	XSetSelectionOwner(x11_display, XA_PRIMARY, x11_window, CurrentTime);
	XSetSelectionOwner(x11_display, XInternAtom(x11_display, "CLIPBOARD", 0), x11_window, CurrentTime);
};

static String _get_clipboard_impl(Atom p_source, Window x11_window, ::Display *x11_display, String p_internal_clipboard, Atom target) {

	String ret;

	Atom type;
	Atom selection = XA_PRIMARY;
	int format, result;
	unsigned long len, bytes_left, dummy;
	unsigned char *data;
	Window Sown = XGetSelectionOwner(x11_display, p_source);

	if (Sown == x11_window) {

		return p_internal_clipboard;
	};

	if (Sown != None) {
		XConvertSelection(x11_display, p_source, target, selection,
				x11_window, CurrentTime);
		XFlush(x11_display);
		while (true) {
			XEvent event;
			XNextEvent(x11_display, &event);
			if (event.type == SelectionNotify && event.xselection.requestor == x11_window) {
				break;
			};
		};

		//
		// Do not get any data, see how much data is there
		//
		XGetWindowProperty(x11_display, x11_window,
				selection, // Tricky..
				0, 0, // offset - len
				0, // Delete 0==FALSE
				AnyPropertyType, //flag
				&type, // return type
				&format, // return format
				&len, &bytes_left, //that
				&data);
		// DATA is There
		if (bytes_left > 0) {
			result = XGetWindowProperty(x11_display, x11_window,
					selection, 0, bytes_left, 0,
					AnyPropertyType, &type, &format,
					&len, &dummy, &data);
			if (result == Success) {
				ret.parse_utf8((const char *)data);
			} else
				printf("FAIL\n");
			XFree(data);
		}
	}

	return ret;
}

static String _get_clipboard(Atom p_source, Window x11_window, ::Display *x11_display, String p_internal_clipboard) {
	String ret;
	Atom utf8_atom = XInternAtom(x11_display, "UTF8_STRING", True);
	if (utf8_atom != None) {
		ret = _get_clipboard_impl(p_source, x11_window, x11_display, p_internal_clipboard, utf8_atom);
	}
	if (ret == "") {
		ret = _get_clipboard_impl(p_source, x11_window, x11_display, p_internal_clipboard, XA_STRING);
	}
	return ret;
}

String OS_X11::get_clipboard() const {

	String ret;
	ret = _get_clipboard(XInternAtom(x11_display, "CLIPBOARD", 0), x11_window, x11_display, OS::get_clipboard());

	if (ret == "") {
		ret = _get_clipboard(XA_PRIMARY, x11_window, x11_display, OS::get_clipboard());
	};

	return ret;
}

String OS_X11::get_name() {

	return "X11";
}

Error OS_X11::shell_open(String p_uri) {

	Error ok;
	List<String> args;
	args.push_back(p_uri);
	ok = execute("xdg-open", args, false);
	if (ok == OK)
		return OK;
	ok = execute("gnome-open", args, false);
	if (ok == OK)
		return OK;
	ok = execute("kde-open", args, false);
	return ok;
}

bool OS_X11::_check_internal_feature_support(const String &p_feature) {

	return p_feature == "pc" || p_feature == "s3tc";
}

String OS_X11::get_config_path() const {

	if (has_environment("XDG_CONFIG_HOME")) {
		return get_environment("XDG_CONFIG_HOME");
	} else if (has_environment("HOME")) {
		return get_environment("HOME").plus_file(".config");
	} else {
		return ".";
	}
}

String OS_X11::get_data_path() const {

	if (has_environment("XDG_DATA_HOME")) {
		return get_environment("XDG_DATA_HOME");
	} else if (has_environment("HOME")) {
		return get_environment("HOME").plus_file(".local/share");
	} else {
		return get_config_path();
	}
}

String OS_X11::get_cache_path() const {

	if (has_environment("XDG_CACHE_HOME")) {
		return get_environment("XDG_CACHE_HOME");
	} else if (has_environment("HOME")) {
		return get_environment("HOME").plus_file(".cache");
	} else {
		return get_config_path();
	}
}

String OS_X11::get_system_dir(SystemDir p_dir) const {

	String xdgparam;

	switch (p_dir) {
		case SYSTEM_DIR_DESKTOP: {

			xdgparam = "DESKTOP";
		} break;
		case SYSTEM_DIR_DCIM: {

			xdgparam = "PICTURES";

		} break;
		case SYSTEM_DIR_DOCUMENTS: {

			xdgparam = "DOCUMENTS";

		} break;
		case SYSTEM_DIR_DOWNLOADS: {

			xdgparam = "DOWNLOAD";

		} break;
		case SYSTEM_DIR_MOVIES: {

			xdgparam = "VIDEOS";

		} break;
		case SYSTEM_DIR_MUSIC: {

			xdgparam = "MUSIC";

		} break;
		case SYSTEM_DIR_PICTURES: {

			xdgparam = "PICTURES";

		} break;
		case SYSTEM_DIR_RINGTONES: {

			xdgparam = "MUSIC";

		} break;
	}

	String pipe;
	List<String> arg;
	arg.push_back(xdgparam);
	Error err = const_cast<OS_X11 *>(this)->execute("xdg-user-dir", arg, true, NULL, &pipe);
	if (err != OK)
		return ".";
	return pipe.strip_edges();
}

void OS_X11::move_window_to_foreground() {

	XEvent xev;
	Atom net_active_window = XInternAtom(x11_display, "_NET_ACTIVE_WINDOW", False);

	memset(&xev, 0, sizeof(xev));
	xev.type = ClientMessage;
	xev.xclient.window = x11_window;
	xev.xclient.message_type = net_active_window;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = 1;
	xev.xclient.data.l[1] = CurrentTime;

	XSendEvent(x11_display, DefaultRootWindow(x11_display), False, SubstructureRedirectMask | SubstructureNotifyMask, &xev);
	XFlush(x11_display);
}

void OS_X11::set_cursor_shape(CursorShape p_shape) {

	ERR_FAIL_INDEX(p_shape, CURSOR_MAX);

	if (p_shape == current_cursor)
		return;
	if (mouse_mode == MOUSE_MODE_VISIBLE) {
		if (cursors[p_shape] != None)
			XDefineCursor(x11_display, x11_window, cursors[p_shape]);
		else if (cursors[CURSOR_ARROW] != None)
			XDefineCursor(x11_display, x11_window, cursors[CURSOR_ARROW]);
	}

	current_cursor = p_shape;
}

void OS_X11::set_custom_mouse_cursor(const RES &p_cursor, CursorShape p_shape, const Vector2 &p_hotspot) {
	if (p_cursor.is_valid()) {
		Ref<Texture> texture = p_cursor;
		Ref<Image> image = texture->get_data();

		ERR_FAIL_COND(texture->get_width() != 32 || texture->get_height() != 32);

		// Create the cursor structure
		XcursorImage *cursor_image = XcursorImageCreate(texture->get_width(), texture->get_height());
		XcursorUInt image_size = 32 * 32;
		XcursorDim size = sizeof(XcursorPixel) * image_size;

		cursor_image->version = 1;
		cursor_image->size = size;
		cursor_image->xhot = p_hotspot.x;
		cursor_image->yhot = p_hotspot.y;

		// allocate memory to contain the whole file
		cursor_image->pixels = (XcursorPixel *)malloc(size);

		image->lock();

		for (XcursorPixel index = 0; index < image_size; index++) {
			int column_index = floor(index / 32);
			int row_index = index % 32;

			*(cursor_image->pixels + index) = image->get_pixel(row_index, column_index).to_argb32();
		}

		image->unlock();

		ERR_FAIL_COND(cursor_image->pixels == NULL);

		// Save it for a further usage
		cursors[p_shape] = XcursorImageLoadCursor(x11_display, cursor_image);

		if (p_shape == CURSOR_ARROW) {
			XDefineCursor(x11_display, x11_window, cursors[p_shape]);
		}
	}
}

void OS_X11::release_rendering_thread() {

#if defined(OPENGL_ENABLED)
	context_gl->release_current();
#endif
}

void OS_X11::make_rendering_thread() {

#if defined(OPENGL_ENABLED)
	context_gl->make_current();
#endif
}

void OS_X11::swap_buffers() {

#if defined(OPENGL_ENABLED)
	context_gl->swap_buffers();
#endif
}

void OS_X11::alert(const String &p_alert, const String &p_title) {

	List<String> args;
	args.push_back("-center");
	args.push_back("-title");
	args.push_back(p_title);
	args.push_back(p_alert);

	execute("xmessage", args, true);
}

void OS_X11::set_icon(const Ref<Image> &p_icon) {
	Atom net_wm_icon = XInternAtom(x11_display, "_NET_WM_ICON", False);

	if (p_icon.is_valid()) {
		Ref<Image> img = p_icon->duplicate();
		img->convert(Image::FORMAT_RGBA8);

		int w = img->get_width();
		int h = img->get_height();

		// We're using long to have wordsize (32Bit build -> 32 Bits, 64 Bit build -> 64 Bits
		Vector<long> pd;

		pd.resize(2 + w * h);

		pd[0] = w;
		pd[1] = h;

		PoolVector<uint8_t>::Read r = img->get_data().read();

		long *wr = &pd[2];
		uint8_t const *pr = r.ptr();

		for (int i = 0; i < w * h; i++) {
			long v = 0;
			//    A             R             G            B
			v |= pr[3] << 24 | pr[0] << 16 | pr[1] << 8 | pr[2];
			*wr++ = v;
			pr += 4;
		}
		XChangeProperty(x11_display, x11_window, net_wm_icon, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)pd.ptr(), pd.size());
	} else {
		XDeleteProperty(x11_display, x11_window, net_wm_icon);
	}
	XFlush(x11_display);
}

void OS_X11::force_process_input() {
	process_xevents(); // get rid of pending events
#ifdef JOYDEV_ENABLED
	joypad->process_joypads();
#endif
}

void OS_X11::run() {

	force_quit = false;

	if (!main_loop)
		return;

	main_loop->init();

	//uint64_t last_ticks=get_ticks_usec();

	//int frames=0;
	//uint64_t frame=0;

	while (!force_quit) {

		process_xevents(); // get rid of pending events
#ifdef JOYDEV_ENABLED
		joypad->process_joypads();
#endif
		if (Main::iteration() == true)
			break;
	};

	main_loop->finish();
}

bool OS_X11::is_joy_known(int p_device) {
	return input->is_joy_mapped(p_device);
}

String OS_X11::get_joy_guid(int p_device) const {
	return input->get_joy_guid_remapped(p_device);
}

void OS_X11::_set_use_vsync(bool p_enable) {
#if defined(OPENGL_ENABLED)
	if (context_gl)
		context_gl->set_use_vsync(p_enable);
#endif
}
/*
bool OS_X11::is_vsync_enabled() const {

	if (context_gl)
		return context_gl->is_using_vsync();

	return true;
}
*/
void OS_X11::set_context(int p_context) {

	XClassHint *classHint = XAllocClassHint();
	if (classHint) {

		if (p_context == CONTEXT_EDITOR)
			classHint->res_name = (char *)"Godot_Editor";
		if (p_context == CONTEXT_PROJECTMAN)
			classHint->res_name = (char *)"Godot_ProjectList";
		classHint->res_class = (char *)"Godot";
		XSetClassHint(x11_display, x11_window, classHint);
		XFree(classHint);
	}
}

OS::PowerState OS_X11::get_power_state() {
	return power_manager->get_power_state();
}

int OS_X11::get_power_seconds_left() {
	return power_manager->get_power_seconds_left();
}

int OS_X11::get_power_percent_left() {
	return power_manager->get_power_percent_left();
}

void OS_X11::disable_crash_handler() {
	crash_handler.disable();
}

bool OS_X11::is_disable_crash_handler() const {
	return crash_handler.is_disabled();
}

static String get_mountpoint(const String &p_path) {
	struct stat s;
	if (stat(p_path.utf8().get_data(), &s)) {
		return "";
	}

#ifdef HAVE_MNTENT
	dev_t dev = s.st_dev;
	FILE *fd = setmntent("/proc/mounts", "r");
	if (!fd) {
		return "";
	}

	struct mntent mnt;
	char buf[1024];
	size_t buflen = 1024;
	while (getmntent_r(fd, &mnt, buf, buflen)) {
		if (!stat(mnt.mnt_dir, &s) && s.st_dev == dev) {
			endmntent(fd);
			return String(mnt.mnt_dir);
		}
	}

	endmntent(fd);
#endif
	return "";
}

Error OS_X11::move_to_trash(const String &p_path) {
	String trash_can = "";
	String mnt = get_mountpoint(p_path);

	// If there is a directory "[Mountpoint]/.Trash-[UID]/files", use it as the trash can.
	if (mnt != "") {
		String path(mnt + "/.Trash-" + itos(getuid()) + "/files");
		struct stat s;
		if (!stat(path.utf8().get_data(), &s)) {
			trash_can = path;
		}
	}

	// Otherwise, if ${XDG_DATA_HOME} is defined, use "${XDG_DATA_HOME}/Trash/files" as the trash can.
	if (trash_can == "") {
		char *dhome = getenv("XDG_DATA_HOME");
		if (dhome) {
			trash_can = String(dhome) + "/Trash/files";
		}
	}

	// Otherwise, if ${HOME} is defined, use "${HOME}/.local/share/Trash/files" as the trash can.
	if (trash_can == "") {
		char *home = getenv("HOME");
		if (home) {
			trash_can = String(home) + "/.local/share/Trash/files";
		}
	}

	// Issue an error if none of the previous locations is appropriate for the trash can.
	if (trash_can == "") {
		ERR_PRINTS("move_to_trash: Could not determine the trash can location");
		return FAILED;
	}

	// Create needed directories for decided trash can location.
	DirAccess *dir_access = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	Error err = dir_access->make_dir_recursive(trash_can);
	memdelete(dir_access);

	// Issue an error if trash can is not created proprely.
	if (err != OK) {
		ERR_PRINTS("move_to_trash: Could not create the trash can \"" + trash_can + "\"");
		return err;
	}

	// The trash can is successfully created, now move the given resource to it.
	// Do not use DirAccess:rename() because it can't move files across multiple mountpoints.
	List<String> mv_args;
	mv_args.push_back(p_path);
	mv_args.push_back(trash_can);
	int retval;
	err = execute("mv", mv_args, true, NULL, NULL, &retval);

	// Issue an error if "mv" failed to move the given resource to the trash can.
	if (err != OK || retval != 0) {
		ERR_PRINTS("move_to_trash: Could not move the resource \"" + p_path + "\" to the trash can \"" + trash_can + "\"");
		return FAILED;
	}

	return OK;
}

OS::LatinKeyboardVariant OS_X11::get_latin_keyboard_variant() const {

	XkbDescRec *xkbdesc = XkbAllocKeyboard();
	ERR_FAIL_COND_V(!xkbdesc, LATIN_KEYBOARD_QWERTY);

	XkbGetNames(x11_display, XkbSymbolsNameMask, xkbdesc);
	ERR_FAIL_COND_V(!xkbdesc->names, LATIN_KEYBOARD_QWERTY);
	ERR_FAIL_COND_V(!xkbdesc->names->symbols, LATIN_KEYBOARD_QWERTY);

	char *layout = XGetAtomName(x11_display, xkbdesc->names->symbols);
	ERR_FAIL_COND_V(!layout, LATIN_KEYBOARD_QWERTY);

	Vector<String> info = String(layout).split("+");
	ERR_FAIL_INDEX_V(1, info.size(), LATIN_KEYBOARD_QWERTY);

	if (info[1].find("colemak") != -1) {
		return LATIN_KEYBOARD_COLEMAK;
	} else if (info[1].find("qwertz") != -1) {
		return LATIN_KEYBOARD_QWERTZ;
	} else if (info[1].find("azerty") != -1) {
		return LATIN_KEYBOARD_AZERTY;
	} else if (info[1].find("qzerty") != -1) {
		return LATIN_KEYBOARD_QZERTY;
	} else if (info[1].find("dvorak") != -1) {
		return LATIN_KEYBOARD_DVORAK;
	} else if (info[1].find("neo") != -1) {
		return LATIN_KEYBOARD_NEO;
	}

	return LATIN_KEYBOARD_QWERTY;
}

OS_X11::OS_X11() {

#ifdef PULSEAUDIO_ENABLED
	AudioDriverManager::add_driver(&driver_pulseaudio);
#endif

#ifdef ALSA_ENABLED
	AudioDriverManager::add_driver(&driver_alsa);
#endif

	minimized = false;
	xim_style = 0L;
	mouse_mode = MOUSE_MODE_VISIBLE;
}
