#pragma once

#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
	
	void drawRect(float x, float y, float width, float height, const glm::vec4& color);
	void drawText(const std::string& text, float x, float y, const glm::vec4& color);
	
	int windowWidth;
	int windowHeight;
	
	GuiRect buttonRect;
	GuiRect dropdownRect;
	GuiRect submenuRect;
	
	std::vector<ModelEntry> models;
	std::vector<ModelEntry> ty1Models;
	std::vector<ModelEntry> ty2Models;
	
	ModelEntry* selectedModel;
	std::string currentModelName;
	
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
