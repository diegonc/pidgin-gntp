#include "pidgin-gntp.h"
#include <snoregrowl/growl.h>

static HANDLE semaphore = NULL;
static unsigned short port = 0;
static int notify_server_fd = -1;
static guint notify_server_watch = 0;
static GString* notif_ctx_buffer = NULL;

static void
notif_ctx_buffer_append(const gchar* ptr, gssize len)
{
	if (notif_ctx_buffer == NULL) {
		notif_ctx_buffer = g_string_new_len(ptr, len);
	} else {
		g_string_append_len(notif_ctx_buffer, ptr, len);
	}
}

static void
notif_ctx_buffer_discard(void)
{
	g_string_free(notif_ctx_buffer, TRUE);
	notif_ctx_buffer = NULL;
}

static gchar*
notif_ctx_buffer_str(void)
{
	return notif_ctx_buffer->str;
}

void
gntp_notify(char* notify, char* icon, char* title, char* message, char* password)
{
	growl_notification_data data = {0};

	data.app_name = PLUGIN_NAME;
	data.notify = notify;
	data.title = title;
	data.message = message;
	data.icon = icon;

	growl_tcp_notify(SERVER_IP, password, &data);
}

static void
gntp_notify_with_callback(char* notify, char* icon, char* title,
                          char* message, char* password, char* context)
{
	growl_notification_data data = {0};

	data.app_name = PLUGIN_NAME;
	data.notify = notify;
	data.title = title;
	data.message = message;
	data.icon = icon;
	data.callback_context = context;

	growl_tcp_notify(SERVER_IP, password, &data);
}

static gchar*
make_context_string(PurpleAccount* account, PurpleConversation* conv)
{
	PurpleConversationType type = purple_conversation_get_type(conv);
	const char* conv_name = purple_conversation_get_name(conv);
	const char* acc_name = purple_account_get_username(account);
	const char* proto = purple_account_get_protocol_id(account);
	size_t conv_name_size = strlen(conv_name) + 1;
	size_t acc_name_size = strlen(acc_name) + 1;
	size_t proto_size = strlen(proto) + 1;
	size_t ctx_size = sizeof type;
	guchar* ctx;
	gchar* encoded;

	ctx_size += conv_name_size;
	ctx_size += acc_name_size;
	ctx_size += proto_size;

	ctx = malloc(sizeof *ctx * ctx_size);
	if (ctx) {
		guchar* ptr = ctx;
		memcpy(ptr, &type, sizeof type);
		ptr += sizeof type;
		memcpy(ptr, conv_name, conv_name_size);
		ptr += conv_name_size;
		memcpy(ptr, acc_name, acc_name_size);
		ptr += acc_name_size;
		memcpy(ptr, proto, proto_size);

		encoded = purple_base64_encode(ctx, ctx_size);
		free(ctx);
		return encoded;
	}

	return 0;
}

static void
parse_context_string(const gchar* ctx, PurpleAccount** account, PurpleConversation** conv)
{
	PurpleConversationType type;
	const char* conv_name;
	const char* acc_name;
	const char* proto;

	guchar* decoded = purple_base64_decode(ctx, NULL);
	const char* ptr = (const char*) decoded;

	memcpy(&type, ptr, sizeof type);
	ptr += sizeof type;
	conv_name = ptr;
	ptr += strlen(conv_name) + 1;
	acc_name = ptr;
	ptr += strlen(acc_name) + 1;
	proto = ptr;

	*account = purple_accounts_find(acc_name, proto);
	if (*account != NULL)
		*conv = purple_find_conversation_with_account(type, conv_name, *account);

	g_free(decoded);
}

static void
notify_context_to_main_thread(const char* context)
{
	struct sockaddr_in localhost;
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1) return;

	memset(&localhost, 0, sizeof(struct sockaddr_in));
	localhost.sin_family = AF_INET;
	localhost.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	localhost.sin_port = htons(port);

	sendto(fd, context, strlen(context) + 1, 0,
		(const struct sockaddr *) &localhost, sizeof localhost);
	close(fd);
}

static void
growl_notify_cb(const growl_callback_data* data)
{
	if (g_str_has_prefix(data->reason, "CLICK")) {
		DWORD result = WaitForSingleObject(semaphore, INFINITE);
		if (result == WAIT_OBJECT_0)
			notify_context_to_main_thread(data->data);
	}
}

