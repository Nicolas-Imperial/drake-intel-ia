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


## Enable GNU make secondary expansion feature
.SECONDEXPANSION:

## The default target is the first target in the makefile
$(DEFAULT):
include Makefile.in

## Export all variables to sub-makefiles
export

## Fills configure directory variables with default values if necessary and define environment variables
final_prefix := $(if $(prefix),$(prefix),$(call PREFIX_$(CONFIG)))
final_libdir := $(if $(libdir),$(libdir),$(final_prefix)/lib)
final_sharedir := $(if $(libdir),$(libdir),$(final_prefix)/share/$(package))
final_includedir := $(if $(includedir),$(includedir),$(final_prefix)/include)
final_bindir := $(if $(bindir),$(bindir),$(final_prefix)/bin)

## https://stackoverflow.com/questions/16144115/makefile-remove-duplicate-words-without-sorting
define uniq =
  $(eval seen :=)
  $(foreach _,$1,$(if $(filter $_,${seen}),,$(eval seen += $_)))
  ${seen}
endef

export LIBRARY_PATH := $(LIBRARY_PATH):$(final_libdir)
#export LD_RUN_PATH := $(shell echo $(call uniq,$(shell echo $(LD_RUN_PATH):$(final_libdir) | tr -s : | tr : ' ')) | tr ' ' :)
export LD_RUN_PATH := $(shell echo $(call uniq,$(shell echo $(final_libdir):$(LD_RUN_PATH) | tr -s : | tr : ' ')) | tr ' ' :)
#export LD_RUN_PATH := $(LD_RUN_PATH):$(final_libdir)
export PKG_CONFIG_PATH := $(PKG_CONFIG_PATH):$(final_libdir)/pkgconfig
export C_INCLUDE_PATH := $(C_INCLUDE_PATH):$(final_includedir)
export CPLUS_INCLUDE_PATH := $(CPLUS_INCLUDE_PATH):$(final_includedir)

## Make this folder as a pkgconfig search path so locally compiled libraries can be resolved before installation
export PKG_CONFIG_PATH:=.:$(PKG_CONFIG_PATH)

## Default target
DEFAULT := all

## Targets asked for running, or default target if none
TARGETS := $(if $(MAKECMDGOALS),$(MAKECMDGOALS),$(DEFAULT))

## Make all targets called dependent on some action to be run beforehand
$(TARGETS): $(FIRST)

## Rebuild all project if any of the files in the list was modified since last build
REBUILD_ON_UPDATE = Makefile Makefile.in

####################################################################
##### Here begin the region you really don't want to mess with #####
####################################################################

drake_schedule := schedule
drake_schedule_extension := xml
drake_taskgraph := taskgraph
drake_taskgraph_extension := graphml

shared_package := $(sort $(sharedcc_package) $(sharedcxx_package))

## Gives the longest prefix in the prefix list (all elements must have the same character at the same index, or no character at that index)
define longest
$(if $(strip $1),$(word $(words $1),$(sort $1)),)
endef

## Gives the source file extension of language $1
define ext
$(if $(filter cc,$1),c,$(if $(filter cxx,$1),cpp,))
endef

## Finds the longest suffix of $1 in the list $2
define suffixin
$(firstword $(shell echo $(sort $(shell echo $(foreach var,$2,$(if $(filter %$(var),$1),$(var),))|rev))|rev))
endef

## Get pkg-config output for the operation $2 (cflags|libs) on package list $1, with the corresponding options from variable $3$<package_name>
define pkg-config
$(if $(strip $1),$(if $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --$2 $(foreach pkg,$1,"$(pkg) $(call $3$(pkg)_version)")),$(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --$2 $(foreach pkg,$1,"$(pkg) $(call $3$(pkg)_version)")),$(if $(strip $(filter-out $(drake_package) $(lib_package) $(sharedcc_package) $(sharedcxx_package),$1)),$(if $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --$2 $(foreach pkg,$(filter-out $(drake_package) $(lib_package) $(sharedcc_package) $(sharedcxx_package),$1),"$(pkg) $(call $3$(pkg)_version)") && echo Display text here so make considers the command successful even if does not produce any output),$(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --$2 $(foreach pkg,$(filter-out $(drake_package) $(lib_package) $(sharedcc_package) $(sharedcxx_package),$1),"$(pkg) $(call $3$(pkg)_version)")),$(error Missing library)))))
endef

## Gives all source files of projects $1 of type $2 (bin|lib|test|shared) with language $3 (cc|cxx)
define src
$(foreach type,cc cxx,$(foreach var,$1,$(call $2$(type)_$(var)_src)))
endef

## Gives source files of projects $1 of type $2 (bin|lib|test|shared|drake) with language $3 (cc|cxx), prefixed with their file variant
define src_file_var
$(foreach var,$1,$(foreach src,$(call $2$3_$(var)_src),$(call VARIANT_$(src))$(src)))
endef

## Gives source files of projects $1 of type $2 (bin|lib|test|shared) with language $3 (cc|cxx), prefixed with their project and file variant
define src_project_file_var
$(foreach var,$(call src_file_var,$1,$2,$3),$(call VARIANT_$1_$(call source,$(patsubst %.$(call typeof,$1),%.o,$(var)),$1,$2,$3))$(var))
endef

## Gives object files of projects in list $1, of type $2 (bin|lib|test|shared|drake) with language $3 (cc|cxx), prefixed with their project name, project type, language $4 (if any, otherwise $3) and source file variant
define object
$(foreach proj,$1,$(patsubst %.$(call ext,$3),$2$(if $4,$4,$3)_$(proj)_%.o,$(call src_project_file_var,$(proj),$2,$3)))
endef

## Same as above, for drake projects
define drake_object
$(foreach proj,$1,$(if $(call drake_graph,$(call longest,$(call suffixin,$(proj),$(drake_package))),$3),$(foreach task,$(shell drake --taskgraph $(call drake_graph,$(call longest,$(call suffixin,$(proj),$(drake_package))),$3) --tasks),$2$3_$(call longest,$(call suffixin,$(proj),$(drake_package)))_$(task).o))) $(foreach proj,$1,$(foreach src,$(call drake_schedule_src_file,$(call longest,$(call suffixin,$(proj),$(drake_package))),$3),$2$3_$(patsubst %.$(call ext,$3),%,$(src)).o))
endef

## Returns the graph file of the drake project $1 of type $2
define drake_graph
$(if $(call $(strip $1)$2_taskgraph),$(call $(strip $1)$2_taskgraph),$(call drake$2_$(strip $1)_graph))
endef

## Gives object files of projects in list $1, of type $2 (bin|lib|test) with language $3 (cc|cxx), prefixed with their project name, project variant and source file variant
define embed
$(foreach proj,$1,$(patsubst %,%.o,$(call $2_$(proj)_embed) $(if $(patsubst drake,$2,$2),$(if $(call $2cc_$(proj)_graph),$(call $2cc_$(proj)_graph),$(call $2cxx_$(proj)_graph)) $(call $2_$(proj)_platform))))
endef

## Gives the source files of the embedded object files in $1
define embed_src
$(foreach var,$1,$(patsubst %.o,%,$(var)))
endef

## Gives object files of projects $1 of type $2 (bin|lib|test) with language $3 (cc|cxx), prefixed with their project name, but no project or source file variant
define object_noprefix
$(foreach proj,$1,$(patsubst %.$(call ext,$3),$2_$(proj)_%.o,$(call src,$(proj),$2,$3)))
endef

