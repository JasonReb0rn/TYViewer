#pragma once

#include <string>
#include <iostream>
#include <unordered_map>

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
#include "loader/ty1_level.h"

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
	void loadTy1Level(const std::string& levelName);
	void inspectTy2Level(const std::string& levelName);
	void clearModels();
	void exportCurrentModel();
	void exportCurrentModelRaw();
	void frameCameraOnModel(const Model* model);
	void frameCameraOnLoadedModels();
	void setCollisionMeshesVisible(bool visible);
	
	// Input forwarding to GUI
	void onMouseButton(int button, int action, double x, double y);
	void onMouseMove(double x, double y);
	void onScroll(double xoffset, double yoffset);
	void onKeyPress(int key);
	void onChar(unsigned int codepoint);

private:
	bool drawGrid = true;
	bool drawBounds = false;
	bool drawColliders = true;
	bool drawBones = true;
	bool drawVertexIds = false;

	bool wireframe = false;
	bool viewingLevel = false;
	bool collisionMeshesVisible = true;

	GLFWwindow* window;

	Renderer renderer;
	Camera camera;

	Content content;

	Shader* shader;
	Shader* basic;

	Grid* grid;

	std::vector<Model*> models;
	std::vector<Ty1Instance> levelObjects;
	// Nonzero instance ID to index in levelObjects.
	std::unordered_map<int, int> levelObjectIds;
	// Unique prop models. Not drawn at the origin; instances reference them.
	std::vector<Model*> propModels;
	int selectedLevelObject = -1;
	// Animation clock. Half of real time: TY1 presents at 30 Hz (lockTo30).
	float ty1AnimTime = 0.0f;
	std::vector<Text*> labels;
	Mesh* mesh;
	
	Gui* gui;

	// Current loaded model metadata (for exporting/tools)
	std::string currentModelName;
	int currentModelArchiveIndex = 0;

private:
	void frameCameraOnModels(const std::vector<const Model*>& list, bool levelFraming);
	void frameCameraOnInstance(int index);
	void refreshObjectInspector();
	void drawSelectedObjectOutline(Shader& shader, const Ty1Instance& instance);
	void refreshCollisionToggle();
	void capturePartDefaults();
	void syncCollisionVisibility();

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