# Copyright 2015 Nicolas Melot
#
# This file is part of QDM.
#
# QDM is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# QDM is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with QDM. If not, see <http://www.gnu.org/licenses/>.
#

## Default target
DEFAULT=all

## The default target is the first target in the makefile
$(DEFAULT):

## Export all variables to sub-makefiles
export

include Makefile.in

## Targets asked for running, or default target if none
TARGETS=$(if $(MAKECMDGOALS),$(MAKECMDGOALS),$(DEFAULT))

## Make all targets called dependant on some action to be run beforehand
$(TARGETS): $(FIRST)

all: submake-all
clean: submake-clean

dist: submake-dist
	mkdir -p $(distdir)
	cp Makefile $(distdir)
	cp Makefile.in $(distdir)
	$(if $(strip $(attach)),cp $(attach) $(distdir))
	
install: pre-install do-install submake-install post-install

pre-install:

do-install:
	$(if $(strip $(attach)),test -d $(DESTDIR)$(prefix)/$(subdir) || install -d $(DESTDIR)$(prefix)/$(subdir))
	$(if $(strip $(attach)),install -m 644 $(attach) $(DESTDIR)$(prefix)/$(subdir))

post-install:
	
uninstall: pre-uninstall do-uninstall submake-uninstall post-uninstall
pre-uninstall:
post-uninstall:
do-uninstall:
	$(RM) $(foreach var,$(attach), $(DESTDIR)$(prefix)/$(subdir)/$(var))
	
check: submake-check
	$(shell echo for i in "$(foreach var,$(subdirs),$(var))"\; do $(MAKE) -C \$$i $@ \; done)

checkdist distcheck: dist

submake-all submake-clean submake-check submake-dist submake-install submake-uninstall:
	@$(shell echo for i in "$(foreach var,$(subdirs),$(var))"\; do $(MAKE) -C \$$i $(patsubst submake-%,%,$@) distdir=$(distdir)/\$$i subdir=$(subdir)/\$$i\|\|exit 1\; done)
        
.PHONY: all dist install pre-install do-install post-install check submake-all submake-check submake-clean submake-dist submake-install submake-uninstall
