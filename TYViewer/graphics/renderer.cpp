#include "renderer.h"

#include "debug.h"
#include "vertex.h"

#define _USE_MATH_DEFINES
#include <math.h>

Renderer::Renderer()
{}

void Renderer::initialize()
{
	glEnable(GL_DEPTH_TEST);
	// Near is the greater depth. See Camera::updateProjectionMatrix.
	glDepthFunc(GL_GEQUAL);
	glClearDepth(0.0);
	// [0, 1] clip z so the float depth target is not quantized back to 24 bits.
	if (glad_glClipControl != nullptr)
		glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::destroyWorldTarget()
{
	if (m_worldFbo != 0)
		glDeleteFramebuffers(1, &m_worldFbo);
	if (m_worldColor != 0)
		glDeleteTextures(1, &m_worldColor);
	if (m_worldDepth != 0)
		glDeleteRenderbuffers(1, &m_worldDepth);
	m_worldFbo = 0;
	m_worldColor = 0;
	m_worldDepth = 0;
	m_worldWidth = 0;
	m_worldHeight = 0;
}

void Renderer::ensureWorldTarget(int width, int height)
{
	if (m_worldTargetFailed)
		return;
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;
	if (m_worldFbo != 0 && m_worldWidth == width && m_worldHeight == height)
		return;

	destroyWorldTarget();

	glGenFramebuffers(1, &m_worldFbo);
	glGenTextures(1, &m_worldColor);
	glBindTexture(GL_TEXTURE_2D, m_worldColor);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glGenRenderbuffers(1, &m_worldDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, m_worldDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH32F_STENCIL8, width, height);

	glBindFramebuffer(GL_FRAMEBUFFER, m_worldFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_worldColor, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_worldDepth);
	const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
	glDrawBuffers(1, &drawBuffer);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		Debug::log("Float depth target is incomplete; level parts will keep fighting at distance");
		m_worldTargetFailed = true;
		destroyWorldTarget();
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (m_worldFbo != 0)
	{
		m_worldWidth = width;
		m_worldHeight = height;
	}
}

void Renderer::beginWorldTarget(int width, int height, const glm::vec4& colour)
{
	// The UI is drawn with a [-1, 1] ortho after presentWorldTarget restores it.
	if (glad_glClipControl != nullptr)
		glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
	ensureWorldTarget(width, height);
	glBindFramebuffer(GL_FRAMEBUFFER, m_worldFbo);
	glViewport(0, 0, width > 0 ? width : 1, height > 0 ? height : 1);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_GEQUAL);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(colour.x, colour.y, colour.z, 1.0f);
	glClearDepth(0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::presentWorldTarget()
{
	if (m_worldFbo == 0)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		if (glad_glClipControl != nullptr)
			glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
		return;
	}

	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_worldFbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBlitFramebuffer(0, 0, m_worldWidth, m_worldHeight, 0, 0, m_worldWidth, m_worldHeight,
		GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, m_worldWidth, m_worldHeight);
	glDrawBuffer(GL_BACK);
	if (glad_glClipControl != nullptr)
		glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
}

void Renderer::drawHollowBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& colour)
{
	std::vector<Vertex> vertices =
	{
		Vertex(glm::vec4(min.x,			min.y, min.z,			1), colour),
		Vertex(glm::vec4(min.x,			min.y, min.z + max.z,	1), colour),
		Vertex(glm::vec4(min.x + max.x, min.y, min.z + max.z,	1), colour),
		Vertex(glm::vec4(min.x + max.x, min.y, min.z,			1), colour),
											
		Vertex(glm::vec4(min.x,			min.y + max.y, min.z,			1), colour),
		Vertex(glm::vec4(min.x,			min.y + max.y, min.z + max.z,	1), colour),
		Vertex(glm::vec4(min.x + max.x, min.y + max.y, min.z + max.z,	1), colour),
		Vertex(glm::vec4(min.x + max.x, min.y + max.y, min.z,			1), colour)
	};

	std::vector<unsigned int> indices =
	{
		0, 1,
		1, 2,
		2, 3,
		3, 0,

		0, 4,
		1, 5,
		2, 6,
		3, 7,

		4, 5,
		5, 6,
		6, 7,
		7, 4
	};

	unsigned int vao, vbo, ebo;

	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &ebo);

	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, 8 * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 24 * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, position));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, normal));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, colour));

	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, texcoord));

	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, skin));

	glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);

	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &vbo);
	glDeleteBuffers(1, &ebo);
}

void Renderer::drawSphere(const glm::vec3& p, float r, const glm::vec4& colour, int quality)
{
	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;

	for (unsigned int i = 0; i < quality * 3; i++)
	{
		float f = ((float)(i % quality) / (float)quality) * M_PI * 2;

		//vertices.push_back({ glm::vec3(std::cos(f), std::sin(f), 0.0f) * r, colour });

		if (i < quality)
			vertices.push_back(Vertex(
				glm::vec4(
					p.x + std::cos(f) * r, 
					p.y + std::sin(f) * r, 
					p.z + 0.0f, 
					1.0f), 
				colour));
		else if (i >= quality && i < quality * 2)
			vertices.push_back(Vertex(
				glm::vec4(
					p.x + std::cos(f) * r, 
					p.y + 0.0f, 
					p.z + std::sin(f) * r, 
					1.0f), 
				colour));
		else
			vertices.push_back(Vertex(
				glm::vec4(
					p.x + 0.0f, 
					p.y + std::cos(f) * r, 
					p.z + std::sin(f) * r, 
					1.0f), 
				colour));


		indices.push_back(i);
		if ((i + 1) % quality == 0)
			indices.push_back((i + 1) - quality);
		else
			indices.push_back(i + 1);
	}


	unsigned int vao, vbo, ebo;

	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &ebo);

	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, position));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, normal));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, colour));

	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, texcoord));

	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, skin));

	glDrawElements(GL_LINES, indices.size(), GL_UNSIGNED_INT, 0);


	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &vbo);
	glDeleteBuffers(1, &ebo);
}

void Renderer::drawLineStrip(const std::vector<glm::vec3>& points, const glm::vec4& colour)
{
	if (points.size() < 2)
		return;

	std::vector<Vertex> vertices;
	vertices.reserve(points.size());
	for (const glm::vec3& point : points)
		vertices.push_back(Vertex(glm::vec4(point, 1.0f), colour));

	std::vector<unsigned int> indices;
	indices.reserve((points.size() - 1) * 2);
	for (unsigned int i = 0; i + 1 < points.size(); i++)
	{
		indices.push_back(i);
		indices.push_back(i + 1);
	}

	unsigned int vao = 0;
	unsigned int vbo = 0;
	unsigned int ebo = 0;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &ebo);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, position));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, normal));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, colour));
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, texcoord));
	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, skin));
	glDrawElements(GL_LINES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, 0);
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &vbo);
	glDeleteBuffers(1, &ebo);
}

void Renderer::clear(const glm::vec4& colour)
{
	glClearColor(colour.x, colour.y, colour.z, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
void Renderer::draw(const Drawable& drawable, Shader& shader)
{
	drawable.draw(shader);
}
void Renderer::render(GLFWwindow* window)
{
	glfwSwapBuffers(window);
}
