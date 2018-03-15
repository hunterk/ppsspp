#pragma once

#include "libretro/libretro.h"
#include "libretro/libretro_vulkan.h"
#include "Common/GraphicsContext.h"

#include "Core/System.h"
#include "GPU/GPUState.h"

class LibretroGraphicsContext : public GraphicsContext {
	public:
	LibretroGraphicsContext() {}
	~LibretroGraphicsContext() override { DestroyDrawContext(); }

	virtual bool Init() = 0;
	virtual void SetRenderTarget() {}
	virtual GPUCore GetGPUCore() = 0;
	virtual const char *Ident() = 0;

	void Shutdown() override {}
	void SwapInterval(int interval) override {}
	void Resize() override {}

	virtual void CreateDrawContext() {}
	void DestroyDrawContext()
	{
		if (!draw_)
			return;
		draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
		delete draw_;
		draw_ = nullptr;
	}
	Draw::DrawContext *GetDrawContext() override { return draw_; }

	static LibretroGraphicsContext *CreateGraphicsContext();

	Draw::DrawContext *draw_ = nullptr;
	static retro_video_refresh_t video_cb;
};

class LibretroHWRenderContext : public LibretroGraphicsContext {
	public:
	LibretroHWRenderContext(retro_hw_context_type context_type, unsigned version_major = 0, unsigned version_minor = 0)
	{
		hw_render_.context_type = context_type;
		hw_render_.version_major = version_major;
		hw_render_.version_minor = version_minor;
		hw_render_.context_reset = context_reset;
		hw_render_.context_destroy = context_destroy;
		hw_render_.depth = true;
	}
	bool Init() override;
	void SetRenderTarget() override {}
	void SwapBuffers() override
	{
		if (gstate_c.skipDrawReason)
			video_cb(NULL, 0, 0, 0);
		else
			video_cb(RETRO_HW_FRAME_BUFFER_VALID, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, 0);
	}

	static void context_reset(void);
	static void context_destroy(void);

	protected:
	retro_hw_render_callback hw_render_ = {};
};

extern "C" retro_hw_get_proc_address_t libretro_get_proc_address;

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
	const char *Ident() override { return "DirectX 9"; }
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
	const char *Ident() override { return "DirectX 11"; }
};
#endif

class SoftwareContext : public LibretroGraphicsContext {
	public:
	SoftwareContext() {}
	bool Init() override { return true; }
	void SwapBuffers() override { video_cb(NULL, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, 0); }
	GPUCore GetGPUCore() override { return GPUCORE_SOFTWARE; }
	const char *Ident() override { return "Software"; }
};

class NullContext : public LibretroGraphicsContext {
	public:
	NullContext() {}

	bool Init() override { return true; }
	void SwapBuffers() override { video_cb(NULL, 0, 0, 0); }
	GPUCore GetGPUCore() override { return GPUCORE_NULL; }
	const char *Ident() override { return "NULL"; }
};

namespace Libretro {
extern LibretroGraphicsContext *ctx;
extern retro_environment_t environ_cb;
}   // namespace Libretro
