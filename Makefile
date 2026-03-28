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

OSNAME := $(shell uname -s)

# Cosmopolitan toolchain — auto-fetched on first build
COSMOCC_VERSION := cosmocc-2026.03.15-bbe7b3cf4
COSMOCC_SHA256 := 344ffe8ec31dc5eeba72a8c3747f29181322e2a8041e505360860d92a9729dc7
COSMOCC_URL := https://github.com/whilp/cosmopolitan/releases/download/$(COSMOCC_VERSION)/cosmocc.zip
COSMOCC_DIR ?= o/cosmocc

export PATH := $(CURDIR)/$(COSMOCC_DIR)/bin:$(PATH)

CC = cosmocc
HOST_CC ?= cc
AR = cosmoar
STRIP ?= true
INSTALL_DIR ?= /persist/whilp/.local/bin

prefix ?= /usr/local
datadir ?= $(prefix)/share
mandir ?= $(prefix)/man
CFLAGS ?= -O2
LDFLAGS ?=
LIBS ?=
EXE ?=
EXTRALIBS ?= -lm
TARGET_OS ?= $(OSNAME)
TARGET_ARCH ?= $(shell uname -m)
SRC_PATH ?= $(CURDIR)
VERSION ?= $(shell cat VERSION)

# Feature flags (control which object files are compiled)
CONFIG_SESSION_DETACH ?= yes
CONFIG_ALL_KMAPS ?= yes
CONFIG_ALL_MODES ?= yes
CONFIG_UNICODE_JOIN ?= yes
CONFIG_HTML ?= yes

-include config.mak

ifeq (,$(V)$(VERBOSE))
    echo := @echo
    cmd  := @
else
    echo := @:
    cmd  :=
endif

-include cflags.mk

CFLAGS += -mcosmo -Wno-unused-result
CFLAGS += -I.

ifeq ($(CC),$(HOST_CC))
  HOST_CFLAGS := $(CFLAGS)
endif

DEFINES = -DHAVE_QE_CONFIG_H -DQE_VERSION='"$(VERSION)"'

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
EMBED_FILES := kmaps ligatures config.eg qe-manual.md qe.1

# Object files
OBJS := qe.o cutils.o util.o color.o charset.o buffer.o search.o input.o display.o \
        qescript.o modes/hex.o test_display.o extras.o variables.o

OBJS += unix.o tty.o
ifdef CONFIG_SESSION_DETACH
  OBJS += session.o
endif
LIBS += $(EXTRALIBS)

ifdef CONFIG_ALL_KMAPS
  OBJS += kmap.o
endif

ifdef CONFIG_UNICODE_JOIN
  OBJS += unicode_join.o arabic.o indic.o
  OBJS += libunicode.o libregexp.o
endif

# more charsets if needed
OBJS += charsetjis.o charsetmore.o

ifdef CONFIG_ALL_MODES
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
endif

ifdef CONFIG_HTML
  CFLAGS += -I./libqhtml
  HOST_CFLAGS += -I./libqhtml
  OBJS += modes/html.o modes/docbook.o
  LIBQHTML_OBJS := libqhtml/css.o libqhtml/xmlparse.o libqhtml/cssparse.o \
                    libqhtml/html_style.o libqhtml/docbook_style.o
  OBJS += $(LIBQHTML_OBJS)
endif

OBJS += modes/stb.o

SRCS := $(OBJS:.o=.c)

DEPENDS := qe.h config.h charset.h color.h cutils.h display.h \
	qestyles.h unicode_join.h util.h variables.h session.h \
	wcwidth.h lang/clang.h

OBJS_DIR := $(o)/obj$(DEBUG_SUFFIX)
CFLAGS += -I$(OBJS_DIR)
OBJS := $(addprefix $(OBJS_DIR)/, $(OBJS))
OBJS += $(OBJS_DIR)/qe_modules.o

BINDIR := $(o)/bin

.DEFAULT_GOAL := all

#
# Build targets
#
all: qe$(DEBUG_SUFFIX)$(EXE) kmaps ligatures qe-manual.md

