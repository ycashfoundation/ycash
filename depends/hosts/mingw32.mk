mingw32_CFLAGS=-pipe
mingw32_CXXFLAGS=$(mingw32_CFLAGS)

ifneq ($(YCASH_TOOLCHAIN), GCC)
  mingw32_CXX = clang++ -target $(host) -B$(build_prefix)/bin
  mingw32_CXXFLAGS += -nostdinc++ -isystem $(host_prefix)/include/c++/v1
  mingw32_LDFLAGS?=-fuse-ld=lld -nostdlib++
else
  mingw32_CC = $(host_toolchain)gcc-posix
  mingw32_CXX = $(host_toolchain)g++-posix
endif

mingw32_release_CFLAGS=-O3
mingw32_release_CXXFLAGS=$(mingw32_release_CFLAGS)

mingw32_debug_CFLAGS=-O0
mingw32_debug_CXXFLAGS=$(mingw32_debug_CFLAGS)

mingw32_debug_CPPFLAGS=-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC
