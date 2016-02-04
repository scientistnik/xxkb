/* -*- tab-width: 4; c-basic-offset: 4; -*- */
/*
 * resource.c
 *
 *     This module deals with the xxkb configuration.
 *
 *     Copyright (c) 1999-2003, by Ivan Pascal <pascal@tsu.ru>
 *
 *   2003-07-27:
 *     The X Resources subsystem is used to store the configuration.
 *     Settings from $XHOME/app-defaults/XXkb may be overwritten by
 *     the user in $HOME/.xxkbrc file.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <err.h>

#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>

#include "xxkb.h"

#ifdef XT_RESOURCE_SEARCH
#include <X11/IntrinsicP.h>
#endif

#ifdef SVG_GRAPHICS
#include <gdk-pixbuf-xlib/gdk-pixbuf-xlib.h>
#include <librsvg/rsvg.h>
#endif

#include <X11/xpm.h>

#define	USERDEFFILE     ".xxkbrc"

/* Forward function declarations */
static SearchList* MakeSearchList(char *string);
static char* GetAppListName(char *match, char *action);
static void FreeSearchList(SearchList *list);
static void CreateLabel(Display *dpy, XXkbElement *elem, Pixmap *pixmap, Pixmap *mask, char *label, char *font, unsigned int foreground, unsigned int background);
static void LoadImage(Display *dpy, XXkbElement *elem, Pixmap *pixmap, Pixmap *mask, char *filename);
#ifdef SHAPE_EXT
static void CreateShade(Display *dpy, XXkbElement *elem, Pixmap *shape, Pixmap *shade);
#endif

#define	countof(a)	(sizeof(a) / sizeof(a[0]))

/* Deprecated. Keep temporary for compatibility. */
static
struct {
	char  *name;
} IgnoreTable[] = {
	{ "ignore.wm_class.class" },
	{ "ignore.wm_class.name" },
	{ "ignore.wm_name" }
};

/* Deprecated. Maintained only for compatibility reasons. */
static
struct {
	char *name;
	int  res;
} GravityTable[] = {
	{ "NorthEast", NorthEastGravity },
	{ "NorthWest", NorthWestGravity },
	{ "North",     NorthGravity },
	{ "SouthEast", SouthEastGravity },
	{ "SouthWest", SouthWestGravity },
	{ "South",     SouthGravity },
	{ "East",      EastGravity },
	{ "West",      WestGravity },
	{ "Center",    CenterGravity }
};

/* Keep in sync with MatchType and IgnoreTable. */
static
struct {
	char      *name;
	MatchType type;
} MatchTable[] = {
	{ "wm_class_class", WMClassClass },
	{ "wm_class_name",  WMClassName },
	{ "wm_name",        WMName },
	{ "property",       Prop }
};

static
struct {
	char       *name;
	ListAction action;
	int        group;
} ActionTable[] = {
	{ "start_alt",  InitAltGrp, 0 },
	{ "alt_group1", AltGrp,     0 },
	{ "alt_group2", AltGrp,     1 },
	{ "alt_group3", AltGrp,     2 },
	{ "alt_group4", AltGrp,     3 },
	{ "ignore",     Ignore,     0 }
};

static
struct {
	char  *name;
	int   flag;
} ControlsTable[] = {
	{ "add_when_start",		When_start },
	{ "add_when_create",		When_create },
	{ "add_when_change",		When_change },
	{ "focusout",			Focus_out },
	{ "two_state",			Two_state },
	{ "button_delete",		Button_delete },
	{ "button_delete_and_forget",	Forget_window },
	{ "mainwindow_delete",		Main_delete }
};


/*
 * GetRes
 *     Reads the program resource specified by the name. Some resources
 *     may be not required.
 *
 * Returns
 *     nothing.
 *     Exits the process if the required resource was not found.
 */

