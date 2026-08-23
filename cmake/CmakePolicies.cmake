include_guard(GLOBAL)

# Interprocedural Optimization.
if (POLICY CMP0069)
    cmake_policy(SET CMP0069 NEW)
endif ()

# option() honors normal variables.
if (POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif ()

# MSVC warning-level flags are not injected into CMAKE_*_FLAGS by default.
if (POLICY CMP0092)
    cmake_policy(SET CMP0092 NEW)
endif ()

# MSVC runtime library selection driven by CMAKE_MSVC_RUNTIME_LIBRARY.
if (POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif ()

# De-duplicate libraries on link lines.
if (POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif ()


if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif ()
