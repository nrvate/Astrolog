# Astrolog (Version 8.00) File: Makefile (Unix version)
#
# IMPORTANT NOTICE: Astrolog and all chart display routines and anything
# not enumerated elsewhere in this program are Copyright (C) 1991-2026 by
# Walter D. Pullen (Astara@msn.com, http://www.astrolog.org/astrolog.htm).
# Permission is granted to freely use, modify, and distribute these
# routines provided these credits and notices remain unmodified with any
# altered or distributed versions of the program.
#
# First created 11/21/1991.
#
# This Makefile is included only for convenience. One could easily compile
# Astrolog on most Unix systems by hand with the command:
# % cc -c -O *.cpp; cc -o astrolog *.o -lm -lX11
# Generally, all that needs to be done to compile once astrolog.h has been
# edited, is compile each source file, and link them together with the math
# library, and if applicable, the main X library.
#
NAME = astrolog
# The source list lives in one file for all five builds; see the header
# there before adding a source.
include Makefile.srcs
OBJS = $(patsubst %.cpp,%.o,$(SRC_CORE) $(SRC_GRAPHICS) $(SRC_SWISS))


# If you don't have X windows, delete the "-lX11" part from the line below:
# If not compiling with GNUC, delete the "-ldl" part from the line below:
LIBS = -lm -lX11 -ldl -s
# -std=gnu++17 is not decoration. calc.cpp uses class template argument
# deduction ("Borrow bciCore(ciCore);"), a C++17 feature. g++ 11 defaults
# to gnu++17 so this build happened to work, but Makefile.win relied on
# the same accident and mingw g++ 10 defaults to gnu++14 -- which is how
# the Windows build went 62 commits without compiling (work log item 146).
# Saying it out loud is the fix for the class, not just the instance.
CPPFLAGS = -MMD -MP -O -std=gnu++17 -Wno-write-strings -Wno-narrowing -Wno-comment
RM = rm -f

$(NAME): $(OBJS)
	g++ -o $(NAME) $(OBJS) $(LIBS)


# "make clean" cleans what this tree can build, which is four binaries and
# four object directories, not just upstream's. That is the conventional
# expectation and it was surprising before.
clean: clean-console
	$(MAKE) -f Makefile.qt clean
	$(MAKE) -f Makefile.qt.test clean
	$(MAKE) -f Makefile.qt.asan clean
	$(MAKE) -f Makefile.win clean
	$(MAKE) -f Makefile.wcli clean
	$(MAKE) -f Makefile.qt OBJDIR=obj-qt6 NAME=astrolog-qt6 clean
	$(MAKE) -f Makefile.qt.test OBJDIR=obj-qt6-test \
	  NAME=astrolog-qt6-test clean

# Upstream's narrower clean, kept because tools/asan-sweep.sh needs
# exactly it: that script builds with overridden CPPFLAGS into this same
# root directory, so it has to remove the sanitized objects on both sides
# of its run or the next ordinary build fails to LINK. It must not take
# the Qt builds with it -- the sweep already deletes ./astrolog while it
# runs, and doing the same to ./astrolog-qt-test would break the suite for
# anyone running it meanwhile.
clean-console:
	$(RM) $(OBJS) $(OBJS:.o=.d) $(NAME)

# This file is upstream's and builds upstream's binary: "make" produces
# ./astrolog, the X11 one. This fork's Qt port has its own makefiles, and
# the default target cannot simply move to it -- ten scripts here expect
# a plain "make" to leave ./astrolog behind, including all three
# differential matrices, tools/asan-sweep.sh (which builds with
# NAME= overridden), tools/settings-round-trip.sh and run-qt-tests.sh.
#
# So the Qt builds get named targets instead. Each is exactly the command
# CLAUDE.md documents; nothing here changes what "make" alone does.

qt:
	$(MAKE) -f Makefile.qt

qt-test:
	$(MAKE) -f Makefile.qt.test

