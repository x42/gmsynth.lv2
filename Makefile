#!/usr/bin/make -f

# these can be overridden using make variables. e.g.
#   make CFLAGS=-O2
#   make install DESTDIR=$(CURDIR)/debian/gmsynth.lv2 PREFIX=/usr
#
PREFIX ?= /usr/local
MANDIR ?= $(PREFIX)/share/man/man1
# see http://lv2plug.in/pages/filesystem-hierarchy-standard.html, don't use libdir
LV2DIR ?= $(PREFIX)/lib/lv2

OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only -DNDEBUG
CFLAGS ?= -Wall -g -Wno-unused-function
STRIP  ?= strip

gmsynth_VERSION?=$(shell git describe --tags HEAD 2>/dev/null | sed 's/-g.*$$//;s/^v//' || echo "LV2")

###############################################################################

BUILDDIR = build/

###############################################################################

LV2NAME=gmsynth
BUNDLE=gmsynth.lv2
targets=

LOADLIBES=-lm

UNAME=$(shell uname)
ifeq ($(UNAME),Darwin)
  LV2LDFLAGS=-dynamiclib
  LIB_EXT=.dylib
  EXTENDED_RE=-E
  STRIPFLAGS=-u -r -arch all -s lv2syms
  targets+=lv2syms
else
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic -Wl,--as-needed -pthread
  LIB_EXT=.so
  STRIPFLAGS= -s
  EXTENDED_RE=-r
endif

ifneq ($(XWIN),)
  CC=$(XWIN)-gcc
  STRIP=$(XWIN)-strip
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic -Wl,--as-needed -pthread
  LIB_EXT=.dll
  override LDFLAGS += -static-libgcc
endif

targets+=$(BUILDDIR)$(LV2NAME)$(LIB_EXT)

targets+=$(BUILDDIR)GeneralUser_LV2.sf2

###############################################################################
# extract versions
LV2VERSION=$(gmsynth_VERSION)
include git2lv2.mk

###############################################################################
# check for build-dependencies
ifeq ($(shell pkg-config --exists lv2 || echo no), no)
  $(error "LV2 SDK was not found")
endif

ifeq ($(shell pkg-config --atleast-version=1.6.0 lv2 || echo no), no)
  $(error "LV2 SDK needs to be version 1.6.0 or later")
endif

ifeq ($(shell pkg-config --exists glib-2.0 || echo no), no)
  $(error "glib-2.0 was not found.")
endif

# check for lv2_atom_forge_object  new in 1.8.1 deprecates lv2_atom_forge_blank
ifeq ($(shell pkg-config --atleast-version=1.8.1 lv2 && echo yes), yes)
  override CFLAGS += -DHAVE_LV2_1_8
endif

# add library dependent flags and libs
override CFLAGS += $(OPTIMIZATIONS) -DVERSION="\"$(gmsynth_VERSION)\""
override CFLAGS += `pkg-config --cflags lv2 glib-2.0`
ifeq ($(XWIN),)
override CFLAGS += -fPIC -fvisibility=hidden
else
override CFLAGS += -DPTW32_STATIC_LIB
endif
override LOADLIBES += `pkg-config $(PKG_UI_FLAGS) --libs glib-2.0`


###############################################################################
# build target definitions
default: all

all: $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(targets)

lv2syms:
	echo "_lv2_descriptor" > lv2syms

$(BUILDDIR)manifest.ttl: lv2ttl/manifest.ttl.in Makefile
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/" \
		lv2ttl/manifest.ttl.in > $(BUILDDIR)manifest.ttl

$(BUILDDIR)$(LV2NAME).ttl: Makefile lv2ttl/$(LV2NAME).*.in
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g" \
		lv2ttl/$(LV2NAME).ttl.in > $(BUILDDIR)$(LV2NAME).ttl

$(BUILDDIR)%.sf2:
	cp -v sf2/$(*F).sf2 $@

FLUID_SRC = \
            fluidsynth/src/fluid_adsr_env.c \
            fluidsynth/src/fluid_chan.c \
            fluidsynth/src/fluid_chorus.c \
            fluidsynth/src/fluid_conv.c \
            fluidsynth/src/fluid_defsfont.c \
            fluidsynth/src/fluid_event.c \
            fluidsynth/src/fluid_gen.c \
            fluidsynth/src/fluid_hash.c \
            fluidsynth/src/fluid_iir_filter.c \
            fluidsynth/src/fluid_lfo.c \
            fluidsynth/src/fluid_list.c \
            fluidsynth/src/fluid_midi.c \
            fluidsynth/src/fluid_mod.c \
            fluidsynth/src/fluid_rev.c \
            fluidsynth/src/fluid_ringbuffer.c \
            fluidsynth/src/fluid_rvoice.c \
            fluidsynth/src/fluid_rvoice_dsp.c \
            fluidsynth/src/fluid_rvoice_event.c \
            fluidsynth/src/fluid_rvoice_mixer.c \
            fluidsynth/src/fluid_samplecache.c \
            fluidsynth/src/fluid_settings.c \
            fluidsynth/src/fluid_sffile.c \
            fluidsynth/src/fluid_sfont.c \
            fluidsynth/src/fluid_synth.c \
            fluidsynth/src/fluid_synth_monopoly.c \
            fluidsynth/src/fluid_sys.c \
            fluidsynth/src/fluid_tuning.c \
            fluidsynth/src/fluid_voice.c

CPPFLAGS += -Ifluidsynth -I fluidsynth/fluidsynth -DHAVE_CONFIG_H -D DEFAULT_SOUNDFONT=\"\"
DSP_SRC  = src/$(LV2NAME).c $(FLUID_SRC)
DSP_DEPS = $(DSP_SRC)

$(BUILDDIR)$(LV2NAME)$(LIB_EXT): $(DSP_DEPS) Makefile
	@mkdir -p $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 \
	  -o $(BUILDDIR)$(LV2NAME)$(LIB_EXT) $(DSP_SRC) \
	  -shared $(LV2LDFLAGS) $(LDFLAGS) $(LOADLIBES)
	$(STRIP) $(STRIPFLAGS) $(BUILDDIR)$(LV2NAME)$(LIB_EXT)

ifneq ($(BUILDOPENGL), no)
 -include $(RW)robtk.mk
endif

$(BUILDDIR)$(LV2GUI)$(LIB_EXT): gui/$(LV2NAME).c

###############################################################################
# install/uninstall/clean target definitions

install: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m644 $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(BUILDDIR)*.sf2 $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m755 $(BUILDDIR)$(LV2NAME)$(LIB_EXT) $(DESTDIR)$(LV2DIR)/$(BUNDLE)

uninstall:
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/manifest.ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME).ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/*.sf2
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME)$(LIB_EXT)
	-rmdir $(DESTDIR)$(LV2DIR)/$(BUNDLE)

install-man:

uninstall-man:

clean:
	rm -f $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl \
	  $(BUILDDIR)$(LV2NAME)$(LIB_EXT) \
	  $(BUILDDIR)*.sf2
	rm -rf $(BUILDDIR)*.dSYM
	-test -d $(BUILDDIR) && rmdir $(BUILDDIR) || true

distclean: clean
	rm -f cscope.out cscope.files tags

.PHONY: clean all install uninstall distclean \
