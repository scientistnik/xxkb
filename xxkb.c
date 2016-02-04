/* -*- tab-width: 4; c-basic-offset: 4; -*- */
/*
 * xxkb.c
 *
 *     Main module of the xxkb program.
 *
 *     Copyright (c) 1999-2003, by Ivan Pascal <pascal@tsu.ru>
 */

#include <stdio.h>
#include <err.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/Xlibint.h>
#include <X11/Xatom.h>

#include "xxkb.h"
#include "wlist.h"

#ifdef XT_RESOURCE_SEARCH
#include <X11/IntrinsicP.h>
static XtAppContext app_cont;
#endif

#ifdef SHAPE_EXT
#include <X11/extensions/shape.h>
#endif

#define	BASE(w)		(w & base_mask)
#define	APPNAME		"XXkb"

#define button_update(win, elem, gc, grp)	win_update((win), (elem), (gc), (grp), 0, 0)

/* Global variables */
Display *dpy;
#ifdef SHAPE_EXT
Bool shape_ext = False;
#endif
/* Local variables */
static int win_x = 0, win_y = 0, revert, base_mask;
static GC gc;
static XXkbConfig conf;
static Window root_win, main_win, icon, focused_win;
static Atom systray_selection_atom, take_focus_atom, wm_del_win_atom, wm_manager_atom, xembed_atom, xembed_info_atom, utf8_string_atom, net_wm_name_atom, net_window_type_atom;
static WInfo def_info, *info;
static kbdState def_state;
static XErrorHandler DefErrHandler;

/* Forward function declarations */
static ListAction GetWindowAction(Window w);
static MatchType GetTypeFromState(unsigned int state);
static Window GetSystray(Display *dpy);
static Window GetGrandParent(Window w);
static char* GetWindowIdent(Window appwin, MatchType type);
static void MakeButton(WInfo *info, Window parent);
static void IgnoreWindow(WInfo *info, MatchType type);
static void DockWindow(Display *dpy, Window systray, Window w);
static void MoveOrigin(Display *dpy, Window w, int *w_x, int *w_y);
static void SendDockMessage(Display *dpy, Window w, long message, long data1, long data2, long data3);
static void Reset(void);
static void Terminate(void);
static void DestroyPixmaps(XXkbElement *elem);

static void GetAppWindow(Window w, Window *app);
static WInfo* AddWindow(Window w, Window parent);
static void AdjustWindowPos(Display *dpy, Window win, Window parent, Bool set_gravity);
static Bool ExpectInput(Window win);