qt-asan:
	$(MAKE) -f Makefile.qt.asan

qt-ubsan:
	$(MAKE) -f Makefile.qt.ubsan

# Qt6 is opt-in and is not what this machine builds by default: pkg-config
# finds Qt5, and the three Qt makefiles follow whatever it finds. A
# hand-installed Qt6 sits outside pkg-config's search path, so building
# against it is a matter of saying where -- and that command was run once,
# by hand (commit ee0623e), leaving ./astrolog-qt6 and obj-qt6/ behind that
# no target built and no clean removed. These two targets are that command
# written down, so the Qt6 build is reproducible and cleanable rather than
# an artifact somebody remembers making.
#
# Deliberately NOT part of "all": most machines have no Qt6, and there the
# pkg-config guard would stop the build with a package name, which is the
# right answer to "make all" only if you asked for Qt6.
#
# The -rpath is not optional for a hand-installed Qt6: without it the
# binary links and then dies at startup with "libQt6PrintSupport.so.6:
# cannot open shared object file", because /usr/local/qt6/lib is not on
# the runtime linker path. Measured, not guessed.
QT6_PKGCONFIG ?= /usr/local/qt6/lib/pkgconfig
QT6_LIBDIR ?= /usr/local/qt6/lib
QT6 = PKG_CONFIG_PATH=$(QT6_PKGCONFIG)
# A distribution's own Qt6 needs neither of those: pkg-config finds it on
# its default path and the runtime linker finds its libraries. So both
# variables take an empty override, which is how CI builds this --
# "make qt6 QT6_PKGCONFIG= QT6_LIBDIR=" on a runner with qt6-base-dev
# installed. An empty QT6_LIBDIR must produce no -rpath flag at all
# rather than a truncated one, hence the conditional.
ifeq ($(strip $(QT6_LIBDIR)),)
QT6LD =
else
QT6LD = LDEXTRA=-Wl,-rpath,$(QT6_LIBDIR)
endif

qt6:
	$(QT6) $(MAKE) -f Makefile.qt OBJDIR=obj-qt6 NAME=astrolog-qt6 \
	  $(QT6LD)

qt6-test:
	$(QT6) $(MAKE) -f Makefile.qt.test OBJDIR=obj-qt6-test \
	  NAME=astrolog-qt6-test $(QT6LD)

win:
	$(MAKE) -f Makefile.win

# The console Windows build. Same toolchain as "win", same shared core,
# but it enters at main() rather than WinMain, so it can be driven under
# Wine with no display -- which is what makes the Windows differential in
# QT_CI_PLAN.md item 6.4b cost 20 seconds instead of minutes of window
# driving. It is in "all" because it needs nothing "win" does not.
wcli:
	$(MAKE) -f Makefile.wcli

# Every build this fork has, in the order the pre-commit checks want them.
all: $(NAME) qt qt-test win wcli

# "make install" puts the two commands on PATH and leaves everything else
# exactly where it is. The data -- the ephemeris files, the atlas, the
# fonts, astrolog.as, the help text -- stays in this checkout on purpose,
# so what gets installed is a two-line wrapper that runs the in-tree
# binary by its absolute path.
#
# That works because of FileOpen() (io.cpp): the first place Astrolog
# looks for any data file is the directory holding the executable, taken
# from argv[0]. Measured on 2026-09-01, from an unrelated working
# directory: the in-tree binary run by absolute path finds nrvate.as; a
# plain copy of the same binary elsewhere reports "File 'nrvate.as' not
# found"; and the copy finds it again with ASTROLOG= pointed at the
# checkout. So a wrapper needs no environment variable and no recompile,
# and the installed command resolves data identically to ./astrolog.
#
# The trade this makes, said plainly: the installed commands depend on
# this checkout staying where it is. Move or delete the tree and they
# stop working -- with "no such file", which is at least legible. Run
# "make install" again after moving it.
#
#   make install                     # /usr/local/bin, needs root
#   make install PREFIX=$$HOME/.local  # no root
#   make uninstall                   # same PREFIX
#
PREFIX ?= /usr/local
BINDIR = $(DESTDIR)$(PREFIX)/bin
CMDS = $(NAME) astrolog-qt

