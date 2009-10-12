#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifndef PURPLE_PLUGINS
# define PURPLE_PLUGINS
#endif

#define PLUGIN_NAME		"Pidgin GNTP"
#define PLUGIN_AUTHOR	"Daniel Dimovski <daniel.k.dimovski@gmail.com>"
#define PLUGIN_DESC		"Plugin sends Pidgin signals to Growl."
#define PLUGIN_ID		"core-pidgin-growl-dkd1"
#define ICON_PATH 		"http://developer.pidgin.im/attachment/wiki/SpreadPidginAvatars/pidgin.2.png?format=raw"

#define SERVER_IP 		"127.0.0.1:23053"

// standard includes
#include <stdio.h>
#include <unistd.h> 
#include <time.h>

// pidgin includes
#include "internal.h"
#include "connection.h"
#include "conversation.h"
#include "core.h"
#include "debug.h"
#include "ft.h"
#include "signals.h"
#include "version.h"
#include "status.h"


#include "plugin.h"
#include "pluginpref.h"
#include "prefs.h"

// this projects includes
#include "linkedlist.h"


unsigned int start_tick_im;
unsigned int start_tick_chat;
unsigned int start_tick_image;

int find_char(char* str, char c);
int find_char_reverse(char* str, char c);
void strip_msn_font_tags(char* str);
int s_strlen(char* str);

item* buddy_icon_list = NULL;

char* notifications[] = {
	"buddy-sign-in",
	"buddy-sign-out",
	"im-msg-recived",
	"connection-error",
	"buddy-change-image",
	"chat-msg-recived",
	"chat-buddy-sign-in",
	"chat-buddy-sign-out",
	"chat-invited",
	"chat-topic-change"
};

/**************************************************************************
 * send growl message (from mattn's gntp-send commandline program)
 * http://github.com/mattn/gntp-send/tree/master
 **************************************************************************/
void gntp_register(char* password);
void gntp_notify(char* notify, char* icon, char* title, char* message, char* password);
#include "gntp-send.h"



/**************************************************************************
 * Buddy Icons signal callbacks
 **************************************************************************/
static void
buddy_icon_changed_cb(PurpleBuddy *buddy)
{	
	//hack to hide spam when signing on to account
	if( GetTickCount() - start_tick_im < 10000 ) return;
	if( GetTickCount() - start_tick_image < 3000 ) return;

	char* buddy_nick = purple_buddy_get_alias(buddy);
	char* buddy_name = purple_buddy_get_name(buddy);
	PurpleBuddyIcon* icon = purple_buddy_get_icon(buddy);
	char* icon_path = purple_buddy_icon_get_full_path(icon);

	if(list_find(buddy_icon_list, icon_path))
		return;
	
	list_add(buddy_icon_list, buddy_name,icon_path);
	
	int len = s_strlen(buddy_nick) + s_strlen(buddy_name);
		
	char *growl_msg = malloc( len + 20 );
	sprintf(growl_msg,"%s changed image\n(%s)", buddy_nick, buddy_name );
	
	gntp_notify("buddy-change-image", icon_path, "Pidgin", growl_msg, NULL);
	free(growl_msg);
}

/**************************************************************************
 * Buddy List subsystem signal callbacks
 **************************************************************************/
static void
buddy_signed_on_cb(PurpleBuddy *buddy, void *data)
{
	start_tick_image = GetTickCount();
	//hack to hide spam when signing on to account
	if( GetTickCount() - start_tick_im < 10000) return;
	
	char* buddy_nick = purple_buddy_get_alias(buddy);
	char* buddy_name = purple_buddy_get_name(buddy);
	PurpleBuddyIcon* icon = purple_buddy_get_icon(buddy);
	char* icon_path = purple_buddy_icon_get_full_path(icon);
	
	
	int len = 10;
	if(buddy_nick != NULL)
		len = strlen(buddy_nick);
	if(buddy_name  != NULL )
		 len += strlen(buddy_name);
		
	char *growl_msg = malloc( len + 20 );
	
	sprintf(growl_msg,"%s signed in\n(%s)", buddy_nick, buddy_name );
	gntp_notify("buddy-sign-in", icon_path, "Pidgin", growl_msg, NULL);
	
	free(growl_msg);
}