int
main(int argc, char ** argv)
{
	int  xkbEventType, xkbError, reason_rtrn, mjr, mnr, scr;
#ifdef SHAPE_EXT
	int shape_event_base, shape_error_base;
#endif
	Bool fout_flag;
	Window systray = None;
	Geometry geom;
	XkbEvent ev;
	XWMHints *wm_hints;
	XSizeHints *size_hints;
	XClassHint *class_hints;
	XSetWindowAttributes win_attr;
	char *display_name, buf[64];
	unsigned long valuemask, xembed_info[2] = { 0, 1 };
	XGCValues values;

	/* Lets begin */
	display_name = NULL;
	mjr = XkbMajorVersion;
	mnr = XkbMinorVersion;
	dpy = XkbOpenDisplay(display_name, &xkbEventType, &xkbError, &mjr, &mnr, &reason_rtrn);
	if (dpy == NULL) {
		warnx("Can't open display named %s", XDisplayName(display_name));
		switch (reason_rtrn) {
		case XkbOD_BadLibraryVersion :
		case XkbOD_BadServerVersion :
			warnx("xxkb was compiled with XKB version %d.%02d",
				  XkbMajorVersion, XkbMinorVersion);
			warnx("But %s uses incompatible version %d.%02d",
				  reason_rtrn == XkbOD_BadLibraryVersion ? "Xlib" : "Xserver",
				  mjr, mnr);
			break;

		case XkbOD_ConnectionRefused :
			warnx("Connection refused");
			break;

		case XkbOD_NonXkbServer:
			warnx("XKB extension not present");
			break;

		default:
			warnx("Unknown error %d from XkbOpenDisplay", reason_rtrn);
			break;
		}
		exit(1);
	}

	scr = DefaultScreen(dpy);
	root_win = RootWindow(dpy, scr);
	base_mask = ~(dpy->resource_mask);
	sprintf(buf, "_NET_SYSTEM_TRAY_S%d", scr);
	systray_selection_atom = XInternAtom(dpy, buf, False);
	take_focus_atom = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	wm_del_win_atom = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wm_manager_atom = XInternAtom(dpy, "MANAGER", False);
	xembed_atom = XInternAtom(dpy, "_XEMBED", False);
	xembed_info_atom = XInternAtom(dpy, "_XEMBED_INFO", False);
	net_wm_name_atom = XInternAtom(dpy, "_NET_WM_NAME", False);
	net_window_type_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	utf8_string_atom = XInternAtom(dpy, "UTF8_STRING", False);

	DefErrHandler = XSetErrorHandler((XErrorHandler) ErrHandler);

#ifdef XT_RESOURCE_SEARCH
	app_cont = XtCreateApplicationContext();
	XtDisplayInitialize(app_cont, dpy, APPNAME, APPNAME, NULL, 0, &argc, argv);
#endif
	/* Do we have SHAPE extension */
#ifdef SHAPE_EXT
	shape_ext = XShapeQueryExtension(dpy, &shape_event_base, &shape_error_base);
#endif
	/* My configuration*/
	memset(&conf, 0, sizeof(conf));
	if (GetConfig(dpy, &conf) != 0) {
		errx(2, "Unable to initialize");
	}
	/* My MAIN window */
	geom = conf.mainwindow.geometry;
	if (conf.controls & Main_enable) {
		if (geom.mask & (XNegative | YNegative)) {
			int x, y;
			unsigned int width, height, border, dep;
			Window rwin;
			XGetGeometry(dpy, root_win, &rwin, &x, &y, &width, &height, &border, &dep);
			if (geom.mask & XNegative) {
				geom.x = width + geom.x - geom.width;
			}
			if (geom.mask & YNegative) {
				geom.y = height + geom.y - geom.height;
			}
		}
	}
	else {
		geom.x = geom.y = 0;
		geom.width = geom.height = 1;
		conf.mainwindow.border_width = 0;
	}
	
	valuemask = 0;
	memset(&win_attr, 0, sizeof(win_attr));
	win_attr.background_pixmap = ParentRelative;
	win_attr.border_pixel = conf.mainwindow.border_color;
	valuemask = CWBackPixmap | CWBorderPixel;
	if (conf.controls & Main_ontop) {
		win_attr.override_redirect = True;
		valuemask |= CWOverrideRedirect;
	}

	main_win = XCreateWindow(dpy, root_win,
					geom.x, geom.y,	geom.width, geom.height,
					conf.mainwindow.border_width,
					CopyFromParent, InputOutput,
					CopyFromParent, valuemask,
					&win_attr);

	XStoreName(dpy, main_win, APPNAME);
	XSetCommand(dpy, main_win, argv, argc);

	/* WMHints */
	wm_hints = XAllocWMHints();
	if (wm_hints == NULL) {
		errx(1, "Unable to allocate WM hints");
	}
	wm_hints->window_group = main_win;
	wm_hints->input = False;
	wm_hints->flags = InputHint | WindowGroupHint;
	XSetWMHints(dpy, main_win, wm_hints);
	XFree(wm_hints);

	/* ClassHints */
	class_hints = XAllocClassHint();
	if (class_hints == NULL) {
		errx(1, "Unable to allocate class hints");
	}
	class_hints->res_name  = APPNAME;
	class_hints->res_class = APPNAME;
	XSetClassHint(dpy, main_win, class_hints);
	XFree(class_hints);

	/* SizeHints */
	size_hints = XAllocSizeHints();
	if (size_hints == NULL) {
		errx(1, "Unable to allocate size hints");
	}
	if (geom.mask & (XValue | YValue)) {
		size_hints->x = geom.x;
		size_hints->y = geom.y;
		size_hints->flags = USPosition;
	}
	size_hints->base_width = size_hints->min_width =
		size_hints->max_width = geom.width;
	size_hints->base_height = size_hints->min_height =
		size_hints->max_height = geom.height;
	size_hints->flags |= PBaseSize | PMinSize | PMaxSize;
	XSetNormalHints(dpy, main_win, size_hints);
	XFree(size_hints);

	/* to fix: fails if mainwindow geometry was not read */
	XSetWMProtocols(dpy, main_win, &wm_del_win_atom, 1);
	XChangeProperty(dpy, main_win, net_wm_name_atom, utf8_string_atom,
					8, PropModeReplace,
					(unsigned char*) APPNAME, strlen(APPNAME));
	XChangeProperty(dpy, main_win, xembed_info_atom, xembed_info_atom, 32, 0,
					(unsigned char *) &xembed_info, 2);

	/* Show window ? */
	if (conf.controls & WMaker) {
		icon = XCreateSimpleWindow(dpy, main_win, geom.x, geom.y,
								   geom.width, geom.height, 0,
								   BlackPixel(dpy, scr), WhitePixel(dpy, scr));

		wm_hints = XGetWMHints(dpy, main_win);
		if (wm_hints != NULL) {
			wm_hints->icon_window = icon;
			wm_hints->initial_state = WithdrawnState;
			wm_hints->flags |= StateHint | IconWindowHint;
			XSetWMHints(dpy, main_win, wm_hints);
			XFree(wm_hints);
		}
	}
	else {
		icon = None;
	}

	if (conf.controls & Main_tray) {
		Atom atom;
		int data = 1;

		atom = XInternAtom(dpy, "KWM_DOCKWINDOW", False);
		if (atom != None) {
			XChangeProperty(dpy, main_win, atom, atom, 32, 0,
							(unsigned char*) &data, 1);
		}

		atom = XInternAtom(dpy, "_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR", False);
		if (atom != None) {
			XChangeProperty(dpy, main_win, atom, XA_WINDOW, 32, 0,
							(unsigned char*) &data, 1);
		}

		systray = GetSystray(dpy);
		if (systray != None) {
			DockWindow(dpy, systray, main_win);
		}
		/* Don't show main window */
		conf.controls &= ~Main_enable;
	}

	/* What events do we want */
	XkbSelectEventDetails(dpy, XkbUseCoreKbd, XkbStateNotify,
						  XkbAllStateComponentsMask, XkbGroupStateMask);
	if (conf.controls & When_create) {
		XSelectInput(dpy, root_win, StructureNotifyMask | SubstructureNotifyMask);
	}

	XSelectInput(dpy, main_win, ExposureMask | ButtonPressMask | ButtonMotionMask | ButtonReleaseMask);
	if (icon) {
		XSelectInput(dpy, icon, ExposureMask | ButtonPressMask);
	}

	valuemask = 0;
	memset(&values, 0, sizeof(XGCValues));
	gc = XCreateGC(dpy, main_win, valuemask, &values);

	/* Set current defaults */
	def_state.group = conf.Base_group;
	def_state.alt = conf.Alt_group;

	def_info.win = icon ? icon : main_win;
	def_info.button = None;
	def_info.state = def_state;

	Reset();

	if (conf.controls & When_start) {
		Window rwin, parent, *children, *child, app;
		int num;

		XQueryTree(dpy, root_win, &rwin, &parent, &children, &num);
		child = children;
		while (num > 0) {
			app = None;
			GetAppWindow(*child, &app);
			if (app != None && app != main_win && app != icon) {
				AddWindow(app, *child);
			}
			child++;
			num--;
		}

		if (children != None)
			XFree(children);

		XGetInputFocus(dpy, &focused_win, &revert);
		info = win_find(focused_win);
		if (info == NULL) {
			info = &def_info;
		}
	}
	/* Show main window */
	if (conf.controls & Main_enable) {
		XMapWindow(dpy, main_win);
	}

	/* Main Loop */
	fout_flag = False;
	while (1) {
		int grp;
		static int move_window = 0;
		static int add_x = 0, add_y = 0;

		XNextEvent(dpy, &ev.core);

		if (ev.type == xkbEventType) {
			switch (ev.any.xkb_type) {
			case XkbStateNotify :
				grp = ev.state.locked_group;

				if ((conf.controls & When_change) && !fout_flag) {
					XGetInputFocus(dpy, &focused_win, &revert);
					if (focused_win == None || focused_win == PointerRoot)
						break;
					if (focused_win != info->win) {
						WInfo *temp_info;
						temp_info = AddWindow(focused_win, focused_win);
						if (temp_info != NULL) {
							info = temp_info;
							info->state.group = grp;
						}
					}
				}
				fout_flag = False;

				if ((conf.controls & Two_state) && ev.state.keycode) {
					int g_min, g_max;
					if (conf.Base_group < info->state.alt) {
						g_min = conf.Base_group;
						g_max = info->state.alt;
					}
					else {
						g_max = conf.Base_group;
						g_min = info->state.alt;
					}

					if ((grp > g_min) && (grp < g_max)) {
						XkbLockGroup(dpy, XkbUseCoreKbd, g_max);
						break;
					}

					if ((grp < g_min) || (grp > g_max)) {
						XkbLockGroup(dpy, XkbUseCoreKbd, g_min);
						break;
					}
				}

				info->state.group = grp;
				if ((conf.controls & Two_state) &&
				    (grp != conf.Base_group) && (grp != info->state.alt)) {
					info->state.alt = grp;
				}

				if (info->button != None) {
					button_update(info->button, &conf.button, gc, grp);
				}
				win_update(main_win, &conf.mainwindow, gc, grp, win_x, win_y);
				if (icon != None) {
					win_update(icon, &conf.mainwindow, gc, grp, win_x, win_y);
				}
				if (conf.controls & Bell_enable) {
					XBell(dpy, conf.Bell_percent);
				}
				break;

			default:
				break;
			}
		} /* xkb events */
		else {
			Window temp_win;
			WInfo *temp_info;
			XClientMessageEvent *cmsg_evt;
			XReparentEvent *repar_evt;
			XButtonEvent *btn_evt;
			XMotionEvent *mov_evt;

			switch (ev.type) {          /* core events */
			case Expose:	/* Update our window or button */
				if (ev.core.xexpose.count != 0)
					break;

				temp_win = ev.core.xexpose.window;
				if (temp_win == main_win) {
					MoveOrigin(dpy, main_win, &win_x, &win_y);
				}
				if (temp_win == main_win || (icon && (temp_win == icon))) {
					win_update(temp_win, &conf.mainwindow, gc, info->state.group, win_x, win_y);
				}
				else {
					temp_info = button_find(temp_win);
					if (temp_info != NULL) {
						button_update(temp_win, &conf.button, gc, temp_info->state.group);
					}
				}
				break;

			case ButtonPress:
				btn_evt = &ev.core.xbutton;
				temp_win = btn_evt->window;
				switch (btn_evt->button) {
				case Button1:
					if (btn_evt->state & ControlMask) {
						int root_x, root_y, mask;
						Window root, child;
						
						move_window = 1;
						XQueryPointer(dpy, temp_win, &root, &child, &root_x, &root_y, &add_x, &add_y, &mask);
						break;
					}

					if (temp_win == info->button || temp_win == main_win ||
					    (icon != None && temp_win == icon)) {
						if (conf.controls & Two_state) {
							if (info->state.group == conf.Base_group) {
								XkbLockGroup(dpy, XkbUseCoreKbd, info->state.alt);
							}
							else {
								XkbLockGroup(dpy, XkbUseCoreKbd, conf.Base_group);
							}
						}
						else {
							if (conf.controls & But1_reverse) {
								XkbLockGroup(dpy, XkbUseCoreKbd, info->state.group - 1);
							}
							else {
								XkbLockGroup(dpy, XkbUseCoreKbd, info->state.group + 1);
							}
						}
					}
					break;

				case Button3:
					if (temp_win == info->button || temp_win == main_win ||
					    (icon != None && temp_win == icon)) {
						if (conf.controls & But3_reverse) {
							XkbLockGroup(dpy, XkbUseCoreKbd, info->state.group - 1);
						}
						else {
							XkbLockGroup(dpy, XkbUseCoreKbd, info->state.group + 1);
						}
					}
					break;

				case Button2:
					if (temp_win != main_win && temp_win != icon) {
						if (conf.controls & Button_delete) {
							XDestroyWindow(dpy, temp_win);
							if (conf.controls & Forget_window) {
								MatchType type;

								type = GetTypeFromState(btn_evt->state);
								if (type == -1)
									break;

								if (temp_win == info->button) {
									IgnoreWindow(info, type);
									Reset();
								} else {
									temp_info = button_find(temp_win);
									if (temp_info == NULL)
										break;

									IgnoreWindow(temp_info, type);
								}
							}
						}
						break;
					}

					if (conf.controls & Main_delete) {
						Terminate();
					}
					break;
				
				case Button4:
					if (temp_win == info->button || temp_win == main_win ||
					    (icon != None && temp_win == icon)) {
						XkbLockGroup(dpy, XkbUseCoreKbd, info->state.group - 1);
					}
					break;

				case Button5:
					if (temp_win == info->button || temp_win == main_win ||
					    (icon != None && temp_win == icon)) {
						XkbLockGroup(dpy, XkbUseCoreKbd, info->state.group + 1);
					}
					break;
				}
				break;
			
			case MotionNotify:
				if (move_window != 0) {
					int x, y;
					Window child;

					mov_evt = &ev.core.xmotion;
					temp_win = mov_evt->window;
					
					/* Don't move window in systray */
					if (temp_win == main_win && (conf.controls & Main_tray)) {
						break;
					}
					
					temp_info = button_find(temp_win);
					if (temp_info != NULL) {
						XTranslateCoordinates(dpy, mov_evt->root, temp_info->parent,
									mov_evt->x_root, mov_evt->y_root,
									&x, &y, &child);
					}
					else {
						x = mov_evt->x_root;
						y = mov_evt->y_root;
					}
					XMoveWindow(dpy, temp_win, x - add_x, y - add_y);
				}
				break;
			
			case ButtonRelease:
				if (move_window != 0) {
					btn_evt = &ev.core.xbutton;
					temp_win = btn_evt->window;
					
					temp_info = button_find(temp_win);
					if (temp_info != NULL) {
						AdjustWindowPos(dpy, temp_win, temp_info->parent, True);
					}
				}
				move_window = 0;
				break;

			case FocusIn:
				info = win_find(ev.core.xfocus.window);
				if (info == NULL) {
					warnx("Oops. FocusEvent from unknown window");
					info = &def_info;
				}

				if (info->ignore == 0) {
					XkbLockGroup(dpy, XkbUseCoreKbd, info->state.group);
				}
				else {
					def_info.state.group = info->state.group;
					info = &def_info;
				}
				break;

			case FocusOut:
				if (conf.controls & Focus_out) {
					temp_info = info;
					info = &def_info;
					info->state.group = conf.Base_group; /*???*/
					if (temp_info->state.group != conf.Base_group) {
						fout_flag = True;
						XkbLockGroup(dpy, XkbUseCoreKbd, info->state.group);
					}
				}
				break;

			case ReparentNotify:
				repar_evt = &ev.core.xreparent;
				temp_win = repar_evt->window;
				if (temp_win != main_win && temp_win != icon &&
                                    repar_evt->parent != root_win &&
				    BASE(repar_evt->parent) != BASE(temp_win) &&
				    repar_evt->override_redirect != True) {
					AddWindow(temp_win, repar_evt->parent);
				}
				break;

			case DestroyNotify:
				if (ev.core.xdestroywindow.event == root_win)
					break;

				temp_win = ev.core.xdestroywindow.window;
				temp_info = win_find(temp_win);
				if (temp_info != NULL) {
					win_free(temp_win);
					if (temp_info == info) {
						Reset();
					}
					break;
				}

				temp_info = button_find(temp_win);
				if (temp_info != NULL) {
					temp_info->button = None;
				}
				break;

			case ConfigureNotify:
				/* Buttons are not enabled */
				if (!(conf.controls & Button_enable)) {
					break;
				}

				/* Adjust position of the button, if necessary */
				temp_win = ev.core.xconfigure.window;
				temp_info = win_find(temp_win);
				if (temp_info != NULL) {
					AdjustWindowPos(dpy, temp_info->button, temp_info->parent, False);
				}

				/* Raise window, if necessary */
				temp_win = ev.core.xconfigure.above;

				if (temp_win == None) {
					break;
				}

				temp_info = button_find(temp_win);
				if (temp_info != NULL) {
					XRaiseWindow(dpy, temp_win);
				}
				break;

			case PropertyNotify:
				temp_win = ev.core.xproperty.window;
				if (temp_win == main_win || temp_win == icon) {
					break;
				}
				
				temp_info = win_find(temp_win);
				if (temp_info == NULL) {
					Window rwin, parent, *children;
					int num;

					XQueryTree(dpy, temp_win, &rwin, &parent, &children, &num);
					AddWindow(temp_win, parent);

					if (children != None) {
						XFree(children);
					}
				}
				break;

			case ClientMessage:
				cmsg_evt = &ev.core.xclient;
				temp_win = cmsg_evt->window;
				if (cmsg_evt->message_type != None && cmsg_evt->format == 32) {
					if (cmsg_evt->message_type == wm_manager_atom
						&& cmsg_evt->data.l[1] == systray_selection_atom
						&& systray == None) {
						systray = cmsg_evt->data.l[2];
						DockWindow(dpy, systray, main_win);
					} 
					else if (cmsg_evt->message_type == xembed_atom &&
							   temp_win == main_win && cmsg_evt->data.l[1] == 0) {
						/* XEMBED_EMBEDDED_NOTIFY */
						MoveOrigin(dpy, main_win, &win_x, &win_y);
						win_update(main_win, &conf.mainwindow, gc, info->state.group, win_x, win_y);
					} 
					else if ((temp_win == main_win || temp_win == icon)
					           && cmsg_evt->data.l[0] == wm_del_win_atom) {
						Terminate();
					}
				}
				break;

			case CreateNotify:
			case NoExpose:
			case UnmapNotify:
			case MapNotify:
			case GravityNotify:
			case MappingNotify:
			case GraphicsExpose:
				/* Ignore these events */
				break;

			default:
				warnx("Unknown event %d", ev.type);
				break;
			}
		}
	}

	return(0);
}

