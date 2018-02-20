#pragma once

#include "libretro/libretro.h"
#include "libretro/libretro_vulkan.h"
#include "Common/GraphicsContext.h"

#include "Core/System.h"

class LibretroGraphicsContext : public GraphicsContext {
	public:
	LibretroGraphicsContext() {}
	~LibretroGraphicsContext() override { delete draw_; }

	virtual bool Init() = 0;
	virtual void InitDrawContext() {}
	virtual void SetRenderTarget() {}
   virtual GPUCore GetGPUCore() = 0;
   virtual const char* Ident() = 0;

   void Shutdown() override {}
	void SwapInterval(int interval) override {}
	void Resize() override {}
	Draw::DrawContext* GetDrawContext() override { return draw_; }

	static LibretroGraphicsContext* CreateGraphicsContext();

	protected:
	Draw::DrawContext* draw_ = nullptr;
};

extern "C" retro_hw_get_proc_address_t libretro_get_proc_address;