ifneq (,$(DEBUG_SUFFIX))
qe$(DEBUG_SUFFIX)$(EXE): $(OBJS) $(DEP_LIBS)
	$(echo) LD $@
	$(cmd)  $(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(call embed-resources,$@)
else
qe_g$(EXE): $(OBJS) $(DEP_LIBS)
	$(echo) LD $@
	$(cmd)  $(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

qe$(EXE): qe_g$(EXE) Makefile
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

$(OBJS_DIR)/qe_modules.o: $(OBJS_DIR)/qe_modules.c Makefile
	$(echo) CC $(ECHO_CFLAGS) -c $<
	$(cmd)  $(CC) $(DEFINES) $(CFLAGS) -o $@ -c $<

$(OBJS_DIR)/qe_modules.c: $(SRCS) Makefile tools/gen-modules.sh
	@echo creating $@
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  sh tools/gen-modules.sh $@ $(SRCS)

$(OBJS_DIR)/charset.o: charset.c wcwidth.c
$(OBJS_DIR)/charsetjis.o: charsetjis.c charsetjis.def
$(OBJS_DIR)/modes/stb.o: modes/stb.c modes/stb_image.h
$(OBJS_DIR)/libunicode.o: libunicode.c libunicode.h libunicode-table.h
$(OBJS_DIR)/libregexp.o: libregexp.c libregexp.h libregexp-opcode.h
$(OBJS_DIR)/libqhtml/html_style.o: libqhtml/html_style.c
$(OBJS_DIR)/libqhtml/docbook_style.o: libqhtml/docbook_style.c
$(OBJS_DIR)/libqhtml/css.o: libqhtml/css.c libqhtml/css.h libqhtml/cssid.h
$(OBJS_DIR)/libqhtml/cssparse.o: libqhtml/cssparse.c libqhtml/css.h libqhtml/cssid.h
$(OBJS_DIR)/libqhtml/xmlparse.o: libqhtml/xmlparse.c libqhtml/css.h libqhtml/htmlent.h

$(OBJS_DIR)/%.o: %.c $(DEPENDS) Makefile | $(COSMOCC_DIR)/bin/cosmocc
	$(echo) CC $(ECHO_CFLAGS) -c $<
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(CC) $(DEFINES) $(CFLAGS) -o $@ -c $<

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

libqhtml/html_style.c: libqhtml/html.css $(BINDIR)/csstoqe$(EXE)
	$(echo) GEN $@
	$(cmd)  $(BINDIR)/csstoqe html_style < $< > $@

libqhtml/docbook_style.c: libqhtml/docbook.css $(BINDIR)/csstoqe$(EXE)
	$(echo) GEN $@
	$(cmd)  $(BINDIR)/csstoqe docbook_style < $< > $@

ifdef BUILD_ALL
ligatures: $(BINDIR)/ligtoqe$(EXE) unifont.lig
	$(BINDIR)/ligtoqe unifont.lig $@
else
ligatures: unifont.lig
	@test -f $@ || cp $< $@
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
kmaps: $(BINDIR)/kmaptoqe$(EXE) $(KMAPS)
	$(BINDIR)/kmaptoqe $@ $(KMAPS)
else
kmaps:
	@test -f $@
endif

#
# documentation
#
qe-manual.md: $(BINDIR)/scandoc qe-manual.c $(SRCS) $(DEPENDS) config.h
	$(BINDIR)/scandoc qe-manual.c $(SRCS) $(DEPENDS) config.h > $@

#
# Test target
#
test:
	$(MAKE) -C tests test

#
# Fetch and verify cosmocc toolchain
#
$(COSMOCC_DIR)/bin/cosmocc:
	@echo "==> fetching cosmocc $(COSMOCC_VERSION)"
	@mkdir -p $(COSMOCC_DIR)
	@curl -fsSL -o $(COSMOCC_DIR)/cosmocc.zip $(COSMOCC_URL)
	@echo "$(COSMOCC_SHA256)  $(COSMOCC_DIR)/cosmocc.zip" | sha256sum -c - >/dev/null
	@unzip -q $(COSMOCC_DIR)/cosmocc.zip -d $(COSMOCC_DIR)
	@rm -f $(COSMOCC_DIR)/cosmocc.zip
	@echo "==> cosmocc installed to $(COSMOCC_DIR)"

# CI target: build, test, and verify
ci:
	$(MAKE) all test
	file qe
	test -f qe

# Release target: build and create a GitHub release
# Requires GH_TOKEN, GITHUB_SHA, and optionally RELEASE to be set
release:
	$(MAKE) all
	mkdir -p release
	cp qe release/qe
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
	rm -rf $(o)/obj $(o)/obj_debug $(o)/bin $(o)/.embed
	rm -f *~ *.o *.a *.exe *_g *_debug TAGS gmon.out core *.exe.stackdump \
	      qe \
	      qe-doc.aux qe-doc.info qe-doc.log qe-doc.pdf qe-doc.toc

distclean: clean
	rm -f config.mak

install: all
	@mkdir -p $(INSTALL_DIR)
	cp qe $(INSTALL_DIR)/qe.new
	mv -f $(INSTALL_DIR)/qe.new $(INSTALL_DIR)/qe
	@echo "installed qe to $(INSTALL_DIR)/qe"

rebuild:
	$(MAKE) clean all

TAGS: force
	etags *.[ch]

help:
	@echo "Usage: make [targets] [VERBOSE=1]"
	@echo ""
	@echo "Requires cosmocc (auto-fetched on first build)."
	@echo ""
	@echo "Build targets:"
	@echo "  all           build qe [default]"
	@echo "  test          run unit tests"
	@echo "  ci            build + test"
	@echo "  release       build + create GitHub release"
	@echo "  debug         debug build (qe_debug)"
	@echo "  install       build + install to INSTALL_DIR"
	@echo ""
	@echo "Flags:"
	@echo "  VERBOSE=1     show full compiler commands"
	@echo "  BUILD_ALL=1   rebuild generated tables (ligatures, kmaps, charsets)"

force:

FILE = qemacs-$(VERSION)

archive:
	git archive --prefix=$(FILE)/ HEAD | gzip > ../$(FILE).tar.gz

.PHONY: all test clean distclean install rebuild help force \
        ci release archive debug