static void
GetRes(XrmDatabase db, char *name, ResType type, Bool required, void *value)
{
	XrmValue val;
	Bool ok = False;
	char *type_ret, *full_res_name;
	size_t len;

	full_res_name = PrependProgramName(name);
	ok = XrmGetResource(db, full_res_name, "", &type_ret, &val);
	if (!ok) {
		if (required) {
			warnx("Unable to get a default value for the required resource `%s'", full_res_name);
			free(full_res_name);

			/* Be sure to exit if the required resource could not be read. */
			exit(2);
		}
		else {
			free(full_res_name);
			return;
		}
	}
	free(full_res_name);

	switch (type) {
	case T_string:
		len = strlen(val.addr);
		*((char**) value) = malloc(len + 1);
		if (*((char**) value) == NULL) {
			err(1, "Failed to allocate memory for the string");
		}
		strcpy(*((char**) value), val.addr);
		break;

	case T_bool:
		*((Bool*) value) =
			(strncasecmp(val.addr, "true", 4) == 0 ||
			 strncasecmp(val.addr, "yes", 3) == 0 ||
			 strncasecmp(val.addr, "on", 2) == 0);
		break;

	case T_int:
		*((int*) value) = strtol(val.addr, (char**) NULL, 10);
		break;

	case T_ulong:
		*((unsigned long*) value) = strtoul(val.addr, (char**) NULL, 16);
		break;
	}
}

static void
SetRes(XrmDatabase db, char *name, ResType type, void *val)
{
	char *full_res_name;

	full_res_name = PrependProgramName(name);
	switch (type) {
	case T_string:
		XrmPutStringResource(&db, full_res_name, (char*) val);
		break;

	default:
		warnx("Setting a resource of an unsupported type");
		break;
	}

	free(full_res_name);
}

static void
GetColorRes(Display *dpy, XrmDatabase db, char *name, unsigned int *color)
{
	XColor scr_def, exact_def;
	Status stat;
	char   *color_name;

	/* First, try if the color is specified by the name */
	GetRes(db, name, T_string, True, &color_name);
	stat = XAllocNamedColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)),
							color_name, &scr_def, &exact_def);
	if (stat != 0) {
		/* success */
		*color = scr_def.pixel;
		return;
	}

	/* No luck, so it must be hex */
	GetRes(db, name, T_ulong, True, color);
}

static void
GetControlRes(XrmDatabase db, char *name, unsigned long *controls, unsigned long flag)
{
	Bool set;
	GetRes(db, name, T_bool, True, &set);
	if (set)
		*controls |= flag;
	else
		*controls &= ~flag;
}