static void
AdjustWindowPos(Display *dpy, Window win, Window parent, Bool set_gravity)
{
	Window rwin;
	int x, y, x1, y1;
	unsigned int w, h, w1, h1, bd, dep;
	XSetWindowAttributes attr;

	XGetGeometry(dpy, parent, &rwin, &x1, &y1, &w1, &h1, &bd, &dep);
	XGetGeometry(dpy, win, &rwin, &x, &y, &w, &h, &bd, &dep);
	/* Normalize coordinates */
	x1 = (x < 0) ? 0 : ((x+w+2*bd) > w1) ? w1-w-2*bd : x;
	y1 = (y < 0) ? 0 : ((y+h+2*bd) > h1) ? h1-h-2*bd : y;
	
	/* Adjust window position, if necessary */
	if (x != x1 || y != y1) {
		XMoveWindow(dpy, win, x1, y1);
	}
	
	if (set_gravity == False) {
		return;
	}

	/* Adjus gravity of the window */
	memset(&attr, 0, sizeof(attr));

	switch(3*((3*x1)/w1) + (3*y1)/h1) {
		case 0:
			attr.win_gravity = NorthWestGravity;
			break;
		case 1:
			attr.win_gravity = WestGravity;
			break;
		case 2:
			attr.win_gravity = SouthWestGravity;
			break;
		case 3:
			attr.win_gravity = NorthGravity;
			break;
		case 4:
			attr.win_gravity = CenterGravity;
			break;
		case 5:
			attr.win_gravity = SouthGravity;
			break;
		case 6:
			attr.win_gravity = NorthEastGravity;
			break;
		case 7:
			attr.win_gravity = EastGravity;
			break;
		case 8:
			attr.win_gravity = SouthEastGravity;
			break;
		default:
			/* warnx("No gravity set"); */
			return;
	}
	XChangeWindowAttributes(dpy, win, CWWinGravity, &attr);
}

