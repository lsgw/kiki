#! /usr/bin/make -rf
# Makefile.in
# libudns Makefile
#
# Copyright (C) 2005  Michael Tokarev <mjt@corpit.ru>
# This file is part of UDNS library, an async DNS stub resolver.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library, in file named COPYING.LGPL; if not,
# write to the Free Software Foundation, Inc., 59 Temple Place,
# Suite 330, Boston, MA  02111-1307  USA

NAME   = udns
VERS   = 0.2
SOVER  = 0

SRCS   = udns_dn.c udns_dntosp.c udns_parse.c udns_resolver.c udns_init.c \
	udns_misc.c udns_XtoX.c \
	udns_rr_a.c udns_rr_ptr.c udns_rr_mx.c udns_rr_txt.c udns_bl.c \
	udns_rr_srv.c udns_rr_naptr.c udns_codes.c udns_jran.c
USRCS  = dnsget.c rblcheck.c ex-rdns.c
DIST   = COPYING.LGPL udns.h udns.3 dnsget.1 rblcheck.1 $(SRCS) $(USRCS) \
	 NEWS TODO NOTES Makefile.in configure configure.lib \
	 inet_XtoX.c getopt.c

OBJS   = $(SRCS:.c=.o) $(GEN:.c=.o)
LIB    = lib$(NAME).a
LIBFL  = -L. -l$(NAME)

SOBJS  = $(OBJS:.o=.lo)
SOLIB  = lib$(NAME)_s.so
SOLIBV = lib$(NAME).so.$(SOVER)
SOLIBFL= -L. -l$(NAME)_s

LIBS   = $(LIB) $(SOLIBV)

UTILS   = $(USRCS:.c=)
UOBJS   = $(USRCS:.c=.o)
SOUTILS = $(USRCS:.c=_s)

NAMEPFX = $(NAME)-$(VERS)

CC = gcc
CFLAGS = -Wall -W -O2 -pipe
CDEFS = -DHAVE_CONFIG_H
PICFLAGS = -fPIC
AWK = awk

all: static

.SUFFIXES: .c .o .lo

static: $(LIB) $(UTILS)
staticlib: $(LIB)
$(LIB): $(OBJS)
	-rm -f $@
	$(AR) rv $@ $(OBJS)
.c.o:
	$(CC) $(CFLAGS) $(CDEFS) -c $<

shared: $(SOLIBV) $(SOUTILS)
sharedlib: $(SOLIBV)

$(SOLIBV): $(SOBJS)
	$(CC) -shared -Wl,--soname,$(SOLIBV) -o $@ $(SOBJS)
$(SOLIB): $(SOLIBV)
	rm -f $@
	ln -s $(SOLIBV) $@
.c.lo:
	$(CC) $(CFLAGS) $(PICFLAGS) $(CDEFS) -o $@ -c $<

# udns_codes.c is generated from udns.h
udns_codes.c: udns.h
	@echo Generating $@
	@set -e; exec >$@.tmp; \
	set T type C class R rcode; \
	echo "/* Automatically generated. */"; \
	echo "#include \"udns.h\""; \
	while [ "$$1" ]; do \
	 echo; \
	 echo "const struct dns_nameval dns_$${2}tab[] = {"; \
	 $(AWK) "/^  DNS_$${1}_[A-Z0-9_]+[ 	]*=/ \
	  { printf \" {%s,\\\"%s\\\"},\\n\", \$$1, substr(\$$1,7) }" \
	  udns.h ; \
	 echo " {0,0}};"; \
	 echo "const char *dns_$${2}name(enum dns_$${2} code) {"; \
	 echo " static char nm[20];"; \
	 echo " switch(code) {"; \
	 $(AWK) "BEGIN{i=0} \
	   /^  DNS_$${1}_[A-Z0-9_]+[ 	]*=/ \
	   {printf \" case %s: return dns_$${2}tab[%d].name;\\n\",\$$1,i++}\
	   " udns.h ; \
	 echo " }"; \
	 echo " return _dns_format_code(nm,\"$$2\",code);"; \
	 echo "}"; \
	 shift 2; \
	done
	@mv $@.tmp $@

udns.3.html: udns.3
	groff -man -Thtml udns.3 > $@.tmp
	mv $@.tmp $@

dist: $(NAMEPFX).tar.gz
$(NAMEPFX).tar.gz: $(DIST)
	mkdir $(NAMEPFX)
	ln $(DIST) $(NAMEPFX)
	tar cvfz $@ $(NAMEPFX)
	rm -rf $(NAMEPFX)