static void
GetElementRes(Display *dpy, XrmDatabase db, char *window_name, XXkbElement *element)
{
	int i;
	Bool labels_enabled;
	char res_name[64], *str_geom, *str_gravity;
	Pixmap *pixmap = element->pictures;
	Pixmap *shape = element->shapemask;
#ifdef SHAPE_EXT
	Pixmap *bound = element->boundmask;
#endif
	Geometry *geom = &element->geometry;

	sprintf(res_name, "%s.geometry", window_name);
	GetRes(db, res_name, T_string, True, &str_geom);
	geom->mask = XParseGeometry(str_geom, &geom->x, &geom->y, &geom->width, &geom->height);
	if (~geom->mask & AllValues) {
		warnx("Incomplete geometry for %s", window_name);
	}
	
	str_gravity = NULL;
	sprintf(res_name, "%s.gravity", window_name);
	GetRes(db, res_name, T_string, False, &str_gravity);
	if (str_gravity == NULL) {
		/* Get the gravity from the geometry */
		XSizeHints *size_hts = XAllocSizeHints();
		Geometry tmp_geom;
		if (size_hts == NULL) {
			warnx("Unable to allocate size hints");
			free(str_geom);
			return;
		}

		XWMGeometry(dpy, DefaultScreen(dpy), str_geom, NULL, 0, size_hts,
					&tmp_geom.x, &tmp_geom.y,
					&tmp_geom.width, &tmp_geom.height,
					&geom->gravity);
		XFree(size_hts);
	}
	else {
		/* Legacy code */
		for (i = 0; i < countof(GravityTable); i++) {
			if (!strncmp(str_gravity, GravityTable[i].name, strlen(GravityTable[i].name))) {
				geom->gravity = GravityTable[i].res;
				break;
			}
		}
		free(str_gravity);
	}
	free(str_geom);

	/* images or labels? */
	sprintf(res_name, "%s.label.enable", window_name);
	GetRes(db, res_name, T_bool, True, &labels_enabled);
	if (labels_enabled) {
		/* labels */
		unsigned int background, foreground;
		char *font, *label;

		sprintf(res_name, "%s.label.font", window_name);
		GetRes(db, res_name, T_string, True, &font);

		sprintf(res_name, "%s.label.background", window_name);
		GetColorRes(dpy, db, res_name, &background);
		sprintf(res_name, "%s.label.foreground", window_name);
		GetColorRes(dpy, db, res_name, &foreground);

		for (i = 0; i < MAX_GROUP; i++) {
			sprintf(res_name, "%s.label.text.%d", window_name, i + 1);
			label = NULL;
			GetRes(db, res_name, T_string, False, &label);
			if (label == NULL) {
				XkbDescPtr desc;

				desc = XkbAllocKeyboard();
				if (desc == NULL) {
					warnx("Unable to allocate a keyboard description");
					continue;
				}

				XkbGetNames(dpy, XkbGroupNamesMask, desc);
				if (desc->names == NULL || desc->names->groups[i] == 0) {
					/*warnx("Unable to get keyboard names");*/
					continue;
				}

				label = XGetAtomName(dpy, desc->names->groups[i]);
				if (label == NULL) {
					warnx("Unable to get a name of the group %d", i);
					continue;
				}
			}

			CreateLabel(dpy, element, &pixmap[i], &shape[i], label, font, foreground, background);
#ifdef SHAPE_EXT
			CreateShade(dpy, element, &shape[i], &bound[i]);
#endif
			free(label);
		}
		free(font);
	} 
	else {
		/* images */
		char res_name[64], *filename, *fullname, *imgpath;
		size_t len;

		GetRes(db, "image.path", T_string, True, &imgpath);

		for (i = 0; i < MAX_GROUP; i++) {
			sprintf(res_name, "%s.image.%d", window_name, i + 1);
			GetRes(db, res_name, T_string, True, &filename);
			if (filename != NULL && *filename != '\0') {
				if (*filename == '/') {
					LoadImage(dpy, element, &pixmap[i], &shape[i], filename);
#ifdef SHAPE_EXT
					CreateShade(dpy, element, &shape[i], &bound[i]);
#endif
				}
				else {
					len = strlen(imgpath) + 1 + strlen(filename);
					fullname = malloc(len + 1);
					if (fullname == NULL) {
						warn("Failed to allocate memory for full filename");
						free(filename);
						pixmap[i] = None;
						shape[i] = None;
#ifdef SHAPE_EXT
						bound[i] = None;
#endif
						continue;
					}
					sprintf(fullname, "%s/%s", imgpath, filename);
					LoadImage(dpy, element, &pixmap[i], &shape[i], fullname);
#ifdef SHAPE_EXT
					CreateShade(dpy, element, &shape[i], &bound[i]);
#endif
					free(fullname);
				}
				free(filename);
			}
			else {
				pixmap[i] = None;
				shape[i] = None;
#ifdef SHAPE_EXT
				bound[i] = None;
#endif
			}
		}

		free(imgpath);
	}
}

