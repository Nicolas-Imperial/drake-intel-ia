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


# http://www.freesoftwaremagazine.com/articles/gnu_coding_standards_applied_to_autotools
## Enable GNU make secondary expansion feature
.SECONDEXPANSION:

## Default target
DEFAULT=all

## The default target is the first target in the makefile
$(DEFAULT):

## Export all variables to sub-makefiles
export
	
include Makefile.in

prefix := $(if $(prefix),$(prefix),$(call PREFIX_$(CONFIG)))

tarname = $(package)
distdir = $(abspath $(tarname)-$(version).$(minor).$(release))

## Targets asked for running, or default target if none
TARGETS = $(if $(MAKECMDGOALS),$(MAKECMDGOALS),$(DEFAULT))

# Check make version
VERSION = $(shell make --version|head -1|cut -f 1-2 -d ' ')

## Make all targets called dependant on some action to be run beforehand
$(TARGETS): $(FIRST)

all check install uninstall: version
all: submake-all
check: submake-check
install: pre-install do-install submake-install post-install
uninstall: submake-uninstall

$(abspath $(distdir)).tar.gz: $(abspath $(distdir))
	tar -ch -C $(abspath $(distdir)) -O .| gzip -9 -c > $(abspath $(distdir)).tar.gz

$(abspath $(distdir))-reset:
ifneq ($(wildcard $(abspath $(distdir))/.),)
	$(RM) -r $(abspath $(distdir))
endif

$(abspath $(distdir)): $(abspath $(distdir))-reset submake-dist
	mkdir -p $(abspath $(distdir))
	cp Makefile $(abspath $(distdir))
	cp Makefile.in $(abspath $(distdir))
	cp $(attach) $(abspath $(distdir))
	
clean: submake-clean clean-tree clean-dist

clean-tree:
	
clean-dist:
	$(RM) -r $(abspath $(distdir))
	$(RM) $(abspath $(distdir)).tar.gz

do-install:

dist: pre-dist do-dist post-dist
do-dist: $(abspath $(distdir)).tar.gz

distcheck: checkdist clean-dist

checkdist: $(abspath $(distdir)).tar.gz
	gzip -cd $+ | tar xvf -
	$(MAKE) -C $(abspath $(distdir)) check
	$(MAKE) -C $(abspath $(distdir)) DESTDIR=$(abspath $(distdir))/_inst install uninstall
	$(MAKE) -C $(abspath $(distdir)) clean

version:
	$(if $(findstring GNU Make,echo $(VERSION)),,@echo This Makefile requires GNU make)
	$(if $(findstring GNU Make,echo $(VERSION)),,@/bin/false)

submake-all submake-dist submake-check submake-install submake-uninstall submake-clean:
	@$(shell echo for i in "$(foreach var,$(subdirs),$(var))"\; do $(MAKE) -C \$$i $(patsubst submake-%,%,$@) distdir=$(distdir)/\$$i subdir=$(subdir)/\$$i\|\|exit 1\; done)

FORCE:
.PHONY: FORCE all version clean dist distcheck copy clean-dist clean-tree
.PHONY: install uninstall submake-all submake-dist submake-check submake-install submake-uninstall submake-clean $(abspath $(distdir)) $(abspath $(distdir))-reset pre-install do-install install post-install pre-dist do-dist post-dist 