## Gives the project of the project-prefixed object file $1 of type $2 (bin|test|shared) and language $3 (cc|cxx)
define projectof
$(call longest,$(foreach proj,$(call $2$(if $(filter bin,$2),$3,$(if $(filter test,$2),$3,$(if $(filter shared,$2),$3,)))_package),$(foreach lang,cc cxx,$(if $(filter $2$(lang)_$(proj)%,$1),$(proj),))))
endef

## Gives the non-variant project name of variant project $1 of type $2 (bin|test|shared) and language $3 (cc|cxx)
define projectof_no-obj
$(if $(filter $1,$(call $2$(if $(filter lib,$2),,$3)_package)),$1,$(call longest,$(call suffixin,$1,$(call $2$(if $(filter lib,$2),,$3)_package))))
endef

## Gives the type of project (bin|lib|test|drake|shared) the object file $1 is part of
define typeof
$(if $(filter bin%,$1),bin,$(if $(filter lib%,$1),lib,$(if $(filter test%,$1),test,$(if $(filter drake%,$1),drake,$(if $(filter shared%,$1),shared,)))))
endef

## Gives the project corresponding the the drake object file $1 in language $2
define drake_project
$(foreach proj,$(drake_package),$(if $(filter drake$2_$(proj)_%.o,$1),$(proj)))
endef

## Gives the taskid corresponding the the drake object file $1 in language $2 of project $3
define drake_task
$(if $(filter drake$2_$(strip $3)%.o,$1),$(patsubst drake$2_$(strip $3)_%.o,%,$1))
endef
#$(if $(filter drake$2_$3%.o,$1),$(patsubst drake$2_$(strip $3)_%.o,%,$1))
#$(if $(filter drake$2_$3%.o,$1),$(patsubst drake$2_$3_%.o,%,$1))

## Gives the source file of object $1 of project $2 of type $3 (bin|lib|test) in language $4 (cc/cxx)
define source
$(if $(filter drake%.o,$1),$(if $(filter %.$(drake_schedule).o,$1),$(patsubst $3$4_%.$(drake_schedule).o,%.$(drake_schedule).$(call ext,$4),$1),$(if $(shell drake --taskgraph  $(call drake_graph,$(sort $2),$4) --module $(call drake_task,$1,$4,$(sort $2)) 2>/dev/null),$(if $(call $3$4_$(sort $2)_$(shell drake --taskgraph  $(call drake_graph,$(sort $2),$4) --module $(call drake_task,$1,$4,$(sort $2)))),$(call $3$4_$(sort $2)_$(shell drake --taskgraph $(call drake_graph,$(sort $2),$4) --module $(call drake_task,$1,$4,$(sort $2)))),$(shell drake --taskgraph $(call drake_graph,$(sort $2),$4) --module $(call $3_task,$1,$4,$(sort $2))).$(call ext,$4)),$(call suffixin,$(patsubst %.o,%.$(call ext,$4),$1),$(call src,$(sort $2),$3,$4)))),$(call suffixin,$(patsubst %.o,%.$(call ext,$4),$1),$(call src,$(sort $2),$3,$4)))
endef

# Returns the package main language of object $1 of project type $2 (bin|lib|test)
define langof
$(foreach lg,cc cxx,$(filter $(lg),$(patsubst $2$(lg)_%,$(lg),$1)))
endef

## Gives all makefile-wise cflags
## Overridable global CFLAG variable
## Base CFLAGS from MAKEFILE
## pkg-config CFLAGS
## Global DEBUG/RELEASE CFLAGS
## No need of a function here

GLOBAL_CFLAGS_PRIVATE := $(CFLAGS_PRIVATE) $(CFLAGS) $(call pkg-config,$(CC_PKG),cflags,CC_PKG_) $(call CFLAGS_CONFIG_$(CONFIG))
GLOBAL_CFLAGS_EXPORT := $(CFLAGS_BASE)
GLOBAL_CFLAGS := $(GLOBAL_CFLAGS_PRIVATE) $(GLOBAL_CFLAGS_EXPORT)
GLOBAL_CXXFLAGS_PRIVATE := $(CXXFLAGS_PRIVATE) $(CXXFLAGS) $(call pkg-config,$(CXX_PKG),cflags,CXX_PKG_) $(call CXXFLAGS_CONFIG_$(CONFIG))
GLOBAL_CXXFLAGS := $(GLOBAL_CXXFLAGS_PRIVATE) $(GLOBAL_CFLAGS_EXPORT)

## Gives cflags for project $1 of type $2 (bin|lib|test) in language $3 (cc|cxx), that will not be exported in pkg-config
#$(shell echo $(call $2_$1_pkg) >&2)
#$(shell echo pkg-config,$(call $2_$1_pkg),cflags,$2_$1_pkg_ >&2)
#$(call pkg-config,$(call $2_$1_pkg),cflags,$2_$1_pkg_)
define cflags_project_private
$(call pkg-config,$(call $2_$(call projectof_no-obj,$1,$2,$3)_pkg),cflags,$2_$1_pkg_)
endef

## Gives cflags for project $1 of type $2 (bin|lib|test) in language $3 (cc|cxx), that will be exported in pkg-config
define cflags_project_export
$(call $2_$1_cflags) $(call $2_$1_cxxflags)
endef

## Gives cflags for project $1 of type $2 (bin|lib|test) in language $3 (cc|cxx)
define cflags_project
$(if $(filter shared,$2),$(if $(filter $3,cc),$(CFLAGS_SHARED),$(if $(filter $3,cxx),$(CXXFLAGS_SHARED)))) $(call cflags_project_private,$1,$2,$3) $(call cflags_project_export,$1,$2,$3)
endef

define prefix_flags
$(if $(filter cxx,$1),cxx,$(if $(filter cc,$1),c,))
endef

## Gives the private cflags of the project corresponding to object file $1
define cflags_private_object
$(foreach lang,cc cxx,$(if $(call projectof,$1,$(call typeof,$1),$(lang)),$(call $(call typeof,$1)_$(call projectof,$1,$(call typeof,$1),$(lang))_$(call prefix_flags,$(lang))flags_private),))$(if $(filter drake%,$1),$(if $(filter %.$(drake_schedule).o,$1),-DAPPLICATION=$(foreach lang,cc cxx,$(if $(filter drake$(lang)_%.$(drake_schedule).o,$1),$(call VARIANT_lib$(patsubst drake$(lang)_%.$(drake_schedule).o,%,$1).a)$(patsubst drake$(lang)_%.$(drake_schedule).o,%,$1))),$(if $(strip $(foreach lang,cc cxx,$(if $(call drake_project,$1,$(lang)),$(shell drake --taskgraph $(call drake_graph,$(call drake_project,$1,$(lang)),$(lang)) --module $(call drake_task,$1,$(lang),$(call drake_project,$1,$(lang))) 2>/dev/null )))),-D_DRAKE_COMPILE -DAPPLICATION=$(strip $(sort $(foreach lang,cc cxx,$(call VARIANT_lib$(call drake_project,$1,$(lang)).a)$(call drake_project,$1,$(lang))))) -DTASK_MODULE=$(strip $(foreach lang,cc cxx,$(if $(call drake_project,$1,$(lang)),$(shell drake --taskgraph $(call drake_graph,$(call drake_project,$1,$(lang)),$(lang)) --module $(call drake_task,$1,$(lang),$(call drake_project,$1,$(lang))) 2> /dev/null)))) -DTASK_NAME=$(strip $(foreach lang,cc cxx,$(call drake_task,$1,$(lang),$(call drake_project,$1,$(lang))))),)))
endef