static void
CreateLabel(Display *dpy, XXkbElement *elem, Pixmap *pixmap, Pixmap *mask, char *label, char *font, unsigned int foreground, unsigned int background)
{
	GC gc;
	Pixmap pixId;
	XGCValues values;
	unsigned long valuemask;
	XFontStruct* font_struct = NULL;
	int w = 3, h = 2, depth, scr;

	*pixmap = None;
	*mask = None;

	scr = DefaultScreen(dpy);
	depth = DefaultDepth(dpy, scr);
	pixId = XCreatePixmap(dpy, RootWindow(dpy, scr),
				elem->geometry.width, elem->geometry.height, depth);
	/* Failed to create pixmap */
	if (pixId == None) {
		return;
	}

	valuemask = 0;
	memset(&values, 0, sizeof(XGCValues));
	gc = XCreateGC(dpy, pixId, valuemask, &values);
	/* Clear the box. Fill it with a background color */
	XSetForeground(dpy, gc, background);
	XFillRectangle(dpy, pixId, gc, 0, 0, elem->geometry.width, elem->geometry.height);

	XSetForeground(dpy, gc, foreground);
	XSetBackground(dpy, gc, background);
	/* Load and set the font */
	font_struct = XLoadQueryFont(dpy, font);
	if (font_struct != NULL) {
		XSetFont(dpy, gc, font_struct->fid);
		h = (elem->geometry.width -
		    	 (font_struct->max_bounds.rbearing
				- font_struct->min_bounds.lbearing) * strlen(label)) / 2;
		
		w = (elem->geometry.height -
			 (font_struct->ascent + font_struct->descent)) / 2
			 	+ font_struct->descent;
		
		XFreeFont(dpy, font_struct);
	}
	XDrawString(dpy, pixId, gc,
		    h + 1, elem->geometry.height - w,
		    label, strlen(label));
	
	XFreeGC(dpy, gc);
	
	*pixmap = pixId;
}

static void
LoadImage(Display *dpy, XXkbElement *elem, Pixmap *pixmap, Pixmap *mask, char *filename)
{
#ifdef SVG_GRAPHICS
	GdkPixbuf *pixbuf;
	GError *error = NULL;

	*pixmap = None;
	*mask = None;

	pixbuf = rsvg_pixbuf_from_file_at_size(filename, elem->geometry.width, elem->geometry.height, &error);
	if (pixbuf == NULL) {
		if (error != NULL) {
			warnx("SVG file `%s': %s", filename, error->message);
		}
		else {
			warnx("SVG file `%s' is invalid", filename);
		}
		return;
	}

	gdk_pixbuf_xlib_render_pixmap_and_mask(pixbuf, pixmap, mask, 127);

	g_object_unref(G_OBJECT(pixbuf));
#else
	int res;
	Pixmap picture, shape;

	*pixmap = None;
	*mask = None;

	res = XpmReadFileToPixmap(dpy, RootWindow(dpy, DefaultScreen(dpy)), filename, &picture, &shape, NULL);
	switch (res) {
	case XpmOpenFailed:
		warnx("Unable to open xpm file `%s'", filename);
		break;

	case XpmFileInvalid:
		warnx("Xpm file `%s' is invalid", filename);
		break;

	case XpmNoMemory:
		warnx("No memory for open xpm file `%s'", filename);
		break;

	default:
		*pixmap = picture;
		*mask = shape;
		break;
	}
#endif
}

#ifdef SHAPE_EXT
static void
CreateShade(Display *dpy, XXkbElement *elem, Pixmap *shape, Pixmap *shade)
{
	GC gc;
	Pixmap shadow;
	XGCValues values;
	unsigned long valuemask;
	int scr;
	
	*shade = None;
	
	scr = DefaultScreen(dpy);
	shadow = XCreatePixmap(dpy, RootWindow(dpy, scr),
			elem->geometry.width + 2*elem->border_width,
			elem->geometry.height + 2*elem->border_width,
			1);
	/* Failed to create pixmap */
	if (shadow == None) {
		return;
	}
	valuemask = 0;
	memset(&values, 0, sizeof(XGCValues));
	gc = XCreateGC(dpy, shadow, valuemask, &values);
	/* Clear the box. Fill it with a background color */
	XSetForeground(dpy, gc, 1);
	XFillRectangle(dpy, shadow, gc,
			0, 0,
			elem->geometry.width + 2*elem->border_width,
			elem->geometry.height + 2*elem->border_width);
			
	valuemask = GCFunction;
	memset(&values, 0, sizeof(XGCValues));
	values.function = GXorReverse;
	XChangeGC(dpy, gc, valuemask, &values);
	if (shape != NULL && *shape != None) {
	    XCopyArea(dpy, *shape, shadow, gc,
			0, 0,
			elem->geometry.width,
			elem->geometry.height,
			elem->border_width,
			elem->border_width);
	}
	XFreeGC(dpy, gc);
	
	*shade = shadow;
}
#endif

