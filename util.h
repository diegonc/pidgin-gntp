int s_strlen(char* str);
void special_entries(char* str);
int translate(char* str, char* entry, char replace);
void strip_tags(char* str);
void substr_remove(char* str, int from, int to);
int abs(int i);

// helper functions
char* 
custom_get_buddy_status_text(PurpleBuddy *buddy)
{
	PurplePluginProtocolInfo *prpl_info;
	PurplePlugin *prpl;
	char* protocol_id;
	PurpleAccount* account = purple_buddy_get_account(buddy);
	
	protocol_id = (char *)purple_account_get_protocol_id(account);
	prpl = purple_find_prpl(protocol_id);
	
	prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO(prpl);
	
	if( prpl_info == NULL || prpl_info->status_text == NULL )	
		return NULL;
	
	return prpl_info->status_text(buddy);
}
		
// util functions (used for stripping tags and special chars from messages)
int abs(int i)
{
	if(i<0)
		i = -1;
		
	return i;
}

void substr_remove(char* str, int from, int to)
{
	int walker, i;
	int len = to-from;
	if(len < 0)
	{		
		walker = to;
		i = from;	
	}
	else
	{
		walker = to;
		i = from;
	}
	len = abs(len);
	
	if(walker > strlen(str))
		walker = strlen(str);
	
	while(str[walker] != 0)
	{
		str[i++] = str[walker++];
	}
	str[i++] = 0;
	
}

void strip_tags(char* str)
{
	int first = -1;
	int i=0;
	while(str[i] != 0)
	{
		if(str[i] == '<')
			first = i;
			
		if(str[i] == '>' && first != -1)
		{			
			substr_remove(str, first, i+1);
			i = first-1;
			first = -1;
		}		
		i++;
	}		 
}

int translate(char* str, char* entry, char replace)
{
	char* found = strstr(str, entry);
	
	int i = 0;
	int offset = strlen(entry)-1;
	if(found)
	{
		found[i++] = replace;
		while( strlen(found) >= i+offset)
		{
			found[i] = found[i+offset];
			i++;
		}
		return 1;
	}
	return 0;
}

void special_entries(char* str)
{
	int found = 1;
	while(found)
	{
		found = 0;
		if(translate(str, "&gt;",'>'))
			found = 1;
		if(translate(str, "&lt;",'<'))
			found = 1;
		if(translate(str, "&quot;",'\"'))
			found = 1;
		if(translate(str, "&amp;",'&'))
			found = 1;
		if(translate(str, "&apos;",'\''))
			found = 1;
			
	}
}


int s_strlen(char* str)
{
	int len = 0;
	if(str != NULL)
		len = strlen(str);
		
	return len;
}
