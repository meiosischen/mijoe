sysconf_joedir = $(sysconfdir)/joe
data_joedir = $(datadir)/joe
data_docdir = $(docdir)

sysconf_joe_built = joerc

sysconf_joe_DATA = $(sysconf_joe_built) ftyperc shell.sh shell.csh

EXTRA_DIST = joerc.in ftyperc shell.sh shell.csh

joerc : joerc.in
	LC_ALL=C sed 's,@JOERC\@,$(sysconf_joedir),; s,@JOEDOC\@,$(data_docdir),; s,@JOEDATA\@,$(data_joedir),; s,@SPELL\@,@SPELL@,' $(srcdir)/joerc.in > $@

MOSTLYCLEANFILES = $(sysconf_joe_built)
