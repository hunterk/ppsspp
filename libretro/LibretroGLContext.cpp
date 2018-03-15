
#include "Common/Log.h"
#include "gfx_es2/gpu_features.h"

#include "libretro/LibretroGLContext.h"

bool LibretroGLContext::Init()
{
	if (!LibretroHWRenderContext::Init())
		return false;

	libretro_get_proc_address = hw_render_.get_proc_address;
	return true;
}

void LibretroGLContext::Shutdown()
{
#if 0
   NativeShutdownGraphics();
   gl->ClearCurrent();
   gl->Shutdown();
   delete gl;
   finalize_glslang();
#endif
}

void LibretroGLContext::CreateDrawContext()
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
