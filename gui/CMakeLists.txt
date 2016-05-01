project(gb1mulator)

#find_package(Qt5 REQUIRED Widgets)

include(FindPkgConfig)

pkg_search_module(SDL REQUIRED sdl2)

#include_directories("${CMAKE_CURRENT_BINARY_DIR}")
include_directories("include"
                    ${SDL_INCLUDE_DIRS})

file(GLOB sources "sources/*.cpp")
#file(GLOB forms "forms/*.ui")
file(GLOB headers "include/*.h")

#qt5_wrap_cpp(mocs ${headers})
#qt5_wrap_ui(uis ${forms})

#message("MOCs: ${mocs}")
#message("UIs: ${uis}")
add_compile_options(${SDL_CFLAGS})

add_executable(b1mulator ${sources})
target_link_libraries(b1mulator  ${SDL_LDFLAGS})
