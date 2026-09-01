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
OBJS = astrolog.o switch.o atlas.o calc.o charts0.o charts1.o charts2.o charts3.o\
 data.o express.o general.o intrpret.o io.o matrix.o placalc.o placalc2.o\
 xdata.o xgeneral.o xdevice.o xcharts0.o xcharts1.o xcharts2.o xscreen.o\
 swecl.o swedate.o swehouse.o swejpl.o swemmoon.o swemplan.o sweph.o\
 swephlib.o


# If you don't have X windows, delete the "-lX11" part from the line below:
# If not compiling with GNUC, delete the "-ldl" part from the line below:
LIBS = -lm -lX11 -ldl -s
CPPFLAGS = -O -Wno-write-strings -Wno-narrowing -Wno-comment
RM = rm -f

$(NAME): $(OBJS)
	g++ -o $(NAME) $(OBJS) $(LIBS)

# Every object depends on the headers this fork edits constantly; see
# the same rule in Makefile.qt for why.
$(OBJS): astrolog.h extern.h

# "make clean" cleans what this tree can build, which is four binaries and
# four object directories, not just upstream's. That is the conventional
# expectation and it was surprising before.
clean: clean-console
	$(MAKE) -f Makefile.qt clean
	$(MAKE) -f Makefile.qt.test clean
	$(MAKE) -f Makefile.qt.asan clean
	$(MAKE) -f Makefile.win clean

# Upstream's narrower clean, kept because tools/asan-sweep.sh needs
# exactly it: that script builds with overridden CPPFLAGS into this same
# root directory, so it has to remove the sanitized objects on both sides
# of its run or the next ordinary build fails to LINK. It must not take
# the Qt builds with it -- the sweep already deletes ./astrolog while it
# runs, and doing the same to ./astrolog-qt-test would break the suite for
# anyone running it meanwhile.
clean-console:
	$(RM) $(OBJS) $(NAME)

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

win:
	$(MAKE) -f Makefile.win

# Every build this fork has, in the order the pre-commit checks want them.
all: $(NAME) qt qt-test win

.PHONY: clean clean-console qt qt-test qt-asan win all
#
