#define PLUGIN_NAME 	"Pidgin GNTP"
#define PLUGIN_AUTHOR 	"Daniel Dimovski <daniel.k.dimovski@gmail.com>"
#define PLUGIN_DESC 	"Plugin sends Pidgin signals to Growl."
#define PLUGIN_ID 		"core-pidgin-growl-dkd1"

#include <stdio.h>

#include "internal.h"
#include "connection.h"
#include "conversation.h"
#include "core.h"
#include "debug.h"
#include "ft.h"
#include "signals.h"
#include "version.h"
#include "status.h"

#include <unistd.h> 
#include <time.h>

unsigned int start_tick;

int find_char(char* str, char c)
{
	int i = 0;
	while(i <= strlen(str))
	{
		if(str[i] == c)
			return i;		
		i++;
	}	
	return -1;
}

int find_char_reverse(char* str, char c)
{
	int i = strlen(str);
	while(i >= 0)
	{
		if(str[i] == c)
			return i;		
		i--;
	}	
	return -1;
}

void strip_msn_font_tags(char* str)
{
	if(str[0] != '<' && str[strlen(str)-1] != '>')
		return;
	
	int front = find_char(str, '>');
	if(front < 0) return;
	front++;

	int i = 0;
	for(i=0; i <= strlen(str); i++)
	{
		str[i] = str[front];
		if(str[front++] == 0)
			break;
	}	

	str[find_char_reverse(str, '<')] = 0;
	
	strip_msn_font_tags(str);
}

/**************************************************************************
 * send growl message (from mattn's gntp-send commandline program)
 * http://github.com/mattn/gntp-send/tree/master
 **************************************************************************/
#include "gntp-send.h"

void gntp_parse_response(int sock)
{
	while (1) {
		char* line = recvline(sock);
		int len = strlen(line);
		/* fprintf(stderr, "%s\n", line); */
		if (strncmp(line, "GNTP/1.0 -ERROR", 15) == 0) {
			purple_debug_error(PLUGIN_NAME, "GNTP/1.0 response: -ERROR\n");
			free(line);
			return;
		}
		free(line);
		if (len == 0) break;
	}
	close_socket(sock);
}

char* server = "127.0.0.1:23053";
char* appname = "Pidgin";

char* notifications[6] = {
	"buddy-sign-in",
	"buddy-sign-out",
	"im-msg-recived",
	"connection-error",
	"buddy-change-image"
};


void
gntp_register(char* name, char* password)
{
	appname = name;
	int sock = -1;
	int c;
	char* salt;
	char* salthash;
	char* keyhash;
	char* authheader = NULL;


	#ifdef _WIN32
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0)
		{		
			WSACleanup();
			return;
		}
		setlocale(LC_CTYPE, "");
	#endif

	if (password) {
		srand(time(NULL));
		salt = gen_salt_alloc(8);
		keyhash = gen_password_hash_alloc(password, salt);
		salthash = string_to_hex_alloc(salt, 8);
		free(salt);
		authheader = (char*)malloc(strlen(keyhash) + strlen(salthash) + 7);
		sprintf(authheader, " MD5:%s.%s", keyhash, salthash);
		free(salthash);
	}

	sock = create_socket(server);
	if (sock == -1)
	{
		#ifdef _WIN32
			WSACleanup();
		#endif
		return;
	}

	char icon_path[512];
	getcwd(icon_path, 512);
	strcat(icon_path, "/plugins/pidgin.png");


	sendline(sock, "GNTP/1.0 REGISTER NONE", authheader);
	sendline(sock, "Application-Name: ", appname);
	sendline(sock, "Notifications-Count: 5", NULL);

	int it = 0;
	for(;it < 5; it++ )
	{
		sendline(sock, "", NULL);
		sendline(sock, "Notification-Name: ", notifications[it]);
		sendline(sock, "Notification-Display-Name: ", notifications[it]);
		sendline(sock, "Notification-Enabled: True", NULL);
		sendline(sock, "Notification-Icon: file://", icon_path);
		
		sendline(sock, "\n\r", NULL);
	}
	sendline(sock, "", NULL);

	gntp_parse_response(sock);

	sock = 0;
}

void
gntp_notify(char* notify, char* icon, char* title, char* message, char* password)
{
	int sock = -1;
	int c;
	char* salt;
	char* salthash;
	char* keyhash;
	char* authheader = NULL;

	//hack to hide spam when signing on to account
	if( GetTickCount() - start_tick < 10000) return;

#ifdef _WIN32
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0)
	{
		WSACleanup();
		return;
	}
	setlocale(LC_CTYPE, "");
