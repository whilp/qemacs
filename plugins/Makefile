#
# QEmacs Lua plugin examples
#
# Plugins are .lua files installed to ~/.qe/ where qemacs loads them
# automatically at startup. You can also load them at runtime with
# M-x load-plugin.
#

DIST_DIR := $(HOME)/.qe
LUA_PLUGINS := $(wildcard *.lua)

all: install

install: $(LUA_PLUGINS) | $(DIST_DIR)
	cp $(LUA_PLUGINS) $(DIST_DIR)/

$(DIST_DIR):
	mkdir -p $@

clean:
	@echo "Nothing to clean (Lua plugins need no compilation)"

.PHONY: all install clean
