# Makefile for a GKrellM Timer plugin

# Linux
GTK_CONFIG = gtk-config
IMLIB_CONFIG = imlib-config

# FreeBSD
#GTK_CONFIG = gtk12-config
#IMLIB_CONFIG = imlib-config
#PLUGIN_DIR = /usr/X11R6/libexec/gkrellm/plugins/

USER_PLUGIN_DIR = $(HOME)/.gkrellm/plugins
PLUGIN_DIR = /usr/share/gkrellm/plugins
GKRELLM_INCLUDE = -I/usr/local/include

GTK_INCLUDE = `$(GTK_CONFIG) --cflags`
GTK_LIB = `$(GTK_CONFIG) --libs`

IMLIB_INCLUDE = `$(IMLIB_CONFIG) --cflags-gdk`
IMLIB_LIB = `$(IMLIB_CONFIG) --libs-gdk`


FLAGS = -g -Wall -fPIC $(GTK_INCLUDE) $(IMLIB_INCLUDE) $(GKRELLM_INCLUDE)
LIBS = $(GTK_LIB) $(IMLIB_LIB)
LFLAGS = -shared

CC = gcc $(CFLAGS) $(FLAGS)

INSTALL = install -c
INSTALL_PROGRAM = $(INSTALL) -s

OBJS = gkrellm_timers.o

all:	gkrellm_timers.so

freebsd:
	make GTK_CONFIG=gtk12-config PLUGIN_DIR=/usr/X11R6/libexec/gkrellm/plugins

gkrellm_timers.so:	$(OBJS)
	$(CC) $(OBJS) -o gkrellm_timers.so $(LFLAGS) $(LIBS)

clean:
	rm -f *.o core *.so* *.bak *~

install-user:	gkrellm_timers.so
	make PLUGIN_DIR=$(USER_PLUGIN_DIR) install

install:	gkrellm_timers.so
	$(INSTALL) -m 755 -d $(PLUGIN_DIR)
	$(INSTALL_PROGRAM) -m 755 gkrellm_timers.so $(PLUGIN_DIR)

gkrellm_timers.o:	gkrellm_timers.c

