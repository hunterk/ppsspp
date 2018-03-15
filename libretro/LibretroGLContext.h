#pragma once

#include "gfx/gl_common.h"
#include "libretro/LibretroGraphicsContext.h"

class LibretroGLContext : public LibretroHWRenderContext {
	public:
	LibretroGLContext()
		 :
#ifdef USING_GLES2
			HWRenderContext(RETRO_HW_CONTEXT_OPENGLES2)
#elif defined(HAVE_OPENGL_CORE)
			HWRenderContext(RETRO_HW_CONTEXT_OPENGL_CORE, 3, 1)
#else
			LibretroHWRenderContext(RETRO_HW_CONTEXT_OPENGL)
#endif
	{
		hw_render_.bottom_left_origin = true;
	}

	bool Init() override;
	void Shutdown() override;
	void CreateDrawContext() override;
	void SetRenderTarget() override
	{
		extern GLuint g_defaultFBO;
		g_defaultFBO = hw_render_.get_current_framebuffer();
	}

	GPUCore GetGPUCore() override { return GPUCORE_GLES; }
	const char *Ident() override { return "OpenGL"; }
};