# The Qt build also gets a menu entry and an icon, in the two places the
# freedesktop specs put them. Both are staged through DESTDIR, but Exec=
# and the icon name inside the .desktop file must be the FINAL paths, not
# the staged ones -- that is the whole distinction, and getting it wrong
# produces a launcher that works only on the machine that built it.
#
# The icons are the same artwork Windows uses: astrlog1.ico is the "icon"
# resource astrolog.rc lists first, and icons/astrolog{16,32,48}.png are
# its three frames extracted once, since .ico is not a format the icon
# theme spec expects. hicolor is the fallback theme every desktop reads.
APPDIR = $(DESTDIR)$(PREFIX)/share/applications
ICONDIR = $(DESTDIR)$(PREFIX)/share/icons/hicolor
ICONSIZES = 16 32 48

install: $(NAME) qt
	mkdir -p $(BINDIR)
	@for b in $(CMDS); do \
	  { echo '#!/bin/sh'; \
	    echo '# Generated by "make install" in $(CURDIR).'; \
	    echo '# Astrolog looks for its data files in the directory of its own'; \
	    echo '# executable first, and this fork keeps that data in the checkout,'; \
	    echo '# so run the binary where it lives rather than copying it out.'; \
	    echo '# If the checkout moves, run "make install" again.'; \
	    echo 'exec "$(CURDIR)/'"$$b"'" "$$@"'; \
	  } > $(BINDIR)/$$b && chmod 755 $(BINDIR)/$$b && \
	  echo "installed $(BINDIR)/$$b -> $(CURDIR)/$$b"; \
	done
	mkdir -p $(APPDIR)
	@{ echo '[Desktop Entry]'; \
	   echo 'Type=Application'; \
	   echo 'Version=1.0'; \
	   echo 'Name=Astrolog'; \
	   echo 'GenericName=Astrology Chart Calculator'; \
	   echo 'Comment=Cast and display astrological charts'; \
	   echo 'Exec=$(PREFIX)/bin/astrolog-qt'; \
	   echo 'Icon=astrolog'; \
	   echo 'Terminal=false'; \
	   echo 'Categories=Science;Astronomy;'; \
	   echo 'Keywords=astrology;horoscope;chart;ephemeris;zodiac;'; \
	   echo 'StartupNotify=true'; \
	 } > $(APPDIR)/astrolog.desktop
	@echo "installed $(APPDIR)/astrolog.desktop"
	@for s in $(ICONSIZES); do \
	  mkdir -p $(ICONDIR)/$${s}x$${s}/apps && \
	  cp icons/astrolog$$s.png $(ICONDIR)/$${s}x$${s}/apps/astrolog.png && \
	  echo "installed $(ICONDIR)/$${s}x$${s}/apps/astrolog.png"; \
	done

uninstall:
	@for b in $(CMDS); do \
	  $(RM) $(BINDIR)/$$b && echo "removed $(BINDIR)/$$b"; \
	done
	@$(RM) $(APPDIR)/astrolog.desktop && \
	  echo "removed $(APPDIR)/astrolog.desktop"
	@for s in $(ICONSIZES); do \
	  $(RM) $(ICONDIR)/$${s}x$${s}/apps/astrolog.png && \
	  echo "removed $(ICONDIR)/$${s}x$${s}/apps/astrolog.png"; \
	done

.PHONY: clean clean-console qt qt-test qt-asan qt6 qt6-test win wcli all \
	install uninstall

# Compiler-generated header dependencies; see Makefile.qt for the
# reasoning and what the hand-written version missed.
-include $(OBJS:.o=.d)
#
