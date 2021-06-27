# -*- cmake -*-
include(Linking)
include(Prebuilt)

if (USESYSTEMLIBS)
  include(FindPkgConfig)
  pkg_check_modules(OGG REQUIRED ogg)
  pkg_check_modules(VORBIS REQUIRED vorbis)
  pkg_check_modules(VORBISENC REQUIRED vorbisenc)
  pkg_check_modules(VORBISFILE REQUIRED vorbisfile)
else (USESYSTEMLIBS)
  use_prebuilt_binary(ogg_vorbis)
  set(VORBIS_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)
  set(VORBISENC_INCLUDE_DIRS ${VORBIS_INCLUDE_DIRS})
  set(VORBISFILE_INCLUDE_DIRS ${VORBIS_INCLUDE_DIRS})

  if (WINDOWS)
    set(OGG_LIBRARIES
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libogg.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libogg.lib)
    set(VORBIS_LIBRARIES
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libvorbis.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libvorbis.lib)
    set(VORBISFILE_LIBRARIES
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libvorbisfile.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libvorbisfile.lib)
  else (WINDOWS)
    set(OGG_LIBRARIES ogg)
    set(VORBIS_LIBRARIES vorbis)
    set(VORBISENC_LIBRARIES vorbisenc)
    set(VORBISFILE_LIBRARIES vorbisfile)
  endif (WINDOWS)
endif (USESYSTEMLIBS)
