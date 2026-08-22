/* Wrapper TU that lets volk's implementation see the Win32 platform headers,
   so it emits function pointers for the KHR_win32_surface extension. */
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <volk.h>
#include <volk.c>
