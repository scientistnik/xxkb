/* -*- tab-width: 4; c-basic-offset: 4; -*- */

#define	MAX_GROUP       4

#define	When_create         (1<<0)
#define	When_change         (1<<1)
#define	When_start          (1<<2)
#define	Focus_out           (1<<3)
#define	Two_state           (1<<4)
#define	Button_enable       (1<<5)
#define	Main_enable         (1<<6)
#define	WMaker              (1<<7)
#define	Button_delete       (1<<8)
#define	Main_delete         (1<<9)
#define	Bell_enable         (1<<10)
#define	Ignore_reverse      (1<<11)

#define	But1_reverse        (1<<12)
#define	But3_reverse        (1<<13)

#define	Forget_window       (1<<14)
#define	Label_enable        (1<<15)
#define	Main_tray           (1<<16)
#define	Main_ontop          (1<<17)

#define	SYSTEM_TRAY_REQUEST_DOCK    0
#define	SYSTEM_TRAY_BEGIN_MESSAGE   1
#define	SYSTEM_TRAY_CANCEL_MESSAGE  2

#define	GrpMask             (0x3)
#define	AltGrp              (1<<2)
#define	InitAltGrp          (1<<3)
#define	Ignore              (1<<4)

typedef struct {
	int	mask;
	int	x,y;
	unsigned int width, height;
	int	gravity;
} Geometry;

typedef enum { T_string, T_bool, T_int, T_ulong } ResType;
typedef enum { WMClassClass = 0, WMClassName, WMName, Prop } MatchType;
typedef int  ListAction;

typedef struct __SearchList {
	ListAction	action;
	MatchType	type;
	int		num;
	char		**idx;
	char		*list;
	struct __SearchList *next;
} SearchList;

typedef	struct {
	Geometry geometry;
	Pixmap   pictures[MAX_GROUP];
	Pixmap   shapemask[MAX_GROUP];
#ifdef SHAPE_EXT
	Pixmap   boundmask[MAX_GROUP];
#endif
	unsigned int  border_width, border_color;
} XXkbElement;

typedef struct {
	unsigned long controls;
	int          Base_group, Alt_group, Bell_percent;
	char*        user_config; /* filename */
	XXkbElement  mainwindow, button;
	SearchList*  app_lists[sizeof(MatchType)];
} XXkbConfig;


/* Implemented in xxkb.c */
extern void ErrHandler(Display *dpy, XErrorEvent *err);
extern char* PrependProgramName(char *string);

/* Implemented in resource.c */
extern int GetConfig(Display *dpy, XXkbConfig *conf);
extern void AddAppToIgnoreList(XXkbConfig *conf, char* app_ident, MatchType type);

extern Display *dpy;

#ifdef SHAPE_EXT
extern Bool shape_ext;
#endif
