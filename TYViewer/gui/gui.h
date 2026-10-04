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

enum class EntryKind
{
	Model,
	Level
};

struct ModelEntry
{
	std::string name;
	std::string archiveName; // "TY1" or "TY2"
	int archiveIndex = 0; // 0 for TY1, 1 for TY2
	EntryKind kind = EntryKind::Model;
};

class Gui
{
public:
	Gui();
	~Gui();

	void initialize(int windowWidth, int windowHeight);
	void render();
	void resize(int width, int height);

	// ---------------------------------------------------------------------
	// Simple notifications (header banner)
	// ---------------------------------------------------------------------
	enum class NotificationKind
	{
		Info,
		Success,
		Error
	};

	// Shows a temporary banner to the right of the export buttons.
	// Duration is in seconds. Call again to replace the current banner.
	void showNotification(const std::string& message,
	                      NotificationKind kind = NotificationKind::Info,
	                      float durationSeconds = 3.0f);
	
	// Model selection
	void setModelList(const std::vector<ModelEntry>& models);
	void setOnModelSelected(std::function<void(const ModelEntry&)> callback);
	void setOnExportRequested(std::function<void()> callback);
	void setOnExportRawRequested(std::function<void()> callback);
	void setOnRecenterCamera(std::function<void()> callback);
	void setOnCollisionToggle(std::function<void()> callback);
	// available: the scene has collision meshes. visible: those meshes are drawn.
	void setCollisionToggle(bool available, bool visible);
	
	// Model debugging
	void setCurrentModel(class Model* model, const std::string& modelName);
	void clearCurrentModel();
	// Level (or other multi-mesh) view: names the button without enabling model export.
	// canRecenter turns the recenter button on when room meshes are in the scene.
	void setSceneLabel(const std::string& name, bool canRecenter);
	
	// Input handling
	void onMouseButton(int button, int action, float x, float y);
	void onMouseMove(float x, float y);
	void onScroll(float yoffset);
	void onKeyPress(int key);
	void onChar(unsigned int codepoint);

	bool isInteracting() const { return dropdownOpen || hovering || activeSearchCategory >= 0; }
	// True when the GUI expects typed characters (e.g., search box focused)
	bool isTextInputActive() const { return activeSearchCategory >= 0; }

private:
	// ---------------------------------------------------------------------
	// "Mesh parts" (submesh/component) list layout constants
	// ---------------------------------------------------------------------
	static constexpr float kMeshPartHeaderHeight = 62.0f;
	static constexpr float kMeshPartItemHeight = 34.0f;    // two-line entry

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
	void renderExportRawButton();
	void renderRecenterButton();
	void renderCollisionButton();
	void renderNotificationBanner();
	void renderScrollbar();
	void renderModelInfo();
	void renderMaterialList();

	// Search/filtering helpers (submenu)
	void markFilterDirty(int category);
	void rebuildFilteredIndicesIfNeeded(int category);
	void updateSubmenuScrollBounds();
	void layoutDropdown();
	void hoverCategory(int index);
	bool hasCategory(int index) const;
	static bool containsCaseInsensitive(const std::string& haystack, const std::string& needle);
	static char normalizeSearchChar(char c);
	
	void drawRect(float x, float y, float width, float height, const glm::vec4& color);
	void drawText(const std::string& text, float x, float y, const glm::vec4& color);
	
	int windowWidth;
	int windowHeight;
	
	GuiRect buttonRect;
	GuiRect exportButtonRect;
	GuiRect exportRawButtonRect;
	GuiRect recenterButtonRect;
	GuiRect collisionButtonRect;
	GuiRect notificationRect;
	GuiRect dropdownRect;
	GuiRect submenuRect;
	GuiRect submenuSearchRect;
	GuiRect modelInfoRect;
	GuiRect materialListRect;
	
	static constexpr int kCategoryCount = 4;

	struct Category
	{
		std::string label;
		glm::vec4 hoverColor = glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
		std::vector<ModelEntry> entries;
		std::string search;
		bool filterDirty = true;
		std::vector<int> filteredIndices;
	};

	std::vector<ModelEntry> models;
	Category categories[kCategoryCount];
	
	ModelEntry* selectedModel;
	std::string currentModelName;
	bool sceneLoaded = false;
	
	// Current model for debugging
	class Model* currentModel;
	float materialListScroll;
	float maxMaterialListScroll;
	int hoveredMaterialItem;
	
	bool dropdownOpen;
	bool hovering;
	bool submenuOpen;
	int hoveredCategory; // -1 = none, otherwise index into categories
	int hoveredSubmenuItem; // Index in filtered submenu list, -1 if none

	// Search bar state. -1 = none, otherwise index into categories.
	int activeSearchCategory;
	
	float mouseX, mouseY; // Track current mouse position
	
	float scrollOffset;
	float maxScroll;
	
	std::function<void(const ModelEntry&)> onModelSelected;
	std::function<void()> onExportRequested;
	std::function<void()> onExportRawRequested;
	std::function<void()> onRecenterCamera;
	std::function<void()> onCollisionToggle;
	bool collisionAvailable = false;
	bool collisionVisible = false;

	// Notification state
	bool notificationActive = false;
	NotificationKind notificationKind = NotificationKind::Info;
	std::string notificationText;
	float notificationTimeRemaining = 0.0f;
	double lastRenderTimeSeconds = 0.0;
	
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
