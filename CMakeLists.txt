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
	if(CMAKE_CL_64)
		set(MSVC_POSTFIX "64")
	endif()
	set(CMAKE_RELEASE_POSTFIX "${MSVC_POSTFIX}${CMAKE_RELEASE_POSTFIX}")
	set(CMAKE_DEBUG_POSTFIX "${MSVC_POSTFIX}${CMAKE_DEBUG_POSTFIX}")
	set(CMAKE_MINSIZEREL_POSTFIX "${MSVC_POSTFIX}${CMAKE_MINSIZEREL_POSTFIX}")
	set(CMAKE_RELWITHDEBINFO_POSTFIX "${MSVC_POSTFIX}${CMAKE_RELWITHDEBINFO_POSTFIX}")
	
	if(NOT MSVC_VERSION LESS 1400)
		add_compile_definitions("_CRT_SECURE_NO_WARNINGS")
	endif()
endif()

if(CMAKE_COMPILER_IS_GNUCC)

# assume Windows 2000 and later for GetConsoleWindow API call
if(WIN32)
	add_compile_definitions("_WIN32_WINNT=0x500")
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


if(WIN32)
	set(MC_DEFAULT "WIN_KEYS")
elseif(UNIX AND NOT APPLE)
	# Note: Finding DBus1 via find_package requires dbus-1.11.12 or higher.
	find_package(DBus1 QUIET)
	if (DBus1_FOUND)
		set(MC_DEFAULT "DBUS")
	else()
		set(MC_DEFAULT "OFF")
	endif()
else()
	set(MC_DEFAULT "OFF")
endif()
set(MEDIA_CONTROLS "${MC_DEFAULT}" CACHE STRING "enable Media Controls")
set_property(CACHE MEDIA_CONTROLS PROPERTY STRINGS "OFF;WIN_KEYS;WIN_OVERLAY;DBUS")


# --- INI reading ---
include(FindPkgConfig OPTIONAL)
if(PKG_CONFIG_FOUND)
	pkg_check_modules(INIH inih)
else()
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
endif()


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
	mediainfo.hpp
	playcfg.hpp
	version.h
)
set(PLAYER_FILES
	utils.cpp
	config.cpp
	m3uargparse.cpp
	main.cpp
	mediainfo.cpp
	playctrl.cpp
	playcfg.cpp
)
set(PLAYER_LIBS)
set(PLAYER_DEFS)

# media control
list(APPEND PLAYER_HEADERS mediactrl.hpp)
list(APPEND PLAYER_FILES mediactrl.cpp)
if(MEDIA_CONTROLS STREQUAL "WIN_KEYS")
	list(APPEND PLAYER_HEADERS mediactrl_WinKeys.hpp)
	list(APPEND PLAYER_FILES mediactrl_WinKeys.cpp)
	list(APPEND PLAYER_DEFS MEDIACTRL_WINKEYS)
elseif(MEDIA_CONTROLS STREQUAL "WIN_OVERLAY")
	list(APPEND PLAYER_HEADERS mediactrl_WinKeys.hpp mediactrl_WinSMTC.hpp)
	list(APPEND PLAYER_FILES mediactrl_WinKeys.cpp mediactrl_WinSMTC.cpp)
	list(APPEND PLAYER_LIBS runtimeobject)	# for WindowsCreateStringReference, RoGetActivationFactory
	list(APPEND PLAYER_DEFS ENABLE_WINRT MEDIACTRL_WINKEYS MEDIACTRL_SMTC)
elseif(MEDIA_CONTROLS STREQUAL "DBUS")
	if (NOT DBus1_FOUND)	# don't import again if already found - the module doesn't like this
		find_package(DBus1 REQUIRED)
	endif()
	list(APPEND PLAYER_HEADERS mediactrl_dbus.hpp)
	list(APPEND PLAYER_FILES mediactrl_dbus.cpp)
	list(APPEND PLAYER_LIBS dbus-1)
	list(APPEND PLAYER_DEFS MEDIACTRL_DBUS)
endif()

add_executable(${PROJECT_NAME} ${HEADERS} ${SOURCES} ${PLAYER_HEADERS} ${PLAYER_FILES})
target_compile_definitions(${PROJECT_NAME} PRIVATE ${PLAYER_DEFS})
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR} ${INCLUDES})
target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBRARIES} libvgm::vgm-utils libvgm::vgm-audio libvgm::vgm-emu libvgm::vgm-player ${PLAYER_LIBS})
if(INIH_FOUND)
	target_include_directories(${PROJECT_NAME} PRIVATE ${INIH_INCLUDE_DIRS})
	target_link_libraries(${PROJECT_NAME} PUBLIC ${INIH_LDFLAGS})
endif(INIH_FOUND)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION "bin")
#add_sanitizers(${PROJECT_NAME})

if(MSVC AND MSVC_VERSION LESS 1400)
	target_include_directories(${PROJECT_NAME} PRIVATE
		${PROJECT_SOURCE_DIR}/libs/include_vc6
	)
endif()