GLOBAL_CC_LDFLAGS := $(CC_LDFLAGS_PRIVATE) $(CC_LDFLAGS) $(call pkg-config,$(CC_PKG),libs-only-L,CC_PKG_)
GLOBAL_CXX_LDFLAGS := $(CXX_LDFLAGS_PRIVATE) $(CXX_LDFLAGS) $(call pkg-config,$(CXX_PKG),libs-only-L,CXX_PKG_)
GLOBAL_CC_SHARED_LDFLAGS := $(CC_SHARED_LDFLAGS_PRIVATE) $(CC_SHARED_LDFLAGS) $(call pkg-config,$(CC_PKG),libs-only-L,CC_PKG_)
GLOBAL_CXX_SHARED_LDFLAGS := $(CXX_SHARED_LDFLAGS_PRIVATE) $(CXX_SHARED_LDFLAGS) $(call pkg-config,$(CXX_PKG),libs-only-L,CXX_PKG_)

define ldflags_project
$(call $2_$1_ldflags)
endef

## Gives all cflags the object file $1 is ought to receive for this project of $2 (bin|lib|test) in language "cc"
define cflags_file
$(call CFLAGS_$(call source,$1,$(call projectof,$1,$2,cc),$2,cc)) $(call $2_$(call projectof,$1,$2,cc)_cflags_$(call source,$1,$(call projectof,$1,$2,cc),$2,cc))
endef

## Same as above for C++ projects
define cxxflags_file
$(call CXXFLAGS_$(call source,$1,$(call projectof,$1,$2,cxx),$2,cxx)) $(call $2_$(call projectof,$1,$2,cxx)_cxxflags_$(call source,$1,$(call projectof,$1,$2,cxx),$2,cxx))
endef

## Returns the list $1 in reverse order
## http://stackoverflow.com/questions/52674/simplest-way-to-reverse-the-order-of-strings-in-a-make-variable
define reverse
$(if $(wordlist 2,2,$1),$(call reverse,$(wordlist 2,$(words $1),$1)) $(firstword $1),$1)
endef

## Gives all library switches brought by the makefile and its options
## Global pkg-config packages
## Global manual LIBS variable
GLOBAL_CC_LIBS := $(CC_LIBS)
GLOBAL_CXX_LIBS := $(CXX_LIBS)
GLOBAL_CC_TEST_LIBS := $(CC_TEST_LIBS)
GLOBAL_CXX_TEST_LIBS := $(CXX_TEST_LIBS)
GLOBAL_CC_PKG := $(call pkg-config,$(CC_PKG),libs-only-l,CC_PKG_)
GLOBAL_CXX_PKG := $(call pkg-config,$(CXX_PKG),libs-only-l,CXX_PKG_)
GLOBAL_CC_TEST_PKG := $(call pkg-config,$(CC_TEST_PKG),libs-only-l,CC_TEST_PKG_)
GLOBAL_CXX_TEST_PKG := $(call pkg-config,$(CXX_TEST_PKG),libs-only-l,CXX_TEST_PKG_)

## Gives all library switches of project $1 of type $2 (bin|lib|test) in language $3
## pkg-config provided switches from _pkg variable
## manual library switches from _libs variable
define libs_project
$($2_$(call suffixin,$1,$($2$3_package))_libs)
endef

## Returns all libraries of packages upon which project $1 (type $2 and language $3) depends on.
define pkg_project
$(foreach var,$1,$(call pkg-config,$(call $2_$(var)_pkg),libs $(if $(strip $(call $2_$(var)_static)),$(if $(filter 0,$(strip $(call $2_$(var)_static))),,--static),),$2_$(var)_pkg_))
endef

## A source filename for drake application $1 in language $2
define drake_schedule_src_file
$(if $(strip $(call drake$2_$1_graph)),$1.$(drake_schedule).$(if $(filter cc,$2),c,$(if $(filter cxx,$2),cxx)))
endef

## All C and C++ objects, eliminating duplicates
drake_schedule_src := $(foreach proj,$(drake_package),$(foreach lang,cc cxx,$(call drake_schedule_src_file,$(proj),$(lang))))
drake_taskgraph_text := $(foreach proj,$(drake_package),$(proj).$(drake_taskgraph).$(drake_taskgraph_extension))
drake_schedule_text := $(foreach proj,$(drake_package),$(proj).$(drake_schedule).$(drake_schedule_extension))
cc_object := $(sort $(call object,$(bincc_package),bin,cc) $(call object,$(bincxx_package),bin,cc,cxx) $(call object,$(lib_package),lib,cc) $(call object,$(testcc_package),test,cc) $(call object,$(testcc_package),test,cc,cxx) $(call object,$(sharedcc_package),shared,cc) $(call object,$(sharedcxx_package),shared,cc,cxx)) $(call drake_object,$(drake_package),drake,cc) $(call object,$(drake_package),drake,cc)
cxx_object := $(sort $(call object,$(bincxx_package),bin,cxx) $(call object,$(bincx_package),bin,cxx,cx) $(call object,$(lib_package),lib,cxx) $(call object,$(testcxx_package),test,cxx) $(call object,$(testcc_package),test,cxx,cc) $(call object,$(drake_package),drake,cxx) $(call drake_object,$(drake_package),drake,cxx) $(call object,$(sharedcxx_package),shared,cxx) $(call object,$(sharedcc_package),shared,cxx,ccx))
#lib_pkgconfig := $(sort $(foreach var,$(lib_package) $(drake_package),$(patsubst %,%.pc,$(call VARIANT_lib$(var).a)$(var))) $(foreach var,$(sharedcxx_package) $(sharedcc_package),$(patsubst %,%.pc,$(var))))
lib_pkgconfig := $(sort $(foreach var,$(lib_package) $(drake_package),$(patsubst %,%.pc,$(call VARIANT_lib$(var).a)$(var))) $(foreach var,$(sharedcxx_package) $(sharedcc_package),$(patsubst %,%.pc,$(call VARIANT_lib$(var).so)$(var))))
embed_object := $(sort $(call embed,$(bincc_package),bin) $(call embed,$(testcc_package),test) $(call embed,$(bincxx_package),bin) $(call embed,$(testcxx_package),test) $(call embed,$(lib_package),lib) $(call embed,$(drake_package),drake) $(call embed,$(sharedcc_package),shared) $(call embed,$(sharedcxx_package),shared))

-include $(foreach file,$(cc_object) $(cxx_object),$(patsubst %.o,%.d,$(file)))

## Build the list of targets this makefile can build
## The list of all c and c++ packages, prefixed all these packages with a specific variant
## variable that may or may not have been defined
bincc_list := $(foreach var,$(bincc_package),$(call VARIANT_$(var))$(var))
bincxx_list := $(foreach var,$(bincxx_package),$(call VARIANT_$(var))$(var))
bin_list := $(bincc_list) $(bincxx_list)

lib_list := $(foreach var,$(lib_package),lib$(call VARIANT_lib$(var).a)$(var).a) $(foreach var,$(drake_package),lib$(call VARIANT_lib$(var).a)$(var).a)
sharedcc_list := $(foreach lgl,cc,$(foreach var,$(call shared$(lgl)_package),lib$(call VARIANT_lib$(var).so)$(var).so.$(version).$(minor).$(release)))
sharedcxx_list := $(foreach lgl,cxx,$(foreach var,$(call shared$(lgl)_package),lib$(call VARIANT_lib$(var).so)$(var).so.$(version).$(minor).$(release)))
shared_list := $(sharedcc_list) $(sharedcxx_list) #$(shell echo $(sharedcc_list) $(sharedcxx_list) >&2)
shared_linkername := $(foreach shared,$(shared_list),$(patsubst %.$(version).$(minor).$(release),%,$(shared)))
shared_soname := $(foreach shared,$(shared_list),$(patsubst %.$(version).$(minor).$(release),%.$(version),$(shared)))