static void
present_conversation_with_event(const char* context)
{
	PurpleAccount* account = NULL;
	PurpleConversation* conv = NULL;

	parse_context_string(context, &account, &conv);

	if (conv != NULL) {
		PidginWindow* win = PIDGIN_CONVERSATION(conv)->win;
		purple_conversation_present(conv);
		pidgin_conv_window_raise(win);
		gtk_window_set_keep_above(GTK_WINDOW(win->window), TRUE);
		gtk_window_set_keep_above(GTK_WINDOW(win->window), FALSE);
	}
}

static void
notification_arrived_at_main_thread(gpointer data, gint source, PurpleInputCondition cond)
{
	const size_t BUFFER_SIZE = 1024;
	ssize_t count;
	char buffer[BUFFER_SIZE];
	char* p = buffer;

	do {
		count = read(source, p, BUFFER_SIZE - (p - buffer));
		if (count == -1) {
			if (errno == EAGAIN) {
				size_t existing = p - buffer;
				if (existing > 0)
					notif_ctx_buffer_append(buffer, existing);
			} else {
				notif_ctx_buffer_discard();
				purple_debug_error(
					"pidgin-gntp",
					"Error while reading notification callback context: %s\n",
					strerror(errno));
				ReleaseSemaphore(semaphore, 1, NULL);
			}
			return;
		} else if (count == 0) {
			notif_ctx_buffer_discard();
			purple_debug_error(
				"pidgin-gntp",
				"read returned 0 while reading notification callback context\n");
			ReleaseSemaphore(semaphore, 1, NULL);
			return;
		} else if (p[count - 1] == 0) {
			gchar* context;
			size_t existing = (p - buffer) + count;
			notif_ctx_buffer_append(buffer, existing);
			context = notif_ctx_buffer_str();
			present_conversation_with_event(context);
			notif_ctx_buffer_discard();
			ReleaseSemaphore(semaphore, 1, NULL);
			return;
		} else {
			size_t remaining = BUFFER_SIZE - (p + count - buffer);
			if (BUFFER_SIZE < (p + count - buffer)) {
				notif_ctx_buffer_discard();
				purple_debug_error(
					"pidgin-gntp",
					"crossed the end of buffer while reading notification callback context\n");
				ReleaseSemaphore(semaphore, 1, NULL);
				return;
			} else if (remaining == 0) {
				notif_ctx_buffer_append(buffer, BUFFER_SIZE);
				p = buffer;
			} else {
				p += count;
			}
		}
	} while (1);
}

static int
is_allowed(PurpleAccount *account)
{
	char* id;
	char *path;
	int len;
	gboolean allowed_protocol;
	gboolean available, unavailable, invisible, away;
	
	available = purple_prefs_get_bool("/plugins/core/pidgin-gntp/on_available");
	unavailable = purple_prefs_get_bool("/plugins/core/pidgin-gntp/on_unavailable");
	invisible = purple_prefs_get_bool("/plugins/core/pidgin-gntp/on_invisible");
	away = purple_prefs_get_bool("/plugins/core/pidgin-gntp/on_away");
		
	if(!available && acc_status == PURPLE_STATUS_AVAILABLE)
			return 0;
	if(!unavailable && acc_status == PURPLE_STATUS_UNAVAILABLE)
			return 0;
	if(!invisible && acc_status == PURPLE_STATUS_INVISIBLE)
			return 0;
	if(!away && acc_status == PURPLE_STATUS_AWAY)
			return 0;
	if(!away && acc_status == PURPLE_STATUS_EXTENDED_AWAY)
			return 0;
			
	id = (char*)purple_account_get_protocol_id (account);		
	len = s_strlen("/plugins/core/pidgin-gntp/") + s_strlen(id) + 1;
	
	path = malloc( len ); 
	g_snprintf(path, len, "%s%s", "/plugins/core/pidgin-gntp/", id );
	
	if(!purple_prefs_exists(path))
		purple_prefs_add_bool(path, TRUE);
		
	allowed_protocol = purple_prefs_get_bool(path);
	free(path);
	
	if(!allowed_protocol)
		return 0;
					
	DEBUG_MSG("allowed by current settings\n");	
	return 1;
}