/*
 * GetConfig
 *     Main routine of this module. Fills in the config object.
 *
 * Returns
 *     0 on success.
 */

int
GetConfig(Display *dpy, XXkbConfig *conf)
{
	XrmDatabase db;
	SearchList *list;
	Status stat;
	char *homedir, *filename;
	char *str_list, *res_app_list, res_ctrls[256];
	int i, j;
#ifndef XT_RESOURCE_SEARCH
	size_t len;
#endif

	/*
	 * Init pixbuf engine
	 */
#ifdef SVG_GRAPHICS
	g_type_init();
	gdk_pixbuf_xlib_init(dpy, DefaultScreen(dpy));
#endif

	homedir = getenv("HOME");

	XrmInitialize();
	/*
	 * read global settings
	 */
#ifdef XT_RESOURCE_SEARCH
	filename = XtResolvePathname(dpy, "app-defaults", NULL, NULL, NULL, NULL, 0, NULL);
#else
	len = strlen(APPDEFDIR) + 1 + strlen(APPDEFFILE);
	filename = malloc(len + 1);
	if (filename == NULL) {
		warn(NULL);
		return 1;
	}
	sprintf(filename, "%s/%s", APPDEFDIR, APPDEFFILE);
#endif
	db = XrmGetFileDatabase(filename);
	if (db == NULL) {
		/*
		 * this situation is not fatal if the user has all
		 * configuration in his $HOME/.xxkbrc file.
		 */
		warnx("Unable to open default resource file `%s'", filename);
	}
#ifdef XT_RESOURCE_SEARCH
	XtFree(filename);
#else
	free(filename);
#endif
	/*
	 * read user-specific settings
	 */
#ifdef XT_RESOURCE_SEARCH
	filename = XtResolvePathname(dpy, homedir, USERDEFFILE, NULL, "%T/%L/%N%C:%T/%l/%N%C:%T/%N%C:%T/%L/%N:%T/%l/%N:%T/%N", NULL, 0, NULL);
#else
	len = strlen(homedir) + 1 + strlen(USERDEFFILE);
	filename = malloc(len + 1);
	if (filename == NULL) {
		warn(NULL);
		XrmDestroyDatabase(db);
		return 1;
	}
	sprintf(filename, "%s/%s", homedir, USERDEFFILE);
#endif
	/*
	 * merge settings
	 */
	stat = XrmCombineFileDatabase(filename, &db, True);
	if (stat == 0 && db == NULL) {
		/* failed */
		warnx("Unable to find configuration data");
		return 5;
	}
	/*
	 * start with the conf object
	 */
	conf->user_config = filename;

	for (i = 0; i < countof(ControlsTable); i++) {
		sprintf(res_ctrls, "controls.%s", ControlsTable[i].name);
		GetControlRes(db, res_ctrls, &conf->controls, ControlsTable[i].flag);
	}

	GetRes(db, "group.base", T_int, True, &conf->Base_group);
	GetRes(db, "group.alt", T_int, True, &conf->Alt_group);
	conf->Base_group--;
	conf->Alt_group--;

	GetControlRes(db, "bell.enable", &conf->controls, Bell_enable);
	if (conf->controls & Bell_enable) {
		GetRes(db, "bell.percent", T_int, True, &conf->Bell_percent);
	}

	GetControlRes(db, "mainwindow.enable", &conf->controls, Main_enable);
	if (conf->controls & Main_enable) {
		char *type;
		
		GetRes(db, "mainwindow.type", T_string, True, &type);
		if (strncasecmp(type, "wmaker", 6) == 0) {
			conf->controls |= WMaker;
		}
		else if (strncasecmp(type, "tray", 4) == 0) {
			conf->controls |= Main_tray;
		}
		else if (strncasecmp(type, "top", 3) == 0) {
			conf->controls |= Main_ontop;
		}
		else if (strncasecmp(type, "normal", 6) == 0) {
			conf->controls &= ~(Main_tray | Main_ontop | WMaker);
		}
		else {
		    errx(2, "Unknown window type '%s'", type);
		}

		if (!(conf->controls & Main_tray)) {
			GetRes(db, "mainwindow.border.width", T_int, True, &conf->mainwindow.border_width);
			GetColorRes(dpy, db, "mainwindow.border.color", &conf->mainwindow.border_color);
		}
		GetElementRes(dpy, db, "mainwindow", &conf->mainwindow);
	}
	
	GetControlRes(db, "button.enable", &conf->controls, Button_enable);
	if (conf->controls & Button_enable) {
		GetRes(db, "button.border.width", T_int, True, &conf->button.border_width);
		GetColorRes(dpy, db, "button.border.color", &conf->button.border_color);
		GetElementRes(dpy, db, "button", &conf->button);
	}

	for (i = 0; i < countof(MatchTable); i++) {
		for (j = 0; j < countof(ActionTable); j++) {
			res_app_list = GetAppListName(MatchTable[i].name, ActionTable[j].name);
			if (res_app_list == NULL)
				continue;

			str_list = NULL;
			GetRes(db, res_app_list, T_string, False, &str_list);
			free(res_app_list);
			if (str_list == NULL)
				continue;

			list = MakeSearchList(str_list);
			free(str_list);
			if (list == NULL)
				continue;

			list->action = ActionTable[j].action
				| (ActionTable[j].group & GrpMask);
			list->type = MatchTable[i].type;
			list->next = conf->app_lists[i];
			conf->app_lists[i] = list;
		}
	}
	/* XXX: keep temporary for compatibility */
	for (i = 0; i < countof(IgnoreTable); i++) {
		str_list = NULL;
		GetRes(db, IgnoreTable[i].name, T_string, False, &str_list);
		if (str_list == NULL)
			continue;

		list = MakeSearchList(str_list);
		free(str_list);
		if (list == NULL)
			continue;

		list->action = Ignore;
		list->type = MatchTable[i].type;
		list->next = conf->app_lists[i];
		conf->app_lists[i] = list;
	}

	GetControlRes(db, "ignore.reverse", &conf->controls, Ignore_reverse);

	GetControlRes(db, "mousebutton.1.reverse", &conf->controls, But1_reverse);
	GetControlRes(db, "mousebutton.3.reverse", &conf->controls, But3_reverse);

	XrmDestroyDatabase(db);

	return 0;
}