static void
buddy_signed_off_cb(PurpleBuddy *buddy, void *data)
{
	char* buddy_nick = purple_buddy_get_alias(buddy);
	char* buddy_name = purple_buddy_get_name(buddy);
	PurpleBuddyIcon* icon = purple_buddy_get_icon(buddy);
	char* icon_path = purple_buddy_icon_get_full_path(icon);
		
	int len = s_strlen(buddy_nick) + s_strlen(buddy_name);
	
	char *growl_msg = malloc( len + 20 );
	
	sprintf(growl_msg,"%s signed out\n(%s)", buddy_nick, buddy_name );
	gntp_notify("buddy-sign-out", icon_path, "Pidgin", growl_msg, NULL);
	
	free(growl_msg);
}

/**************************************************************************
 * Connection subsystem signal callbacks
 **************************************************************************/
static void
signed_on_cb(PurpleConnection *gc, void *data)
{
	start_tick_im = GetTickCount();
}

static void
signed_off_cb(PurpleConnection *gc, void *data)
{
	
}

static void
connection_error_cb(PurpleConnection *gc, PurpleConnectionError err,
                    const gchar *desc, void *data)
{
	PurpleAccount* account = purple_connection_get_account(gc);
	const gchar *username =	purple_account_get_username(account);


	int len = s_strlen(desc) + s_strlen(username);
	
	char *growl_msg = malloc( len + 25 );
	sprintf(growl_msg, "%s\ncode: %u\n%s", desc, err, username);
	
	gntp_notify("connection-error", NULL, "Connection Error", growl_msg, NULL);
	
	free(growl_msg);
}

/**************************************************************************
 * Conversation subsystem signal callbacks
 **************************************************************************/

static void
wrote_im_msg_cb(PurpleAccount *account, const char *who, const char *buffer,
				PurpleConversation *conv, PurpleMessageFlags flags, void *data)
{
	
}

static void
sent_im_msg_cb(PurpleAccount *account, const char *recipient, const char *buffer, void *data)
{
}

static void
received_im_msg_cb(PurpleAccount *account, char *sender, char *buffer,
				   PurpleConversation *conv, PurpleMessageFlags flags, void *data)
{
	gboolean on_focus = purple_prefs_get_bool("/plugins/core/pidgin-gntp/bool");
	if(!on_focus && conv->ui_ops->has_focus(conv))
			return;
		
	char *message, *notification, *buddy_nick, *iconpath;
	PurpleBuddy* buddy;
	PurpleBuddyIcon* icon;
	
	// copy string to temporary variable)
	message = malloc(strlen(buffer)+1);
	strcpy(message, buffer);
	special_entries(message);
	strip_tags(message);

	// nickname
	buddy = purple_find_buddy(account, sender);
	buddy_nick = purple_buddy_get_alias( buddy );

	int len = s_strlen(buddy_nick) + s_strlen(message);

	// message
	notification = malloc( len + 10 );
	sprintf(notification, "%s: %s", buddy_nick, message);
	
	// icon
	icon = purple_buddy_get_icon( buddy );
	iconpath = purple_buddy_icon_get_full_path( icon );
	
	gntp_notify("im-msg-recived", iconpath, "IM Recived", notification, NULL);
	
	free(message);
	free(notification);
}

static void
wrote_chat_msg_cb(PurpleAccount *account, const char *who, const char *buffer,
				PurpleConversation *conv, PurpleMessageFlags flags, void *data)
{
}

static void
sent_chat_msg_cb(PurpleAccount *account, const char *buffer, int id, void *data)
{
}