/**************************************************************************
 * Account signal callbacks
 **************************************************************************/
static void 
account_status_changed_cb(PurpleAccount *account,
						PurpleStatus *old_status, PurpleStatus *new_status)
{
	acc_status = purple_status_type_get_primitive( purple_status_get_type(new_status) );		
}

/**************************************************************************
 * Buddy Icons signal callbacks
 **************************************************************************/
 
  
static void
buddy_icon_changed_cb(PurpleBuddy *buddy)
{	
	PurpleBuddyIcon* icon;
	int len;
	char *icon_path, *buddy_nick, *buddy_name, *growl_msg;
	
	DEBUG_MSG("buddy_icon_changed_cb\n");
	
	g_return_if_fail( buddy != NULL );

	buddy_nick = (char*)purple_buddy_get_alias(buddy);
	buddy_name = (char*)purple_buddy_get_name(buddy);
	icon = purple_buddy_get_icon(buddy);
	icon_path = (char*)purple_buddy_icon_get_full_path(icon);
	
	if(buddy_nick == NULL)
		DEBUG_MSG("buddy_nick is NULL\n");
	if(buddy_name == NULL)
		DEBUG_MSG("buddy_name is NULL\n");	
	if(icon == NULL)
		DEBUG_MSG("icon is NULL\n");	
	if(icon_path == NULL)
		DEBUG_MSG("icon_path is NULL\n");	
	
	if(icon_path != NULL)
	{
		if(g_list_find_custom(img_list, icon_path, (GCompareFunc)strcmp))
		{
			DEBUG_MSG("icon_path found in list - leaving function\n");
			return;
		}
		img_list = g_list_prepend(img_list, icon_path);
		
		if(g_list_find_custom(img_list, icon_path, (GCompareFunc)strcmp))
			DEBUG_MSG("icon_path added to list of known icons\n");
	}
		
	// is allowed?
	g_return_if_fail( is_allowed(purple_buddy_get_account(buddy)) );	
	//hack to hide spam when signing on to account
	g_return_if_fail( GetTickCount() - start_tick_image > 500 );
	
	len = s_strlen(buddy_nick) + s_strlen(buddy_name) + 2;
	growl_msg = malloc( len ); 
	g_snprintf(growl_msg, len, "%s\n%s", buddy_nick, buddy_name );
	
	gntp_notify("buddy-change-image", icon_path, "Changed Image", growl_msg, NULL);
	free(growl_msg);
}





// returns NULL if its the same
GList* buddy_status_is_new(PurpleBuddy* buddy, char* status_msg)
{
	struct buddy_status temp; 
	temp.buddy = buddy;
	GList* node = g_list_find_custom(buddys_status, &temp, (GCompareFunc)compare_status);
	if(node != NULL)
	{
		DEBUG_MSG("buddy status node found\n");
		struct buddy_status* node_status = node->data;
		if(strcmp(status_msg, node_status->status) == 0)
			return NULL;
	}
	else
	{
		DEBUG_MSG("buddy status node created\n");
		struct buddy_status* node_status = malloc(sizeof(struct buddy_status));
		char* the_status = malloc( s_strlen(status_msg)+1 );
		strcpy(the_status, status_msg);
		
		node_status->buddy = buddy;
		node_status->status = the_status;
		buddys_status = g_list_prepend(buddys_status, node_status);
	}
	
	return node;
}
/**************************************************************************
 * Buddy List subsystem signal callbacks
 **************************************************************************/				