void
AddAppToIgnoreList(XXkbConfig *conf, char *app_ident, MatchType ident_type)
{
	XrmDatabase db;
 	SearchList *cur, *prev, *list;
	char *res_name, *new_list, *orig_list;
	size_t len;

	/* read the current list once again before updating it */
	XrmInitialize();

	db = XrmGetFileDatabase(conf->user_config);
	if (db == NULL) {
		warnx("Unable to open resource file `%s'", conf->user_config);
		return;
	}

	res_name = GetAppListName(MatchTable[ident_type].name, "ignore");
	if (res_name == NULL) {
		return;
	}

	len = strlen(app_ident);

	orig_list = NULL;
	GetRes(db, res_name, T_string, False, &orig_list);
	if (orig_list != NULL) {
		len += strlen(orig_list);
		len += 1; /* 1 for the space-separator */
	}
	/* create a new list */
	new_list = malloc(len + 1);
	if (new_list == NULL) {
		warn("Failed to allocate memory for ignore list");
		free(orig_list);
		free(res_name);
		XrmDestroyDatabase(db);
		return;
	}
	/* fill in the new list */
	*new_list = '\0';
	if (orig_list != NULL) {
		strcat(new_list, orig_list);
		strcat(new_list, " ");
	}
	strcat(new_list, app_ident);
	/* parse the new list */
	list = MakeSearchList(new_list);
	if (list == NULL) {
		free(orig_list);
		free(res_name);
		free(new_list);
		XrmDestroyDatabase(db);
		return;
	}
	list->action = Ignore;
	list->type = ident_type;
	list->next = conf->app_lists[ident_type];
	conf->app_lists[ident_type] = list;

	for (prev = list, cur = list->next;
		 cur != NULL && cur->action != Ignore;
		 prev = cur, cur = cur->next) /* empty body */;

	if (cur != NULL) {
		prev->next = cur->next;
		FreeSearchList(cur);
	}
	/* we have an updated list now,
	 * let's update the resource database
	 */
	SetRes(db, res_name, T_string, new_list);
	free(new_list);
	free(res_name);
	free(orig_list);
	/* save the database */
	XrmPutFileDatabase(db, conf->user_config);
	XrmDestroyDatabase(db);
}