static void
Reset()
{
	info = &def_info;
	info->state = def_state;
	XkbLockGroup(dpy, XkbUseCoreKbd, conf.Base_group);
}

static void
Terminate()
{
	win_free_list();
	XFreeGC(dpy, gc);
	
	DestroyPixmaps(&conf.mainwindow);
	DestroyPixmaps(&conf.button);
	
	if (icon != None) {
		XDestroyWindow(dpy, icon);
	}
	if (main_win != None) {
		XDestroyWindow(dpy, main_win);
	}

#ifdef XT_RESOURCE_SEARCH
	XtCloseDisplay(dpy);
#else
	XCloseDisplay(dpy);
#endif

	exit(0);
}

static void
DestroyPixmaps(XXkbElement *elem)
{
	int i = 0;
	for (i = 0; i < MAX_GROUP; i++) {
		if (elem->pictures[i] != None) {
			XFreePixmap(dpy, elem->pictures[i]);
		}
		if (elem->shapemask[i] != None) {
			XFreePixmap(dpy, elem->shapemask[i]);
		}
#ifdef SHAPE_EXT
		if (elem->boundmask[i] != None) {
			XFreePixmap(dpy, elem->boundmask[i]);
		}
#endif
	}
}

static WInfo*
AddWindow(Window win, Window parent)
{
	int ignore = 0;
	WInfo *info;
	ListAction action;
	XWindowAttributes attr;

	/* properties can be unsuitable at this moment so we need to have
	   a posibility to reconsider when they will be changed */
	XSelectInput(dpy, win, PropertyChangeMask);

	/* don't deal with windows that never get a focus */
	if (!ExpectInput(win)) {
		return NULL;
	}

	action = GetWindowAction(win);
	if (((action & Ignore) && !(conf.controls & Ignore_reverse)) ||
		(!(action & Ignore) && (conf.controls & Ignore_reverse))) {
		ignore = 1;
	}

	info = win_find(win);
	if (info == NULL) {
		info = win_add(win, &def_state);
		if (info == NULL) {
			return NULL;
		}
		if (action & AltGrp) {
			info->state.alt = action & GrpMask;
		}
		if (action & InitAltGrp) {
			info->state.group = info->state.alt;
		}
	}

	XGetInputFocus(dpy, &focused_win, &revert);
	XSelectInput(dpy, win, FocusChangeMask | StructureNotifyMask | PropertyChangeMask);
	if (focused_win == win) {
		XFocusChangeEvent focused_evt;
		focused_evt.type = FocusIn;
		focused_evt.display = dpy;
		focused_evt.window = win;
		XSendEvent(dpy, main_win, 0, 0, (XEvent *) &focused_evt);
	}

	info->ignore = ignore;
	if ((conf.controls & Button_enable) && (!info->button) && !ignore) {
		MakeButton(info, parent);
	}

	/* make sure that window still exists */
	if (XGetWindowAttributes(dpy, win, &attr) == 0) {
		/* failed */
		win_free(win);
		return NULL;
	}
	return info;
}

