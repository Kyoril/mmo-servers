function(enable_unity_build UB_SUFFIX SOURCE_VARIABLE_NAME)
	#(copied from http://cheind.wordpress.com/2009/12/10/reducing-compilation-time-unity-builds/ )
	#modified to support pch in unity build file and support splitting N files in one big unity cpp file
	set(files ${${SOURCE_VARIABLE_NAME}})
	list(LENGTH files fileCount)
	if(fileCount GREATER 2)
		set(unity_index 0)
		# Exclude all translation units from compilation
		set_source_files_properties(${files} PROPERTIES HEADER_FILE_ONLY true)
		math(EXPR file_end "${fileCount}-1")
		# Generate N unity build files
		foreach(loop_var RANGE 0 ${file_end} ${MMO_UNITY_FILES_PER_UNIT})
			message(STATUS "  Generating unity build file ub_${UB_SUFFIX}.${unity_index}.cpp")
			# Generate a unique filename for the unity build translation unit
			set(unit_build_file "${CMAKE_CURRENT_BINARY_DIR}/ub_${UB_SUFFIX}.${unity_index}.cpp")
			# Open the ub file
			file(WRITE ${unit_build_file} "// Unity Build generated by CMake (do not edit this file)\n#include \"pch.h\"\n")
			set(file_index 0)
			math(EXPR min_file "${unity_index}*${MMO_UNITY_FILES_PER_UNIT}")
			math(EXPR max_file "${min_file}+${MMO_UNITY_FILES_PER_UNIT}+1")
			# Add include statement for each translation unit
			foreach(source_file ${files})
				#adding pch.cpp is unnecessary
				if(NOT(${source_file} MATCHES ".*pch\\.cpp"))
					math(EXPR file_index "${file_index}+1")
					if (file_index GREATER min_file AND file_index LESS max_file)
						file(APPEND ${unit_build_file} "#include <${source_file}>\n")
					endif()
				else()
					set_source_files_properties(${source_file} PROPERTIES HEADER_FILE_ONLY false)
				endif()
			endforeach()
			# Complement list of translation units with the name of ub
			set(${SOURCE_VARIABLE_NAME} "${${SOURCE_VARIABLE_NAME}}" "${unit_build_file}")
			set(${SOURCE_VARIABLE_NAME} "${${SOURCE_VARIABLE_NAME}}" "${unit_build_file}" PARENT_SCOPE)
			# Increment unity build file counter
			math(EXPR unity_index "${unity_index}+1")
		endforeach()
	endif()
endfunction()

macro(add_lib name)
	file(GLOB sources "*.cpp" "*.c")
	file(GLOB headers "*.h" "*.hpp")
	#remove_pch_cpp(sources "${CMAKE_CURRENT_SOURCE_DIR}/pch.cpp")
	if(MMO_UNITY_BUILD)
		message(STATUS "Unity build enabled for ${name}")
		include_directories(${CMAKE_CURRENT_SOURCE_DIR})
		enable_unity_build(${name} sources)
	endif()
	add_library(${name} ${headers} ${sources})
	#add_precompiled_header(${name} "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
	source_group(src FILES ${headers} ${sources})
	
	if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
		target_link_libraries(${name} stdc++fs)
		target_link_libraries(${name} ${OPENSSL_LIBRARIES})
	endif()
endmacro()

macro(add_exe name)
	file(GLOB sources "*.cpp")
	file(GLOB headers "*.h" "*.hpp")
	#remove_pch_cpp(sources "${CMAKE_CURRENT_SOURCE_DIR}/pch.cpp")
	if(MMO_UNITY_BUILD)
		message(STATUS "Unity build enabled for ${name}")
		include_directories(${CMAKE_CURRENT_SOURCE_DIR})
		enable_unity_build(${name} sources)
	endif()
	add_executable(${name} ${headers} ${sources})
	#add_precompiled_header(${name} "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
	source_group(src FILES ${headers} ${sources})
	if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
		target_link_libraries(${name} stdc++fs)
		target_link_libraries(${name} ${OPENSSL_LIBRARIES})
	endif()
endmacro()

