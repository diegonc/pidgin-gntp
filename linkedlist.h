struct list_el {
   char* name;
   char* icon;
   
   struct list_el * next;
};

typedef struct list_el item;


void list_add(item* head, char* username, char* iconpath)
{
	item *new_item = (item *)malloc(sizeof(item));
	new_item->name = username;
	new_item->icon = iconpath;
	
    new_item->next = head;
    head = new_item;
}

boolean list_find(item* head, char* icon)
{
	item *walker = head;
	while(walker != NULL)
	{
		if( strcmp(walker->icon,icon) == 0)
			return 1;
			
		walker = walker->next;
	}
	return 0;
}

void list_deallocate(item* head)
{
	while(head != NULL)
	{
		item *next = head->next;
		free(head);
		head = next;		
	}
}