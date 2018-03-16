
#include <mutex>
#include <condition_variable>

#include "Common/Vulkan/VulkanLoader.h"
#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanDebug.h"
#include "Common/Log.h"
#include "GPU/GPUInterface.h"
#include "util/text/parsers.h"

#include "libretro/libretro_vulkan.h"
#include "libretro/LibretroVulkanContext.h"

namespace Libretro {
static retro_hw_render_interface_vulkan *vulkan;
}

static struct
{
	VkInstance instance;
	VkPhysicalDevice gpu;
	VkSurfaceKHR surface;
	PFN_vkGetInstanceProcAddr get_instance_proc_addr;
	const char **required_device_extensions;
	unsigned num_required_device_extensions;
	const char **required_device_layers;
	unsigned num_required_device_layers;
	const VkPhysicalDeviceFeatures *required_features;
} vk_init_info;

static VulkanContext *vk;

static PFN_vkCreateDevice vkCreateDevice_org;
static PFN_vkQueueSubmit vkQueueSubmit_org;
static PFN_vkQueueWaitIdle vkQueueWaitIdle_org;
static PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier_org;
static PFN_vkCreateRenderPass vkCreateRenderPass_org;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR_org;
#define VULKAN_MAX_SWAPCHAIN_IMAGES 8

struct VkSwapchainKHR_T
{
	uint32_t count;
	struct
	{
		VkImage handle;
		VkDeviceMemory memory;
		retro_vulkan_image retro_image;
	} images[VULKAN_MAX_SWAPCHAIN_IMAGES];
	std::mutex mutex;
	std::condition_variable condVar;
	int current_index;
};

static VkSwapchainKHR_T chain;

static VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance)
{
	*pInstance = vk_init_info.instance;
	return VK_SUCCESS;
}