testcc_target := $(testcc_package:%=test_%)
testcxx_target := $(testcxx_package:%=test_%)
test_target := $(testcc_package:%=test_%) $(testcxx_package:%=test_%)
testcc_list := $(foreach var,$(testcc_package),test_$(call VARIANT_test_$(var))$(var))
testcxx_list := $(foreach var,$(testcxx_package),test_$(call VARIANT_test_$(var))$(var))
test_list := $(testcc_list) $(testcxx_list)

## Disable implicit rules
.SUFFIXES:

all: submake-all $(bin_list) $(lib_pkgconfig) $(lib_list)

## Force rebuild upon Makefile update
(cc_object) $(cxx_object) $(embed_object) $(bin_list) $(lib_list) $(shared_list) $(shared_linkername) $(shared_soname) $(lib_pkgconfig) $(drake_schedule_src) $(drake_schedule_text) $(drake_taskgraph_text)): $(REBUILD_ON_UPDATE)

$(drake_taskgraph_text): $$(if $$(call $$(patsubst %.$(drake_schedule).$(drake_schedule_extension),%,$$@)_platform),$$(if $$($$(call $$(patsubst %.$(drake_schedule).$(drake_schedule_extension),%,$$@)_schedule)),,FORCE),$$(call drake_$$(patsubst %.$(drake_schedule).$(drake_schedule_extension),%,$$@)_platform)) $$(firstword $$(foreach type,cc cxx,$$(if $$(call $$(patsubst %$$(type).$(drake_schedule).$(drake_schedule_extension),%,$$@)_taskgraph),$$(if $$($$(call $$(patsubst %.$(drake_schedule).$(drake_schedule_extension),%,$$@)_schedule)),,FORCE),$$(call drake_$$(patsubst %$$(type).$(drake_schedule).$(drake_schedule_extension),%,$$@)_taskgraph))))
$(drake_schedule_text): $$(if $$(call $$(patsubst %.$(drake_schedule).$(drake_schedule_extension),%,$$@)_platform),$$(if $$($$(call $$(patsubst %.$(drake_schedule).$(drake_schedule_extension),%,$$@)_schedule)),,FORCE),$$(call drake_$$(patsubst %.$(drake_schedule).$(drake_schedule_extension),%,$$@)_platform)) $$(firstword $$(foreach type,cc cxx,$$(if $$(call $$(patsubst %$$(type).$(drake_schedule).$(drake_schedule_extension),%,$$@)_taskgraph),$$(if $$($$(call $$(patsubst %.$(drake_schedule).$(drake_schedule_extension),%,$$@)_schedule)),,FORCE),$$(call drake_$$(patsubst %$$(type).$(drake_schedule).$(drake_schedule_extension),%,$$@)_taskgraph))))
	pelib-convert --input --lib taskgraph-graphml --file $(sort $(foreach type,cc cxx,$(call drake_graph,$(patsubst %.$(drake_schedule).$(drake_schedule_extension),%,$@),$(type)))) --input --lib platform-ampl_input --file $(if $(call $(patsubst %.$(drake_schedule).$(drake_schedule_extension),%,$@)_platform),$(call $(patsubst %.$(drake_schedule).$(drake_schedule_extension),%,$@)_platform),$(call drake_$(patsubst %.$(drake_schedule).$(drake_schedule_extension),%,$@)_platform)) --process --lib schedule --args --lib $(if $(call $(patsubst %.$(drake_schedule).$(drake_schedule_extension),%,$@)_scheduler),,$(call drake_$(patsubst %.$(drake_schedule).$(drake_schedule_extension),%,$@)_scheduler)) -- --output --lib schedule-xml --file $@ --output --lib taskgraph-graphml --file $(patsubst %.$(drake_schedule).$(drake_schedule_extension),%.$(drake_taskgraph).$(drake_taskgraph_extension),$@)

## Generate drake schedules
$(drake_schedule_src): $$(if $$(call $$(patsubst %.$(drake_schedule).$$(if $$(filter %.c,$$@),c,$$(if $$(filter %.cpp,$$@),cpp)),%,$$@)_platform),,$$(call drake_$$(patsubst %.$(drake_schedule).$$(if $$(filter %.c,$$@),c,$$(if $$(filter %.cpp,$$@),cpp)),%,$$@)_platform)) $$(if $$(call $$(patsubst %.$(drake_schedule).$$(if $$(filter %.c,$$@),c,$$(if $$(filter %.cpp,$$@),cpp)),%,$$@)_schedule),,$$(patsubst %.$(drake_schedule).$$(if $$(filter %.c,$$@),c,$$(if $$(filter %.cpp,$$@),cpp)),%,$$@).$(drake_schedule).$(drake_schedule_extension)) $$(if $$(call $$(patsubst %.$(drake_schedule).$$(if $$(filter %.c,$$@),c,$$(if $$(filter %.cpp,$$@),cpp)),%,$$@)$$(if $$(filter %.c,$$@),cc,$$(if $$(filter %.cpp,$$@),cxx))_taskgraph),,$$(patsubst %.$(drake_schedule).$$(if $$(filter %.c,$$@),c,$$(if $$(filter %.cpp,$$@),cxx)),%,$$@).$(drake_taskgraph).$(drake_taskgraph_extension)) $$(if $$(call $$(patsubst %.$(drake_schedule).$$(if $$(filter %.c,$$@),c,$$(if $$(filter %.cpp,$$@),cpp)),%,$$@)_platform),FORCE) $$(if $$(call $$(patsubst %.$(drake_schedule).$$(if $$(filter %.c,$$@),c,$$(if $$(filter %.cpp,$$@),cpp)),%,$$@)$$(if $$(filter %.c,$$@),cc,$$(if $$(filter %.cpp,$$@),cxx))_taskgraph),FORCE) $$(if $$(call $$(patsubst %.$(drake_schedule).$$(if $$(filter %.c,$$@),c,$$(if $$(filter %.cpp,$$@),cpp)),%,$$@)_schedule),FORCE)
	pelib-convert --input --lib platform-ampl_input --file $(if $(call $(patsubst %.$(drake_schedule).$(if $(filter %.c,$@),c,$(if $(filter %.cpp,$@),cpp)),%,$@)_platform),$(call $(patsubst %.$(drake_schedule).$(if $(filter %.c,$@),c,$(if $(filter %.cpp,$@),cpp)),%,$@)_platform),$(call drake_$(patsubst %.$(drake_schedule).$(if $(filter %.c,$@),c,$(if $(filter %.cpp,$@),cpp)),%,$@)_platform)) --input --lib taskgraph-graphml --file $(call drake_graph,$(patsubst %.$(drake_schedule).$(if $(filter %.c,$@),c,$(if $(filter %.cpp,$@),cpp)),%,$@),$(if $(filter %.c,$@),cc,$(if $(filter %.cpp,$@),cxx))) --input --lib schedule-xml --file $(if $(call $(patsubst %.$(drake_schedule).$(if $(filter %.c,$@),c,$(if $(filter %.cpp,$@),cpp)),%,$@)_schedule),$(call $(patsubst %.$(drake_schedule).$(if $(filter %.c,$@),c,$(if $(filter %.cpp,$@),cpp)),%,$@)_schedule),$(patsubst %.$(drake_schedule).$(if $(filter %.c,$@),c,$(if $(filter %.cpp,$@),cpp)),%,$@).$(drake_schedule).$(drake_schedule_extension)) --output --lib schedule-drake --file $@

