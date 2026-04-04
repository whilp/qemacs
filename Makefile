# QEmacs, tiny but powerful multimode editor
#
# Copyright (c) 2000-2002 Fabrice Bellard.
# Copyright (c) 2000-2025 Charlie Gordon.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

# Default to parallel builds using all available cores
MAKEFLAGS += -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

OSNAME := $(shell uname -s)

# Cosmopolitan toolchain — auto-fetched on first build
COSMOCC_VERSION := cosmocc-2026.03.15-bbe7b3cf4
COSMOCC_SHA256 := 344ffe8ec31dc5eeba72a8c3747f29181322e2a8041e505360860d92a9729dc7
COSMOCC_URL := https://github.com/whilp/cosmopolitan/releases/download/$(COSMOCC_VERSION)/cosmocc.zip
COSMOCC_FETCH_DIR := o/fetch/cosmocc/$(COSMOCC_SHA256)
COSMOCC_DIR ?= o/cosmocc

export PATH := $(CURDIR)/$(COSMOCC_DIR)/bin:$(PATH)

CC = $(COSMOCC_DIR)/bin/cosmocc
HOST_CC ?= cc
AR = $(COSMOCC_DIR)/bin/cosmoar
STRIP ?= true
prefix ?= $(HOME)/.local
bindir ?= $(prefix)/bin
datadir ?= $(prefix)/share
mandir ?= $(prefix)/man
INSTALL_DIR ?= $(bindir)
CFLAGS ?= -O2
LDFLAGS ?=
LIBS ?=
EXE ?=
EXTRALIBS ?= -lm
TARGET_OS ?= $(OSNAME)
TARGET_ARCH ?= $(shell uname -m)
SRC_PATH ?= $(CURDIR)
VERSION ?= $(shell cat VERSION)

-include config.mak

ifeq (,$(V)$(VERBOSE))
    echo := @echo
    cmd  := @
else
    echo := @:
    cmd  :=
endif

-include cflags.mk

CFLAGS += -mcosmo
# Security: strict warnings — zero tolerance (inspired by curl's hardening)
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -Wformat=2 -Wformat-security
CFLAGS += -Wshadow -Wwrite-strings
CFLAGS += -Wcast-align
CFLAGS += -Wunused -Wno-unused-parameter -Wno-unused-result
CFLAGS += -Wstrict-prototypes -Wmissing-prototypes
CFLAGS += -Wold-style-definition
CFLAGS += -Wnull-dereference
CFLAGS += -Wdouble-promotion
CFLAGS += -Wimplicit-fallthrough
CFLAGS += -Wsign-conversion -Wconversion
CFLAGS += -Wundef -Wvla -Wpointer-arith -Wcast-qual
CFLAGS += -Wbad-function-cast -Wnested-externs -Winit-self
CFLAGS += -Wduplicated-cond -Wrestrict
# Catch logical mistakes: duplicate conditions, skipped initializations, float compares
CFLAGS += -Wlogical-op
CFLAGS += -Wjump-misses-init
CFLAGS += -Wfloat-equal
CFLAGS += -Wduplicated-branches
# -Wstringop-overflow: disabled — GCC emits false positives for dynamic heap buffers
# (array bounds inferred as [0] when allocated via malloc, triggering spurious truncation warnings)
# CFLAGS += -Wstringop-overflow
# -fstack-protector-strong: disabled — cosmopolitan libc provides its own canary mechanism
# and stack-protector conflicts with its APE/TLS bootstrap, causing link failures
# CFLAGS += -fstack-protector-strong
# Runtime buffer overflow detection for libc functions
CFLAGS += -D_FORTIFY_SOURCE=2
CFLAGS += -I.
# Auto-generate .d dependency files so only affected objects rebuild on header changes
CFLAGS += -MMD -MP

ifeq ($(CC),$(HOST_CC))
  HOST_CFLAGS := $(CFLAGS)
endif

DEFINES = -DQE_VERSION='"$(VERSION)"' \
          -DCONFIG_HAS_TYPEOF -DCONFIG_PTSNAME -DCONFIG_NETWORK -DCONFIG_MMAP \
          -DCONFIG_SESSION_DETACH -DCONFIG_ALL_KMAPS -DCONFIG_UNICODE_JOIN \
          -DCONFIG_ALL_MODES -DCONFIG_HTML

