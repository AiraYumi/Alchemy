# -*- cmake -*-
include(Prebuilt)
include(Linking)

include_guard()

add_library( ll::boost INTERFACE IMPORTED )
if( USE_CONAN )
  target_link_libraries( ll::boost INTERFACE CONAN_PKG::boost )
  target_compile_definitions( ll::boost INTERFACE BOOST_ALLOW_DEPRECATED_HEADERS BOOST_BIND_GLOBAL_PLACEHOLDERS )
  return()
endif()

use_prebuilt_binary(boost)

# As of sometime between Boost 1.67 and 1.72, Boost libraries are suffixed
# with the address size.
set(addrsfx "-x${ADDRESS_SIZE}")

find_library(BOOST_CONTEXT_LIBRARY
    NAMES
    boost_context
    boost_context-mt
    boost_context-mt${addrsfx}
    PATHS "${ARCH_PREBUILT_DIRS_ARCH_RELEASE}" "${ARCH_PREBUILT_DIRS_RELEASE}" REQUIRED NO_DEFAULT_PATH)

find_library(BOOST_FIBER_LIBRARY
    NAMES
    boost_fiber
    boost_fiber-mt
    boost_fiber-mt${addrsfx}
    PATHS "${ARCH_PREBUILT_DIRS_ARCH_RELEASE}" "${ARCH_PREBUILT_DIRS_RELEASE}" REQUIRED NO_DEFAULT_PATH)

find_library(BOOST_FILESYSTEM_LIBRARY
    NAMES
    boost_filesystem
    boost_filesystem-mt
    boost_filesystem-mt${addrsfx}
    PATHS "${ARCH_PREBUILT_DIRS_ARCH_RELEASE}" "${ARCH_PREBUILT_DIRS_RELEASE}" REQUIRED NO_DEFAULT_PATH)

find_library(BOOST_PROGRAMOPTIONS_LIBRARY
    NAMES
    boost_program_options
    boost_program_options-mt
    boost_program_options-mt${addrsfx}
    PATHS "${ARCH_PREBUILT_DIRS_ARCH_RELEASE}" "${ARCH_PREBUILT_DIRS_RELEASE}" REQUIRED NO_DEFAULT_PATH)

find_library(BOOST_THREAD_LIBRARY
    NAMES
    boost_thread
    boost_thread-mt
    boost_thread-mt${addrsfx}
    PATHS "${ARCH_PREBUILT_DIRS_ARCH_RELEASE}" "${ARCH_PREBUILT_DIRS_RELEASE}" REQUIRED NO_DEFAULT_PATH)

find_library(BOOST_URL_LIBRARY
    NAMES
    boost_url
    boost_url-mt
    boost_url-mt${addrsfx}
    PATHS "${ARCH_PREBUILT_DIRS_ARCH_RELEASE}" "${ARCH_PREBUILT_DIRS_RELEASE}" REQUIRED NO_DEFAULT_PATH)

target_link_libraries(ll::boost INTERFACE
    ${BOOST_FIBER_LIBRARY}
    ${BOOST_CONTEXT_LIBRARY}
    ${BOOST_FILESYSTEM_LIBRARY}
    ${BOOST_PROGRAMOPTIONS_LIBRARY}
    ${BOOST_THREAD_LIBRARY}
    ${BOOST_URL_LIBRARY})

if (LINUX)
    target_link_libraries(ll::boost INTERFACE rt)
endif (LINUX)