static void
MakeButton(WInfo *info, Window parent)
{
	Window button, rwin;
	int x, y;
	unsigned int width, height, border, dep;
	XSetWindowAttributes butt_attr;
	Geometry geom = conf.button.geometry;

	parent = GetGrandParent(parent);
	if (parent == None) {
		return;
	}

	XGetGeometry(dpy, parent, &rwin, &x, &y, &width, &height, &border, &dep);
	x = (geom.mask & XNegative) ? width  + geom.x - geom.width  : geom.x;
	y = (geom.mask & YNegative) ? height + geom.y - geom.height : geom.y;

	if ((geom.width > width) || (geom.height > height)) {
		return;
	}

	memset(&butt_attr, 0, sizeof(butt_attr));
	butt_attr.background_pixmap = ParentRelative;
	butt_attr.border_pixel = conf.button.border_color;
	butt_attr.override_redirect = True;
	butt_attr.win_gravity = geom.gravity;

	button = XCreateWindow(dpy, parent,
					x, y,
					geom.width, geom.height, conf.button.border_width,
					CopyFromParent, InputOutput, CopyFromParent,
					CWWinGravity | CWOverrideRedirect | CWBackPixmap | CWBorderPixel,
					&butt_attr);

	XSelectInput(dpy, parent, SubstructureNotifyMask);
	XSelectInput(dpy, button, ExposureMask | ButtonPressMask | ButtonMotionMask | ButtonReleaseMask);
	XMapRaised(dpy, button);
	
	info->parent = parent;
	info->button = button;
}