$(embed_object): $$(call embed_src,$$@)
	$(LD) -r -b binary -o $@ $(patsubst %.o,%,$@)

$(cc_object): $$(call source,$$@,$$(sort $$(foreach lang,cc cxx,$$(call projectof,$$@,$$(call typeof,$$@),$$(lang)))),$$(call typeof,$$@),cc)
	$(CC) -E $(GLOBAL_CFLAGS) $(call cflags_project,$(call projectof,$@,$(call typeof,$@),$(strip $(foreach var,cc cxx,$(if $(filter $(call typeof,$@)$(var)_%,$@),$(var))))),$(call typeof,$@),cc) $(call cflags_private_object,$@) $(call cflags_file,$@,$(call typeof,$@)) $(CPPFLAGS) -o $(patsubst %.o,%.e,$@) $<
	$(CC) -c $(GLOBAL_CFLAGS) $(call cflags_project,$(call projectof,$@,$(call typeof,$@),$(strip $(foreach var,cc cxx,$(if $(filter $(call typeof,$@)$(var)_%,$@),$(var))))),$(call typeof,$@),cc) $(call cflags_private_object,$@) $(call cflags_file,$@,$(call typeof,$@)) $(CPPFLAGS) -o $@ $<
	$(CC) -M $(GLOBAL_CFLAGS) $(call cflags_project,$(call projectof,$@,$(call typeof,$@),$(strip $(foreach var,cc cxx,$(if $(filter $(call typeof,$@)$(var)_%,$@),$(var))))),$(call typeof,$@),cc) $(call cflags_private_object,$@) $(call cflags_file,$@,$(call typeof,$@)) $(CPPFLAGS) $< -MF $(patsubst %.o,%.d,$@)
	@# Generate dependencies to header files
	@# More tricks to manage header renaming stored in different folders (http://scottmcpeak.com/autodepend/autodepend.html)
	@mv -f $(patsubst %.o,%.d,$@) $(patsubst %o,%d,$@).tmp
	@sed -e 's|.*:|$@:|' < $(patsubst %.o,%.d,$@).tmp > $(patsubst %.o,%.d,$@)
	@sed -e 's/.*://' -e 's/\\$$//' < $(patsubst %.o,%.d,$@).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(patsubst %.o,%.d,$@)
	@rm -f $(patsubst %.o,%.d,$@).tmp

$(cxx_object): $$(call source,$$@,$$(sort $$(foreach lang,cc cxx,$$(call projectof,$$@,$$(call typeof,$$@),$$(lang)))) ,$$(call typeof,$$@),cxx)
	$(CXX) -E $(GLOBAL_CXXFLAGS) $(call cflags_project,$(call projectof,$@,$(call typeof,$@),$(strip $(foreach var,cc cxx,$(if $(filter $(call typeof,$@)$(var)_%,$@),$(var))))),$(call typeof,$@),cxx) $(call cflags_private_object,$@) $(call cxxflags_file,$@,$(call typeof,$@)) $(CPPFLAGS) -o $(patsubst %.o,%.e,$@) $<
	$(CXX) -c $(GLOBAL_CXXFLAGS) $(call cflags_project,$(call projectof,$@,$(call typeof,$@),$(strip $(foreach var,cc cxx,$(if $(filter $(call typeof,$@)$(var)_%,$@),$(var))))),$(call typeof,$@),cxx) $(call cflags_private_object,$@) $(call cxxflags_file,$@,$(call typeof,$@)) $(CPPFLAGS) -o $@ $<
	$(CXX) -M $(GLOBAL_CXXFLAGS) $(call cflags_project,$(call projectof,$@,$(call typeof,$@),$(strip $(foreach var,cc cxx,$(if $(filter $(call typeof,$@)$(var)_%,$@),$(var))))),$(call typeof,$@),cxx) $(call cflags_private_object,$@) $(call cxxflags_file,$@,$(call typeof,$@)) $(CPPFLAGS) $< -MF $(patsubst %.o,%.d,$@)
	@# Generate dependencies to header files
	@# More tricks to manage header renaming stored in different folders (http://scottmcpeak.com/autodepend/autodepend.html)
	@mv -f $(patsubst %.o,%.d,$@) $(patsubst %o,%d,$@).tmp
	@sed -e 's|.*:|$@:|' < $(patsubst %.o,%.d,$@).tmp > $(patsubst %.o,%.d,$@)
	@sed -e 's/.*://' -e 's/\\$$//' < $(patsubst %.o,%.d,$@).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(patsubst %.o,%.d,$@)
	@rm -f $(patsubst %.o,%.d,$@).tmp

$(bincc_list): $$(call object,$$(call suffixin,$$@,$$(bincc_package)),bin,cc) $$(call object,$$(call suffixin,$$@,$$(bincc_package)),bin,cxx,cc) $$(call $$(patsubst %,bin_%_deps,$$(call suffixin,$$@,$$(bincc_package)))) $$(call embed,$$(call suffixin,$$@,$$(bincc_package)),bin)
	$(CC) -o $@ $(if $(strip $(call bin_$(call projectof_no-obj,$@,bin,cc)_static)),$(if $(filter 0,$(strip $(call bin_$(call projectof_no-obj,$@,bin,cc)_static))),,--static),) $(GLOBAL_CC_LDFLAGS) $(call ldflags_project,$(call suffixin,$@,$(bincc_package),bin,cc),bin,cc) $(LDFLAGS) $(call object,$(call suffixin,$@,$(bincc_package)),bin,cc) $(call object,$(call suffixin,$@,$(bincc_package)),bin,cxx,cc) $(call embed,$(call suffixin,$@,$(bincc_package)),bin) $(call libs_project,$@,bin,cc) $(call pkg_project,$(call projectof_no-obj,$@,bin,cc),bin,cc) $(GLOBAL_CC_PKG) $(GLOBAL_CC_LIBS) $(LIBS)

$(bincxx_list): $$(call object,$$(call suffixin,$$@,$$(bincxx_package)),bin,cxx) $$(call object,$$(call suffixin,$$@,$$(bincxx_package)),bin,cc,cxx) $$(call $$(patsubst %,bin_%_deps,$$(call suffixin,$$@,$$(bincxx_package)))) $$(call embed,$$(call suffixin,$$@,$$(bincxx_package)),bin)
	$(CXX) -o $@ $(if $(strip $(call bin_$(call projectof_no-obj,$@,bin,cxx)_static)),$(if $(filter 0,$(strip $(call bin_$(call projectof_no-obj,$@,bin,cxx)_static))),,--static),) $(GLOBAL_CXX_LDFLAGS) $(call ldflags_project,$(call suffixin,$@,$(bincxx_package),bin,cxx),bin,cxx) $(LDFLAGS) $(call object,$(call suffixin,$@,$(bincxx_package)),bin,cxx) $(call object,$(call suffixin,$@,$(bincxx_package)),bin,cc,cxx) $(call embed,$(call suffixin,$@,$(bincxx_package)),bin) $(call libs_project,$@,bin,cxx) $(call pkg_project,$(call projectof_no-obj,$@,bin,cxx),bin,cxx) $(GLOBAL_CXX_PKG) $(GLOBAL_CXX_LIBS) $(LIBS)

