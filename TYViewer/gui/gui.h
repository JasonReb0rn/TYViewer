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
	void setOnExportRequested(std::function<void()> callback);
	
	// Model debugging
	void setCurrentModel(class Model* model, const std::string& modelName);
	void clearCurrentModel();
	
	// Input handling
	void onMouseButton(int button, int action, float x, float y);
	void onMouseMove(float x, float y);
	void onScroll(float yoffset);
	void onKeyPress(int key);
	void onChar(unsigned int codepoint);

	bool isInteracting() const { return dropdownOpen || hovering || activeSearchCategory != 0; }
	// True when the GUI expects typed characters (e.g., search box focused)
	bool isTextInputActive() const { return activeSearchCategory != 0; }

private:
	// ---------------------------------------------------------------------
	// TY2 "material" name parsing (rudimentary suffix identification)
	// ---------------------------------------------------------------------
	enum MaterialNameFlags : unsigned int
	{
		MAT_NONE  = 0,
		MAT_GLASS = 1 << 0, // "...Glass" or "..._Glass"
		MAT_SPEC  = 1 << 1, // "...Spec" or "..._Spec"
		MAT_OVERLAY = 1 << 2 // "...Overlay" or "..._Overlay"
	};

	struct ParsedMaterialName
	{
		std::string baseName;      // name without suffix/variant (best-effort)
		unsigned int flags = MAT_NONE;
	};

	static ParsedMaterialName parseMaterialName(const std::string& name);

	void renderDropdown();
	void renderSubmenu();
	void renderButton();
	void renderExportButton();
	void renderScrollbar();
	void renderModelInfo();
	void renderMaterialList();

	// Search/filtering helpers (submenu)
	void markFilterDirty(int category);
	void rebuildFilteredIndicesIfNeeded(int category);
	void updateSubmenuScrollBounds();
	static bool containsCaseInsensitive(const std::string& haystack, const std::string& needle);
	static char normalizeSearchChar(char c);
	
	void drawRect(float x, float y, float width, float height, const glm::vec4& color);
	void drawText(const std::string& text, float x, float y, const glm::vec4& color);
	
	int windowWidth;
	int windowHeight;
	
	GuiRect buttonRect;
	GuiRect exportButtonRect;
	GuiRect dropdownRect;
	GuiRect submenuRect;
	GuiRect submenuSearchRect;
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
	int hoveredSubmenuItem; // Index in filtered submenu list, -1 if none

	// Search bar state (one per submenu)
	int activeSearchCategory; // 0 = none, 1 = TY1, 2 = TY2
	std::string ty1Search;
	std::string ty2Search;
	bool ty1FilterDirty;
	bool ty2FilterDirty;
	std::vector<int> ty1FilteredIndices; // indices into ty1Models
	std::vector<int> ty2FilteredIndices; // indices into ty2Models
	
	float mouseX, mouseY; // Track current mouse position
	
	float scrollOffset;
	float maxScroll;
	
	std::function<void(const ModelEntry&)> onModelSelected;
	std::function<void()> onExportRequested;
	
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