/*
 * GetGrandParent
 * Returns
 *     highest-level parent of the window. The one which is a child of the
 *     root window.
 */

static Window
GetGrandParent(Window w)
{
	Window rwin, parent, *children;
	int num;

	while (1) {
		if (!XQueryTree(dpy, w, &rwin, &parent, &children, &num)) {
			return None;
		}

		if (children != None)
			XFree(children);

		if (parent == rwin) {
			return w;
		}
		w = parent;
	}
}

static void
GetAppWindow(Window win, Window *core)
{
	Window rwin, parent, *children, *child;
	int num;

	if (!XQueryTree(dpy, win, &rwin, &parent, &children, &num)) {
		return;
	}
	child = children;

	while (num != 0) {
		if (BASE(*child) != BASE(win)) {
			*core = *child;
			break;
		}
		GetAppWindow(*child, core);
		if (*core)
			break;
		child++;
		num--;
	}

	if (children != None)
		XFree(children);
}

static Bool
Compare(char *pattern, char *str)
{
	char *i = pattern, *j = str, *sub = i, *lpos = j;
	Bool aster = False;

	do {
		if (*i == '*') {
			i++;
			if (*i == '\0') {
				return True;
			}
			aster = True; sub = i; lpos = j;
			continue;
		}
		if (*i == *j) {
			i++; j++;
			continue;
		}
		if (*j == '\0') {
			return False;
		}

		if (aster) {
			j = ++lpos;
			i = sub;
		} else {
			return False;
		}
	} while (*j || *i);

	return ((*i == *j) ? True : False);
}

