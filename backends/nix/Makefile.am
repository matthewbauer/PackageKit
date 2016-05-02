plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_nix.la

libpk_backend_nix_la_SOURCES = pk-backend-nix.cc
libpk_backend_nix_la_LIBADD = -lnixmain -lnixexpr -lnixstore $(PK_PLUGIN_LIBS)
libpk_backend_nix_la_LDFLAGS = -module -avoid-version $(NIX_LIBS)
libpk_backend_nix_la_CXXFLAGS = $(PK_PLUGIN_CFLAGS) $(NIX_CPPFLAGS) \
  $(AM_CPPFLAGS) -std=gnu++11

-include $(top_srcdir)/git.mk