static void
received_chat_msg_cb(PurpleAccount *account, char *sender, char *buffer,
					 PurpleConversation *chat, PurpleMessageFlags flags, void *data)
{
	char *message, *notification;

	// copy string to temporary variable)
	message = malloc(s_strlen(buffer)+1);
	strcpy(message, buffer);
	special_entries(message);
	strip_tags(message);
	
	// message
	notification = malloc( s_strlen(sender)+s_strlen(message) + 5 );
	sprintf(notification, "%s: %s", sender, message);
	
	gntp_notify("chat-msg-recived", NULL, "Chat Message", notification, NULL);
}

static void
conversation_created_cb(PurpleConversation *conv, void *data)
{
}

static void
chat_buddy_joined_cb(PurpleConversation *conv, const char *user,
					 PurpleConvChatBuddyFlags flags, gboolean new_arrival, void *data)
{
	//hack to hide spam when join channel
	if( GetTickCount() - start_tick_chat < 10000) return;

	char *notification;

	notification = malloc( s_strlen(conv->title)+s_strlen(user) + 20 );
	sprintf(notification, "%s joined %s", user, conv->title);
	
	gntp_notify("chat-buddy-sign-in", NULL, "Chat Join", notification, NULL);
}

static void
chat_buddy_flags_cb(PurpleConversation *conv, const char *user,
					PurpleConvChatBuddyFlags oldflags, PurpleConvChatBuddyFlags newflags, void *data)
{
}

static void
chat_buddy_left_cb(PurpleConversation *conv, const char *user,
				   const char *reason, void *data)
{
	char *notification;

	notification = malloc( s_strlen(conv->title)+s_strlen(user) + 20 );
	sprintf(notification, "%s left %s", user, conv->title);
	
	gntp_notify("chat-buddy-sign-out", NULL, "Chat Leave", notification, NULL);
}

static void
chat_invited_user_cb(PurpleConversation *conv, const char *name,
					  const char *reason, void *data)
{
}

static gint
chat_invited_cb(PurpleAccount *account, const char *inviter,
				const char *room_name, const char *message,
				const GHashTable *components, void *data)
{
	char *notification;

	notification = malloc( s_strlen(inviter)+s_strlen(room_name)+s_strlen(message) + 20 );
	sprintf(notification, "%s has invited you to %s\n%s", inviter, room_name, message);
	
	gntp_notify("chat-invited", NULL, "Chat Invite", notification, NULL);
	
	return 0;
}

static void
chat_joined_cb(PurpleConversation *conv, void *data)
{
	start_tick_chat = GetTickCount();
}

static void
chat_left_cb(PurpleConversation *conv, void *data)
{
}

static void
chat_topic_changed_cb(PurpleConversation *conv, const char *who,
					  const char *topic, void *data)
{
	char *notification;

	notification = malloc( s_strlen(who)+s_strlen(conv->title)+s_strlen(topic) + 25 );
	sprintf(notification, "%s topic: %s\nby %s", conv->title, topic, who);
	
	gntp_notify("chat-topic-change", NULL, "Chat Topic Changed", notification, NULL);
}

/**************************************************************************
 * Core signal callbacks
 **************************************************************************/
static void
quitting_cb(void *data)
{
}

/**************************************************************************
 * File transfer signal callbacks
 **************************************************************************/
static void
ft_recv_accept_cb(PurpleXfer *xfer, gpointer data) {
}

static void
ft_send_accept_cb(PurpleXfer *xfer, gpointer data) {
}

static void
ft_recv_start_cb(PurpleXfer *xfer, gpointer data) {
}

static void
ft_send_start_cb(PurpleXfer *xfer, gpointer data) {
}

static void
ft_recv_cancel_cb(PurpleXfer *xfer, gpointer data) {
}

static void
ft_send_cancel_cb(PurpleXfer *xfer, gpointer data) {
}

static void
ft_recv_complete_cb(PurpleXfer *xfer, gpointer data) {
}

static void
ft_send_complete_cb(PurpleXfer *xfer, gpointer data) {
}

