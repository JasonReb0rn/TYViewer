#pragma once

#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Forward declarations
class Model;

struct GuiRect
{
	float x, y, width, height;
	
	bool contains(float px, float py) const
	{
		return px >= x && px <= x + width && py >= y && py <= y + height;
	}
};

struct ModelEntry
{
	std::string name;
	std::string archiveName; // "TY1" or "TY2"
	int archiveIndex; // 0 for TY1, 1 for TY2
};

class Gui
{
public:
	Gui();
	~Gui();

	void initialize(int windowWidth, int windowHeight);
	void render();
	void resize(int width, int height);
	
	// Model selection
	void setModelList(const std::vector<ModelEntry>& models);
	void setOnModelSelected(std::function<void(const ModelEntry&)> callback);
	
	// Model debugging
	void setCurrentModel(class Model* model, const std::string& modelName);
	void clearCurrentModel();
	
	// Input handling
	void onMouseButton(int button, int action, float x, float y);
	void onMouseMove(float x, float y);
	void onScroll(float yoffset);
	void onKeyPress(int key);

	bool isInteracting() const { return dropdownOpen || hovering; }

private:
	void renderDropdown();
	void renderSubmenu();
	void renderButton();
	void renderScrollbar();
	void renderModelInfo();
	void renderMaterialList();
	
	void drawRect(float x, float y, float width, float height, const glm::vec4& color);
	void drawText(const std::string& text, float x, float y, const glm::vec4& color);
	
	int windowWidth;
	int windowHeight;
	
	GuiRect buttonRect;
	GuiRect dropdownRect;
	GuiRect submenuRect;
	GuiRect modelInfoRect;
	GuiRect materialListRect;
	
	std::vector<ModelEntry> models;
	std::vector<ModelEntry> ty1Models;
	std::vector<ModelEntry> ty2Models;
	
	ModelEntry* selectedModel;
	std::string currentModelName;
	
	// Current model for debugging
	class Model* currentModel;
	float materialListScroll;
	float maxMaterialListScroll;
	int hoveredMaterialItem;
	
	bool dropdownOpen;
	bool hovering;
	bool submenuOpen;
	int hoveredCategory; // 0 = none, 1 = TY1, 2 = TY2
	int hoveredSubmenuItem; // Index of hovered item in submenu, -1 if none
	
	float mouseX, mouseY; // Track current mouse position
	
	float scrollOffset;
	float maxScroll;
	
	std::function<void(const ModelEntry&)> onModelSelected;
	
	// OpenGL resources
	unsigned int shaderProgram;
	unsigned int textShaderProgram;
	unsigned int VAO, VBO;
	unsigned int textVAO, textVBO;
	unsigned int fontTexture;
	
	void initializeGL();
	void cleanupGL();
	void createFontTexture();
};
