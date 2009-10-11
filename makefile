PIDGIN_TREE_TOP := ../../..
include $(PIDGIN_TREE_TOP)/libpurple/win32/global.mak

TARGET = pidgin-gntp

##
## INCLUDE PATHS
##
INCLUDE_PATHS +=	-I. \
			-I$(GTK_TOP)/include \
			-I$(GTK_TOP)/include/gtk-2.0 \
			-I$(GTK_TOP)/include/glib-2.0 \
			-I$(GTK_TOP)/include/pango-1.0 \
			-I$(GTK_TOP)/include/atk-1.0 \
			-I$(GTK_TOP)/include/cairo \
			-I$(GTK_TOP)/lib/glib-2.0/include \
			-I$(GTK_TOP)/lib/gtk-2.0/include \
			-I$(PURPLE_TOP) \
			-I$(PURPLE_TOP)/win32 \
			-I$(PIDGIN_TOP) \
			-I$(PIDGIN_TOP)/win32 \
			-I$(PIDGIN_TREE_TOP)

LIB_PATHS +=	-L$(GTK_TOP)/lib \
				-L$(PURPLE_TOP)

INCLUDE_PATHS += 	-I$(PIDGIN_TREE_TOP)/libpurple
INCLUDE_PATHS += 	-I$(PIDGIN_TREE_TOP)/../win32-dev/gtk_2_0/include
LIB_PATHS += 		-L$(PIDGIN_TREE_TOP)/../win32-dev/gtk_2_0/lib

##
##  SOURCES, OBJECTS
##
C_SRC =			kp.c

OBJECTS = $(C_SRC:%.c=%.o)

##
## LIBRARIES
##
LIBS =		-l gtk-win32-2.0 \
			-l glib-2.0 \
			-l gdk-win32-2.0 \
			-l gobject-2.0 \
			-l intl \
			-l purple \
			-l ws2_32

include $(PIDGIN_COMMON_RULES)

##
## TARGET DEFINITIONS
##
.PHONY: all install clean

all: $(TARGET).dll

install: $(PIDGIN_INSTALL_PLUGINS_DIR) all
	cp $(TARGET).dll $(PIDGIN_INSTALL_PLUGINS_DIR)

$(OBJECTS): $(PIDGIN_CONFIG_H)

$(TARGET).dll: $(PURPLE_DLL).a $(PIDGIN_DLL).a $(OBJECTS)
	$(CC) -shared $(OBJECTS) $(LIB_PATHS) $(LIBS) $(DLL_LD_FLAGS) -o $(TARGET).dll

##
## CLEAN RULES
##
clean:
	rm -rf $(OBJECTS)
	rm -rf $(TARGET).dll

include $(PIDGIN_COMMON_TARGETS)
