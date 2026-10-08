#include "water_reflection.h"

#include "debug.h"

WaterReflection::~WaterReflection()
{
	for (Target& target : m_targets)
	{
		if (target.fbo != 0)
			glDeleteFramebuffers(1, &target.fbo);
		if (target.color != 0)
			glDeleteTextures(1, &target.color);
		if (target.depth != 0)
			glDeleteRenderbuffers(1, &target.depth);
		target.fbo = 0;
		target.color = 0;
		target.depth = 0;
	}
}

bool WaterReflection::ensure()
{
	if (m_failed)
		return false;
	if (m_targets[0].fbo != 0)
		return true;

	GLint previousFbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);

	for (int i = 0; i < kMaxPlanes; i++)
	{
		Target& target = m_targets[i];
		glGenFramebuffers(1, &target.fbo);
		glGenTextures(1, &target.color);
		glBindTexture(GL_TEXTURE_2D, target.color);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glGenRenderbuffers(1, &target.depth);
		glBindRenderbuffer(GL_RENDERBUFFER, target.depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH32F_STENCIL8, kSize, kSize);

		glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.color, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target.depth);
		const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
		glDrawBuffers(1, &drawBuffer);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			Debug::log("Water reflection target is incomplete");
			m_failed = true;
			glBindTexture(GL_TEXTURE_2D, 0);
			glBindRenderbuffer(GL_RENDERBUFFER, 0);
			glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
			return false;
		}
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
	return true;
}

bool WaterReflection::begin(int plane)
{
	if (plane < 0 || plane >= kMaxPlanes)
		return false;

	glGetIntegerv(GL_VIEWPORT, m_savedViewport);
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_savedFbo);
	if (!ensure())
		return false;
	glBindFramebuffer(GL_FRAMEBUFFER, m_targets[plane].fbo);
	glViewport(0, 0, kSize, kSize);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_GEQUAL);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth(0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	return true;
}

void WaterReflection::end()
{
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_savedFbo));
	glViewport(m_savedViewport[0], m_savedViewport[1], m_savedViewport[2], m_savedViewport[3]);
}

unsigned WaterReflection::colorTexture(int plane) const
{
	if (plane < 0 || plane >= kMaxPlanes)
		return 0;
	return m_targets[plane].color;
}
