/* -*- tab-width: 4; c-basic-offset: 4; -*- */

typedef struct {
	int	group;
	int	alt;
} kbdState;

typedef struct __WInfo {
	struct __WInfo	*next;
	Window		win;
	Window		parent;
	Window		button;
	kbdState	state;
	int		ignore;
} WInfo;

WInfo* win_add(Window w, kbdState *state);

WInfo* win_find(Window win);
WInfo* button_find(Window button);

void win_update(Window win, XXkbElement *elem, GC gc, int group, int win_x, int win_y);

void win_free(Window w);
void win_free_list(void);