/*
 * MakeSearchList
 *     Converts a space- (and tab-) separated list into a list
 *     of NUL-separated chunks.
 *
 * Returns
 *     A pointer to the list on success, and
 *     a NULL pointer on failure.
 */

#define	IS_SEPARATOR(a)		(((a) == ' ') || ((a) == '\t'))
#define	IS_NOT_SEPARATOR(a)	(!IS_SEPARATOR(a))

static SearchList*
MakeSearchList(char *str)
{
	size_t len;
	int count;
	char *i, *j;
	SearchList *ret;

	/* allocate the memory for the list */
	ret = malloc(sizeof(SearchList));
	if (ret == NULL) {
		warn("Failed to allocate memory for search list");
		return NULL;
	}

	/* initialize the list structure */
	memset(ret, 0, sizeof(SearchList));

	len = strlen(str);
	if (len == 0) {
		return ret;
	}

	ret->list = malloc(len + 1);
	if (ret->list == NULL) {
		warn("Failed to allocate memory for search list");
		free(ret);
		return NULL;
	}

	/* tokenize the string */
	i = str; j = ret->list; count = 0;
	while (len) {
		count++;
		while (IS_NOT_SEPARATOR(*i)) {
			*j++ = *i++;
			if (!(--len)) {
				*j = '\0';
				break;
			}
		}
		*j++ = '\0';
		while (IS_SEPARATOR(*i)) {
			i++;
			if (!(--len))
				break;
		}
	}
	ret->num = count;

	/* allocate the memory for the index list */
	ret->idx = malloc(count * sizeof(char*));
	if (ret->idx == NULL) {
		warn("Failed to allocate memory");
		free(ret->list);
		free(ret);
		return NULL;
	}

	/* store the chunk pointers */
	for (count = 0, i = ret->list; count < ret->num; count++) {
		ret->idx[count] = i;
		while (*i++) /* empty body */;
	}

	return ret;
}

/*
 * FreeSearchList
 *     cleans the search list structure.
 */

static void
FreeSearchList(SearchList *list)
{
	if (list == NULL)
		return;
	free(list->list);
	free(list->idx);
	free(list);
}

/*
 * GetAppListName
 * Returns
 *     a resource name for a app_list resource.
 *
 * Note
 *     Caller must free the returned pointer.
 */

static char*
GetAppListName(char *match, char *action)
{
	char *res_patt = "app_list.%s.%s", *res_name;
	size_t len;

	len = strlen(res_patt) + strlen(match) + strlen(action);
	res_name = malloc(len + 1);
	if (res_name == NULL) {
		warn("Faile dto allocate memory for res name");
		return NULL;
	}

	sprintf(res_name, res_patt, match, action);

	return res_name;
}