/**************************************************************************
 * Notify signals callbacks
 **************************************************************************/
static void
notify_email_cb(char *subject, char *from, char *to, char *url) {
}

static void
notify_emails_cb(char **subjects, char **froms, char **tos, char **urls, guint count) {
}

/**************************************************************************
 * Plugin stuff
 **************************************************************************/
static gboolean
plugin_load(PurplePlugin *plugin)
{
	gntp_register(NULL);

	void *core_handle     = purple_get_core();
	void *blist_handle    = purple_blist_get_handle();
	void *conn_handle     = purple_connections_get_handle();
	void *conv_handle     = purple_conversations_get_handle();
	void *ft_handle       = purple_xfers_get_handle();
	void *notify_handle   = purple_notify_get_handle();

	/* Buddy List subsystem signals */
	purple_signal_connect(blist_handle, "buddy-signed-on",
						plugin, PURPLE_CALLBACK(buddy_signed_on_cb), NULL);
	purple_signal_connect(blist_handle, "buddy-signed-off",
						plugin, PURPLE_CALLBACK(buddy_signed_off_cb), NULL);
	purple_signal_connect(blist_handle, "buddy-icon-changed",
						plugin, PURPLE_CALLBACK(buddy_icon_changed_cb), NULL);
						
	/* Connection subsystem signals */
	purple_signal_connect(conn_handle, "signed-on",
						plugin, PURPLE_CALLBACK(signed_on_cb), NULL);
	purple_signal_connect(conn_handle, "signed-off",
						plugin, PURPLE_CALLBACK(signed_off_cb), NULL);
	purple_signal_connect(conn_handle, "connection-error",
						plugin, PURPLE_CALLBACK(connection_error_cb), NULL);

	/* Conversations subsystem signals */
	purple_signal_connect(conv_handle, "wrote-im-msg",
						plugin, PURPLE_CALLBACK(wrote_im_msg_cb), NULL);
	purple_signal_connect(conv_handle, "sent-im-msg",
						plugin, PURPLE_CALLBACK(sent_im_msg_cb), NULL);
	purple_signal_connect(conv_handle, "received-im-msg",
						plugin, PURPLE_CALLBACK(received_im_msg_cb), NULL);
	purple_signal_connect(conv_handle, "wrote-chat-msg",
						plugin, PURPLE_CALLBACK(wrote_chat_msg_cb), NULL);
	purple_signal_connect(conv_handle, "sent-chat-msg",
						plugin, PURPLE_CALLBACK(sent_chat_msg_cb), NULL);
	purple_signal_connect(conv_handle, "received-chat-msg",
						plugin, PURPLE_CALLBACK(received_chat_msg_cb), NULL);
	purple_signal_connect(conv_handle, "conversation-created",
						plugin, PURPLE_CALLBACK(conversation_created_cb), NULL);
	purple_signal_connect(conv_handle, "chat-buddy-joined",
						plugin, PURPLE_CALLBACK(chat_buddy_joined_cb), NULL);
	purple_signal_connect(conv_handle, "chat-buddy-flags",
						plugin, PURPLE_CALLBACK(chat_buddy_flags_cb), NULL);
	purple_signal_connect(conv_handle, "chat-buddy-left",
						plugin, PURPLE_CALLBACK(chat_buddy_left_cb), NULL);
	purple_signal_connect(conv_handle, "chat-invited-user",
						plugin, PURPLE_CALLBACK(chat_invited_user_cb), NULL);
	purple_signal_connect(conv_handle, "chat-invited",
						plugin, PURPLE_CALLBACK(chat_invited_cb), NULL);
	purple_signal_connect(conv_handle, "chat-joined",
						plugin, PURPLE_CALLBACK(chat_joined_cb), NULL);
	purple_signal_connect(conv_handle, "chat-left",
						plugin, PURPLE_CALLBACK(chat_left_cb), NULL);
	purple_signal_connect(conv_handle, "chat-topic-changed",
						plugin, PURPLE_CALLBACK(chat_topic_changed_cb), NULL);

	/* Core signals */
	purple_signal_connect(core_handle, "quitting",
						plugin, PURPLE_CALLBACK(quitting_cb), NULL);

	/* File transfer signals */
	purple_signal_connect(ft_handle, "file-recv-accept",
						plugin, PURPLE_CALLBACK(ft_recv_accept_cb), NULL);
	purple_signal_connect(ft_handle, "file-recv-start",
						plugin, PURPLE_CALLBACK(ft_recv_start_cb), NULL);
	purple_signal_connect(ft_handle, "file-recv-cancel",
						plugin, PURPLE_CALLBACK(ft_recv_cancel_cb), NULL);
	purple_signal_connect(ft_handle, "file-recv-complete",
						plugin, PURPLE_CALLBACK(ft_recv_complete_cb), NULL);
	purple_signal_connect(ft_handle, "file-send-accept",
						plugin, PURPLE_CALLBACK(ft_send_accept_cb), NULL);
	purple_signal_connect(ft_handle, "file-send-start",
						plugin, PURPLE_CALLBACK(ft_send_start_cb), NULL);
	purple_signal_connect(ft_handle, "file-send-cancel",
						plugin, PURPLE_CALLBACK(ft_send_cancel_cb), NULL);
	purple_signal_connect(ft_handle, "file-send-complete",
						plugin, PURPLE_CALLBACK(ft_send_complete_cb), NULL);

	/* Notify signals */
	purple_signal_connect(notify_handle, "displaying-email-notification",
						plugin, PURPLE_CALLBACK(notify_email_cb), NULL);
	purple_signal_connect(notify_handle, "displaying-emails-notification",
						plugin, PURPLE_CALLBACK(notify_emails_cb), NULL);

	return TRUE;
}



