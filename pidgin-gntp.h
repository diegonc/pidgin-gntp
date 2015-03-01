#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifndef PURPLE_PLUGINS
# define PURPLE_PLUGINS
#endif

#define PLUGIN_NAME		"Pidgin GNTP"
#define PLUGIN_AUTHOR	"Daniel Dimovski <daniel.k.dimovski@gmail.com>"
#define PLUGIN_DESC		"Plugin sends Pidgin signals to Growl."
#define PLUGIN_ID		"core-pidgin-growl-dkd1"
#define ICON_PATH 		"pixmaps/pidgin/growl_icon.png"
#define REV				"Pidgin-GNTP rev 23"
#define SERVER_IP 		"127.0.0.1:23053"
	
#include <direct.h> // for getcwd
#include <stdlib.h>// for MAX_PATH

char DefaultIcon[_MAX_PATH];

#if 0
	#define DEBUG_MSG(x) MessageBox(0,x,"DEBUG",0);
#else
	#define DEBUG_MSG(x) purple_debug(PURPLE_DEBUG_INFO, "pidgin-gntp", x);
#endif
#define DEBUG_ERROR(x) purple_debug(PURPLE_DEBUG_ERROR, "pidgin-gntp", x);

// standard includes
#include <stdio.h>
#include <unistd.h> 
#include <time.h>

// pidgin includes
#include "internal.h"
#include "connection.h"
#include "account.h"
#include "conversation.h"
#include "core.h"
#include "debug.h"
#include "ft.h"
#include "signals.h"
#include "version.h"
#include "status.h"
#include "savedstatuses.h"

#include "plugin.h"
#include "pluginpref.h"
#include "prefs.h"
#include "network.h"
#include <libpurple/util.h>
#include <pidgin/gtkutils.h>

char* custom_get_buddy_status_text(PurpleBuddy *buddy);

// this projects includes
#include "util.h"

int find_char(char* str, char c);
int find_char_reverse(char* str, char c);
void strip_msn_font_tags(char* str);
int s_strlen(char* str);


GList *buddys_status = NULL;
GList *img_list = NULL;
PurpleStatusPrimitive acc_status = 0;

unsigned int start_tick_im;
unsigned int start_tick_chat;
unsigned int start_tick_image;


char* notifications[] = {
	"buddy-sign-in",
	"buddy-sign-out",
	"buddy-change-status",
	"buddy-change-msg",
	"im-msg-received",
	"connection-error",
	"buddy-change-image",
	"chat-msg-received",
	"chat-buddy-sign-in",
	"chat-buddy-sign-out",
	"chat-invited",
	"chat-topic-change"
};

struct buddy_status
{
	PurpleBuddy *buddy;
	char* status;	
};

gint compare_status(struct buddy_status* a, struct buddy_status* b)
{
	if( a->buddy == b->buddy )
		return 0;
	else 
		return 1;
}

int registered; 