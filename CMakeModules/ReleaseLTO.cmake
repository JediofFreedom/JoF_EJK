option(UseLTO "Enable supported link-time optimization for Release builds" ON)

function(configure_release_lto)
	if(NOT UseLTO)
		message(STATUS "Release IPO/LTO automatic default disabled (UseLTO=OFF)")
		return()
	endif()

	# An explicit per-configuration or global CMake setting takes precedence,
	# including OFF. Other configurations keep their existing CMake defaults.
	foreach(setting CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE CMAKE_INTERPROCEDURAL_OPTIMIZATION)
		if(DEFINED ${setting})
			message(STATUS "Release IPO/LTO preserves explicit ${setting}=${${setting}}")
			return()
		endif()
	endforeach()

	cmake_policy(PUSH)
	cmake_policy(SET CMP0069 NEW)
	if(POLICY CMP0138)
		cmake_policy(SET CMP0138 NEW)
	endif()
	if(POLICY CMP0137)
		cmake_policy(SET CMP0137 NEW)
	endif()
	include(CheckIPOSupported)
	# Newer CheckIPOSupported versions probe a Debug configuration. Check our
	# Release flags instead; MSVC's /ZI and /RTC1 are intentionally Debug-only
	# and incompatible with /GL. These overrides remain local to this function.
	set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
	foreach(language C CXX)
		set(CMAKE_${language}_FLAGS_DEBUG "${CMAKE_${language}_FLAGS_RELEASE}")
	endforeach()
	if(MSVC)
		# CMake 4 also supplies Debug /RTC1 through a target property rather
		# than FLAGS_DEBUG. Pass its Release value into the probe project.
		set(CMAKE_MSVC_RUNTIME_CHECKS "")
		list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CMAKE_MSVC_RUNTIME_CHECKS)
	endif()
	check_ipo_supported(RESULT supported OUTPUT diagnostic LANGUAGES C CXX)
	cmake_policy(POP)
	if(supported)
		# Do not cache the automatic default: UseLTO=OFF on a later configure
		# must remove it without overriding a user's explicit cache settings.
		set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE PARENT_SCOPE)
		message(STATUS "Release IPO/LTO enabled (C and C++ support checks passed)")
	else()
		message(STATUS "Release IPO/LTO unavailable; using normal compilation. ${diagnostic}")
	endif()
endfunction()