#endif

	if (password) {
		srand(time(NULL));
		salt = gen_salt_alloc(8);
		keyhash = gen_password_hash_alloc(password, salt);
		salthash = string_to_hex_alloc(salt, 8);
		free(salt);
		authheader = (char*)malloc(strlen(keyhash) + strlen(salthash) + 7);
		sprintf(authheader, " MD5:%s.%s", keyhash, salthash);
		free(salthash);
	}

	sock = create_socket(server);
	if (sock == -1)
	{
#ifdef _WIN32
		WSACleanup();
#endif
	}


	sendline(sock, "GNTP/1.0 NOTIFY NONE", authheader);
	sendline(sock, "Application-Name: ", appname);
	sendline(sock, "Notification-Name: ", notify);
	sendline(sock, "Notification-Title: ", title);
	sendline(sock, "Notification-Text: ", message);
	if (icon) sendline(sock, "Notification-Icon: ", icon);
	sendline(sock, "", NULL);

	gntp_parse_response(sock);

	sock = 0;
}


/**************************************************************************
 * Buddy Icons signal callbacks
 **************************************************************************/
static void
buddy_icon_changed_cb(PurpleBuddy *buddy)
{
	char growl_msg[2048];
	sprintf(growl_msg,"%s changed image\n(%s)", purple_buddy_get_alias(buddy), purple_buddy_get_name(buddy) );
	gntp_notify("buddy-change-image", purple_buddy_icon_get_full_path(purple_buddy_get_icon(buddy)), "Pidgin", growl_msg, NULL);
}

/**************************************************************************
 * Buddy List subsystem signal callbacks
 **************************************************************************/
static void
buddy_signed_on_cb(PurpleBuddy *buddy, void *data)
{
	char growl_msg[2048];
	sprintf(growl_msg,"%s signed in\n(%s)", purple_buddy_get_alias(buddy), purple_buddy_get_name(buddy) );
	gntp_notify("buddy-sign-in", purple_buddy_icon_get_full_path(purple_buddy_get_icon(buddy)), "Pidgin", growl_msg, NULL);
}

static void
buddy_signed_off_cb(PurpleBuddy *buddy, void *data)
{
	char growl_msg[2048];
	sprintf(growl_msg,"%s signed out\n(%s)", purple_buddy_get_alias(buddy), purple_buddy_get_name(buddy) );
	gntp_notify("buddy-sign-out", purple_buddy_icon_get_full_path(purple_buddy_get_icon(buddy)), "Pidgin", growl_msg, NULL);
}

/**************************************************************************
 * Connection subsystem signal callbacks
 **************************************************************************/
static void
signed_on_cb(PurpleConnection *gc, void *data)
{
	start_tick = GetTickCount();
}

static void
signed_off_cb(PurpleConnection *gc, void *data)
{
}

static void
connection_error_cb(PurpleConnection *gc,
                    PurpleConnectionError err,
                    const gchar *desc,
                    void *data)
{
	const gchar *username =
		purple_account_get_username(purple_connection_get_account(gc));

	char growl_msg[2048];
	sprintf(growl_msg, "%s\ncode: %u\n%s", desc, err, username);
	gntp_notify("connection-error", NULL, "Connection Error", growl_msg, NULL);
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
	char temp[2048];
	strcpy(temp, buffer);
	strip_msn_font_tags(temp);

	char growl_msg[2048];
	sprintf(growl_msg, "%s: %s", purple_buddy_get_alias(purple_find_buddy(account, sender)), temp);
	gntp_notify("im-msg-recived", purple_buddy_icon_get_full_path(purple_buddy_get_icon(purple_find_buddy(account, sender))), "IM Recived", growl_msg, NULL);
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
}

static void
conversation_created_cb(PurpleConversation *conv, void *data)
{
}

static void
chat_buddy_joined_cb(PurpleConversation *conv, const char *user,
					 PurpleConvChatBuddyFlags flags, gboolean new_arrival, void *data)
{
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
	return 0;
}

static void
chat_joined_cb(PurpleConversation *conv, void *data)
{
}

static void
chat_left_cb(PurpleConversation *conv, void *data)
{
}

static void
chat_topic_changed_cb(PurpleConversation *conv, const char *who,
					  const char *topic, void *data)
{
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
	gntp_register("Pidgin Growl", NULL);

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
	NULL,				/**< prefs_info     */
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
}

PURPLE_INIT_PLUGIN(pidgingrowl, init_plugin, info)