static void
buddy_status_changed_cb(PurpleBuddy *buddy, PurpleStatus *old_status, PurpleStatus *status)
{
	char *status_name, *old_status_name, *status_msg;
	char* buddy_nick, *buddy_name, *icon_path, *growl_msg;
	PurpleBuddyIcon* icon;
	int len, hack_ms;
	PurpleAccount* account;

	DEBUG_MSG("buddy_status_changed_cb\n");
	
	g_return_if_fail( buddy != NULL );
	account = purple_buddy_get_account(buddy);
	g_return_if_fail( is_allowed(account) );	

	status_name 	= (char *)purple_status_get_name(status);
	old_status_name = (char *)purple_status_get_name(old_status);
	buddy_nick = (char*)purple_buddy_get_alias(buddy);
	buddy_name = (char*)purple_buddy_get_name(buddy);
	icon = purple_buddy_get_icon(buddy);
	icon_path = (char*)purple_buddy_icon_get_full_path(icon);

	status_msg = custom_get_buddy_status_text(buddy);
	if( status_msg == NULL )
		status_msg = "";
	special_entries(status_msg);
	strip_tags(status_msg);
		
	//hack to hide spam when signing on to account
	hack_ms = purple_prefs_get_int("/plugins/core/pidgin-gntp/hack_ms");
	
	GList *node = buddy_status_is_new(buddy, status_msg);
	
	if( GetTickCount() - start_tick_im < hack_ms) return;
	
	if( node != NULL )
	{
		DEBUG_MSG("status node received\n");
		struct buddy_status* node_status = node->data;
		
		free(node_status->status);
		char* the_status = malloc( s_strlen(status_msg)+1 );
		strcpy(the_status, status_msg);
		node_status->status = the_status;
		
		if(status_msg[0] == 0)
		{	
			len = s_strlen(buddy_nick) + s_strlen(buddy_name) + 25;
			growl_msg = malloc( len );
			g_snprintf(growl_msg, len, "status message removed\n%s\n%s",buddy_nick, buddy_name );							
			
			gntp_notify("buddy-change-msg", icon_path, "Status Message Changed", growl_msg, NULL);
			free(growl_msg);
		}
		else
		{
			len = s_strlen(buddy_nick) + s_strlen(buddy_name) + s_strlen(status_msg)+5;
			growl_msg = malloc( len );
			g_snprintf(growl_msg, len, "\"%s\"\n%s\n%s",
												status_msg, buddy_nick, buddy_name );
												
			gntp_notify("buddy-change-msg", icon_path, "Status Changed", growl_msg, NULL);
			free(growl_msg);
		}
	}
	
	if( strcmp(status_name, old_status_name) == 0)
		return;
		
	len = s_strlen(buddy_nick) + s_strlen(buddy_name) + s_strlen(status_name) + 3;
		
	growl_msg = malloc( len );
	
	g_snprintf(growl_msg, len, "%s\n%s\n%s",
										status_name, buddy_nick, buddy_name );
										
	gntp_notify("buddy-change-status", icon_path, "Status Changed", growl_msg, NULL);
	
	free(growl_msg);
}
					
static void
buddy_signed_on_cb(PurpleBuddy *buddy, void *data)
{
	int hack_ms;
	char* buddy_nick, *buddy_name, *icon_path, *growl_msg;
	PurpleBuddyIcon* icon;
	int len = 2;

	DEBUG_MSG("buddy_signed_on_cb\n");
	
	g_return_if_fail( buddy != NULL );	
	g_return_if_fail( is_allowed(purple_buddy_get_account(buddy)) );
		
	start_tick_image = GetTickCount();
	//hack to hide spam when signing on to account
	hack_ms = purple_prefs_get_int("/plugins/core/pidgin-gntp/hack_ms");
	if( GetTickCount() - start_tick_im < hack_ms) return;
	
	
	buddy_nick = (char*)purple_buddy_get_alias(buddy);
	buddy_name = (char*)purple_buddy_get_name(buddy);
	
	purple_blist_update_buddy_icon(buddy);	
	
	icon = purple_buddy_get_icon(buddy);
	icon_path = (char*)purple_buddy_icon_get_full_path(icon);

	if(buddy_nick != NULL)
		len = strlen(buddy_nick) + 1;
	if(buddy_name  != NULL )
		 len += strlen(buddy_name);
		
	growl_msg = malloc( len );
	
	g_snprintf(growl_msg, len, "%s\n%s", buddy_nick, buddy_name );
	gntp_notify("buddy-sign-in", icon_path, "Signed In", growl_msg, NULL);
	
	free(growl_msg);
}