subdist:
	cp -p $(DIST) $(TARGET)/

clean:
	rm -f $(OBJS)
	rm -f $(SOBJS)
	rm -f $(UOBJS)
	rm -f config.log
distclean: clean
	rm -f $(LIBS) $(SOLIB) udns.3.html
	rm -f $(UTILS) $(SOUTILS)
	rm -f config.status config.h Makefile


Makefile: configure configure.lib Makefile.in
	./configure
	@echo
	@echo Please rerun make >&2
	@exit 1

.PHONY: all static staticlib shared sharedlib dist clean distclean subdist \
  depend dep deps

depend dep deps: $(SRCS) $(USRC)
	@echo Generating deps for:
	@echo \ $(SRCS)
	@echo \ $(USRCS)
	@sed '/^# depend/q' Makefile.in > Makefile.tmp
	@set -e; \
	for f in $(SRCS) $(USRCS); do \
	 echo $${f%.c}.o $${f%.c}.lo: $$f \
	  `sed -n 's/^#[ 	]*include[ 	]*"\(.*\)".*/\1/p' $$f`; \
	done >> Makefile.tmp; \
	for f in $(USRCS:.c=.o); do \
	 echo "$${f%.?}: $$f \$$(LIB)"; \
	 echo "	\$$(CC) \$$(CFLAGS) -o \$$@ $$f \$$(LIBFL)"; \
	 echo "$${f%.?}_s: $$f \$$(SOLIB)"; \
	 echo "	\$$(CC) \$$(CFLAGS) -o \$$@ $$f \$$(SOLIBFL)"; \
	done >> Makefile.tmp ; \
	if cmp Makefile.tmp Makefile.in >/dev/null 2>&1 ; then \
	 echo Makefile.in unchanged; rm -f Makefile.tmp; \
	else \
	 echo Updating Makfile.in; mv -f Makefile.tmp Makefile.in ; \
	fi

# depend
udns_dn.o udns_dn.lo: udns_dn.c udns.h
udns_dntosp.o udns_dntosp.lo: udns_dntosp.c udns.h
udns_parse.o udns_parse.lo: udns_parse.c udns.h
udns_resolver.o udns_resolver.lo: udns_resolver.c config.h udns.h
udns_init.o udns_init.lo: udns_init.c config.h udns.h
udns_misc.o udns_misc.lo: udns_misc.c udns.h
udns_XtoX.o udns_XtoX.lo: udns_XtoX.c config.h udns.h inet_XtoX.c
udns_rr_a.o udns_rr_a.lo: udns_rr_a.c udns.h
udns_rr_ptr.o udns_rr_ptr.lo: udns_rr_ptr.c udns.h
udns_rr_mx.o udns_rr_mx.lo: udns_rr_mx.c udns.h
udns_rr_txt.o udns_rr_txt.lo: udns_rr_txt.c udns.h
udns_bl.o udns_bl.lo: udns_bl.c udns.h
udns_rr_srv.o udns_rr_srv.lo: udns_rr_srv.c udns.h
udns_rr_naptr.o udns_rr_naptr.lo: udns_rr_naptr.c udns.h
udns_codes.o udns_codes.lo: udns_codes.c udns.h
udns_jran.o udns_jran.lo: udns_jran.c udns.h
dnsget.o dnsget.lo: dnsget.c config.h udns.h getopt.c
rblcheck.o rblcheck.lo: rblcheck.c config.h udns.h getopt.c
ex-rdns.o ex-rdns.lo: ex-rdns.c udns.h
dnsget: dnsget.o $(LIB)
	$(CC) $(CFLAGS) -o $@ dnsget.o $(LIBFL)
dnsget_s: dnsget.o $(SOLIB)
	$(CC) $(CFLAGS) -o $@ dnsget.o $(SOLIBFL)
rblcheck: rblcheck.o $(LIB)
	$(CC) $(CFLAGS) -o $@ rblcheck.o $(LIBFL)
rblcheck_s: rblcheck.o $(SOLIB)
	$(CC) $(CFLAGS) -o $@ rblcheck.o $(SOLIBFL)
ex-rdns: ex-rdns.o $(LIB)
	$(CC) $(CFLAGS) -o $@ ex-rdns.o $(LIBFL)
ex-rdns_s: ex-rdns.o $(SOLIB)
	$(CC) $(CFLAGS) -o $@ ex-rdns.o $(SOLIBFL)
