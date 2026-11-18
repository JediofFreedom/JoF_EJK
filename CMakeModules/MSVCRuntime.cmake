
macro(configure_msvc_runtime)
	if(MSVC)
		# Default to dynamically-linked runtime.
		if("${MSVC_RUNTIME}" STREQUAL "")
			set(MSVC_RUNTIME "dynamic")
		endif()

		# Set compiler options.
		set(variables
			CMAKE_C_FLAGS
			CMAKE_C_FLAGS_DEBUG
			CMAKE_C_FLAGS_MINSIZEREL
			CMAKE_C_FLAGS_RELEASE
			CMAKE_C_FLAGS_RELWITHDEBINFO
			CMAKE_CXX_FLAGS
			CMAKE_CXX_FLAGS_DEBUG
			CMAKE_CXX_FLAGS_MINSIZEREL
			CMAKE_CXX_FLAGS_RELEASE
			CMAKE_CXX_FLAGS_RELWITHDEBINFO)

		if(${MSVC_RUNTIME} STREQUAL "static")
			message(STATUS "MSVC: using statically-linked runtime (/MT and /MTd).")
			foreach(variable ${variables})
				if(${variable} MATCHES "/MD")
					string(REGEX REPLACE "/MD" "/MT" ${variable} "${${variable}}")
				endif()
			endforeach()
		else()
			message(STATUS "MSVC: using dynamically-linked runtime (/MD and /MDd).")
			foreach(variable ${variables})
				if(${variable} MATCHES "/MT")
					string(REGEX REPLACE "/MT" "/MD" ${variable} "${${variable}}")
				endif()
			endforeach()
		endif()

		# Keep language-specific/toolchain flags intact. These are directory
		# overrides, not cache defaults: persisting the augmented flags caused
		# every configure to append the project's options again.
	endif()
endmacro(configure_msvc_runtime)