static void
buddy_signed_off_cb(PurpleBuddy *buddy, void *data)
{
	int len;
	char* buddy_nick, *buddy_name, *icon_path, *growl_msg;
	PurpleBuddyIcon* icon;

	DEBUG_MSG("buddy_signed_off_cb\n");
	
	g_return_if_fail( buddy != NULL );	
	g_return_if_fail( is_allowed(purple_buddy_get_account(buddy)) );
		
	buddy_nick = (char*)purple_buddy_get_alias(buddy);
	buddy_name = (char*)purple_buddy_get_name(buddy);
	icon = purple_buddy_get_icon(buddy);
	icon_path = (char*)purple_buddy_icon_get_full_path(icon);
		
	len = s_strlen(buddy_nick) + s_strlen(buddy_name) + 2;
	
	growl_msg = malloc( len );
	
	g_snprintf(growl_msg, len, "%s\n%s", buddy_nick, buddy_name );
	gntp_notify("buddy-sign-out", icon_path, "Signed Off", growl_msg, NULL);
	
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
	char* growl_msg;
	PurpleAccount* account = purple_connection_get_account(gc);
	const gchar *username =	purple_account_get_username(account);
	int len = s_strlen((char*)desc) + s_strlen((char*)username) + 2 ;
	
	DEBUG_MSG("connection_error_cb\n");
	
	growl_msg = malloc( len);
	g_snprintf(growl_msg, len, "%s\n%s", desc, username);
	
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
	gboolean on_focus;
	char *message, *notification, *buddy_nick, *iconpath;
	PurpleBuddy* buddy;
	PurpleBuddyIcon* icon;
	int len;
	
	DEBUG_MSG("received_im_msg_cb\n");
		
	g_return_if_fail( is_allowed(account) );
		
	on_focus = purple_prefs_get_bool("/plugins/core/pidgin-gntp/on_focus");
	if(conv != NULL && !on_focus && conv->ui_ops->has_focus(conv))
		return;
	
	
	// copy string to temporary variable)
	message = malloc(s_strlen(buffer)+1);
	strcpy(message, buffer);
	special_entries(message);
	strip_tags(message);

	
	// nickname
	buddy = purple_find_buddy(account, sender);
	if(buddy == NULL)
		buddy_nick = sender;
	else
		buddy_nick = (char*)purple_buddy_get_alias( buddy );
	
	len = s_strlen(buddy_nick) + s_strlen(message) + 3;

	// message
	notification = malloc( len );
	g_snprintf(notification, len, "%s: %s", buddy_nick, message);
	
	// icon
	icon = purple_buddy_get_icon( buddy );
	iconpath = purple_buddy_icon_get_full_path( icon );
	
	gntp_notify("im-msg-received", iconpath, "IM Message", notification, NULL);
	
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
	gboolean on_focus;
	char *message, *notification, *cb_context;
	int len;
	
	DEBUG_MSG("received_chat_msg_cb\n");
		
	g_return_if_fail( is_allowed(account) );
		
	on_focus = purple_prefs_get_bool("/plugins/core/pidgin-gntp/on_focus");
	if(chat != NULL && !on_focus && chat->ui_ops->has_focus(chat))
		return;
		
	// copy string to temporary variable)
	message = malloc(s_strlen(buffer)+1);
	strcpy(message, buffer);
	special_entries(message);
	strip_tags(message);
	
	len = s_strlen(sender) + s_strlen(message) + 3;
	// message
	notification = malloc( len );
	g_snprintf(notification, len, "%s: %s", sender, message);
	
	cb_context = make_context_string(account, chat);
	gntp_notify_with_callback("chat-msg-received", NULL, "Chat Message", notification, NULL, cb_context);
	g_free(cb_context);
	
	free(message);
	free(notification);
}

static void
conversation_created_cb(PurpleConversation *conv, void *data)
{
}

