ifneq ($(YCASH_TOOLCHAIN), GCC)
  default_host_CC = clang -target $(host) -B$(build_prefix)/bin
  default_host_CXX = clang++ -target $(host) -B$(build_prefix)/bin -stdlib=libc++
  default_host_AR = llvm-ar
  default_host_RANLIB = llvm-ranlib
  default_host_STRIP = llvm-strip
  default_host_LIBTOOL = $(host_toolchain)libtool
  default_host_INSTALL_NAME_TOOL = $(host_toolchain)install_name_tool
  default_host_OTOOL = $(host_toolchain)otool
  default_host_NM = llvm-nm

  $(host_os)_native_binutils?=native_clang
  $(host_os)_native_toolchain?=native_clang
else
  ifneq ($(host),$(build))
    host_toolchain:=$(host)-
  else
    host_toolchain:=
  endif
  default_host_CC = $(host_toolchain)gcc
  default_host_CXX = $(host_toolchain)g++
  default_host_AR = $(host_toolchain)ar
  default_host_RANLIB = $(host_toolchain)ranlib
  default_host_STRIP = $(host_toolchain)strip
  default_host_LIBTOOL = $(host_toolchain)libtool
  default_host_INSTALL_NAME_TOOL = $(host_toolchain)install_name_tool
  default_host_OTOOL = $(host_toolchain)otool
  default_host_NM = $(host_toolchain)nm
endif

define add_host_tool_func
ifneq ($(filter $(origin $1),undefined default),)
# Do not consider the well-known var $1 if it is undefined or is taking a value
# that is predefined by "make" (e.g. the make variable "CC" has a predefined
# value of "cc")
$(host_os)_$1?=$$(default_host_$1)
$(host_arch)_$(host_os)_$1?=$$($(host_os)_$1)
$(host_arch)_$(host_os)_$(release_type)_$1?=$$($(host_os)_$1)
else
$(host_os)_$1=$(or $($1),$($(host_os)_$1),$(default_host_$1))
$(host_arch)_$(host_os)_$1=$(or $($1),$($(host_arch)_$(host_os)_$1),$$($(host_os)_$1))
$(host_arch)_$(host_os)_$(release_type)_$1=$(or $($1),$($(host_arch)_$(host_os)_$(release_type)_$1),$$($(host_os)_$1))
endif
host_$1=$$($(host_arch)_$(host_os)_$1)
endef

define add_host_flags_func
$(host_arch)_$(host_os)_$1 += $($(host_os)_$1)
$(host_arch)_$(host_os)_$(release_type)_$1 += $($(host_os)_$(release_type)_$1)
host_$1 = $$($(host_arch)_$(host_os)_$1)
host_$(release_type)_$1 = $$($(host_arch)_$(host_os)_$(release_type)_$1)
endef

$(foreach tool,CC CXX AR RANLIB STRIP NM LIBTOOL OTOOL INSTALL_NAME_TOOL,$(eval $(call add_host_tool_func,$(tool))))
$(foreach flags,CFLAGS CXXFLAGS CPPFLAGS LDFLAGS, $(eval $(call add_host_flags_func,$(flags))))