static void add_name_unique(std::vector<const char *> &list, const char *value)
{
	for (const char *name : list)
		if (!strcmp(value, name))
			return;

	list.push_back(value);
}
static VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
	VkDeviceCreateInfo info = *pCreateInfo;
	std::vector<const char *> EnabledLayerNames(info.ppEnabledLayerNames, info.ppEnabledLayerNames + info.enabledLayerCount);
	std::vector<const char *> EnabledExtensionNames(info.ppEnabledExtensionNames, info.ppEnabledExtensionNames + info.enabledExtensionCount);
	VkPhysicalDeviceFeatures EnabledFeatures = *info.pEnabledFeatures;

	for (int i = 0; i < vk_init_info.num_required_device_layers; i++)
		add_name_unique(EnabledLayerNames, vk_init_info.required_device_layers[i]);

	for (int i = 0; i < vk_init_info.num_required_device_extensions; i++)
		add_name_unique(EnabledExtensionNames, vk_init_info.required_device_extensions[i]);

	add_name_unique(EnabledExtensionNames, VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME);
	for (int i = 0; i < sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32); i++)
	{
		if (((VkBool32 *)vk_init_info.required_features)[i])
			((VkBool32 *)&EnabledFeatures)[i] = VK_TRUE;
	}

	info.enabledLayerCount = (uint32_t)EnabledLayerNames.size();
	info.ppEnabledLayerNames = info.enabledLayerCount ? EnabledLayerNames.data() : nullptr;
	info.enabledExtensionCount = (uint32_t)EnabledExtensionNames.size();
	info.ppEnabledExtensionNames = info.enabledExtensionCount ? EnabledExtensionNames.data() : nullptr;
	info.pEnabledFeatures = &EnabledFeatures;

	return vkCreateDevice_org(physicalDevice, &info, pAllocator, pDevice);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateSurfaceKHR(VkInstance instance, const void *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface)
{
	*pSurface = vk_init_info.surface;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
	VkResult res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR_org(physicalDevice, surface, pSurfaceCapabilities);
	if(res == VK_SUCCESS)
	{
		pSurfaceCapabilities->currentExtent.width = -1;
		pSurfaceCapabilities->currentExtent.height = -1;
	}
	return res;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain)
{
	uint32_t swapchain_mask = Libretro::vulkan->get_sync_index_mask(Libretro::vulkan->handle);

	chain.count = 0;
	while (swapchain_mask)
	{
		chain.count++;
		swapchain_mask >>= 1;
	}
	assert(chain.count <= VULKAN_MAX_SWAPCHAIN_IMAGES);

	for (uint32_t i = 0; i < chain.count; i++)
	{
		{
			VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
			info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
			info.imageType = VK_IMAGE_TYPE_2D;
			info.format = pCreateInfo->imageFormat;
			info.extent.width = pCreateInfo->imageExtent.width;
			info.extent.height = pCreateInfo->imageExtent.height;
			info.extent.depth = 1;
			info.mipLevels = 1;
			info.arrayLayers = 1;
			info.samples = VK_SAMPLE_COUNT_1_BIT;
			info.tiling = VK_IMAGE_TILING_OPTIMAL;
			info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			vkCreateImage(device, &info, pAllocator, &chain.images[i].handle);
		}

		VkMemoryRequirements memreq;
		vkGetImageMemoryRequirements(device, chain.images[i].handle, &memreq);

		VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		alloc.allocationSize = memreq.size;

		VkMemoryDedicatedAllocateInfoKHR dedicated{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR };
		if (vk->DeviceExtensions().DEDICATED_ALLOCATION)
		{
			alloc.pNext = &dedicated;
			dedicated.image = chain.images[i].handle;
		}

		vk->MemoryTypeFromProperties(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &alloc.memoryTypeIndex);
		VkResult res = vkAllocateMemory(device, &alloc, pAllocator, &chain.images[i].memory);
		assert(res == VK_SUCCESS);
		res = vkBindImageMemory(device, chain.images[i].handle, chain.images[i].memory, 0);
		assert(res == VK_SUCCESS);

		chain.images[i].retro_image.create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		chain.images[i].retro_image.create_info.image = chain.images[i].handle;
		chain.images[i].retro_image.create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		chain.images[i].retro_image.create_info.format = pCreateInfo->imageFormat;
		chain.images[i].retro_image.create_info.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
		chain.images[i].retro_image.create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		chain.images[i].retro_image.create_info.subresourceRange.layerCount = 1;
		chain.images[i].retro_image.create_info.subresourceRange.levelCount = 1;
		res = vkCreateImageView(device, &chain.images[i].retro_image.create_info, pAllocator, &chain.images[i].retro_image.image_view);
		assert(res == VK_SUCCESS);

		chain.images[i].retro_image.image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	chain.current_index = -1;
	*pSwapchain = (VkSwapchainKHR *)&chain;

	return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages)
{
	if (pSwapchainImages)
	{
		assert(*pSwapchainImageCount <= swapchain->count);
		for (int i = 0; i < *pSwapchainImageCount; i++)
			pSwapchainImages[i] = swapchain->images[i].handle;
	}
	else
		*pSwapchainImageCount = swapchain->count;

	return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex)
{
	Libretro::vulkan->wait_sync_index(Libretro::vulkan->handle);
	*pImageIndex = Libretro::vulkan->get_sync_index(Libretro::vulkan->handle);
	return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo)
{
	std::unique_lock<std::mutex> lock(pPresentInfo->pSwapchains[0]->mutex);
#if 0
	if(chain.current_index >= 0)
		chain.condVar.wait(lock);
#endif

	chain.current_index = pPresentInfo->pImageIndices[0];
	Libretro::vulkan->set_image(Libretro::vulkan->handle, &pPresentInfo->pSwapchains[0]->images[pPresentInfo->pImageIndices[0]].retro_image, 0, nullptr, Libretro::vulkan->queue_index);
	pPresentInfo->pSwapchains[0]->condVar.notify_all();

	return VK_SUCCESS;
}

void LibretroVulkanContext::SwapBuffers()
{
	std::unique_lock<std::mutex> lock(chain.mutex);
	if (chain.current_index < 0)
		chain.condVar.wait(lock);
	LibretroHWRenderContext::SwapBuffers();
#if 0
	chain.current_index = -1;
	chain.condVar.notify_all();
#endif
}

static VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator) {}
static VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator) {}
static VKAPI_ATTR void VKAPI_CALL DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator) {}
static VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator)
{
	for (int i = 0; i < chain.count; i++)
	{
		vkDestroyImage(device, chain.images[i].handle, pAllocator);
		vkDestroyImageView(device, chain.images[i].retro_image.image_view, pAllocator);
		vkFreeMemory(device, chain.images[i].memory, pAllocator);
	}

	memset(&chain.images, 0x00, sizeof(chain.images));
	chain.count = 0;
	chain.current_index = -1;
}

VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence)
{
	VkResult res = VK_SUCCESS;

#if 0
	for(int i = 0; i < submitCount; i++)
		Libretro::vulkan->set_command_buffers(Libretro::vulkan->handle, pSubmits[i].commandBufferCount, pSubmits[i].pCommandBuffers);
#else
	for (int i = 0; i < submitCount; i++)
	{
		((VkSubmitInfo *)pSubmits)[i].waitSemaphoreCount = 0;
		((VkSubmitInfo *)pSubmits)[i].pWaitSemaphores = nullptr;
		((VkSubmitInfo *)pSubmits)[i].signalSemaphoreCount = 0;
		((VkSubmitInfo *)pSubmits)[i].pSignalSemaphores = nullptr;
	}
	Libretro::vulkan->lock_queue(Libretro::vulkan->handle);
	res = vkQueueSubmit_org(queue, submitCount, pSubmits, fence);
	Libretro::vulkan->unlock_queue(Libretro::vulkan->handle);
#endif

	return res;
}

VKAPI_ATTR VkResult VKAPI_CALL QueueWaitIdle(VkQueue queue)
{
	Libretro::vulkan->lock_queue(Libretro::vulkan->handle);
	VkResult res = vkQueueWaitIdle_org(queue);
	Libretro::vulkan->unlock_queue(Libretro::vulkan->handle);
	return res;
}