static void
chat_buddy_joined_cb(PurpleConversation *conv, const char *user,
					 PurpleConvChatBuddyFlags flags, gboolean new_arrival, void *data)
{	
	char *notification;
	int len = s_strlen((char*)conv->title)+s_strlen((char*)user) + 9;
	
	DEBUG_MSG("chat_buddy_joined_cb\n");
	
	g_return_if_fail( is_allowed(purple_conversation_get_account(conv)) );
		
	//hack to hide spam when join channel
	if( GetTickCount() - start_tick_chat < 10000) return;

	notification = malloc( len );
	g_snprintf(notification, len, "%s joined %s", user, conv->title);
	
	gntp_notify("chat-buddy-sign-in", NULL, "Chat Join", notification, NULL);
	free(notification);
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
	int len = s_strlen((char*)conv->title)+s_strlen((char*)user) + 7;
	
	DEBUG_MSG("chat_buddy_left_cb\n");
	
	g_return_if_fail( is_allowed(purple_conversation_get_account(conv)) );
		
	notification = malloc( len );
	g_snprintf(notification, len, "%s left %s", user, conv->title);
	
	gntp_notify("chat-buddy-sign-out", NULL, "Chat Leave", notification, NULL);
	free(notification);
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
	int len = 	s_strlen((char*)inviter)+
				s_strlen((char*)room_name)+
				s_strlen((char*)message) + 23;
	
	DEBUG_MSG("chat_invited_cb\n");
	
	if( !is_allowed(account) )
		return 0;
		
	notification = malloc( len );
	g_snprintf(notification, len, "%s has invited you to %s\n%s", inviter, room_name, message);
	
	gntp_notify("chat-invited", NULL, "Chat Invite", notification, NULL);
	free(notification);
	
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
	int len = 	s_strlen((char*)who)+
				s_strlen((char*)conv->title)+
				s_strlen((char*)topic) + 40;
							
	DEBUG_MSG("chat_topic_changed_cb\n");
	
	if(conv == NULL || topic == NULL || who == NULL)
		return;
	
	g_return_if_fail( is_allowed(purple_conversation_get_account(conv)) );
		
	notification = malloc( len );
					
	g_snprintf(notification, len, "%s topic: %s\nby %s", conv->title, topic, who);				
	
	gntp_notify("chat-topic-change", NULL, "Chat Topic Changed", notification, NULL);
	
	free(notification);
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

static gboolean
notify_server_start(void)
{
	int err;
	struct sockaddr_in serveraddr;
	port = purple_prefs_get_int("/plugins/core/pidgin-gntp/notify_port");

	semaphore = CreateSemaphore(NULL, 0, 1, NULL);
	if (semaphore == NULL)
		return FALSE;

	notify_server_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (notify_server_fd == -1) {
		CloseHandle(semaphore);
		return FALSE;
	}

	memset(&serveraddr, 0, sizeof serveraddr);
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port);
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);

	err = bind(notify_server_fd, (struct sockaddr*) &serveraddr,
				sizeof serveraddr);
	if (err == -1) {
		CloseHandle(semaphore);
		close(notify_server_fd);
		notify_server_fd = -1;
		return FALSE;
	}

	notify_server_watch = purple_input_add(notify_server_fd,
							PURPLE_INPUT_READ,
							notification_arrived_at_main_thread,
							NULL);

	ReleaseSemaphore(semaphore, 1, NULL);
	return TRUE;
}

/**************************************************************************
 * Plugin stuff
 **************************************************************************/