$(lib_list): $$(foreach lang,cc cxx,$$(foreach type,lib drake,$$(call object,$$(patsubst lib%.a,%,$$(call suffixin,$$(patsubst lib%.a,%,$$@),$$(call $$(type)_package))),lib,$$(lang)) $$(call object,$$(patsubst lib%.a,%,$$(call suffixin,$$(patsubst lib%.a,%,$$@),$$(call $$(type)_package))),drake,$$(lang)) $$(call drake_object,$$(patsubst lib%.a,%,$$@),drake,$$(lang)) $$(call $$(patsubst %,$$(type)$$(lang)_%_deps,$$(call suffixin,$$(patsubst lib%.a,%,$$@),$$($$(type)$_package)))) $$(call embed,$$(call longest,$$(call suffixin,$$(patsubst lib%.a,%,$$@),$$(call $$(type)_package))),$$(type),$$(lang))))
	ar rs $@ $(foreach lang,cc cxx,$(foreach type,lib drake,$(call object,$(patsubst lib%.a,%,$(call suffixin,$(patsubst lib%.a,%,$@),$(call $(type)_package))),lib,$(lang)) $(call object,$(patsubst lib%.a,%,$(call suffixin,$(patsubst lib%.a,%,$@),$(call $(type)_package))),drake,$(lang)) $(call drake_object,$(patsubst lib%.a,%,$@),drake,$(lang))) $(call embed,$(call longest,$(call suffixin,$(patsubst lib%.a,%,$@),$(call $(type)_package))),$(type),))

$(testcc_list): $$(call object,$$(patsubst test_%,%,$$@),test,cc) $$(call object,$$(patsubst test_%,%,$$@),test,cxx,cc) $$(call $$(patsubst %,test_%_deps,$$(call suffixin,$$(patsubst lib%.a,%,$$@),$$(testcc_package)))) $$(call embed,$$(call suffixin,$$@,$$(testcc_package)),test)
	$(CC) -o $@ $(if $(strip $(call testcc_$(call suffixin,$(call projectof_no-obj,$@,test,cc),$(testcc_package))_static)),$(if $(filter 0,$(strip $(call testcc_$(call suffixin,$(call projectof_no-obj,$@,test,cc),$(testcc_package))_static))),,--static),) $(GLOBAL_CC_TEST_LDFLAGS) $(call ldflags_project,$(call suffixin,$@,$(testcc_package),test,cc),test,cc) $(GLOBAL_CC_TEST_LDFLAGS) $(LDFLAGS) $(call object,$(call suffixin,$@,$(testcc_package)),test,cc) $(call object,$(call suffixin,$@,$(testcc_package)),test,cxx,cc) $(call embed,$(call suffixin,$@,$(testcc_package)),test) $(call libs_project,$(call suffixin,$@,$(testcc_package)),test,cc) $(call pkg_project,$(call suffixin,$(call projectof_no-obj,$@,test,cc),$(testcc_package)),test,cc) $(GLOBAL_CC_TEST_PKG) $(GLOBAL_CC_TEST_LIBS) $(LIBS)

$(testcxx_list): $$(call object,$$(patsubst test_%,%,$$@),test,cxx) $$(call object,$$(patsubst test_%,%,$$@),test,cc,cxx) $$(call $$(patsubst %,test_%_deps,$$(call suffixin,$$(patsubst lib%.a,%,$$@),$$(testcxx_package)))) $$(call embed,$$(call suffixin,$$@,$$(testcxx_package)),test)
	$(CXX) -o $@ $(if $(strip $(call testcxx_$(call suffixin,$(call projectof_no-obj,$@,test,cxx),$(testcxx_package))_static)),$(if $(filter 0,$(strip $(call testcxx_$(call suffixin,$(call projectof_no-obj,$@,test,cxx),$(testcxx_package))_static))),,--static),) $(GLOBAL_CXX_TEST_LDFLAGS) $(call ldflags_project,$(call suffixin,$@,$(testcxx_package),test,cxx),test,cxx) $(LDFLAGS) $(call object,$(call suffixin,$@,$(testcxx_package)),test,cxx) $(call object,$(call suffixin,$@,$(testcxx_package)),test,cc,cxx) $(call embed,$(call suffixin,$@,$(testcxx_package)),test) $(call libs_project,$(call suffixin,$@,$(testcxx_package)),test,cxx) $(call pkg_project,$(call suffixin,$(call projectof_no-obj,$@,test,cxx),$(testcxx_package)),test,cxx) $(GLOBAL_CXX_TEST_PKG) $(GLOBAL_CXX_TEST_LIBS) $(LIBS)

$(sharedcc_list): $$(call object,$$(patsubst lib%.so.$$(version).$$(minor).$$(release),%,$$(call suffixin,$$(patsubst %.so.$$(version).$$(minor).$$(release),%,$$@),$$(sharedcc_package))),shared,cc) $$(call object,$$(patsubst lib%.so.$$(version).$$(minor).$$(release),%,$$(call suffixin,$$(patsubst %.so.$$(version).$$(minor).$$(release),%,$$@),$$(sharedcc_package))),shared,cxx,cc) $$(call $$(patsubst %,shared_%_deps,$$(call suffixin,$$(patsubst lib%.so.$$(version).$$(minor).$$(release),%,$$@),$$(sharedcc_package)))) $$(call embed,$$(call suffixin,$$(patsubst %.so.$$(version).$$(minor).$$(release),%,$$@),$$(sharedcc_package)),shared)
	$(CC) -shared -Wl,-soname=$(patsubst %.$(minor).$(release),%,$@) -o $@ $(if $(strip $(call sharedcc_$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),$(sharedcc_package))static)),$(if $(filter 0,$(strip $(call sharedcc_$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),$(sharedcc_package))static))),,--static),) $(GLOBAL_CC_SHARED_LDFLAGS) $(call ldflags_project,$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),$(sharedcc_package),shared,cc),shared,cc) $(GLOBAL_CC_SHARED_LDFLAGS) $(LDFLAGS) $(sort $(call object,$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),$(sharedcc_package)),shared,cc) $(call object,$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),$(sharedcc_package)),shared,cxx,cc) $(call embed,$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),$(sharedcc_package)),shared)) $(call libs_project,$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),$(sharedcc_package)),shared,cc) $(call pkg_project,$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$(call projectof_no-obj,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),shared,cc)),$(sharedcc_package)),shared,cc) $(GLOBAL_CC_SHARED_PKG) $(GLOBAL_CC_SHARED_LIBS) $(LIBS)

