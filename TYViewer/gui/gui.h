#pragma once

#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Forward declarations
class Model;
class Mesh;

struct GuiRect
{
	float x, y, width, height;
	
	bool contains(float px, float py) const
	{
		return px >= x && px <= x + width && py >= y && py <= y + height;
	}
};

// Colour of an OBJECTS kind chip. Viewport guides use the same value.
glm::vec4 objectKindColour(const std::string& kindLabel);

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

struct LevelObjectItem
{
	std::string typeName;
	std::string modelFile;
	bool visible = true;
	// Visibility from the level file, before show-all or hide-all.
	bool defaultVisible = true;
	// Empty for a prop. Otherwise critter, water, trigger, sound, patrol, or range.
	std::string kindLabel;
	// Name from `ID = number,label` when it is not blank or none.
	std::string idLabel;
};

// One inspector row. `link` selects another object, or -1.
struct ObjectInfoLine
{
	std::string text;
	int link = -1;
	bool dim = false;
	bool heading = false;
	int indent = 0;
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
	void setOnBoundsToggle(std::function<void()> callback);
	// visible: mesh bounding boxes are drawn in the viewport.
	void setBoundsVisible(bool visible);
	void setOnCrittersToggle(std::function<void()> callback);
	// available: the level has critter fields. playing: they are moving.
	void setCrittersToggle(bool available, bool playing);
	
	// Model debugging
	void setCurrentModel(class Model* model, const std::string& modelName);
	void clearCurrentModel();
	// Level view: one row per mesh across every loaded room model.
	void setLevelModels(const std::vector<class Model*>& models, const std::string& name);
	// Placed objects for the current level. Separate from the room-mesh list.
	void setLevelObjects(const std::vector<LevelObjectItem>& objects);
	void setObjectInfo(std::vector<ObjectInfoLine> lines);
	void setOnLevelObjectToggled(std::function<void(int index, bool visible)> callback);
	// Level-part show/hide changed mesh visibility. The app refreshes the collision toggle.
	void setOnPartVisibilityChanged(std::function<void()> callback);
	void setOnLevelObjectSelected(std::function<void(int index)> callback);
	void setOnLevelObjectFocused(std::function<void(int index)> callback);
	// Level (or other multi-mesh) view: names the button without enabling model export.
	// canRecenter turns the recenter button on when room meshes are in the scene.
	void setSceneLabel(const std::string& name, bool canRecenter);
	// Scene-pass counters for the current frame (Application::render). Shown in a
	// bar across the bottom of the screen, independent of the OBJECTS/LEVEL PARTS
	// panels (draws counts level geometry too, not just placed objects).
	void setRenderStats(int drawCalls, int instancedBatches, int instancesVisited, int instancesCulled,
		int partsVisited, int partsCulled, long long trianglesDrawn, float sceneMs,
		int simTicks, float simMs);
	
	// Input handling
	void onMouseButton(int button, int action, float x, float y);
	void onMouseMove(float x, float y);
	void onScroll(float yoffset);
	void onKeyPress(int key);
	void onChar(unsigned int codepoint);