########################################################

# Output directory
o ?= o

DEBUG_SUFFIX :=
ifdef DEBUG
$(info Building with debug info)
DEBUG_SUFFIX := _debug
ECHO_CFLAGS += -DCONFIG_DEBUG
CFLAGS += -g -O0
LDFLAGS += -g -O0
endif

# Files to embed in the APE executable via zip (accessible at /zip/share/qe/)
EMBED_FILES := $(o)/kmaps $(o)/ligatures config.eg $(o)/qe-manual.md qe.1

# Object files
OBJS := qe.o cutils.o util.o color.o charset.o buffer.o search.o input.o display.o \
        modes/hex.o test_display.o extras.o variables.o

OBJS += unix.o tty.o session.o plugin.o third_party/lua/lua-amalg.o
LIBS += $(EXTRALIBS)

OBJS += kmap.o

OBJS += unicode_join.o arabic.o indic.o libunicode.o libregexp.o

OBJS += charsetjis.o charsetmore.o

OBJS += modes/mkd_render.o modes/mkd_render_parse.o
OBJS += modes/unihex.o   modes/bufed.o    modes/orgmode.o  modes/markdown.o \
        lang/clang.o     lang/xml.o       lang/htmlsrc.o   lang/forth.o     \
        lang/arm.o       lang/lisp.o      lang/makemode.o  lang/perl.o      \
        lang/script.o    lang/ebnf.o      lang/cobol.o     lang/rlang.o     \
        lang/txl.o       lang/nim.o       lang/rebol.o     lang/elm.o       \
        lang/jai.o       lang/ats.o       lang/rust.o      lang/swift.o     \
        lang/icon.o      lang/groovy.o    lang/virgil.o    lang/ada.o       \
        lang/basic.o     lang/vimscript.o lang/pascal.o    lang/fortran.o   \
        lang/haskell.o   lang/lua.o       lang/python.o    lang/ruby.o      \
        lang/smalltalk.o lang/sql.o       lang/elixir.o    lang/agena.o     \
        lang/coffee.o    lang/erlang.o    lang/julia.o     lang/ocaml.o     \
        lang/scad.o      lang/magpie.o    lang/falcon.o    lang/wolfram.o   \
        lang/tiger.o     lang/asm.o       lang/inifile.o   lang/postscript.o \
        lang/sharp.o     lang/emf.o       lang/csv.o       lang/crystal.o   \
        lang/rye.o       lang/nanorc.o    lang/tcl.o       modes/fractal.o  \
        lang/algol68.o   $(EXTRA_MODES)
OBJS += modes/shell.o    modes/dired.o    modes/archive.o  modes/latex-mode.o

CFLAGS += -I./libqhtml
HOST_CFLAGS += -I./libqhtml
OBJS += modes/html.o modes/docbook.o \
        libqhtml/css.o libqhtml/xmlparse.o libqhtml/cssparse.o \
        libqhtml/html_style.o libqhtml/docbook_style.o

OBJS += modes/stb.o

OBJS_DIR := $(o)/obj$(DEBUG_SUFFIX)
BINDIR := $(o)/bin
GENDIR := $(o)/gen

SRCS := $(OBJS:.o=.c)
SRCS := $(subst libqhtml/html_style.c,$(GENDIR)/libqhtml/html_style.c,$(SRCS))
SRCS := $(subst libqhtml/docbook_style.c,$(GENDIR)/libqhtml/docbook_style.c,$(SRCS))

# Header dependencies are auto-generated via -MMD -MP (see .d files in OBJS_DIR)
# This list is only used for qe_modules.c generation and documentation
DEPENDS := qe.h charset.h color.h cutils.h display.h \
	qestyles.h unicode_join.h util.h variables.h session.h \
	wcwidth.h lang/clang.h

CFLAGS += -I$(OBJS_DIR)
OBJS := $(addprefix $(OBJS_DIR)/, $(OBJS))
OBJS += $(OBJS_DIR)/qe_modules.o

