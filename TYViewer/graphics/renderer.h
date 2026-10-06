#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "drawable.h"

class Renderer
{
public:
	Renderer();

	void initialize();

	void drawHollowBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& colour);
	void drawSphere(const glm::vec3& p, float r, const glm::vec4& colour, int quality = 16);
	void drawLineStrip(const std::vector<glm::vec3>& points, const glm::vec4& colour);

	void clear(const glm::vec4& colour = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
	// World colour and a 32-bit float depth buffer. The camera projection is reversed,
	// so this target is cleared to depth 0 and keeps the greater value.
	void beginWorldTarget(int width, int height, const glm::vec4& colour);
	// Copies the world colour onto the window. The UI draws after this.
	void presentWorldTarget();
	void draw(const Drawable& drawable, Shader& shader);
	void render(GLFWwindow* window);

private:
	void ensureWorldTarget(int width, int height);
	void destroyWorldTarget();

	unsigned int m_worldFbo = 0;
	unsigned int m_worldColor = 0;
	unsigned int m_worldDepth = 0;
	int m_worldWidth = 0;
	int m_worldHeight = 0;
	bool m_worldTargetFailed = false;
};