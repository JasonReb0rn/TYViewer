#pragma once

#include <glad/glad.h>

// Two planar reflection targets, matching ReflectionDetail = high (1024, two planes).
// Depth matches the world target: reversed float Z, clear 0, test GL_GEQUAL.
class WaterReflection
{
public:
	static const int kSize = 1024;
	static const int kMaxPlanes = 2;

	~WaterReflection();

	// Binds the plane's target, sets the viewport, and clears to transparent black.
	// False when the target could not be created.
	bool begin(int plane);
	// Restores the framebuffer and viewport saved by begin.
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