.DEFAULT_GOAL := all

#
# Build targets
#
all: $(o)/qe$(DEBUG_SUFFIX)$(EXE)

# Documentation is built as a dependency of the final binary (embedded via zip)
# but doesn't need to block object compilation
docs: $(o)/qe-manual.md

ifneq (,$(DEBUG_SUFFIX))
$(o)/qe$(DEBUG_SUFFIX)$(EXE): $(OBJS) $(DEP_LIBS) | $(EMBED_FILES)
	$(echo) LD $@
	$(cmd)  $(CC) $(LDFLAGS) -o $@ $(OBJS) $(DEP_LIBS) $(LIBS)
	$(call embed-resources,$@)
else
$(o)/qe_g$(EXE): $(OBJS) $(DEP_LIBS)
	$(echo) LD $@
	$(cmd)  $(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(o)/qe$(EXE): $(o)/qe_g$(EXE) Makefile | $(EMBED_FILES)
	@rm -f $@
	cp $< $@
	-$(STRIP) $@
	$(call embed-resources,$@)
	@ls -l $@
endif

# Embed resource files into APE binary via zip
# Cosmopolitan makes these accessible at /zip/ paths at runtime
# The binary is temporarily renamed to .com so zip modifies it in-place
# (without an extension, zip creates a separate .zip file instead)
define embed-resources
	$(echo) ZIP $1
	$(cmd)  mkdir -p $(o)/.embed/share/qe
	$(cmd)  for f in $(EMBED_FILES); do \
		if [ -f "$$f" ]; then cp "$$f" $(o)/.embed/share/qe/; fi; \
	done
	$(cmd)  mv $(1) $(1).com
	$(cmd)  cd $(o)/.embed && zip -qr0 ../../$(1).com share/qe/
	$(cmd)  mv $(1).com $(1)
	$(cmd)  rm -rf $(o)/.embed
endef

debug qe_debug: force
	$(MAKE) DEBUG=1

$(OBJS_DIR)/qe_modules.o: $(OBJS_DIR)/qe_modules.c Makefile | $(COSMOCC_DIR)/bin/cosmocc
	$(echo) CC $(ECHO_CFLAGS) -c $<
	$(cmd)  $(CC) $(DEFINES) $(CFLAGS) -MT $@ -MF $(@:.o=.d) -o $@ -c $<

$(OBJS_DIR)/qe_modules.c: $(SRCS) Makefile tools/gen-modules.sh
	@echo creating $@
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  sh tools/gen-modules.sh $@ $(SRCS)

$(OBJS_DIR)/charset.o: charset.c wcwidth.c
$(OBJS_DIR)/charsetjis.o: charsetjis.c charsetjis.def
# stb_image.h is vendored third-party code: relax warnings
$(OBJS_DIR)/modes/stb.o: modes/stb.c modes/stb_image.h Makefile | $(COSMOCC_DIR)/bin/cosmocc
	$(echo) CC $(ECHO_CFLAGS) -c $<
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(CC) $(DEFINES) $(CFLAGS) -Wno-error -Wno-double-promotion -Wno-sign-conversion -Wno-conversion -Wno-unused-but-set-variable -Wno-duplicated-branches -MT $@ -MF $(@:.o=.d) -o $@ -c $<
# libunicode and libregexp are vendored QuickJS code: relax warnings
$(OBJS_DIR)/libunicode.o: libunicode.c libunicode.h libunicode-table.h Makefile | $(COSMOCC_DIR)/bin/cosmocc
	$(echo) CC $(ECHO_CFLAGS) -c $<
	$(cmd)  $(CC) $(DEFINES) $(CFLAGS) -Wno-sign-conversion -Wno-conversion -MT $@ -MF $(@:.o=.d) -o $@ -c $<

$(OBJS_DIR)/libregexp.o: libregexp.c libregexp.h libregexp-opcode.h Makefile | $(COSMOCC_DIR)/bin/cosmocc
	$(echo) CC $(ECHO_CFLAGS) -c $<
	$(cmd)  $(CC) $(DEFINES) $(CFLAGS) -Wno-sign-conversion -Wno-conversion -MT $@ -MF $(@:.o=.d) -o $@ -c $<
$(OBJS_DIR)/libqhtml/html_style.o: $(GENDIR)/libqhtml/html_style.c Makefile | $(COSMOCC_DIR)/bin/cosmocc
	$(echo) CC $(ECHO_CFLAGS) -c $<
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(CC) $(DEFINES) $(CFLAGS) -MT $@ -MF $(@:.o=.d) -o $@ -c $<

$(OBJS_DIR)/libqhtml/docbook_style.o: $(GENDIR)/libqhtml/docbook_style.c Makefile | $(COSMOCC_DIR)/bin/cosmocc
	$(echo) CC $(ECHO_CFLAGS) -c $<
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(CC) $(DEFINES) $(CFLAGS) -MT $@ -MF $(@:.o=.d) -o $@ -c $<

$(OBJS_DIR)/libqhtml/css.o: libqhtml/css.c libqhtml/css.h libqhtml/cssid.h
$(OBJS_DIR)/libqhtml/cssparse.o: libqhtml/cssparse.c libqhtml/css.h libqhtml/cssid.h
$(OBJS_DIR)/libqhtml/xmlparse.o: libqhtml/xmlparse.c libqhtml/css.h libqhtml/htmlent.h

# Lua amalgamation: compile without QEmacs defines, relaxed warnings for vendored code
$(OBJS_DIR)/third_party/lua/lua-amalg.o: third_party/lua/lua-amalg.c third_party/lua/lua-amalg.h Makefile | $(COSMOCC_DIR)/bin/cosmocc
	$(echo) CC -c $<
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(CC) $(CFLAGS) -Wno-error -Wno-format-nonliteral -Wno-null-dereference -Wno-unused-value -Wno-sign-conversion -Wno-conversion -Wno-bad-function-cast -Wno-cast-qual -Wno-float-equal -Wno-duplicated-branches -MT $@ -MF $(@:.o=.d) -o $@ -c $<

$(OBJS_DIR)/%.o: %.c Makefile | $(COSMOCC_DIR)/bin/cosmocc
	$(echo) CC $(ECHO_CFLAGS) -c $<
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(CC) $(DEFINES) $(CFLAGS) -MT $@ -MF $(@:.o=.d) -o $@ -c $<

# Include auto-generated dependency files (created by -MMD -MP)
DEPS := $(OBJS:.o=.d)
-include $(DEPS)

#
# Host utilities (pattern rule covers tools/*.c -> o/bin/*)
#
$(BINDIR)/%$(EXE): tools/%.c
	$(echo) CC -o $@ $^
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(HOST_CC) $(HOST_CFLAGS) -o $@ $^

# Tools that also need cutils.c
$(BINDIR)/ligtoqe$(EXE): tools/ligtoqe.c cutils.c
$(BINDIR)/kmaptoqe$(EXE): tools/kmaptoqe.c cutils.c

# libqhtml: csstoqe tool and generated style sheets
$(BINDIR)/csstoqe$(EXE): libqhtml/csstoqe.c
	$(echo) CC -o $@ $<
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(HOST_CC) $(HOST_CFLAGS) -o $@ $<

$(GENDIR)/libqhtml/html_style.c: libqhtml/html.css $(BINDIR)/csstoqe$(EXE)
	$(echo) GEN $@
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(BINDIR)/csstoqe html_style < $< > $@

$(GENDIR)/libqhtml/docbook_style.c: libqhtml/docbook.css $(BINDIR)/csstoqe$(EXE)
	$(echo) GEN $@
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(BINDIR)/csstoqe docbook_style < $< > $@

ifdef BUILD_ALL
$(o)/ligatures: $(BINDIR)/ligtoqe$(EXE) unifont.lig
	$(BINDIR)/ligtoqe unifont.lig $@
else
$(o)/ligatures: ligatures
	@mkdir -p $(dir $@)
	@cp $< $@
endif

#
# build kmap table
#
KMAPS_DIR := kmap
KMAPS := Arabic.kmap langstrlist.kmap langstrcom.kmap langstrswap.kmap \
         langstrxfrm.kmap langstrflip.kmap langstrdiag.kmap \
         langstrbid.kmap langstrorig.kmap langstrstat.kmap \
         langstrocr.kmap langstrtbl.kmap Langstrpriv.kmap \
         langstrdic.kmap langstruser.kmap langstrstroke.kmap \
         langstrbraille.kmap langstrmorse.kmap
KMAPS := $(addprefix $(KMAPS_DIR)/, $(KMAPS))

ifdef BUILD_ALL
$(o)/kmaps: $(BINDIR)/kmaptoqe$(EXE) $(KMAPS)
	$(BINDIR)/kmaptoqe $@ $(KMAPS)
else
$(o)/kmaps: kmaps
	@mkdir -p $(dir $@)
	@cp $< $@
endif

#
# documentation
#
$(o)/qe-manual.md: $(BINDIR)/scandoc qe-manual.c $(SRCS) $(DEPENDS)
	@mkdir -p $(dir $@)
	$(BINDIR)/scandoc qe-manual.c $(SRCS) $(DEPENDS) > $@

#
# Test target
#
test:
	$(MAKE) -C tests test

# Lint: catch unbounded blocking I/O patterns that cause deadlocks.
# Allowlist:
#   session.c select/pselect: top-level event-loop multiplexers (safe)
#   write_all_timeout/qe_write_timeout definitions: the bounded helpers themselves
lint:
	@echo "==> checking for unbounded blocking I/O patterns"
	@fail=0; \
	if grep -rn 'poll(.*,\s*-1)' --include='*.c' \
	   | grep -v 'tests/' | grep -v '^\s*//' \
	   | grep -v 'write_all_timeout\|qe_write_timeout'; then \
	    echo "ERROR: found poll(..., -1) outside of write helpers"; \
	    fail=1; \
	fi; \
	if grep -rn 'write_all\b' --include='*.c' \
	   | grep -v 'tests/' | grep -v '^\s*//' \
	   | grep -v 'write_all_timeout\|qe_write_timeout' \
	   | grep -v 'session\.c'; then \
	    echo "ERROR: found write_all() usage — use write_all_timeout/qe_write_timeout with bounded timeout"; \
	    fail=1; \
	fi; \
	if [ "$$fail" -eq 1 ]; then exit 1; fi
	@echo "==> checking for banned function usage in source"
	@fail=0; \
	if grep -rn '\bsprintf\s*(' --include='*.c' \
	   | grep -v 'tests/' | grep -v 'third_party/' | grep -v 'tools/' \
	   | grep -v 'snprintf\|do_not_use\|vsnprintf'; then \
	    echo "ERROR: found sprintf() — use snprintf instead"; \
	    fail=1; \
	fi; \
	if grep -rn '\bstrcpy\s*(' --include='*.c' \
	   | grep -v 'tests/' | grep -v 'third_party/' | grep -v 'tools/' \
	   | grep -v 'pstrcpy\|do_not_use'; then \
	    echo "ERROR: found strcpy() — use pstrcpy instead"; \
	    fail=1; \
	fi; \
	if grep -rn '\bstrcat\s*(' --include='*.c' \
	   | grep -v 'tests/' | grep -v 'third_party/' | grep -v 'tools/' \
	   | grep -v 'pstrcat\|pstrncat\|do_not_use'; then \
	    echo "ERROR: found strcat() — use pstrcat instead"; \
	    fail=1; \
	fi; \
	if grep -rn '\batoi\s*(' --include='*.c' \
	   | grep -v 'tests/' | grep -v 'third_party/' | grep -v 'tools/' \
	   | grep -v 'qe_atoi'; then \
	    echo "ERROR: found atoi() — use qe_atoi instead"; \
	    fail=1; \
	fi; \
	if [ "$$fail" -eq 1 ]; then exit 1; fi
	@echo "==> checking for dangerous Unicode (bidi overrides, zero-width chars)"
	@fail=0; \
	if grep -rPn '\xe2\x80[\xaa-\xae\x8b-\x8f]|\xe2\x81[\xa6-\xa9]|\xef\xbb\xbf' --include='*.c' --include='*.h' \
	   | grep -v 'third_party/' | grep -v 'tests/' | grep -v 'tools/'; then \
	    echo "ERROR: dangerous Unicode (bidi override or zero-width) in source"; \
	    fail=1; \
	fi; \
	if [ "$$fail" -eq 1 ]; then exit 1; fi
	@echo "==> lint passed"

#
# Fetch and verify cosmocc toolchain
#
$(COSMOCC_DIR)/bin/cosmocc: $(COSMOCC_FETCH_DIR)/bin/cosmocc
	@ln -sfn $(CURDIR)/$(COSMOCC_FETCH_DIR) $(COSMOCC_DIR)
	@echo "==> symlinked $(COSMOCC_DIR) -> $(COSMOCC_FETCH_DIR)"

$(COSMOCC_FETCH_DIR)/bin/cosmocc:
	@echo "==> fetching cosmocc $(COSMOCC_VERSION)"
	@mkdir -p $(COSMOCC_FETCH_DIR)
	@curl -fsSL -o $(COSMOCC_FETCH_DIR)/cosmocc.zip $(COSMOCC_URL)
	@echo "$(COSMOCC_SHA256)  $(COSMOCC_FETCH_DIR)/cosmocc.zip" | sha256sum -c - >/dev/null
	@unzip -q $(COSMOCC_FETCH_DIR)/cosmocc.zip -d $(COSMOCC_FETCH_DIR)
	@rm -f $(COSMOCC_FETCH_DIR)/cosmocc.zip
	@echo "==> cosmocc installed to $(COSMOCC_FETCH_DIR)"

# CI target: build, test, lint, and verify
ci: all
	$(MAKE) -C tests test
	$(MAKE) lint
	file $(o)/qe
	test -f $(o)/qe

# Release target: build and create a GitHub release
# Requires GH_TOKEN, GITHUB_SHA, and optionally RELEASE to be set
release:
	$(MAKE) all
	mkdir -p release
	cp $(o)/qe release/qe
	chmod +x release/qe
	tag="$$(date -u +%Y-%m-%d)-$$(printf '%.7s' "$$GITHUB_SHA")" && \
	(cd release && sha256sum qe > SHA256SUMS && cat SHA256SUMS) && \
	gh release create "$$tag" \
		$$([ -z "$(RELEASE)" ] && echo --prerelease) \
		--title "$$tag" \
		release/qe release/SHA256SUMS

#
# Maintenance targets
#
clean:
	rm -rf $(filter-out $(COSMOCC_DIR),$(wildcard $(o)/*))

distclean: clean
	rm -f config.mak

install: all
	@mkdir -p $(INSTALL_DIR)
	cp $(o)/qe $(INSTALL_DIR)/qe.new
	mv -f $(INSTALL_DIR)/qe.new $(INSTALL_DIR)/qe
	@echo "installed qe to $(INSTALL_DIR)/qe"

rebuild:
	$(MAKE) clean all

TAGS: force
	etags *.[ch]

help:
	@echo "Usage: make -j\$$(nproc) [targets] [VERBOSE=1]"
	@echo ""
	@echo "Requires cosmocc (auto-fetched on first build)."
	@echo "Use -j\$$(nproc) for parallel builds (recommended)."
	@echo ""
	@echo "Build targets:"
	@echo "  all           build qe [default]"
	@echo "  test          run unit tests"
	@echo "  ci            build + test"
	@echo "  release       build + create GitHub release"
	@echo "  debug         debug build (qe_debug)"
	@echo "  install       build + install to INSTALL_DIR"
	@echo "  docs          build documentation only"
	@echo ""
	@echo "Flags:"
	@echo "  VERBOSE=1     show full compiler commands"
	@echo "  BUILD_ALL=1   rebuild generated tables (ligatures, kmaps, charsets)"

force:

FILE = qemacs-$(VERSION)

archive:
	git archive --prefix=$(FILE)/ HEAD | gzip > ../$(FILE).tar.gz

.PHONY: all docs test clean distclean install rebuild help force \
        ci release archive debug