macro(add_gui_exe name)
	set (add_sources ${ARGN})

	file(GLOB sources "*.cpp")
	file(GLOB headers "*.h" "*.hpp")
	file(GLOB resources "*.rc")
	
	# Append additional sources if any
	list(LENGTH add_sources num_add_sources)
    if (${num_add_sources} GREATER 0)
		list(APPEND sources ${add_sources})
	endif()
	
	#remove_pch_cpp(sources "${CMAKE_CURRENT_SOURCE_DIR}/pch.cpp")
	if(MMO_UNITY_BUILD)
		message(STATUS "Unity build enabled for ${name}")
		include_directories(${CMAKE_CURRENT_SOURCE_DIR})
		enable_unity_build(${name} sources)
	endif()
	add_executable(${name} WIN32 MACOSX_BUNDLE ${resources} ${headers} ${sources})
	#add_precompiled_header(${name} "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
	source_group(src FILES ${headers} ${sources})
	if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
		target_link_libraries(${name} stdc++fs)
	endif()
endmacro()

MACRO(ADD_PRECOMPILED_HEADER _targetName _input)
	GET_FILENAME_COMPONENT(_name ${_input} NAME)
	get_filename_component(_name_no_ext ${_name} NAME_WE)
	if(MSVC)
		set_target_properties(${_targetName} PROPERTIES COMPILE_FLAGS "/Yu${_name}")
		set_source_files_properties("${_name_no_ext}.cpp" PROPERTIES COMPILE_FLAGS "/Yc${_name}")
	else()
		IF(APPLE)
			set_target_properties(
        		    ${_targetName} 
        		    PROPERTIES
        		    XCODE_ATTRIBUTE_GCC_PREFIX_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/${_name_no_ext}.h"
        		    XCODE_ATTRIBUTE_GCC_PRECOMPILE_PREFIX_HEADER "YES"
        		)
		ELSE()
			if(UNIX)
				GET_FILENAME_COMPONENT(_path ${_input} PATH)
				SET(_outdir "${CMAKE_CURRENT_BINARY_DIR}")
				SET(_output "${_input}.gch")

				set_directory_properties(PROPERTY ADDITIONAL_MAKE_CLEAN_FILES _output)

				STRING(TOUPPER "CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}" _flags_var_name)
				SET(_compile_FLAGS ${${_flags_var_name}})

				GET_DIRECTORY_PROPERTY(_directory_flags INCLUDE_DIRECTORIES)
				list(REMOVE_ITEM _directory_flags /usr/include) #/usr/include is not needed in this list and can cause problems with gcc
				foreach(d ${_directory_flags})
                                        #-isystem to ignore warnings in foreign headers
					list(APPEND _compile_FLAGS "-isystem${d}")
				endforeach()

				GET_DIRECTORY_PROPERTY(_directory_flags COMPILE_DEFINITIONS)
				LIST(APPEND defines ${_directory_flags})

				string(TOUPPER "COMPILE_DEFINITIONS_${CMAKE_BUILD_TYPE}" defines_for_build_name)
				get_directory_property(defines_build ${defines_for_build_name})
				list(APPEND defines ${defines_build})

				foreach(item ${defines})
					list(APPEND _compile_FLAGS "-D${item}")
				endforeach()

				LIST(APPEND _compile_FLAGS ${CMAKE_CXX_FLAGS})

				SEPARATE_ARGUMENTS(_compile_FLAGS)
				ADD_CUSTOM_COMMAND(
					OUTPUT ${_output}
					COMMAND ${CMAKE_CXX_COMPILER}
					${_compile_FLAGS}
					-x c++-header
					-fPIC
					-o ${_output}
					${_input}
					DEPENDS ${_input} #${CMAKE_CURRENT_BINARY_DIR}/${_name}
				)
				ADD_CUSTOM_TARGET(${_targetName}_gch
					DEPENDS	${_output}
				)
				ADD_DEPENDENCIES(${_targetName} ${_targetName}_gch)
			endif()
		ENDIF()
	endif()
ENDMACRO()

MACRO(remove_pch_cpp sources pch_cpp)
	if(UNIX)
		list(REMOVE_ITEM ${sources} ${pch_cpp})
	endif()
ENDMACRO()

