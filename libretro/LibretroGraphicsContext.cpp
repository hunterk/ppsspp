
#include "libretro/libretro.h"
#include "libretro/libretro_vulkan.h"
#include "libretro/LibretroGraphicsContext.h"

#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "gfx/gl_common.h"
#include "gfx_es2/gpu_features.h"

retro_hw_get_proc_address_t libretro_get_proc_address;

extern retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }

class HWRenderContext : public LibretroGraphicsContext {
	public:
	HWRenderContext(retro_hw_context_type context_type, unsigned version_major = 0, unsigned version_minor = 0)
	{
		hw_render_.context_type = context_type;
		hw_render_.version_major = version_major;
		hw_render_.version_minor = version_minor;
		hw_render_.context_reset = context_reset;
		hw_render_.context_destroy = context_destroy;
		hw_render_.depth = true;
	}
	bool Init() override { return environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render_); }
	void SetRenderTarget() override {}
	void SwapBuffers() override
	{
		if (gstate_c.skipDrawReason)
			video_cb(NULL, 0, 0, 0);
		else
			video_cb(RETRO_HW_FRAME_BUFFER_VALID, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, 0);
	}

	protected:
	retro_hw_render_callback hw_render_ = {};

	private:
	static void context_reset(void)
	{
		INFO_LOG(G3D, "Context reset");
		if (gpu)
			gpu->DeviceRestore();
	}

	static void context_destroy(void)
	{
		INFO_LOG(G3D, "Context destroy");
		if (gpu)
			gpu->DeviceLost();
	}
};

class GLContext : public HWRenderContext {
	public:
	GLContext()
		 :
#ifdef USING_GLES2
			HWRenderContext(RETRO_HW_CONTEXT_OPENGLES2)
#elif defined(HAVE_OPENGL_CORE)
			HWRenderContext(RETRO_HW_CONTEXT_OPENGL_CORE, 3, 1)
#else
			HWRenderContext(RETRO_HW_CONTEXT_OPENGL)
#endif
	{
		hw_render_.bottom_left_origin = true;
	}

   bool Init() override
	{
		if (!HWRenderContext::Init())
			return false;

		libretro_get_proc_address = hw_render_.get_proc_address;
		return true;
	}

   void Shutdown() override
	{
#if 0
      NativeShutdownGraphics();
      gl->ClearCurrent();
      gl->Shutdown();
      delete gl;
      finalize_glslang();
#endif
	}

	void InitDrawContext() override
	{
#if !defined(IOS) && !defined(USING_GLES2)
		if (glewInit() != GLEW_OK)
		{
			ERROR_LOG(G3D, "glewInit() failed.\n");
			return;
		}
#endif
		CheckGLExtensions();

		draw_ = Draw::T3DCreateGLContext();
		draw_->CreatePresets();
	}

	void SetRenderTarget() override
	{
		extern GLuint g_defaultFBO;
		g_defaultFBO = hw_render_.get_current_framebuffer();
	}

	GPUCore GetGPUCore() override { return GPUCORE_GLES; }
   const char* Ident() override { return "OpenGL"; }
};

class VKContext : public HWRenderContext {
	public:
	VKContext() : HWRenderContext(RETRO_HW_CONTEXT_VULKAN, VK_MAKE_VERSION(1, 0, 18)) {}
	bool Init() override
	{
		if (!HWRenderContext::Init())
			return false;

		static const struct retro_hw_render_context_negotiation_interface_vulkan iface = {
			RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN,
			RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION,
			GetApplicationInfo,
			NULL,
		};
		environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, (void*)&iface);

		return true;
	}

	void InitDrawContext() override
	{
#if 0
      if (!environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, (void**)&vulkan) || !vulkan)
      {
         ERROR_LOG(G3D, "Failed to get HW rendering interface!\n");
         return false;
      }

      if (vulkan->interface_version != RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION)
      {
         ERROR_LOG(G3D, "HW render interface mismatch, expected %u, got %u!\n", RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION, vulkan->interface_version);
         vulkan = NULL;
         return false;
      }
      PSP_CoreParameter().thin3d = Draw::T3DCreateVulkanContext();
      break;
		draw_->CreatePresets();
#endif
	}

	GPUCore GetGPUCore() override { return GPUCORE_VULKAN; }
   const char* Ident() override { return "Vulkan"; }

	private:
	static const VkApplicationInfo* GetApplicationInfo(void)
	{
		static const VkApplicationInfo info = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "PPSSPP", 0, "PPSSPP-vulkan", 0, 0 };
		return &info;
	}

	retro_hw_render_interface_vulkan* vulkan = nullptr;
};

#ifdef _WIN32
class D3D9Context : public HWRenderContext {
	public:
	D3D9Context() : HWRenderContext(RETRO_HW_CONTEXT_DIRECT3D, 9) {}
	bool Init() override { return false; }
#if 0
	void InitDrawContext() override
	{
		draw_ = Draw::T3DCreateDX9Context();
		draw_->CreatePresets();
	}
#endif
	GPUCore GetGPUCore() override { return GPUCORE_DIRECTX9; }
   const char* Ident() override { return "DirectX 9"; }
};

class D3D11Context : public HWRenderContext {
	public:
	D3D11Context() : HWRenderContext(RETRO_HW_CONTEXT_DIRECT3D, 11) {}
	bool Init() override { return false; }
#if 0
	void InitDrawContext() override
	{
		draw_ = Draw::T3DCreateD3D11Context();
		draw_->CreatePresets();
	}
#endif
	GPUCore GetGPUCore() override { return GPUCORE_DIRECTX11; }
   const char* Ident() override { return "DirectX 11"; }
};
#endif

class SoftwareContext : public LibretroGraphicsContext {
	public:
	SoftwareContext() {}
	bool Init() override { return true; }
	void SwapBuffers() override { video_cb(NULL, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, 0); }
	GPUCore GetGPUCore() override { return GPUCORE_SOFTWARE; }
   const char* Ident() override { return "Software"; }
};

class NullContext : public LibretroGraphicsContext {
	public:
	NullContext() {}

	bool Init() override { return true; }
	void SwapBuffers() override { video_cb(NULL, 0, 0, 0); }
	GPUCore GetGPUCore() override { return GPUCORE_NULL; }
   const char* Ident() override { return "NULL"; }
};

LibretroGraphicsContext* LibretroGraphicsContext::CreateGraphicsContext()
{
	libretro_get_proc_address = NULL;

	LibretroGraphicsContext* ctx;

	ctx = new GLContext();
	if (ctx->Init())
		return ctx;
	delete ctx;

	ctx = new VKContext();
	if (ctx->Init())
		return ctx;
	delete ctx;

#ifdef _WIN32
	ctx = new D3D11Context();
	if (ctx->Init())
		return ctx;
	delete ctx;

	ctx = new D3D9Context();
	if (ctx->Init())
		return ctx;
	delete ctx;
#endif

#if 1
	ctx = new SoftwareContext();
	if (ctx->Init())
		return ctx;
	delete ctx;
#endif

	return new NullContext();
}
