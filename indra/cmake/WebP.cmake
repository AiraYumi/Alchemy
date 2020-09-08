# -*- cmake -*-
include(Prebuilt)

set(WEBP_FIND_QUIETLY ON)
set(WEBP_FIND_REQUIRED ON)

if (USESYSTEMLIBS)
  include(FindWEBP)
else (USESYSTEMLIBS)
  use_prebuilt_binary(libwebp)
  if (WINDOWS)
    set(WEBP_LIBRARIES 
        debug libwebp_debug_dll
        optimized libwebp_dll
        )
    set(WEBP_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/webp)
  elseif(DARWIN)
    set(WEBP_LIBRARIES webp)
    set(WEBP_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/webp)
  else()
    set(WEBP_LIBRARIES webp)
    set(WEBP_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/webp)
  endif()
endif (USESYSTEMLIBS)