static ListAction
searchInList(SearchList *list, char *ident)
{
	ListAction ret = 0;
	int i;

	while (list != NULL) {
		for (i = 0; i < list->num; i++) {
			if (Compare(list->idx[i], ident)) {
				ret |= list->action; /*???*/
				break;
			}
		}
		list = list->next;
	}

	return ret;
}

static ListAction
searchInPropList(SearchList *list, Window win)
{
	ListAction ret = 0;
	int i, j, prop_num;
	Atom *props, *atoms;

	while (list != NULL) {
		atoms = (Atom *) calloc(list->num, sizeof(Atom));
		if (atoms == NULL) {
			return 0;
		}

		XInternAtoms(dpy, list->idx, list->num, False, atoms);
		for (i = 0, j = 0; i < list->num; i++) {
			if (atoms[i] != None) {
				j = 1;
				break;
			}
		}
		if (j != 0) {
			props = XListProperties(dpy, win, &prop_num);
			if (props != NULL) {
				for (i = 0; i < list->num; i++) {
					if (atoms[i] != None) {
						for (j = 0; j < prop_num; j++) {
							if (atoms[i] == props[j]) {
								ret |= list->action;
							}
						}
					}
				}
				XFree(props);
			}
		}
		XFree(atoms);
		list = list->next;
	}

	return ret;
}


/*
 * GetWindowAction
 * Returns
 *     an action associated with a window.
 */

static ListAction
GetWindowAction(Window w)
{
	ListAction ret = 0;
	Status stat;
	XClassHint wm_class;
	char *name;

	stat = XGetClassHint(dpy, w, &wm_class);
	if (stat != 0) {
		/* success */
		ret |= searchInList(conf.app_lists[0], wm_class.res_class);
		ret |= searchInList(conf.app_lists[1], wm_class.res_name);
		XFree(wm_class.res_name);
		XFree(wm_class.res_class);
	}

	stat = XFetchName(dpy, w, &name);
	if (stat != 0 && name != NULL) {
		/* success */
		ret |= searchInList(conf.app_lists[2], name);
		XFree(name);
	}

	ret |= searchInPropList(conf.app_lists[3], w);

	return ret;
}

