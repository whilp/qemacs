#
# QEmacs plugin Makefile
#
# Builds native shared libraries that qemacs loads from ~/.qe/ at startup.
# Plugins can contain modes, key bindings, or any dynamically registerable
# resource. They are compiled with -DQE_MODULE and export __qe_module_init.
#
# On Linux:  produces .so  files
# On macOS:  produces .dylib files
# On Windows/Cygwin: produces .dll files
#

# Path to QEmacs source tree
QEMACS_PATH := ..

# Plugin name (without extension — set automatically per platform)
PLUGIN_NAME := my_plugin

# Object files
OBJS := $(PLUGIN_NAME).o

# Detect platform and set shared library extension/flags
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  SHARED_EXT := .dylib
  SHARED_FLAGS := -dynamiclib
else ifeq ($(findstring CYGWIN,$(UNAME_S)),CYGWIN)
  SHARED_EXT := .dll
  SHARED_FLAGS := -shared
else ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
  SHARED_EXT := .dll
  SHARED_FLAGS := -shared
else
  SHARED_EXT := .so
  SHARED_FLAGS := -shared
endif

LIB := $(PLUGIN_NAME)$(SHARED_EXT)
DIST_DIR := $(HOME)/.qe
DIST_LIB := $(DIST_DIR)/$(LIB)

CC ?= cc
CFLAGS := -O2 -Wall -g -fPIC -I$(QEMACS_PATH) -DQE_MODULE

# Default: build and install to ~/.qe/
all: $(DIST_LIB)

$(DIST_LIB): $(LIB) | $(DIST_DIR)
	install -m 644 $< $@

$(DIST_DIR):
	mkdir -p $@

$(LIB): $(OBJS) Makefile
	$(CC) $(SHARED_FLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(DIST_LIB)

clean:
	rm -f *.o *$(SHARED_EXT) *~

.PHONY: all install clean
