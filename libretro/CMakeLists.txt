set(LIBRETRO_SRCS
  libretro.cpp
  LibretroGraphicsContext.cpp
  LibretroGLContext.cpp
  LibretroVulkanContext.cpp
  libretro_vulkan.cpp
)

include_directories(libretro)

add_library(ppsspp_libretro SHARED ${LIBRETRO_SRCS})
set_target_properties(ppsspp_libretro PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set_target_properties(ppsspp_libretro PROPERTIES PREFIX "")
if(ANDROID)
	set_target_properties(ppsspp_libretro PROPERTIES SUFFIX "_android.so")
endif()

target_link_libraries(ppsspp_libretro "-Wl,-Bsymbolic")
target_link_libraries(ppsspp_libretro ${LinkCommon})
