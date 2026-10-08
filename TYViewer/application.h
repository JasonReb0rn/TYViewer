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

#include "graphics/grass_cards.h"
#include "graphics/water_reflection.h"
#include "graphics/mesh.h"
#include "graphics/texture.h"
#include "graphics/text.h"

#include "grid.h"

#include "config.h"
#include "critters/critter_field.h"
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
	bool drawGrass = true;
	// Planar water reflections. R toggles this, matching ReflectionDetail = off.
	bool waterReflections = true;
	WaterReflection waterReflection;

	bool wireframe = false;
	bool viewingLevel = false;
	bool collisionMeshesVisible = true;
	// Companion `{id}ex.lv2` objects. Per-object `visible` is unchanged by this.
	bool showLevelExtras = true;

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
	// Mesh part shared by every instance that places it, to the levelObjects indices
	// that place it. Rebuilt once when a level loads (rebuildPropBatches). Visibility
	// and frustum culling are applied per instance, per frame, in Application::render.
	std::unordered_map<Mesh*, std::vector<int>> propMeshInstances;
	// Blade cards from the room meshes' grass materials. Rebuilt per TY1 level.
	GrassCards grassCards;
	// Reused every frame for one batch's world matrices, so a 2000+ object level
	// doesn't reallocate a vector per mesh part per frame.
	std::vector<glm::mat4> scratchInstanceMatrices;
	int selectedLevelObject = -1;
	// Animation clock. Half of real time: TY1 presents at 30 Hz (lockTo30).
	float ty1AnimTime = 0.0f;
	// Critter fields for the open TY1 level. Their placeholder instances are not batched.
	CritterSystem critters;
	// Unskinned critter meshes to this frame's world matrices. Cleared per pass.
	std::unordered_map<Mesh*, std::vector<glm::mat4>> critterBatches;
	// One unit quad per frame of a critter sprite sheet, keyed by texture name.
	std::unordered_map<std::string, std::vector<std::unique_ptr<Mesh>>> critterSpriteFrames;
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
	// Drawn when the instance is visible, and not a companion object while extras are hidden.
	bool levelInstanceShown(const Ty1Instance& instance) const;
	void drawSelectedObjectOutline(Shader& shader, const Ty1Instance& instance);
	void refreshCollisionToggle();
	void refreshCrittersToggle();
	const std::vector<std::unique_ptr<Mesh>>& critterSpriteFramesFor(const CritterSpecies& species);
	// Each critter in `field` with its model and world matrix between the last two ticks.
	void forEachCritterDraw(CritterField& field, const std::function<void(Model&, const glm::mat4&, Critter&)>& visit) const;
	void capturePartDefaults();
	void syncCollisionVisibility();
	// Groups levelObjects by the Mesh parts they place, for instanced batch drawing.
	void rebuildPropBatches();
	// Fills in Ty1Instance::worldAabbMin/Max/hasAabb for every placed instance.
	void computeInstanceWorldBounds();

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