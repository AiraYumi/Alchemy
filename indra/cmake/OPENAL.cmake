# -*- cmake -*-
include_guard()

include(Linking)
include(Prebuilt)

set(USE_OPENAL ON CACHE BOOL "Enable OpenAL")

if (USE_OPENAL)
  add_library( ll::openal INTERFACE IMPORTED )
  target_include_directories( ll::openal SYSTEM INTERFACE "${LIBS_PREBUILT_DIR}/include/AL")
  target_compile_definitions( ll::openal INTERFACE LL_OPENAL=1)
  use_prebuilt_binary(openal)

  find_library(OPENAL_LIBRARY
      NAMES
      OpenAL32
      openal
      libopenal.dylib
      PATHS "${ARCH_PREBUILT_DIRS_RELEASE}" REQUIRED NO_DEFAULT_PATH)

  find_library(ALUT_LIBRARY
      NAMES
      alut
      libalut.dylib
      PATHS "${ARCH_PREBUILT_DIRS_RELEASE}" REQUIRED NO_DEFAULT_PATH)

  target_link_libraries(ll::openal INTERFACE ${OPENAL_LIBRARY} ${ALUT_LIBRARY})

endif ()