$(sharedcxx_list): $$(call object,$$(patsubst lib%.so.$$(version).$$(minor).$$(release),%,$$(call suffixin,$$(patsubst %.so.$$(version).$$(minor).$$(release),%,$$@),$$(sharedcxx_package))),shared,cxx) $$(call object,$$(patsubst lib%.so.$$(version).$$(minor).$$(release),%,$$(call suffixin,$$(patsubst %.so.$$(version).$$(minor).$$(release),%,$$@),$$(sharedcxx_package))),shared,cc,cxx) $$(call $$(patsubst %,shared_%_deps,$$(call suffixin,$$(patsubst lib%.so.$$(version).$$(minor).$$(release),%,$$@),$$(sharedcxx_package)))) $$(call embed,$$(call suffixin,$$(patsubst %.so.$$(version).$$(minor).$$(release),%,$$@),$$(sharedcxx_package)),shared)
	$(CXX) -shared -Wl,-soname=$(patsubst %.$(minor).$(release),%,$@) -o $@ $(if $(strip $(call shared_$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),$(sharedcxx_package))_static)),$(if $(filter 0,$(strip $(call shared_$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),$(sharedcxx_package))_static))),,--static),) $(GLOBAL_CXX_SHARED_LDFLAGS) $(call ldflags_project,$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),$(sharedcxx_package),shared,cxx),shared,cxx) $(LDFLAGS) $(sort $(call object,$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),$(sharedcxx_package)),shared,cxx) $(call object,$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),$(sharedcxx_package)),shared,cc,cxx) $(call embed,$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),$(sharedcxx_package)),shared)) $(call libs_project,$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),$(sharedcxx_package)),shared,cxx) $(call pkg_project,$(call suffixin,$(patsubst lib%.so.$(version).$(minor).$(release),%,$(call projectof_no-obj,$(patsubst lib%.so.$(version).$(minor).$(release),%,$@),shared,cxx)),$(sharedcxx_package)),shared,cxx) $(GLOBAL_CXX_SHARED_PKG) $(GLOBAL_CXX_SHARED_LIBS) $(LIBS)

$(shared_linkername): $$@.$(version)
	ln -sf $(patsubst %,%.$(version),$@) $@

$(shared_soname): $$@.$(minor).$(release)
	ln -sf $(patsubst %,%.$(minor).$(release),$@) $@

$(lib_pkgconfig): $$(if $$(filter $$(patsubst %.pc,%,$$(call suffixin,$$(patsubst %.pc,%,$$@),$(call lib_package))),$$(lib_package)),$$(patsubst %,lib%.a,$$(patsubst %.pc,%,$$@))) $$(if $$(strip $$(if $(sharedcxx_package),$$(filter $$(patsubst %.pc,%,$$(call suffixin,$$(patsubst %.pc,%,$$@),$(call sharedcxx_package))),$(sharedcxx_package))) $$(if $(sharedcc_package),$$(filter $$(patsubst %.pc,%,$$(call suffixin,$$(patsubst %.pc,%,$$@),$(call sharedcc_package))),$(sharedcc_package)))),$$(patsubst %.pc,lib%.so,$$@)) $$(if $$(filter $$(patsubst %.pc,%,$$(call suffixin,$$(patsubst %.pc,%,$$@),$(call lib_package))),$(drake_package)),$$(patsubst %,lib%.a,$$(patsubst %.pc,%,$$(call suffixin,$$(patsubst %.pc,%,$$@),$(call lib_package)))))
	echo $?
	echo prefix=$(final_prefix) > $@
	echo exec_prefix=$(if $(foreach var,lib shared drake,$(call $(var)_$(patsubst %.pc,%,$@)_pkg_exec_prefix)),$(sort $(foreach var,lib shared drake,$(call $(var)_$(patsubst %.pc,%,$@)_pkg_exec_prefix))),'$$'\{prefix\}/bin) >> $@
	echo libdir=$(if $(sort $(foreach var,lib shared drake,$(call $(var)_$(patsubst %.pc,%,$@)_pkg_libdir))),$(sort $(foreach var,lib shared drake,$(call $(var)_$(patsubst %.pc,%,$@)_pkg_libdir))),'$$'\{prefix\}/lib) >> $@
	echo includedir=$(if $(sort $(foreach var,lib shared drake,$(call $(var)_$(patsubst %.pc,%,$@)_pkg_includedir))),$(sort $(foreach var,lib shared drake,$(call $(var)_$(patsubst %.pc,%,$@)_pkg_includedir))),'$$'\{prefix\}/include) >> $@
	echo >> $@
	echo Name: $(if $(sort $(foreach var,lib shared drake,$(call $(var)_$(patsubst %.pc,%,$@)_pkg_name))),$(sort $(foreach var,lib shared drake,$(call $(var)_$(patsubst %.pc,%,$@)_pkg_name))),$(patsubst %.pc,lib%,$@)) >> $@
	echo Description: $(sort $(foreach var,lib shared drake,$(call $(var)_$(call longest,$(call suffixin,$(patsubst %.pc,%,$@),$(call $(var)_package)))_pkg_description))) >> $@
	echo Version: $(version).$(minor).$(release) >> $@
	echo URL: $(sort $(foreach var,lib shared drake,$(call $(var)_$(patsubst %.pc,%,$@)_pkg_url))) >> $@
	echo Libs: -L'$$'\{libdir\} -l$(patsubst %.pc,%,$@) $(call drake_$(patsubst %.pc,%,$@)_ldflags) $(if $(filter $(patsubst %.pc,%,$@),$(drake_package)),$(GLOBAL_LIBS)) $(sort $(foreach var,lib shared,$(call $(var)_$(call longest,$(call suffixin,$(patsubst %.pc,%,$@),$(call $(var)_package)))_pkg_libs))) $(call drake_$(patsubst %.pc,%,$@)_libs) $(call drake_$(patsubst %.pc,%,$@)_pkg_libs_private) >> $@
	echo Libs.private: $(sort $(CC_LDFLAGS) $(CXX_LDFLAGS) $(foreach var,lib shared drake,$(call $(var)_$(call longest,$(call suffixin,$(patsubst %.pc,%,$@),$(call $(var)_package)))_ldflags)) $(GLOBAL_LIBS) $(foreach var,lib shared drake,$(call $(var)_$(call longest,$(call suffixin,$(patsubst %.pc,%,$@),$(call $(var)_package)))_libs)) $(foreach var,lib shared drake,$(call $(var)_$(call longest,$(call suffixin,$(patsubst %.pc,%,$@),$(call $(var)_package)))_pkg_libs_private))) >> $@
	echo Requires: $(shell echo "$(foreach var,$(sort $(foreach var,lib shared drake,$(foreach var2,$(call $(var)_$(call longest,$(call suffixin,$(patsubst %.pc,%,$@),$(call $(var)_package)))_pkg_requires),$(var2)))),\"$(strip $(var)) $(strip $(call reverse,$(sort $(foreach var2,lib shared drake,$(call $(var2)_$(call longest,$(call suffixin,$(patsubst %.pc,%,$@),$(call $(var2)_package)))_pkg_$(var)_version)))))\",)" | rev | cut -f 1 -d ',' --complement | rev) $(shell echo "$(foreach var,$(sort $(foreach var,drake,$(foreach var2,$(call $(var)_$(call longest,$(call suffixin,$(patsubst %.pc,%,$@),$(call $(var)_package)))_pkg),$(var2)))),\"$(strip $(var)) $(strip $(call reverse,$(sort $(foreach var2,drake,$(call $(var2)_$(call longest,$(call suffixin,$(patsubst %.pc,%,$@),$(call $(var2)_package)))_pkg_$(var)_version)))))\",)" | rev | cut -f 1 -d ',' --complement | rev) >> $@
	echo Requires.private: $(sort $(CC_PKG) $(CXX_PKG)) $(shell echo "$(foreach var,$(sort $(foreach var,lib shared drake,$(foreach var2,$(call $(var)_$(call longest,$(call suffixin,$(patsubst %.pc,%,$@),$(call $(var)_package)))_pkg),$(var2)))),\"$(strip $(var)) $(strip $(call reverse,$(sort $(foreach var2,lib shared drake,$(call $(var2)_$(call longest,$(call suffixin,$(patsubst %.pc,%,$@),$(call $(var2)_package)))_pkg_$(var)_version)))))\",)" | rev | cut -f 1 -d ',' --complement | rev) >> $@
	echo Conflicts: $(sort $(foreach var,lib shared drake,$(call $(var)_$(patsubst %.pc,%,$@)_pkg_conflicts))) >> $@
	echo Cflags: $(GLOBAL_CFLAGS_EXPORT) $() -I'$$'\{includedir\} $(foreach var,cc cxx,$(call cflags_project_export,$(patsubst %.pc,%,$@),lib,$(var))) $(CPPFLAGS) $(foreach var,cc cxx,$(sort $(foreach var,lib shared drake,$(call $(var2)_$(patsubst %.pc,%,$@)_pkg_$(call prefix_cflags,$(var))flags)))) >> $@

