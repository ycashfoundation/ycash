package=libcxx
$(package)_version=$(if $(native_clang_version_$(host_arch)_$(host_os)),$(native_clang_version_$(host_arch)_$(host_os)),$(if $(native_clang_version_$(host_os)),$(native_clang_version_$(host_os)),$(native_clang_default_version)))
$(package)_llvm_mingw_version=20240619

ifneq ($(canonical_host),$(build))
ifneq ($(host_os),mingw32)
# Clang is provided pre-compiled for a bunch of targets; fetch the one we need
# and stage its copies of the static libraries.
$(package)_download_path=$(native_clang_download_path)
$(package)_download_file_aarch64_linux=clang+llvm-$($(package)_version)-aarch64-linux-gnu.tar.xz
$(package)_file_name_aarch64_linux=clang-llvm-$($(package)_version)-aarch64-linux-gnu.tar.xz
$(package)_sha256_hash_aarch64_linux=dcaa1bebbfbb86953fdfbdc7f938800229f75ad26c5c9375ef242edad737d999
$(package)_download_file_linux=clang+llvm-$($(package)_version)-x86_64-linux-gnu-ubuntu-22.04.tar.xz
$(package)_file_name_linux=clang-llvm-$($(package)_version)-x86_64-linux-gnu-ubuntu-22.04.tar.xz
$(package)_sha256_hash_linux=54ec30358afcc9fb8aa74307db3046f5187f9fb89fb37064cdde906e062ebf36

# Starting from LLVM 14.0.0, some Clang binary tarballs store libc++ in a
# target-specific subdirectory.
define $(package)_stage_cmds
  mkdir -p $($(package)_staging_prefix_dir)/lib && \
  (test ! -f lib/*/libc++.a    || cp lib/*/libc++.a    $($(package)_staging_prefix_dir)/lib) && \
  (test ! -f lib/*/libc++abi.a || cp lib/*/libc++abi.a $($(package)_staging_prefix_dir)/lib) && \
  (test ! -f lib/libc++.a      || cp lib/libc++.a      $($(package)_staging_prefix_dir)/lib) && \
  (test ! -f lib/libc++abi.a   || cp lib/libc++abi.a   $($(package)_staging_prefix_dir)/lib)
endef

else
# For Windows cross-compilation, use llvm-mingw which provides libc++, libc++abi,
# and libunwind all built with LLVM's SEH unwinder — no GCC runtime dependency.
$(package)_download_path=https://github.com/mstorsjo/llvm-mingw/releases/download/$($(package)_llvm_mingw_version)
$(package)_download_file=llvm-mingw-$($(package)_llvm_mingw_version)-msvcrt-ubuntu-20.04-x86_64.tar.xz
$(package)_file_name=llvm-mingw-$($(package)_llvm_mingw_version)-msvcrt-ubuntu-20.04-x86_64.tar.xz
$(package)_sha256_hash=8a78af8e05a707df02dae9e98e5277e92ba22b8d7f346db1ee3d603c7518ff12

$(package)_llvm_mingw_dir=llvm-mingw-$($(package)_llvm_mingw_version)-msvcrt-ubuntu-20.04-x86_64

define $(package)_extract_cmds
  echo "$($(package)_sha256_hash)  $($(package)_source)" > .$($(package)_file_name).hash && \
  $(build_SHA256SUM) -c .$($(package)_file_name).hash && \
  tar --no-same-owner -xf $($(package)_source)
endef

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_prefix_dir)/lib && \
  mkdir -p $($(package)_staging_prefix_dir)/include && \
  cp $($(package)_llvm_mingw_dir)/x86_64-w64-mingw32/lib/libc++.a    $($(package)_staging_prefix_dir)/lib && \
  cp $($(package)_llvm_mingw_dir)/x86_64-w64-mingw32/lib/libc++abi.a $($(package)_staging_prefix_dir)/lib && \
  cp $($(package)_llvm_mingw_dir)/x86_64-w64-mingw32/lib/libunwind.a $($(package)_staging_prefix_dir)/lib && \
  cp -r $($(package)_llvm_mingw_dir)/generic-w64-mingw32/include/c++ $($(package)_staging_prefix_dir)/include
endef
endif

else
# For native compilation, use the static libraries from native_clang.
# We explicitly stage them so that subsequent dependencies don't link to the
# shared libraries distributed with Clang.
define $(package)_fetch_cmds
endef

define $(package)_extract_cmds
endef

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_prefix_dir)/lib && \
  (test ! -f $(build_prefix)/lib/*/libc++.a    || cp $(build_prefix)/lib/*/libc++.a    $($(package)_staging_prefix_dir)/lib) && \
  (test ! -f $(build_prefix)/lib/*/libc++abi.a || cp $(build_prefix)/lib/*/libc++abi.a $($(package)_staging_prefix_dir)/lib) && \
  (test ! -f $(build_prefix)/lib/libc++.a      || cp $(build_prefix)/lib/libc++.a      $($(package)_staging_prefix_dir)/lib) && \
  (test ! -f $(build_prefix)/lib/libc++abi.a   || cp $(build_prefix)/lib/libc++abi.a   $($(package)_staging_prefix_dir)/lib)
endef

endif