static gboolean
plugin_load(PurplePlugin *plugin)
{
	void *core_handle     = purple_get_core();
	void *acc_handle	  = purple_accounts_get_handle();
	void *blist_handle    = purple_blist_get_handle();
	void *conn_handle     = purple_connections_get_handle();
	void *conv_handle     = purple_conversations_get_handle();
	void *ft_handle       = purple_xfers_get_handle();
	void *notify_handle   = purple_notify_get_handle();

	//gntp_register(NULL);

	registered = 0;
	
	_getcwd(DefaultIcon, _MAX_PATH);
	strcat(DefaultIcon, "/pixmaps/pidgin/growl_icon.png");
	
	if(!registered)
	{
		if (!notify_server_start())
			return FALSE;

		growl_init(growl_notify_cb);
		growl_tcp_register(SERVER_IP, PLUGIN_NAME, (const char **const)notifications, 12, 0, DefaultIcon);
		registered = 1;
	}
	
	
	/* Account subsystem signals */
	purple_signal_connect(acc_handle, "account-status-changed",
						plugin, PURPLE_CALLBACK(account_status_changed_cb), NULL);
						
	/* Buddy List subsystem signals */
	purple_signal_connect(blist_handle, "buddy-status-changed",
						plugin, PURPLE_CALLBACK(buddy_status_changed_cb), NULL);
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
get_plugin_pref_frame(PurplePlugin *plugin)
{
	PurplePluginPrefFrame *frame;
	PurplePluginPref *ppref;

	PurpleAccount *account;
	char *name, *id, *path;
	int len;
	GList *l, *listed_protocols;
	
	DEBUG_MSG("get_plugin_pref_frame");
	
	frame = purple_plugin_pref_frame_new();

	ppref = purple_plugin_pref_new_with_name_and_label(
	"/plugins/core/pidgin-gntp/on_focus", "show message when window is focused");
	purple_plugin_pref_frame_add(frame, ppref);
	
	ppref = purple_plugin_pref_new_with_label("Send notifications when status is:");
	purple_plugin_pref_frame_add(frame, ppref);	
	
	ppref = purple_plugin_pref_new_with_name_and_label(
	"/plugins/core/pidgin-gntp/on_available", "available");
	purple_plugin_pref_frame_add(frame, ppref);
	
	ppref = purple_plugin_pref_new_with_name_and_label(
	"/plugins/core/pidgin-gntp/on_unavailable", "unavailable");
	purple_plugin_pref_frame_add(frame, ppref);
	
	ppref = purple_plugin_pref_new_with_name_and_label(
	"/plugins/core/pidgin-gntp/on_invisible", "invisible");
	purple_plugin_pref_frame_add(frame, ppref);
	
	ppref = purple_plugin_pref_new_with_name_and_label(
	"/plugins/core/pidgin-gntp/on_away", "away");
	purple_plugin_pref_frame_add(frame, ppref);
	
	
	ppref = purple_plugin_pref_new_with_label("Starting delay (to prevent spam while connecting):");
	purple_plugin_pref_frame_add(frame, ppref);
	
	ppref = purple_plugin_pref_new_with_name_and_label(
	"/plugins/core/pidgin-gntp/hack_ms", "value in milliseconds (1000ms = 1 sec)");
	purple_plugin_pref_set_bounds(ppref, 0, 30000);
	purple_plugin_pref_frame_add(frame, ppref);
	
	
	ppref = purple_plugin_pref_new_with_label("Following protocols sends notifications:");
	purple_plugin_pref_frame_add(frame, ppref);
		
	listed_protocols = NULL;
	for (l = purple_accounts_get_all(); l != NULL; l = l->next)
	{
		account = (PurpleAccount *)l->data;
		
		name = (char*)purple_account_get_protocol_name (account);
		id = (char*)purple_account_get_protocol_id (account);		
		
		if( !g_list_find_custom(listed_protocols, id, (GCompareFunc)strcmp) )
		{
			listed_protocols = g_list_prepend (listed_protocols, id);
			
			len = s_strlen("/plugins/core/pidgin-gntp/") + s_strlen(id) + 1;
			path = malloc( len ); 
			g_snprintf(path, len, "%s%s", "/plugins/core/pidgin-gntp/", id );
				
			if(!purple_prefs_exists(path))
				purple_prefs_add_bool(path, TRUE);
			
			ppref = purple_plugin_pref_new_with_name_and_label(path, name);
			purple_plugin_pref_frame_add(frame, ppref);
			free(path);
		}
	}
	g_list_free(listed_protocols);
			
	ppref = purple_plugin_pref_new_with_label(REV);
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
	
	purple_prefs_add_bool("/plugins/core/pidgin-gntp/on_focus", FALSE);	
	
	purple_prefs_add_bool("/plugins/core/pidgin-gntp/on_available", TRUE);
	purple_prefs_add_bool("/plugins/core/pidgin-gntp/on_unavailable", TRUE);
	purple_prefs_add_bool("/plugins/core/pidgin-gntp/on_invisible", TRUE);
	purple_prefs_add_bool("/plugins/core/pidgin-gntp/on_away", TRUE);
	
	purple_prefs_add_int("/plugins/core/pidgin-gntp/hack_ms", 10000);
	purple_prefs_add_int("/plugins/core/pidgin-gntp/notify_port", 27492);
	
	acc_status = purple_savedstatus_get_type( purple_savedstatus_get_startup() );		
}

PURPLE_INIT_PLUGIN(pidgingrowl, init_plugin, info)
