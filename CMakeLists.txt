#### VGMPlay (libvgm-based) ####
cmake_minimum_required(VERSION 3.1)
if(POLICY CMP0042)
	cmake_policy(SET CMP0042 NEW)
endif()
if(POLICY CMP0048)
	cmake_policy(SET CMP0048 NEW)
endif()
project(vgmplay VERSION 0.5 LANGUAGES C CXX)

#set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
find_package(Iconv QUIET)
find_package(libvgm REQUIRED)

if(MSVC)
	set(CMAKE_DEBUG_POSTFIX "d")
	if(MSVC_VERSION LESS 1400)
		set(MSVC_POSTFIX "VC6")	# there are a few limitations for MSVC versions before 2005
	elseif(CMAKE_CL_64)
		set(MSVC_POSTFIX "Win64")
	else()
		set(MSVC_POSTFIX "Win32")
	endif()
	set(CMAKE_RELEASE_POSTFIX "_${MSVC_POSTFIX}${CMAKE_RELEASE_POSTFIX}")
	set(CMAKE_DEBUG_POSTFIX "_${MSVC_POSTFIX}${CMAKE_DEBUG_POSTFIX}")
	set(CMAKE_MINSIZEREL_POSTFIX "_${MSVC_POSTFIX}${CMAKE_MINSIZEREL_POSTFIX}")
	set(CMAKE_RELWITHDEBINFO_POSTFIX "_${MSVC_POSTFIX}${CMAKE_RELWITHDEBINFO_POSTFIX}")
	
	if(NOT MSVC_VERSION LESS 1400)
		add_definitions("/D _CRT_SECURE_NO_WARNINGS")
	endif()
endif()

if(CMAKE_COMPILER_IS_GNUCC)

# assume Windows 2000 and later for GetConsoleWindow API call
if(WIN32)
	add_definitions("-D _WIN32_WINNT=0x500")
endif()
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_DEBUG")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wpedantic")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-long-long -Wno-unused-value")

# silence typical sound core warnings
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Wno-unknown-pragmas")
#set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Wno-sign-compare")
#set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Wno-unused-variable -Wno-unused-const-variable -Wno-unused-function")

# additional warnings from http://blog.httrack.com/blog/2014/03/09/what-are-your-gcc-flags/
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wpointer-arith -Winit-self -Wstrict-aliasing")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wformat -Wformat-security -Wformat-nonliteral")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector")
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fstack-protector")

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

endif(CMAKE_COMPILER_IS_GNUCC)

#list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/sanitizers")
#find_package(Sanitizers)


# --- INI reading ---
set(INIH_DIR libs/inih)
set(HEADERS ${HEADERS}
		${INIH_DIR}/ini.h
	)
set(SOURCES ${SOURCES}
		${INIH_DIR}/ini.c
	)
set(INCLUDES ${INCLUDES}
		${INIH_DIR}
	)


# --- Windows getopt --
if(MSVC)
	# not using add_subdirectory(), because I don't want to install it
	set(WINGETOPT_DIR libs/wingetopt/src)
	set(HEADERS ${HEADERS}
			${WINGETOPT_DIR}/getopt.h
		)
	set(SOURCES ${SOURCES}
			${WINGETOPT_DIR}/getopt.c
		)
	set(INCLUDES ${INCLUDES}
			${WINGETOPT_DIR}
		)
endif()	# wingetopt


set(PLAYER_HEADERS
	utils.hpp
	config.hpp
	m3uargparse.hpp
)
set(PLAYER_FILES
	utils.cpp
	config.cpp
	m3uargparse.cpp
	main.cpp
	player.cpp
	playctrl.cpp
)

add_executable(${PROJECT_NAME} ${HEADERS} ${SOURCES} ${PLAYER_HEADERS} ${PLAYER_FILES})
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR} ${INCLUDES})
target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBRARIES} libvgm::vgm-utils libvgm::vgm-audio libvgm::vgm-emu libvgm::vgm-player)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION "bin")
#add_sanitizers(${PROJECT_NAME})
