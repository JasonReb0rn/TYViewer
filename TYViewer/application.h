#pragma once

#include <string>
#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "input/keyboard.h"
#include "input/mouse.h"

#include "content.h"

#include "graphics/camera.h"
#include "graphics/renderer.h"
#include "graphics/shader.h"

#include "graphics/mesh.h"
#include "graphics/texture.h"
#include "graphics/text.h"

#include "grid.h"

#include "config.h"
#include "gui/gui.h"

class Application
{
public:
	static std::string APPLICATION_PATH;
	static std::string ARCHIVE_PATH;

public:
	Application(GLFWwindow* window);

	void initialize();
	void run();
	void terminate();

	void update(float dt);
	void render(Shader& shader);

	void resize(int width, int height);
	
	// Model management
	void loadModel(const std::string& modelName, int archiveIndex);
	void clearModels();
	
	// Input forwarding to GUI
	void onMouseButton(int button, int action, double x, double y);
	void onMouseMove(double x, double y);
	void onScroll(double xoffset, double yoffset);
	void onKeyPress(int key);
	void onChar(unsigned int codepoint);

private:
	bool drawGrid = true;
	bool drawBounds = true;
	bool drawColliders = true;
	bool drawBones = true;
	bool drawVertexIds = false;

	bool wireframe = false;

	GLFWwindow* window;

	Renderer renderer;
	Camera camera;

	Content content;

	Shader* shader;
	Shader* basic;

	Grid* grid;

	std::vector<Model*> models;
	std::vector<Text*> labels;
	Mesh* mesh;
	
	Gui* gui;

private:
	// ------------------------------------------------------------------
	// Screen-space vertex index overlay ("V")
	// ------------------------------------------------------------------
	void initializeVertexIdOverlay();
	void cleanupVertexIdOverlay();
	void drawVertexIdOverlay(const glm::mat4& vpmatrix);

	unsigned int vertexOverlayTextShaderProgram = 0;
	unsigned int vertexOverlayTextVAO = 0;
	unsigned int vertexOverlayTextVBO = 0;
	unsigned int vertexOverlayFontTexture = 0;
};