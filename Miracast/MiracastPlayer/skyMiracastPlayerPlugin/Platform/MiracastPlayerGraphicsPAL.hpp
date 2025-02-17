/*
	Copyright (C) 2021 Apple Inc. All Rights Reserved. Not to be used or disclosed without permission from Apple.
*/

#ifndef MiracastPlayerGraphicsPAL_hpp
#define MiracastPlayerGraphicsPAL_hpp

namespace MiracastPlayer {
namespace Graphics {

//The Graphics Delegate is used by the MiracastPlayer application to coordinate drawing with the platform.
class Delegate {
public:
	// Setup OpenGL. E.g. initialize framebuffers/renderbuffers. Return false upon error.
	virtual bool initialize() = 0;
	
	// Called immediately prior to MiracastPlayer drawing. Can be used for debugging, performance measurement, OpenGL state management, etc.
	virtual void preFrameHook() = 0;
	
	// Called after MiracastPlayer has finished drawing. If flush is true, a frame was drawn to the back buffer and should be swapped to the active buffer.
	virtual void postFrameHook(bool flush) = 0;
	
	// Called when the application is shut down and no longer drawing to the screen. The platform should release graphics resources here.
	virtual void teardown() = 0;
};
}
}

#endif /* MiracastPlayerGraphicsPAL_hpp */
