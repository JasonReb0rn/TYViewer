#pragma once

#include <glad/glad.h>

// Two planar reflection targets, matching ReflectionDetail = high (1024, two planes).
// Depth is reversed float Z with no stencil: clear 0, test GL_GEQUAL.
class WaterReflection
{
public:
	static const int kSize = 1024;
	static const int kMaxPlanes = 2;

	~WaterReflection();

	// Binds the plane's target and sets the viewport. Does not clear. The caller passes
	// the framebuffer and viewport to restore; begin does not query GL. False when the
	// target could not be created.
	bool begin(int plane, unsigned savedFbo, int viewportX, int viewportY, int viewportW, int viewportH);
	// Clears colour and depth. A scissor set before this limits the clear to that rect.
	void clear();
	// Later draws and clear land in this pixel rect.
	void setDrawRect(int x, int y, int width, int height);
	// Restores the framebuffer and viewport passed to begin, and turns scissor off.
	void end();
	unsigned colorTexture(int plane) const;

private:
	struct Target
	{
		GLuint fbo = 0;
		GLuint color = 0;
		GLuint depth = 0;
	};

	bool ensure();

	Target m_targets[kMaxPlanes];
	bool m_failed = false;
	GLint m_savedViewport[4] = { 0, 0, 1, 1 };
	GLint m_savedFbo = 0;
};