static Bool
ExpectInput(Window w)
{
	Bool ok = False;
	XWMHints *hints;

	hints = XGetWMHints(dpy, w);
	if (hints != NULL) {
		if ((hints->flags & InputHint) && hints->input) {
			ok = True;
		}
		XFree(hints);
	}

	if (!ok) {
		Atom *protocols;
		Status stat;
		int n, i;

		stat = XGetWMProtocols(dpy, w, &protocols, &n);
		if (stat != 0) {
			/* success */
			for (i = 0; i < n; i++) {
				if (protocols[i] == take_focus_atom) {
					ok = True;
					break;
				}
			}
			XFree(protocols);
		}
	}

	return ok;
}


/*
 * GetWindowIdent
 * Returns
 *     the window identifier, which should be freed by the caller.
 */

static char*
GetWindowIdent(Window appwin, MatchType type)
{
	Status stat;
	XClassHint wm_class;
	char *ident = NULL;

	switch (type) {
	case WMName:
		XFetchName(dpy, appwin, &ident);
		break;

	default:
		stat = XGetClassHint(dpy, appwin, &wm_class);
		if (stat != 0) {
			/* success */
			if (type == WMClassClass) {
				XFree(wm_class.res_name);
				ident = wm_class.res_class;
			} else if (type == WMClassName) {
				XFree(wm_class.res_class);
				ident = wm_class.res_name;
			}
		}
		break;
	}

	return ident;
}


/*
 * IgnoreWindow
 *     Appends a window to the ignore list.
 */

static void
IgnoreWindow(WInfo *info, MatchType type)
{
	char *ident;

	ident = GetWindowIdent(info->win, type);
	if (ident == NULL) {
		XBell(dpy, conf.Bell_percent);
		return;
	}

	AddAppToIgnoreList(&conf, ident, type);
	info->ignore = 1;

	XFree(ident);
}

static MatchType
GetTypeFromState(unsigned int state)
{
	MatchType type = -1;

	switch (state & (ControlMask | ShiftMask)) {
	case 0:
		type = -1;
		break;

	case ControlMask:
		type = WMClassClass;
		break;

	case ShiftMask:
		type = WMName;
		break;

	case ControlMask | ShiftMask:
		type = WMClassName;
		break;
	}

	return type;
}

void
ErrHandler(Display *dpy, XErrorEvent *err)
{
	switch (err->error_code) {
	case BadWindow:
	case BadDrawable:
		/* Ignore these errors */
		break;

	default:
		(*DefErrHandler)(dpy, err);
		break;
	}
}

static Window
GetSystray(Display *dpy)
{
	Window systray = None;

	XGrabServer(dpy);

	systray = XGetSelectionOwner(dpy, systray_selection_atom);
	if (systray != None) {
		XSelectInput(dpy, systray, StructureNotifyMask | PropertyChangeMask);
	}

	XUngrabServer(dpy);

	XFlush(dpy);

	return systray;
}

static void
SendDockMessage(Display* dpy, Window w, long message, long data1, long data2, long data3)
{
	XEvent ev;

	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = w;
	ev.xclient.message_type = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = CurrentTime;
	ev.xclient.data.l[1] = message;
	ev.xclient.data.l[2] = data1;
	ev.xclient.data.l[3] = data2;
	ev.xclient.data.l[4] = data3;

	XSendEvent(dpy, w, False, NoEventMask, &ev);
	XFlush(dpy);
}

static void
DockWindow(Display *dpy, Window systray, Window w)
{
	if (systray != None) {
		SendDockMessage(dpy, systray, SYSTEM_TRAY_REQUEST_DOCK, w, 0, 0);
	}
}

static void
MoveOrigin(Display *dpy, Window w, int *w_x, int *w_y)
{
	Window rwin;
	Geometry geom;
	int x, y;
	unsigned int width, height, border, dep;

	geom = conf.mainwindow.geometry;
	XGetGeometry(dpy, w, &rwin, &x, &y, &width, &height, &border, &dep);

	/* X axis */
	if (width > geom.width + border) {
		*w_x = (width - geom.width - border) / 2;
	}
	/* Y axis */
	if (height > geom.height + border) {
		*w_y = (height - geom.height - border) / 2;
	}
}


/*
 * PrependProgramName
 *     Prepends a program name to the string.
 *
 * Returns
 *     a new string, which must be freed by the caller.
 *     Exits the process if fails, which should never happen.
 */

char*
PrependProgramName(char *string)
{
	size_t len;
	char *result;

	len = strlen(APPNAME) + 1 + strlen(string);

	result = malloc(len + 1);
	if (result == NULL) {
		err(1, NULL);
	}

	strcpy(result, APPNAME);
	strcat(result, ".");
	strcat(result, string);

	return result;
}
