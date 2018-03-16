
#include "libretro/libretro.h"
#include "libretro/LibretroGraphicsContext.h"
#include "libretro/LibretroGLContext.h"
#ifndef NO_VULKAN
#include "libretro/LibretroVulkanContext.h"
#endif

#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "GPU/GPUInterface.h"

retro_video_refresh_t LibretroGraphicsContext::video_cb;
retro_hw_get_proc_address_t libretro_get_proc_address;

void retro_set_video_refresh(retro_video_refresh_t cb)
{
	LibretroGraphicsContext::video_cb = cb;
}

bool LibretroHWRenderContext::Init()
{
	return Libretro::environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render_);
}

void LibretroHWRenderContext::context_reset(void)
{
	INFO_LOG(G3D, "Context reset");

	if (gpu)
		gpu->DeviceRestore();
	else
	{
		if (!Libretro::ctx->GetDrawContext())
		{
			Libretro::ctx->CreateDrawContext();
			PSP_CoreParameter().thin3d = Libretro::ctx->GetDrawContext();
		}

		GPU_Init(Libretro::ctx, Libretro::ctx->GetDrawContext());
	}
}

void LibretroHWRenderContext::context_destroy(void)
{
	INFO_LOG(G3D, "Context destroy");

	if (gpu)
		gpu->DeviceLost();
}

LibretroGraphicsContext *LibretroGraphicsContext::CreateGraphicsContext()
{
	libretro_get_proc_address = NULL;

	LibretroGraphicsContext *ctx;

	ctx = new LibretroGLContext();

	if (ctx->Init())
		return ctx;

	delete ctx;

#ifndef NO_VULKAN
	ctx = new LibretroVulkanContext();

	if (ctx->Init())
		return ctx;

	delete ctx;
#endif

#ifdef _WIN32
	ctx = new LibretroD3D11Context();

	if (ctx->Init())
		return ctx;

	delete ctx;

	ctx = new LibretroD3D9Context();

	if (ctx->Init())
		return ctx;

	delete ctx;
#endif

#if 1
	ctx = new LibretroSoftwareContext();

	if (ctx->Init())
		return ctx;

	delete ctx;
#endif

	return new LibretroNullContext();
}