VKAPI_ATTR void VKAPI_CALL CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers)
{
	VkImageMemoryBarrier *barriers = (VkImageMemoryBarrier *)pImageMemoryBarriers;
	for (int i = 0; i < imageMemoryBarrierCount; i++)
	{
		if (pImageMemoryBarriers[i].oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
		{
			barriers[i].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barriers[i].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		}
		if (pImageMemoryBarriers[i].newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
		{
			barriers[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		}
	}
	return vkCmdPipelineBarrier_org(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, barriers);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass)
{
	if (pCreateInfo->pAttachments[0].finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
		((VkAttachmentDescription *)pCreateInfo->pAttachments)[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	return vkCreateRenderPass_org(device, pCreateInfo, pAllocator, pRenderPass);
}

static bool create_device(retro_vulkan_context *context, VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr, const char **required_device_extensions, unsigned num_required_device_extensions, const char **required_device_layers, unsigned num_required_device_layers, const VkPhysicalDeviceFeatures *required_features)
{
	assert(surface);

	vk_init_info.instance = instance;
	vk_init_info.gpu = gpu;
	vk_init_info.surface = surface;
	vk_init_info.get_instance_proc_addr = get_instance_proc_addr;
	vk_init_info.required_device_extensions = required_device_extensions;
	vk_init_info.num_required_device_extensions = num_required_device_extensions;
	vk_init_info.required_device_layers = required_device_layers;
	vk_init_info.num_required_device_layers = num_required_device_layers;
	vk_init_info.required_features = required_features;

	init_glslang();

	vk = new VulkanContext;
	if (!vk->InitError().empty())
	{
		ERROR_LOG(G3D, vk->InitError().c_str());
		return false;
	}

	vkCreateInstance = CreateInstance;
	vk->CreateInstance({});

	vkDestroyInstance = DestroyInstance;
	vkCreateDevice_org = vkCreateDevice;
	vkCreateDevice = CreateDevice;
	vkDestroyDevice = DestroyDevice;

	int physical_device = 0;
	while (gpu && vk->GetPhysicalDevice(physical_device) != gpu)
		physical_device++;

	if (!gpu)
		physical_device = vk->GetBestPhysicalDevice();

	vk->ChooseDevice(physical_device);
	vk->CreateDevice();
#if 1
	vk->InitSurface(WINDOWSYSTEM_LIBRETRO, surface, nullptr);
#else
#ifdef _WIN32
	vkCreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)CreateSurfaceKHR;
	vk->InitSurface(WINDOWSYSTEM_WIN32, nullptr, nullptr);
#endif
#ifdef __ANDROID__
	vkCreateAndroidSurfaceKHR = (PFN_vkCreateAndroidSurfaceKHR)CreateSurfaceKHR;
	vk->InitSurface(WINDOWSYSTEM_ANDROID, nullptr, nullptr);
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
	vkCreateXlibSurfaceKHR = (PFN_vkCreateXlibSurfaceKHR)CreateSurfaceKHR;
	vk->InitSurface(WINDOWSYSTEM_XLIB, nullptr, nullptr);
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
	vkCreateXcbSurfaceKHR = (PFN_vkCreateXcbSurfaceKHR)CreateSurfaceKHR;
	vk->InitSurface(WINDOWSYSTEM_XCB, nullptr, nullptr);
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	vkCreateWaylandSurfaceKHR = (PFN_vkCreateWaylandSurfaceKHR)CreateSurfaceKHR;
	vk->InitSurface(WINDOWSYSTEM_WAYLAND, nullptr, nullptr);
#endif
#endif
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR_org = vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR = GetPhysicalDeviceSurfaceCapabilitiesKHR;
	vkDestroySurfaceKHR = DestroySurfaceKHR;
	vkCreateSwapchainKHR = CreateSwapchainKHR;
	vkGetSwapchainImagesKHR = GetSwapchainImagesKHR;
	vkAcquireNextImageKHR = AcquireNextImageKHR;
	vkQueuePresentKHR = QueuePresentKHR;
	vkDestroySwapchainKHR = DestroySwapchainKHR;

	if (!vk->InitQueue())
		return false;

	vkQueueSubmit_org = vkQueueSubmit;
	vkQueueSubmit = QueueSubmit;

	vkQueueWaitIdle_org = vkQueueWaitIdle;
	vkQueueWaitIdle = QueueWaitIdle;

	vkCmdPipelineBarrier_org = vkCmdPipelineBarrier;
	vkCmdPipelineBarrier = CmdPipelineBarrier;

	vkCreateRenderPass_org = vkCreateRenderPass;
	vkCreateRenderPass = CreateRenderPass;

	context->gpu = vk->GetPhysicalDevice(physical_device);
	context->device = vk->GetDevice();
	context->queue = vk->GetGraphicsQueue();
	context->queue_family_index = vk->GetGraphicsQueueFamilyIndex();
	context->presentation_queue = context->queue;
	context->presentation_queue_family_index = context->queue_family_index;
#ifdef _DEBUG
	fflush(stdout);
#endif
	return true;
}

static void destroy_device(void)
{
	if (!vk)
		return;

	vk->WaitUntilQueueIdle();

	vk->DestroyObjects();
	vk->DestroyDevice();
	vk->DestroyInstance();
	delete vk;
	vk = nullptr;

	finalize_glslang();
}

static void context_reset_vulkan(void)
{
	INFO_LOG(G3D, "Context reset");
	assert(!Libretro::ctx->GetDrawContext());

	if (!Libretro::environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, (void **)&Libretro::vulkan) || !Libretro::vulkan)
	{
		ERROR_LOG(G3D, "Failed to get HW rendering interface!\n");
		return;
	}
	if (Libretro::vulkan->interface_version != RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION)
	{
		ERROR_LOG(G3D, "HW render interface mismatch, expected %u, got %u!\n", RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION, Libretro::vulkan->interface_version);
		Libretro::vulkan = NULL;
		return;
	}

	vk->ReinitSurface(PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);

	if (!vk->InitSwapchain())
		return;

	Libretro::ctx->CreateDrawContext();
	PSP_CoreParameter().thin3d = Libretro::ctx->GetDrawContext();
	Libretro::ctx->GetDrawContext()->HandleEvent(Draw::Event::GOT_BACKBUFFER, vk->GetBackbufferWidth(), vk->GetBackbufferHeight());

	if (gpu)
		gpu->DeviceRestore();
	else
		GPU_Init(Libretro::ctx, Libretro::ctx->GetDrawContext());
}

static void context_destroy_vulkan(void)
{
	LibretroHWRenderContext::context_destroy();

	Libretro::ctx->DestroyDrawContext();
	PSP_CoreParameter().thin3d = nullptr;

	// temporary workaround, destroy_device is currently being called too late/never
	destroy_device();
}

static const VkApplicationInfo *GetApplicationInfo(void)
{
	static VkApplicationInfo app_info{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app_info.pApplicationName = "PPSSPP";
	app_info.applicationVersion = Version(PPSSPP_GIT_VERSION).ToInteger();
	app_info.pEngineName = "PPSSPP";
	app_info.engineVersion = 2;
	app_info.apiVersion = VK_API_VERSION_1_0;
	return &app_info;
}

bool LibretroVulkanContext::Init()
{
	hw_render_.context_reset = context_reset_vulkan;
	hw_render_.context_destroy = context_destroy_vulkan;

	if (!LibretroHWRenderContext::Init())
		return false;

	static const struct retro_hw_render_context_negotiation_interface_vulkan iface = { RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN, RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION, GetApplicationInfo, create_device, nullptr };
	Libretro::environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, (void *)&iface);

	return true;
}
void LibretroVulkanContext::Shutdown()
{
	destroy_device();
	Libretro::vulkan = nullptr;
}

void *LibretroVulkanContext::GetAPIContext()
{
	return vk;
}

void LibretroVulkanContext::CreateDrawContext()
{
	draw_ = Draw::T3DCreateVulkanContext(vk, false);
	draw_->CreatePresets();
}