	bool isInteracting() const
	{
		return dropdownOpen || hovering || activeSearchCategory >= 0 || materialSearchActive
			|| objectSearchActive
			|| materialListRect.contains(mouseX, mouseY)
			|| (!levelObjectItems.empty() && (objectListRect.contains(mouseX, mouseY)
				|| objectInfoRect.contains(mouseX, mouseY)));
	}
	// True when the GUI expects typed characters (e.g., search box focused)
	bool isTextInputActive() const { return activeSearchCategory >= 0 || materialSearchActive || objectSearchActive; }

private:
	// ---------------------------------------------------------------------
	// "Mesh parts" (submesh/component) list layout constants
	// ---------------------------------------------------------------------
	static constexpr float kMeshPartHeaderHeight = 88.0f;
	static constexpr float kMeshPartItemHeight = 34.0f;    // two-line entry
	// OBJECTS header is taller than LEVEL PARTS because of the kind filter.
	static constexpr float kObjectListHeaderHeight = 130.0f;
	static constexpr float kCollapsedListHeight = 28.0f;
	static constexpr float kObjectColumnWidth = 420.0f;
	static constexpr float kObjectColumnTop = 180.0f;
	static constexpr float kInfoHeaderHeight = 22.0f;
	static constexpr float kInfoLineHeight = 12.0f;
	// Height of the bottom scene stats bar (Gui::renderSceneStatsBar), always drawn
	// once a level has rendered a frame. Layouts in the OBJECTS column reserve this
	// much space (plus a gap) above it so the INFO panel never sits behind it.
	static constexpr float kSceneStatsBarHeight = 24.0f;

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
	void renderBoundsButton();
	void renderCrittersButton();
	void renderNotificationBanner();
	void renderScrollbar();
	void renderModelInfo();
	void renderMaterialList();
	void renderObjectList();
	void renderObjectInfo();
	bool hasMaterialPanel() const;
	int materialMeshCount() const;
	class Mesh* materialMeshAt(int flatIndex);
	void rebuildMaterialFilter();
	void layoutMaterialList();
	GuiRect materialSearchRect() const;
	void clearObjectList();
	void rebuildObjectFilter();
	void layoutObjectList();
	GuiRect objectSearchRect() const;
	void setObjectVisible(int index, bool visible);
	void selectLevelObject(int index);
	void revealLevelObject(int index);
	void rebuildInfoDrawLines();
	void drawCollapseButton(const GuiRect& rect, bool collapsed);
	void drawTextButton(const GuiRect& rect, const char* label, const glm::vec4& fill, const glm::vec4& hoverFill);
	GuiRect objectKindChipRect(int index) const;

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
	GuiRect boundsButtonRect;
	GuiRect crittersButtonRect;
	GuiRect notificationRect;
	GuiRect dropdownRect;
	GuiRect submenuRect;
	GuiRect submenuSearchRect;
	GuiRect modelInfoRect;
	GuiRect materialListRect;
	GuiRect objectListRect;
	GuiRect objectInfoRect;
	
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
	
	// Current model for debugging. A level uses levelModels instead.
	class Model* currentModel;
	std::vector<class Model*> levelModels;
	std::string materialSearch;
	bool materialSearchActive = false;
	bool materialFilterDirty = true;
	std::vector<int> materialFiltered;
	float materialListScroll;
	float maxMaterialListScroll;
	int hoveredMaterialItem;

	std::vector<LevelObjectItem> levelObjectItems;
	std::string objectSearch;
	bool objectSearchActive = false;
	bool objectFilterDirty = true;
	// Zero shows every kind. Otherwise bits for model, critter, water, trigger, sound, patrol, range.
	unsigned int objectKindFilter = 0;
	bool objectListCollapsed = false;
	bool materialListCollapsed = false;
	std::vector<int> objectFiltered;
	float objectListScroll = 0.0f;
	float maxObjectListScroll = 0.0f;
	int hoveredObjectItem = -1;
	int selectedObjectIndex = -1;
	int lastObjectClickIndex = -1;
	double lastObjectClickTime = 0.0;
	std::function<void(int index, bool visible)> onLevelObjectToggled;
	std::function<void()> onPartVisibilityChanged;
	std::function<void(int index)> onLevelObjectSelected;
	std::function<void(int index)> onLevelObjectFocused;

	std::vector<ObjectInfoLine> objectInfoLines;
	struct InfoDrawLine
	{
		std::string text;
		int link = -1;
		bool dim = false;
		bool heading = false;
		float indent = 0.0f;
	};
	std::vector<InfoDrawLine> infoDrawLines;
	float objectInfoScroll = 0.0f;
	float maxObjectInfoScroll = 0.0f;
	int hoveredInfoRow = -1;

	// Last scene-pass counters from Application::render. statsValid stays false
	// until the first frame after a level loads reports in.
	bool statsValid = false;
	int statDrawCalls = 0;
	int statInstancedBatches = 0;
	int statInstancesVisited = 0;
	int statInstancesCulled = 0;
	int statPartsVisited = 0;
	int statPartsCulled = 0;
	long long statTrianglesDrawn = 0;
	float statSceneMs = 0.0f;
	int statSimTicks = 0;
	float statSimMs = 0.0f;
	void renderSceneStatsBar();
	
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
	std::function<void()> onBoundsToggle;
	bool boundsVisible = false;
	std::function<void()> onCrittersToggle;
	bool crittersAvailable = false;
	bool crittersPlaying = true;

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
