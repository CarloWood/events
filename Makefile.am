AM_CPPFLAGS = -iquote $(top_builddir) -iquote $(top_srcdir) -iquote $(top_srcdir)/cwds

SUBDIRS =

noinst_LTLIBRARIES = libevents.la

SOURCES = \
	Events.h \
	Events.cxx

libevents_la_SOURCES = ${SOURCES}
libevents_la_CXXFLAGS = -std=c++11 @LIBCWD_R_FLAGS@
libevents_la_LIBADD = @LIBCWD_R_LIBS@

# ---------------- Test suite ------------------

bin_PROGRAMS = test1 test2 test3 test4

test1_SOURCES = test1.cxx
test1_CXXFLAGS = @LIBCWD_R_FLAGS@ -pthread
test1_LDADD = libevents.la ../utils/libutils_r.la ../cwds/libcwds_r.la

test2_SOURCES = test2.cxx
test2_CXXFLAGS = @LIBCWD_R_FLAGS@ -pthread
test2_LDADD = libevents.la ../utils/libutils_r.la ../cwds/libcwds_r.la

test3_SOURCES = test3.cxx
test3_CXXFLAGS = @LIBCWD_R_FLAGS@ -pthread
test3_LDADD = libevents.la ../utils/libutils_r.la ../cwds/libcwds_r.la

test4_SOURCES = test4.cxx
test4_CXXFLAGS = @LIBCWD_R_FLAGS@ -pthread
test4_LDADD = libevents.la ../utils/libutils_r.la ../cwds/libcwds_r.la

# --------------- Maintainer's Section

if MAINTAINER_MODE
distclean-local:
	-rm -f *.s *.ii

clean-local:
endif

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