dist: pre-dist submake-dist do-dist post-dist

do-dist:
	mkdir -p $(distdir)
	cp Makefile $(distdir)
	cp Makefile.in $(distdir)
	$(if $(strip $(attach)),cp $(attach) $(distdir))
	$(if $(strip $(bincc_package:%=bin_%_export)),cp $(sort $(foreach var,$(bincc_package:%=bin_%_export),$(call $(var)))) $(distdir),)
	$(if $(strip $(bincxx_package:%=bin_%_export)),cp $(sort $(foreach var,$(bincxx_package:%=bin_%_export),$(call $(var)))) $(distdir),)
	$(if $(strip $(lib_package:%=lib_%_export)),cp $(sort $(foreach var,$(lib_package:%=lib_%_export),$(call $(var)))) $(distdir),)
	$(if $(strip $(testcc_package:%=test_%_export)),cp $(sort $(foreach var,$(testcc_package:%=test_%_export),$(call $(var)))) $(distdir),)
	$(if $(strip $(testcxx_package:%=test_%_export)),cp $(sort $(foreach var,$(testcxx_package:%=test_%_export),$(call $(var)))) $(distdir),)
	$(if $(strip $(sharedcc_package:%=shared_%_export)),cp $(sort $(foreach var,$(sharedcc_package:%=shared_%_export),$(call $(var)))) $(distdir),)
	$(if $(strip $(sharedcxx_package:%=shared_%_export)),cp $(sort $(foreach var,$(sharedcxx_package:%=shared_%_export),$(call $(var)))) $(distdir),)
	$(foreach proj,$(drake_package),cp $(sort $(foreach var,$(drake_package:%=drake_%_export),$(call $(var)))) $(sort $(foreach lang,cc cxx,$(if $(call drake$(lang)_$(proj)_graph),$(call drake$(lang)_$(proj)_graph) $(foreach task,$(shell drake --taskgraph $(call drake$(lang)_$(proj)_graph) --tasks),$(shell drake --taskgraph $(call drake$(lang)_$(proj)_graph) --module $(task)).$(call ext,$(lang)))))) $(call drake_$(proj)_platform) $(distdir))
        
clean: submake-clean
	$(RM) $(cc_object) $(foreach var,$(cc_object),$(var:%.o=%.d)) $(foreach var,$(cc_object),$(var:%.o=%.e))
	$(RM) $(cxx_object) $(foreach var,$(cxx_object),$(var:%.o=%.d)) $(foreach var,$(cxx_object),$(var:%.o=%.e))
	$(RM) $(lib_pkgconfig)
	$(RM) $(drake_schedule_src) $(drake_taskgraph_text) $(drake_schedule_text)
	$(RM) $(embed_object)
	$(RM) $(test_target)
	$(RM) $(bin_list)
	$(RM) $(lib_list)
	$(RM) $(shared_list) $(shared_linkername) $(shared_soname)
	$(RM) $(clean)

check: submake-check $(test_list)
	@$(shell echo for i in "$(foreach var,$(testcc_list),$(var))"\; do eval echo Running \$$i...\; eval LD_LIBRARY_PATH=.:$(LD_LIBRARY_PATH) ./\$$i \|\| exit 1\; done)
	@$(shell echo for i in "$(foreach var,$(testcxx_list),$(var))"\; do eval echo Running \$$i...\; eval LD_LIBRARY_PATH=.:$(LD_LIBRARY_PATH) ./\$$i \|\| exit 1\; done)

checkdist distcheck: dist

install: pre-install submake-install do-install post-install
   
pre-install:
     
do-install: $(lib_pkgconfig) $(lib_list) $(bin_list) $(shared_list) $(shared_linkername) $(shared_soname)
	$(if $(strip $(attach)),test -d $(DESTDIR)$(final_sharedir) || install -d $(DESTDIR)$(final_sharedir))
	$(if $(strip $(attach)),install -m 644 $(attach) $(DESTDIR)$(final_sharedir))
	$(if $(strip $(lib_list)),test -d $(DESTDIR)$(final_libdir) || install -d $(DESTDIR)$(final_libdir))
	$(if $(strip $(lib_list)),install -m 644 $(lib_list) $(DESTDIR)$(final_libdir))
	$(if $(strip $(shared_list)),test -d $(DESTDIR)$(final_libdir) || install -d $(DESTDIR)$(final_libdir))
	$(if $(strip $(shared_list)),install -m 755 $(shared_list) $(DESTDIR)$(final_libdir))
	$(if $(strip $(shared_linkername)),cp -a $(shared_linkername) $(DESTDIR)$(final_libdir))
	$(if $(strip $(shared_soname)),cp -a $(shared_soname) $(DESTDIR)$(final_libdir))
	$(if $(strip $(bin_list)),test -d $(DESTDIR)$(final_bindir) || install -d $(DESTDIR)$(final_bindir))
	$(if $(strip $(bin_list)),install -m 755 $(bin_list) $(DESTDIR)$(final_bindir))
	$(if $(strip $(lib_list)),test -d $(DESTDIR)$(final_libdir)/pkgconfig || install -d $(DESTDIR)$(final_libdir)/pkgconfig)
	$(if $(strip $(shared_list)),test -d $(DESTDIR)$(final_libdir)/pkgconfig || install -d $(DESTDIR)$(final_libdir)/pkgconfig)
	$(if $(strip $(lib_pkgconfig)),install -m 644 $(lib_pkgconfig) $(DESTDIR)$(final_libdir)/pkgconfig)

post-install:

uninstall: submake-uninstall
	$(RM) $(foreach var,$(bin_list), $(DESTDIR)$(final_bindir)/$(call VARIANT_$(var))$(var))
	$(RM) $(foreach var,$(lib_list), $(DESTDIR)$(final_libdir)/lib$(call VARIANT_lib$(patsubst lib%.a,%,$(var)).a)$(patsubst lib%.a,%,$(var)).a)
	$(RM) $(foreach var,$(shared_list), $(DESTDIR)$(final_libdir)/$(var))
	$(RM) $(foreach var,$(shared_linkername), $(DESTDIR)$(final_libdir)/$(var))
	$(RM) $(foreach var,$(shared_soname), $(DESTDIR)$(final_libdir)/$(var))
	$(RM) $(foreach var,$(lib_pkgconfig), $(DESTDIR)$(final_libdir)/pkgconfig/$(call VARIANT_lib$(patsubst %.pc,%,$(var)).pc)$(var))
	
submake-all submake-dist submake-check submake-clean submake-install submake-uninstall:
	@$(shell echo for i in "$(foreach var,$(subdirs),$(var))"\; do $(MAKE) -C \$$i $(patsubst submake-%,%,$@) distdir=$(distdir)/\$$i subdir=$(subdir)/\$$i\|\|exit 1\; done)

FORCE:
.PHONY: all clean check install pre-install do-install post-install uninstall dist FORCE submake-all submake-dist submake-check submake-clean submake-install submake-uninstall pre-dist do-dist post-dist