static PurplePluginPrefFrame *
get_plugin_pref_frame(PurplePlugin *plugin) {
	PurplePluginPrefFrame *frame;
	PurplePluginPref *ppref;

	frame = purple_plugin_pref_frame_new();

	ppref = purple_plugin_pref_new_with_label("Pidgin-GNTP settings");
	purple_plugin_pref_frame_add(frame, ppref);

	ppref = purple_plugin_pref_new_with_name_and_label(
											"/plugins/core/pidgin-gntp/bool",
											"Show message when window is focused");
	purple_plugin_pref_frame_add(frame, ppref);

	return frame;
}

static PurplePluginUiInfo prefs_info = {
	get_plugin_pref_frame,
	0,   /* page_num (Reserved) */
	NULL, /* frame (Reserved) */
	/* Padding */
	NULL,
	NULL,
	NULL,
	NULL
};
 

static PurplePluginInfo info =
{
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,		/**< type           */
	NULL,				/**< ui_requirement */
	0,				/**< flags          */
	NULL,				/**< dependencies   */
	PURPLE_PRIORITY_DEFAULT,	/**< priority       */

	PLUGIN_ID,			/**< id             */
	N_(PLUGIN_NAME),		/**< name           */
	DISPLAY_VERSION,		/**< version        */
	N_(PLUGIN_DESC),		/**< summary        */
	N_(PLUGIN_DESC),		/**< description    */
	PLUGIN_AUTHOR,       		/**< author         */
	PURPLE_WEBSITE,			/**< homepage       */

	plugin_load,			/**< load           */
	NULL,				/**< unload         */
	NULL,				/**< destroy        */

	NULL,				/**< ui_info        */
	NULL,				/**< extra_info     */
	&prefs_info,				/**< prefs_info     */
	NULL,
	/* Padding */
	NULL,
	NULL,
	NULL,
	NULL
};


static void
init_plugin(PurplePlugin *plugin)
{
	purple_prefs_add_none("/plugins/core/pidgin-gntp");
	purple_prefs_add_bool("/plugins/core/pidgin-gntp/bool", TRUE);
}

PURPLE_INIT_PLUGIN(pidgingrowl, init_plugin, info)