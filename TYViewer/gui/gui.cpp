#include "gui.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <glm/gtc/type_ptr.hpp>
#include "../debug.h"
#include "../model.h"
#include "../graphics/mesh.h"

namespace
{
	struct KindChipDef
	{
		const char* label;
		unsigned int bit;
		glm::vec4 color;
	};

	// bit 0 is the "all" chip. The other bits match objectKindFilter.
	const KindChipDef kKindChips[] =
	{
		{ "all", 0, glm::vec4(0.82f, 0.82f, 0.82f, 1.0f) },
		{ "model", 1u << 0, glm::vec4(0.65f, 0.85f, 0.70f, 1.0f) },
		{ "critter", 1u << 1, glm::vec4(1.0f, 0.55f, 0.45f, 1.0f) },
		{ "water", 1u << 2, glm::vec4(0.45f, 0.70f, 1.0f, 1.0f) },
		{ "trigger", 1u << 3, glm::vec4(1.0f, 0.85f, 0.35f, 1.0f) },
		{ "sound", 1u << 4, glm::vec4(0.40f, 0.95f, 1.0f, 1.0f) },
		{ "patrol", 1u << 5, glm::vec4(1.0f, 0.50f, 0.90f, 1.0f) },
		{ "range", 1u << 6, glm::vec4(0.50f, 1.0f, 0.55f, 1.0f) },
	};
	const int kKindChipCount = 8;

	GuiRect collapseButtonRect(const GuiRect& panel)
	{
		const float size = 16.0f;
		return { panel.x + panel.width - 8.0f - size, panel.y + 6.0f, size, size };
	}

	unsigned int kindBitForLabel(const std::string& kindLabel)
	{
		if (kindLabel == "critter") return 1u << 1;
		if (kindLabel == "water") return 1u << 2;
		if (kindLabel == "trigger") return 1u << 3;
		if (kindLabel == "sound") return 1u << 4;
		if (kindLabel == "patrol") return 1u << 5;
		if (kindLabel == "range") return 1u << 6;
		return 1u << 0;
	}

	// A row is drawn only when its whole box sits in the list body. A tall window
	// leaves a remainder under the last full row, and drawing that next row paints
	// past the panel edge.
	bool rowFits(float rowTop, float bodyTop, float bodyBottom, float boxHeight)
	{
		return rowTop >= bodyTop && rowTop + boxHeight <= bodyBottom;
	}

	struct BulkButtonRects
	{
		GuiRect showDefault;
		GuiRect showAll;
		GuiRect hideAll;
	};

	BulkButtonRects bulkButtonRects(const GuiRect& panel)
	{
		const float height = 18.0f;
		const float y = panel.y + 38.0f;
		const float gap = 4.0f;
		const float edge = 6.0f;
		const float hideWidth = 72.0f;
		const float showWidth = 72.0f;
		const float defaultWidth = 104.0f;
		float right = panel.x + panel.width - edge;
		BulkButtonRects rects;
		rects.hideAll = { right - hideWidth, y, hideWidth, height };
		right -= hideWidth + gap;
		rects.showAll = { right - showWidth, y, showWidth, height };
		right -= showWidth + gap;
		rects.showDefault = { right - defaultWidth, y, defaultWidth, height };
		return rects;
	}
}

glm::vec4 objectKindColour(const std::string& kindLabel)
{
	for (int chip = 0; chip < kKindChipCount; chip++)
	{
		if (kindLabel == kKindChips[chip].label)
			return kKindChips[chip].color;
	}
	return glm::vec4(0.82f, 0.82f, 0.82f, 1.0f);
}

// Simple vertex shader for GUI rendering
const char* guiVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
uniform mat4 projection;
void main()
{
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
}
)";

// Simple fragment shader for GUI rendering
const char* guiFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
uniform vec4 color;
void main()
{
    FragColor = color;
}
)";

// Text vertex shader
const char* textVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
uniform mat4 projection;
void main()
{
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

// Text fragment shader
const char* textFragmentShaderSource = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D fontTexture;
uniform vec4 textColor;
void main()
{
    float alpha = texture(fontTexture, TexCoord).r;
    FragColor = vec4(textColor.rgb, textColor.a * alpha);
}
)";

Gui::Gui() :
	windowWidth(1280),
	windowHeight(720),
	dropdownOpen(false),
	hovering(false),
	submenuOpen(false),
	hoveredCategory(-1),
	hoveredSubmenuItem(-1),
	activeSearchCategory(-1),
	mouseX(0.0f),
	mouseY(0.0f),
	scrollOffset(0.0f),
	maxScroll(0.0f),
	selectedModel(nullptr),
	currentModelName(""),
	currentModel(nullptr),
	materialListScroll(0.0f),
	maxMaterialListScroll(0.0f),
	hoveredMaterialItem(-1),
	shaderProgram(0),
	textShaderProgram(0),
	VAO(0),
	VBO(0),
	textVAO(0),
	textVBO(0),
	fontTexture(0)
{
	categories[0].label = "TY 1 Models";
	categories[0].hoverColor = glm::vec4(0.2f, 0.4f, 0.7f, 1.0f);
	categories[1].label = "TY 2 Models";
	categories[1].hoverColor = glm::vec4(0.7f, 0.35f, 0.2f, 1.0f);
	categories[2].label = "TY 1 Levels";
	categories[2].hoverColor = glm::vec4(0.2f, 0.55f, 0.35f, 1.0f);
	categories[3].label = "TY 2 Levels";
	categories[3].hoverColor = glm::vec4(0.55f, 0.3f, 0.7f, 1.0f);
}

Gui::~Gui()
{
	cleanupGL();
}

void Gui::showNotification(const std::string& message, NotificationKind kind, float durationSeconds)
{
	notificationText = message;
	notificationKind = kind;
	notificationTimeRemaining = (durationSeconds > 0.0f) ? durationSeconds : 0.0f;
	notificationActive = !notificationText.empty() && notificationTimeRemaining > 0.0f;
}

Gui::ParsedMaterialName Gui::parseMaterialName(const std::string& name)
{
	ParsedMaterialName out;
	out.baseName = name;
	out.flags = MAT_NONE;

	auto endsWith = [](const std::string& s, const std::string& suffix) -> bool
	{
		return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
	};

	auto stripSuffix = [&](const std::string& suffix)
	{
		if (endsWith(out.baseName, suffix))
		{
			out.baseName.erase(out.baseName.size() - suffix.size());
		}
	};

	// Detect common render-state suffixes first.
	// NOTE: this is intentionally minimal; we don't yet decode real material state fields.
	if (endsWith(out.baseName, "Glass") || endsWith(out.baseName, "_Glass"))
	{
		out.flags |= MAT_GLASS;
		stripSuffix("Glass");
		stripSuffix("_Glass");
	}

	if (endsWith(out.baseName, "Spec") || endsWith(out.baseName, "_Spec"))
	{
		out.flags |= MAT_SPEC;
		stripSuffix("Spec");
		stripSuffix("_Spec");
	}

	if (endsWith(out.baseName, "Overlay") || endsWith(out.baseName, "_Overlay"))
	{
		out.flags |= MAT_OVERLAY;
		stripSuffix("Overlay");
		stripSuffix("_Overlay");
	}

	// Trim any leftover trailing separators
	while (!out.baseName.empty() && (out.baseName.back() == '_' || out.baseName.back() == '-'))
	{
		out.baseName.pop_back();
	}

	return out;
}

void Gui::initialize(int width, int height)
{
	windowWidth = width;
	windowHeight = height;
	
	// Button at top of screen
	buttonRect = {10.0f, 10.0f, 200.0f, 30.0f};
	// Export buttons next to the model selector
	exportButtonRect = {buttonRect.x + buttonRect.width + 10.0f, 10.0f, 90.0f, 30.0f};
	exportRawButtonRect = {exportButtonRect.x + exportButtonRect.width + 10.0f, 10.0f, 110.0f, 30.0f};
	recenterButtonRect = {exportRawButtonRect.x + exportRawButtonRect.width + 10.0f, 10.0f, 30.0f, 30.0f};
	collisionButtonRect = {recenterButtonRect.x + recenterButtonRect.width + 10.0f, 10.0f, 96.0f, 30.0f};
	boundsButtonRect = {collisionButtonRect.x + collisionButtonRect.width + 10.0f, 10.0f, 144.0f, 30.0f};
	
	// Model info panel on the right
	modelInfoRect = {(float)width - 310.0f, 10.0f, 300.0f, 150.0f};
	
	// Material list panel below model info (default size, will resize when model is loaded)
	materialListRect = {(float)width - 310.0f, 170.0f, 300.0f, 200.0f};
	objectListRect = { 10.0f, kObjectColumnTop, kObjectColumnWidth, 200.0f };
	objectInfoRect = { 10.0f, 388.0f, kObjectColumnWidth, 220.0f };
	
	initializeGL();
}

void Gui::initializeGL()
{
	// Create rectangle shader program
	unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &guiVertexShaderSource, NULL);
	glCompileShader(vertexShader);
	
	unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &guiFragmentShaderSource, NULL);
	glCompileShader(fragmentShader);
	
	shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);
	
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
	
	// Create text shader program
	unsigned int textVertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(textVertexShader, 1, &textVertexShaderSource, NULL);
	glCompileShader(textVertexShader);
	
	unsigned int textFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(textFragmentShader, 1, &textFragmentShaderSource, NULL);
	glCompileShader(textFragmentShader);
	
	textShaderProgram = glCreateProgram();
	glAttachShader(textShaderProgram, textVertexShader);
	glAttachShader(textShaderProgram, textFragmentShader);
	glLinkProgram(textShaderProgram);
	
	glDeleteShader(textVertexShader);
	glDeleteShader(textFragmentShader);
	
	// Create VAO/VBO for rectangles
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	
	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 12, nullptr, GL_DYNAMIC_DRAW);
	
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	
	glBindVertexArray(0);
	
	// Create VAO/VBO for text rendering
	glGenVertexArrays(1, &textVAO);
	glGenBuffers(1, &textVBO);
	
	glBindVertexArray(textVAO);
	glBindBuffer(GL_ARRAY_BUFFER, textVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
	
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
	
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
	
	glBindVertexArray(0);
	
	// Create font texture
	createFontTexture();
}

void Gui::cleanupGL()
{
	if (VAO) glDeleteVertexArrays(1, &VAO);
	if (VBO) glDeleteBuffers(1, &VBO);
	if (textVAO) glDeleteVertexArrays(1, &textVAO);
	if (textVBO) glDeleteBuffers(1, &textVBO);
	if (shaderProgram) glDeleteProgram(shaderProgram);
	if (textShaderProgram) glDeleteProgram(textShaderProgram);
	if (fontTexture) glDeleteTextures(1, &fontTexture);
}

void Gui::createFontTexture()
{
	// Simple 8x8 bitmap font (ASCII 32-127)
	// Each character is 8x8 pixels, stored as bits
	// This is a minimal readable font
	const int charWidth = 8;
	const int charHeight = 8;
	const int charsPerRow = 16;
	const int numChars = 96; // ASCII 32-127
	
	// Font data - simple readable pixel font
	// Format: 8 bytes per character (each byte is a row of 8 pixels)
	unsigned char fontData[96][8] = {
		// Space (32)
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		// ! (33)
		{0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00},
		// " (34)
		{0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
		// # (35)
		{0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},
		// $ (36)
		{0x0C, 0x3F, 0x68, 0x3E, 0x0B, 0x7E, 0x18, 0x00},
		// % (37)
		{0x60, 0x66, 0x0C, 0x18, 0x30, 0x66, 0x06, 0x00},
		// & (38)
		{0x38, 0x6C, 0x6C, 0x38, 0x6D, 0x66, 0x3B, 0x00},
		// ' (39)
		{0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
		// ( (40)
		{0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00},
		// ) (41)
		{0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00},
		// * (42)
		{0x00, 0x18, 0x7E, 0x3C, 0x7E, 0x18, 0x00, 0x00},
		// + (43)
		{0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00},
		// , (44)
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30},
		// - (45)
		{0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00},
		// . (46)
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00},
		// / (47)
		{0x00, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x00, 0x00},
		// 0-9 (48-57)
		{0x3C, 0x66, 0x6E, 0x7E, 0x76, 0x66, 0x3C, 0x00},
		{0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
		{0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00},
		{0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00},
		{0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00},
		{0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00},
		{0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00},
		{0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00},
		{0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00},
		{0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00},
		// : ; < = > ? @ (58-64)
		{0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00},
		{0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x30},
		{0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00},
		{0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00},
		{0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0x00},
		{0x3C, 0x66, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00},
		{0x3C, 0x66, 0x6E, 0x6A, 0x6E, 0x60, 0x3C, 0x00},
		// A-Z (65-90)
		{0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00},
		{0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00},
		{0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00},
		{0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00},
		{0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00},
		{0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00},
		{0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00},
		{0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00},
		{0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
		{0x3E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00},
		{0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00},
		{0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00},
		{0x63, 0x77, 0x7F, 0x6B, 0x6B, 0x63, 0x63, 0x00},
		{0x66, 0x66, 0x76, 0x7E, 0x6E, 0x66, 0x66, 0x00},
		{0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
		{0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00},
		{0x3C, 0x66, 0x66, 0x66, 0x6A, 0x6C, 0x36, 0x00},
		{0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0x00},
		{0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00},
		{0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
		{0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
		{0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00},
		{0x63, 0x63, 0x6B, 0x6B, 0x7F, 0x77, 0x63, 0x00},
		{0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00},
		{0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00},
		{0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00},
		// [ \ ] ^ _ ` (91-96)
		{0x7C, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7C, 0x00},
		{0x00, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x00, 0x00},
		{0x3E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x3E, 0x00},
		{0x18, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00},
		{0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00},
		// a-z (97-122)
		{0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00},
		{0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00},
		{0x00, 0x00, 0x3C, 0x66, 0x60, 0x66, 0x3C, 0x00},
		{0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00},
		{0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00},
		{0x1C, 0x36, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x00},
		{0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C},
		{0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00},
		{0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00},
		{0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38},
		{0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00},
		{0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
		{0x00, 0x00, 0x66, 0x7F, 0x7F, 0x6B, 0x6B, 0x00},
		{0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00},
		{0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00},
		{0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60},
		{0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x06},
		{0x00, 0x00, 0x6C, 0x76, 0x60, 0x60, 0x60, 0x00},
		{0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00},
		{0x30, 0x30, 0x7C, 0x30, 0x30, 0x36, 0x1C, 0x00},
		{0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00},
		{0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00},
		{0x00, 0x00, 0x63, 0x6B, 0x6B, 0x7F, 0x36, 0x00},
		{0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x00},
		{0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x3C},
		{0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0x00},
		// { | } ~ (123-126)
		{0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00},
		{0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},
		{0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00},
		{0x31, 0x6B, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00},
	};
	
	// Create texture with all characters
	const int texWidth = charsPerRow * charWidth;
	const int texHeight = ((numChars + charsPerRow - 1) / charsPerRow) * charHeight;
	
	unsigned char* texData = new unsigned char[texWidth * texHeight];
	memset(texData, 0, texWidth * texHeight);
	
	// Fill texture with font data
	for (int i = 0; i < numChars; i++)
	{
		int charX = (i % charsPerRow) * charWidth;
		int charY = (i / charsPerRow) * charHeight;
		
		for (int y = 0; y < charHeight; y++)
		{
			unsigned char row = fontData[i][y];
			for (int x = 0; x < charWidth; x++)
			{
				if (row & (0x80 >> x))
				{
					texData[(charY + y) * texWidth + (charX + x)] = 255;
				}
			}
		}
	}
	
	// Create OpenGL texture
	glGenTextures(1, &fontTexture);
	glBindTexture(GL_TEXTURE_2D, fontTexture);
	
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, texWidth, texHeight, 0, GL_RED, GL_UNSIGNED_BYTE, texData);
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	
	delete[] texData;
}

void Gui::setModelList(const std::vector<ModelEntry>& modelList)
{
	models = modelList;
	for (int i = 0; i < kCategoryCount; i++)
		categories[i].entries.clear();
	
	for (const auto& entry : models)
	{
		int index = -1;
		if (entry.archiveName == "TY1" && entry.kind == EntryKind::Model)
			index = 0;
		else if (entry.archiveName == "TY2" && entry.kind == EntryKind::Model)
			index = 1;
		else if (entry.archiveName == "TY1" && entry.kind == EntryKind::Level)
			index = 2;
		else if (entry.archiveName == "TY2" && entry.kind == EntryKind::Level)
			index = 3;

		if (index >= 0)
			categories[index].entries.push_back(entry);
	}

	layoutDropdown();
	for (int i = 0; i < kCategoryCount; i++)
		markFilterDirty(i);
}

void Gui::layoutDropdown()
{
	int categoryCount = 0;
	for (int i = 0; i < kCategoryCount; i++)
	{
		if (!categories[i].entries.empty())
			categoryCount++;
	}

	float dropdownHeight = (categoryCount * 30.0f) + 6.0f; // 3px top + 3px bottom padding
	dropdownRect = {buttonRect.x, buttonRect.y + buttonRect.height + 2.0f, 200.0f, dropdownHeight};
}

bool Gui::hasCategory(int index) const
{
	return index >= 0 && index < kCategoryCount;
}

void Gui::setOnModelSelected(std::function<void(const ModelEntry&)> callback)
{
	onModelSelected = callback;
}

void Gui::setOnExportRequested(std::function<void()> callback)
{
	onExportRequested = callback;
}

void Gui::setOnExportRawRequested(std::function<void()> callback)
{
	onExportRawRequested = callback;
}

void Gui::setOnRecenterCamera(std::function<void()> callback)
{
	onRecenterCamera = callback;
}

void Gui::resize(int width, int height)
{
	windowWidth = width;
	windowHeight = height;
	
	// Update panel positions
	modelInfoRect = {(float)width - 310.0f, 10.0f, 300.0f, 150.0f};
	
	layoutMaterialList();
	layoutObjectList();
}

void Gui::render()
{
	// Update notification timer.
	{
		const double now = glfwGetTime();
		if (lastRenderTimeSeconds == 0.0)
			lastRenderTimeSeconds = now;
		const float dt = (float)(now - lastRenderTimeSeconds);
		lastRenderTimeSeconds = now;

		if (notificationActive)
		{
			notificationTimeRemaining -= dt;
			if (notificationTimeRemaining <= 0.0f)
			{
				notificationActive = false;
				notificationTimeRemaining = 0.0f;
			}
		}
	}

	// Save OpenGL state
	GLboolean depthTestEnabled;
	glGetBooleanv(GL_DEPTH_TEST, &depthTestEnabled);
	
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	glUseProgram(shaderProgram);
	
	// Set up orthographic projection
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	
	if (!levelObjectItems.empty())
	{
		renderObjectList();
		renderObjectInfo();
	}

	renderButton();
	renderExportButton();
	renderExportRawButton();
	renderRecenterButton();
	renderCollisionButton();
	renderBoundsButton();
	renderNotificationBanner();
	
	if (dropdownOpen)
	{
		renderDropdown();
		
		if (submenuOpen)
		{
			renderSubmenu();
		}
	}
	
	// Render model info and material list panels
	if (currentModel)
		renderModelInfo();
	if (hasMaterialPanel())
		renderMaterialList();

	renderSceneStatsBar();
	
	// Restore OpenGL state
	if (depthTestEnabled)
		glEnable(GL_DEPTH_TEST);
	
	glUseProgram(0);
}

void Gui::renderButton()
{
	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	
	// Draw button background
	glm::vec4 buttonColor = hovering ? glm::vec4(0.3f, 0.3f, 0.3f, 0.9f) : glm::vec4(0.2f, 0.2f, 0.2f, 0.9f);
	drawRect(buttonRect.x, buttonRect.y, buttonRect.width, buttonRect.height, buttonColor);
	
	// Draw button border
	drawRect(buttonRect.x, buttonRect.y, buttonRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f)); // Top
	drawRect(buttonRect.x, buttonRect.y + buttonRect.height - 2.0f, buttonRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f)); // Bottom
	drawRect(buttonRect.x, buttonRect.y, 2.0f, buttonRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f)); // Left
	drawRect(buttonRect.x + buttonRect.width - 2.0f, buttonRect.y, 2.0f, buttonRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f)); // Right
	
	// Draw button text - show current model or "Select Model"
	std::string buttonText = currentModelName.empty() ? "Select Model" : currentModelName;
	
	// Truncate if too long for button
	if (buttonText.length() > 23)
		buttonText = buttonText.substr(0, 20) + "...";
	
	drawText(buttonText, buttonRect.x + 8.0f, buttonRect.y + 11.0f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
}

void Gui::renderExportButton()
{
	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	const bool enabled = (currentModel != nullptr);
	const bool hovered = exportButtonRect.contains(mouseX, mouseY);

	glm::vec4 bgColor;
	glm::vec4 border = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
	glm::vec4 textColor;
	if (!enabled)
	{
		bgColor = glm::vec4(0.12f, 0.12f, 0.12f, 0.75f);
		textColor = glm::vec4(0.55f, 0.55f, 0.55f, 1.0f);
	}
	else if (hovered)
	{
		bgColor = glm::vec4(0.25f, 0.45f, 0.25f, 0.95f);
		textColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	}
	else
	{
		bgColor = glm::vec4(0.18f, 0.35f, 0.18f, 0.95f);
		textColor = glm::vec4(0.95f, 0.95f, 0.95f, 1.0f);
	}

	drawRect(exportButtonRect.x, exportButtonRect.y, exportButtonRect.width, exportButtonRect.height, bgColor);
	drawRect(exportButtonRect.x, exportButtonRect.y, exportButtonRect.width, 2.0f, border);
	drawRect(exportButtonRect.x, exportButtonRect.y + exportButtonRect.height - 2.0f, exportButtonRect.width, 2.0f, border);
	drawRect(exportButtonRect.x, exportButtonRect.y, 2.0f, exportButtonRect.height, border);
	drawRect(exportButtonRect.x + exportButtonRect.width - 2.0f, exportButtonRect.y, 2.0f, exportButtonRect.height, border);

	drawText("Export", exportButtonRect.x + 18.0f, exportButtonRect.y + 11.0f, textColor);
}

void Gui::renderExportRawButton()
{
	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	const bool enabled = (currentModel != nullptr);
	const bool hovered = exportRawButtonRect.contains(mouseX, mouseY);

	glm::vec4 bgColor;
	glm::vec4 border = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
	glm::vec4 textColor;
	if (!enabled)
	{
		bgColor = glm::vec4(0.12f, 0.12f, 0.12f, 0.75f);
		textColor = glm::vec4(0.55f, 0.55f, 0.55f, 1.0f);
	}
	else if (hovered)
	{
		bgColor = glm::vec4(0.25f, 0.35f, 0.50f, 0.95f);
		textColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	}
	else
	{
		bgColor = glm::vec4(0.18f, 0.28f, 0.42f, 0.95f);
		textColor = glm::vec4(0.95f, 0.95f, 0.95f, 1.0f);
	}

	drawRect(exportRawButtonRect.x, exportRawButtonRect.y, exportRawButtonRect.width, exportRawButtonRect.height, bgColor);
	drawRect(exportRawButtonRect.x, exportRawButtonRect.y, exportRawButtonRect.width, 2.0f, border);
	drawRect(exportRawButtonRect.x, exportRawButtonRect.y + exportRawButtonRect.height - 2.0f, exportRawButtonRect.width, 2.0f, border);
	drawRect(exportRawButtonRect.x, exportRawButtonRect.y, 2.0f, exportRawButtonRect.height, border);
	drawRect(exportRawButtonRect.x + exportRawButtonRect.width - 2.0f, exportRawButtonRect.y, 2.0f, exportRawButtonRect.height, border);

	drawText("Export Raw", exportRawButtonRect.x + 12.0f, exportRawButtonRect.y + 11.0f, textColor);
}

void Gui::renderRecenterButton()
{
	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	const bool enabled = (currentModel != nullptr || sceneLoaded);
	const bool hovered = recenterButtonRect.contains(mouseX, mouseY);

	glm::vec4 bgColor;
	glm::vec4 border = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
	glm::vec4 iconColor;
	if (!enabled)
	{
		bgColor = glm::vec4(0.12f, 0.12f, 0.12f, 0.75f);
		iconColor = glm::vec4(0.45f, 0.45f, 0.45f, 1.0f);
	}
	else if (hovered)
	{
		bgColor = glm::vec4(0.32f, 0.32f, 0.32f, 0.95f);
		iconColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	}
	else
	{
		bgColor = glm::vec4(0.2f, 0.2f, 0.2f, 0.9f);
		iconColor = glm::vec4(0.92f, 0.92f, 0.92f, 1.0f);
	}

	const float x = recenterButtonRect.x;
	const float y = recenterButtonRect.y;
	const float s = recenterButtonRect.width;
	drawRect(x, y, s, recenterButtonRect.height, bgColor);
	drawRect(x, y, s, 2.0f, border);
	drawRect(x, y + recenterButtonRect.height - 2.0f, s, 2.0f, border);
	drawRect(x, y, 2.0f, recenterButtonRect.height, border);
	drawRect(x + s - 2.0f, y, 2.0f, recenterButtonRect.height, border);

	// Four corner brackets, like a framing reticle.
	const float m = 8.0f;
	const float a = 7.0f;
	const float t = 2.0f;
	drawRect(x + m, y + m, a, t, iconColor);
	drawRect(x + m, y + m, t, a, iconColor);
	drawRect(x + s - m - a, y + m, a, t, iconColor);
	drawRect(x + s - m - t, y + m, t, a, iconColor);
	drawRect(x + m, y + s - m - t, a, t, iconColor);
	drawRect(x + m, y + s - m - a, t, a, iconColor);
	drawRect(x + s - m - a, y + s - m - t, a, t, iconColor);
	drawRect(x + s - m - t, y + s - m - a, t, a, iconColor);
}

void Gui::renderCollisionButton()
{
	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	const bool hovered = collisionAvailable && collisionButtonRect.contains(mouseX, mouseY);

	glm::vec4 bgColor;
	glm::vec4 textColor;
	if (!collisionAvailable)
	{
		bgColor = glm::vec4(0.12f, 0.12f, 0.12f, 0.75f);
		textColor = glm::vec4(0.45f, 0.45f, 0.45f, 1.0f);
	}
	else if (collisionVisible)
	{
		bgColor = hovered ? glm::vec4(0.35f, 0.55f, 0.32f, 0.98f) : glm::vec4(0.22f, 0.42f, 0.22f, 0.95f);
		textColor = glm::vec4(0.95f, 0.95f, 0.95f, 1.0f);
	}
	else
	{
		bgColor = hovered ? glm::vec4(0.32f, 0.32f, 0.32f, 0.95f) : glm::vec4(0.18f, 0.18f, 0.18f, 0.9f);
		textColor = glm::vec4(0.75f, 0.75f, 0.75f, 1.0f);
	}

	const float x = collisionButtonRect.x;
	const float y = collisionButtonRect.y;
	const float w = collisionButtonRect.width;
	const float h = collisionButtonRect.height;
	drawRect(x, y, w, h, bgColor);
	drawRect(x, y, w, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(x, y + h - 2.0f, w, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(x, y, 2.0f, h, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(x + w - 2.0f, y, 2.0f, h, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));

	const char* label = collisionVisible ? "Col: On" : "Col: Off";
	drawText(label, x + 12.0f, y + 11.0f, textColor);
}

void Gui::renderBoundsButton()
{
	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	const bool hovered = boundsButtonRect.contains(mouseX, mouseY);

	glm::vec4 bgColor;
	glm::vec4 textColor;
	if (boundsVisible)
	{
		bgColor = hovered ? glm::vec4(0.35f, 0.55f, 0.32f, 0.98f) : glm::vec4(0.22f, 0.42f, 0.22f, 0.95f);
		textColor = glm::vec4(0.95f, 0.95f, 0.95f, 1.0f);
	}
	else
	{
		bgColor = hovered ? glm::vec4(0.32f, 0.32f, 0.32f, 0.95f) : glm::vec4(0.18f, 0.18f, 0.18f, 0.9f);
		textColor = glm::vec4(0.75f, 0.75f, 0.75f, 1.0f);
	}

	const float x = boundsButtonRect.x;
	const float y = boundsButtonRect.y;
	const float w = boundsButtonRect.width;
	const float h = boundsButtonRect.height;
	drawRect(x, y, w, h, bgColor);
	drawRect(x, y, w, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(x, y + h - 2.0f, w, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(x, y, 2.0f, h, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(x + w - 2.0f, y, 2.0f, h, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));

	const char* label = boundsVisible ? "Bounds: On [2]" : "Bounds: Off [2]";
	drawText(label, x + 12.0f, y + 11.0f, textColor);
}

void Gui::setOnBoundsToggle(std::function<void()> callback)
{
	onBoundsToggle = callback;
}

void Gui::setBoundsVisible(bool visible)
{
	boundsVisible = visible;
}

void Gui::setOnCollisionToggle(std::function<void()> callback)
{
	onCollisionToggle = callback;
}

void Gui::setCollisionToggle(bool available, bool visible)
{
	collisionAvailable = available;
	collisionVisible = visible;
}

void Gui::renderNotificationBanner()
{
	// Banner sits to the right of the header buttons.
	const float kPad = 10.0f;
	const float x = boundsButtonRect.x + boundsButtonRect.width + kPad;
	const float y = exportRawButtonRect.y;
	const float h = exportRawButtonRect.height;
	const float maxW = (float)windowWidth - x - kPad;
	const float w = (maxW > 0.0f) ? std::min(420.0f, maxW) : 0.0f;

	notificationRect = { x, y, w, h };

	if (!notificationActive || w <= 0.0f)
		return;

	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	glm::vec4 bg;
	glm::vec4 border = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
	glm::vec4 text = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

	switch (notificationKind)
	{
		case NotificationKind::Success:
			bg = glm::vec4(0.18f, 0.45f, 0.18f, 0.92f);
			break;
		case NotificationKind::Error:
			bg = glm::vec4(0.55f, 0.18f, 0.18f, 0.92f);
			break;
		case NotificationKind::Info:
		default:
			bg = glm::vec4(0.20f, 0.28f, 0.45f, 0.92f);
			break;
	}

	// Slightly brighten when hovered to hint it can be dismissed.
	const bool hovered = notificationRect.contains(mouseX, mouseY);
	if (hovered)
		bg.a = 0.98f;

	drawRect(notificationRect.x, notificationRect.y, notificationRect.width, notificationRect.height, bg);
	drawRect(notificationRect.x, notificationRect.y, notificationRect.width, 2.0f, border);
	drawRect(notificationRect.x, notificationRect.y + notificationRect.height - 2.0f, notificationRect.width, 2.0f, border);
	drawRect(notificationRect.x, notificationRect.y, 2.0f, notificationRect.height, border);
	drawRect(notificationRect.x + notificationRect.width - 2.0f, notificationRect.y, 2.0f, notificationRect.height, border);

	// Text (fixed-width font, 8px per character).
	std::string msg = notificationText;
	const int maxChars = (int)((notificationRect.width - 12.0f) / 8.0f);
	if (maxChars > 3 && (int)msg.size() > maxChars)
		msg = msg.substr(0, (size_t)maxChars - 3) + "...";
	else if (maxChars <= 0)
		return;

	drawText(msg, notificationRect.x + 6.0f, notificationRect.y + 11.0f, text);
}

void Gui::renderDropdown()
{
	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	
	// Draw dropdown background
	drawRect(dropdownRect.x, dropdownRect.y, dropdownRect.width, dropdownRect.height, glm::vec4(0.15f, 0.15f, 0.15f, 0.95f));
	
	// Draw border
	drawRect(dropdownRect.x, dropdownRect.y, dropdownRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(dropdownRect.x, dropdownRect.y + dropdownRect.height - 2.0f, dropdownRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(dropdownRect.x, dropdownRect.y, 2.0f, dropdownRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(dropdownRect.x + dropdownRect.width - 2.0f, dropdownRect.y, 2.0f, dropdownRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	
	float yOffset = dropdownRect.y + 3.0f;
	const glm::vec4 idleColor(0.3f, 0.3f, 0.3f, 1.0f);

	for (int i = 0; i < kCategoryCount; i++)
	{
		if (categories[i].entries.empty())
			continue;

		glm::vec4 bgColor = (hoveredCategory == i) ? categories[i].hoverColor : idleColor;
		drawRect(dropdownRect.x + 5.0f, yOffset, dropdownRect.width - 10.0f, 25.0f, bgColor);

		std::string categoryText = categories[i].label + " (" + std::to_string(categories[i].entries.size()) + ") >";
		drawText(categoryText, dropdownRect.x + 10.0f, yOffset + 9.0f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
		yOffset += 30.0f;

		// drawText switches programs; rectangles need the GUI shader again.
		glUseProgram(shaderProgram);
		glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	}
}

void Gui::renderSubmenu()
{
	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	const float kTopPad = 5.0f;
	const float kBottomPad = 5.0f;
	const float kSidePad = 5.0f;
	const float kSearchHeight = 25.0f;
	const float kSearchGap = 5.0f;
	const float kItemHeight = 25.0f;
	const float kItemBoxHeight = 20.0f;
	
	// Draw submenu background
	drawRect(submenuRect.x, submenuRect.y, submenuRect.width, submenuRect.height, glm::vec4(0.12f, 0.12f, 0.12f, 0.95f));
	
	// Draw border
	drawRect(submenuRect.x, submenuRect.y, submenuRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(submenuRect.x, submenuRect.y + submenuRect.height - 2.0f, submenuRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(submenuRect.x, submenuRect.y, 2.0f, submenuRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(submenuRect.x + submenuRect.width - 2.0f, submenuRect.y, 2.0f, submenuRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));

	// Search bar (takes space at the top of the submenu)
	submenuSearchRect = {submenuRect.x + kSidePad, submenuRect.y + kTopPad, submenuRect.width - (kSidePad * 2.0f), kSearchHeight};

	const bool searchActive = (activeSearchCategory == hoveredCategory);
	glm::vec4 searchBg = searchActive ? glm::vec4(0.22f, 0.22f, 0.22f, 1.0f) : glm::vec4(0.18f, 0.18f, 0.18f, 1.0f);
	glm::vec4 searchBorder = searchActive ? glm::vec4(0.7f, 0.7f, 0.7f, 1.0f) : glm::vec4(0.45f, 0.45f, 0.45f, 1.0f);
	drawRect(submenuSearchRect.x, submenuSearchRect.y, submenuSearchRect.width, submenuSearchRect.height, searchBg);
	drawRect(submenuSearchRect.x, submenuSearchRect.y, submenuSearchRect.width, 2.0f, searchBorder);
	drawRect(submenuSearchRect.x, submenuSearchRect.y + submenuSearchRect.height - 2.0f, submenuSearchRect.width, 2.0f, searchBorder);
	drawRect(submenuSearchRect.x, submenuSearchRect.y, 2.0f, submenuSearchRect.height, searchBorder);
	drawRect(submenuSearchRect.x + submenuSearchRect.width - 2.0f, submenuSearchRect.y, 2.0f, submenuSearchRect.height, searchBorder);

	std::string searchText = hasCategory(hoveredCategory) ? categories[hoveredCategory].search : std::string();
	if (searchText.empty())
		searchText = "Search...";
	else if (searchActive)
		searchText += "_";

	// Truncate to fit the box (8px per character font)
	const int maxChars = (int)((submenuSearchRect.width - 12.0f) / 8.0f);
	if ((int)searchText.size() > maxChars && maxChars > 3)
		searchText = "..." + searchText.substr(searchText.size() - (maxChars - 3));

	drawText(searchText, submenuSearchRect.x + 6.0f, submenuSearchRect.y + 9.0f, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));

	// CRITICAL: Rebind rectangle shader after drawText switched programs
	glUseProgram(shaderProgram);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	// Filtered model list (scrolls beneath the search bar)
	rebuildFilteredIndicesIfNeeded(hoveredCategory);
	const std::vector<ModelEntry>* modelList = hasCategory(hoveredCategory) ? &categories[hoveredCategory].entries : nullptr;
	const std::vector<int>* filteredPtr = hasCategory(hoveredCategory) ? &categories[hoveredCategory].filteredIndices : nullptr;
	if (modelList == nullptr || filteredPtr == nullptr)
		return;
	const std::vector<int>& filtered = *filteredPtr;

	const float listTop = submenuRect.y + kTopPad + kSearchHeight + kSearchGap;
	const float listBottom = submenuRect.y + submenuRect.height - kBottomPad;
	const float listHeight = std::max(0.0f, listBottom - listTop);

	maxScroll = std::max(0.0f, (float)filtered.size() * kItemHeight - listHeight);
	if (scrollOffset < 0.0f) scrollOffset = 0.0f;
	if (scrollOffset > maxScroll) scrollOffset = maxScroll;

	float yOffset = listTop - scrollOffset;
	for (size_t pos = 0; pos < filtered.size(); pos++)
	{
		const float itemY = yOffset + (float)pos * kItemHeight;
		if (itemY + kItemBoxHeight < listTop || itemY >= listBottom)
			continue;

		const ModelEntry& entry = (*modelList)[filtered[pos]];

		glm::vec4 itemColor = glm::vec4(0.18f, 0.18f, 0.18f, 1.0f);
		glm::vec4 textColor = glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);

		bool isSelected = (currentModelName == entry.name);
		bool isHovered = (hoveredSubmenuItem == (int)pos);

		if (isSelected)
		{
			itemColor = glm::vec4(0.3f, 0.6f, 0.3f, 1.0f);
			textColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else if (isHovered)
		{
			itemColor = glm::vec4(0.28f, 0.28f, 0.28f, 1.0f);
			textColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		}

		drawRect(submenuRect.x + kSidePad, itemY, submenuRect.width - (kSidePad * 2.0f), kItemBoxHeight, itemColor);

		std::string displayName = entry.name;
		if (displayName.length() > 40)
			displayName = displayName.substr(0, 37) + "...";

		drawText(displayName, submenuRect.x + kSidePad + 5.0f, itemY + 6.0f, textColor);

		// CRITICAL: Rebind rectangle shader after drawText switched programs
		glUseProgram(shaderProgram);
		glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	}
}

void Gui::markFilterDirty(int category)
{
	if (hasCategory(category))
		categories[category].filterDirty = true;
}

char Gui::normalizeSearchChar(char c)
{
	// Keep printable ASCII only; normalize newlines/tabs to space.
	if (c == '\r' || c == '\n' || c == '\t')
		return ' ';
	return c;
}

bool Gui::containsCaseInsensitive(const std::string& haystack, const std::string& needle)
{
	if (needle.empty())
		return true;

	auto lowerChar = [](unsigned char ch) { return (char)std::tolower(ch); };

	// Naive search is fine here; filtering only runs when query changes.
	for (size_t i = 0; i < haystack.size(); i++)
	{
		size_t j = 0;
		while (i + j < haystack.size() && j < needle.size() &&
		       lowerChar((unsigned char)haystack[i + j]) == lowerChar((unsigned char)needle[j]))
		{
			j++;
		}
		if (j == needle.size())
			return true;
	}
	return false;
}

void Gui::rebuildFilteredIndicesIfNeeded(int category)
{
	if (!hasCategory(category))
		return;

	Category& cat = categories[category];
	if (!cat.filterDirty)
		return;

	cat.filteredIndices.clear();
	cat.filteredIndices.reserve(cat.entries.size());

	for (int i = 0; i < (int)cat.entries.size(); i++)
	{
		if (containsCaseInsensitive(cat.entries[i].name, cat.search))
			cat.filteredIndices.push_back(i);
	}

	cat.filterDirty = false;
}

void Gui::updateSubmenuScrollBounds()
{
	if (!submenuOpen || !hasCategory(hoveredCategory))
	{
		maxScroll = 0.0f;
		if (scrollOffset < 0.0f) scrollOffset = 0.0f;
		return;
	}

	const float kTopPad = 5.0f;
	const float kBottomPad = 5.0f;
	const float kSearchHeight = 25.0f;
	const float kSearchGap = 5.0f;
	const float kItemHeight = 25.0f;

	rebuildFilteredIndicesIfNeeded(hoveredCategory);
	const std::vector<int>& filtered = categories[hoveredCategory].filteredIndices;

	// IMPORTANT: submenu height must expand/shrink with filter results.
	// Without this, filtering down to a few items makes the submenu tiny, and clearing the query
	// won't show more items (you'd only see a couple at once).
	const float maxSubmenuHeight = windowHeight * 0.7f;
	const float contentHeight = kTopPad + kSearchHeight + kSearchGap + ((float)filtered.size() * kItemHeight) + kBottomPad;
	const float desiredHeight = (contentHeight < maxSubmenuHeight) ? contentHeight : maxSubmenuHeight;
	if (submenuRect.height != desiredHeight)
		submenuRect.height = desiredHeight;

	const float listTop = submenuRect.y + kTopPad + kSearchHeight + kSearchGap;
	const float listBottom = submenuRect.y + submenuRect.height - kBottomPad;
	const float listHeight = std::max(0.0f, listBottom - listTop);

	maxScroll = std::max(0.0f, (float)filtered.size() * kItemHeight - listHeight);
	if (scrollOffset < 0.0f) scrollOffset = 0.0f;
	if (scrollOffset > maxScroll) scrollOffset = maxScroll;
}

void Gui::hoverCategory(int index)
{
	if (!hasCategory(index) || categories[index].entries.empty())
		return;

	if (hoveredCategory != index)
	{
		hoveredCategory = index;
		submenuOpen = true;
		scrollOffset = 0.0f;

		const float kTopPad = 5.0f;
		const float kBottomPad = 5.0f;
		const float kSearchHeight = 25.0f;
		const float kSearchGap = 5.0f;
		const float kItemHeight = 25.0f;

		rebuildFilteredIndicesIfNeeded(index);
		float contentHeight = kTopPad + kSearchHeight + kSearchGap + ((float)categories[index].filteredIndices.size() * kItemHeight) + kBottomPad;
		float maxSubmenuHeight = windowHeight * 0.7f;
		float submenuHeight = (contentHeight < maxSubmenuHeight) ? contentHeight : maxSubmenuHeight;

		submenuRect = {dropdownRect.x + dropdownRect.width + 2.0f, dropdownRect.y, 350.0f, submenuHeight};
		updateSubmenuScrollBounds();

		if (!currentModelName.empty())
		{
			const std::vector<int>& filtered = categories[index].filteredIndices;
			for (size_t pos = 0; pos < filtered.size(); pos++)
			{
				int idx = filtered[pos];
				if (categories[index].entries[idx].name == currentModelName)
				{
					scrollOffset = (float)pos * kItemHeight;
					updateSubmenuScrollBounds();
					break;
				}
			}
		}
	}
	else
	{
		hoveredCategory = index;
	}
}

void Gui::drawRect(float x, float y, float width, float height, const glm::vec4& color)
{
	float vertices[] = {
		x, y,
		x + width, y,
		x, y + height,
		x + width, y,
		x + width, y + height,
		x, y + height
	};
	
	glUniform4fv(glGetUniformLocation(shaderProgram, "color"), 1, glm::value_ptr(color));
	
	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
	
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
}

void Gui::onMouseButton(int button, int action, float x, float y)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT)
	{
		if (action == GLFW_PRESS)
		{
			// Click notification banner to dismiss (unobtrusive).
			if (notificationActive && notificationRect.width > 0.0f && notificationRect.contains(x, y))
			{
				notificationActive = false;
				notificationTimeRemaining = 0.0f;
				return;
			}

			if (exportButtonRect.contains(x, y))
			{
				// Only allow export when a model is loaded
				if (currentModel && onExportRequested)
				{
					onExportRequested();
				}
				return;
			}

			if (exportRawButtonRect.contains(x, y))
			{
				if (currentModel && onExportRawRequested)
				{
					onExportRawRequested();
				}
				return;
			}

			if (recenterButtonRect.contains(x, y))
			{
				if ((currentModel || sceneLoaded) && onRecenterCamera)
				{
					onRecenterCamera();
				}
				return;
			}

			if (collisionButtonRect.contains(x, y))
			{
				if (collisionAvailable && onCollisionToggle)
					onCollisionToggle();
				return;
			}

			if (boundsButtonRect.contains(x, y))
			{
				if (onBoundsToggle)
					onBoundsToggle();
				return;
			}

			if (buttonRect.contains(x, y))
			{
				dropdownOpen = !dropdownOpen;
				submenuOpen = false;
				scrollOffset = 0.0f;
				hoveredCategory = -1;
				hoveredSubmenuItem = -1;
				activeSearchCategory = -1;
			}
			else if (submenuOpen && submenuRect.contains(x, y))
			{
				const float kTopPad = 5.0f;
				const float kBottomPad = 5.0f;
				const float kSidePad = 5.0f;
				const float kSearchHeight = 25.0f;
				const float kSearchGap = 5.0f;
				const float kItemHeight = 25.0f;

				// Search bar click focuses typing for that submenu
				GuiRect searchRect = {submenuRect.x + kSidePad, submenuRect.y + kTopPad, submenuRect.width - (kSidePad * 2.0f), kSearchHeight};
				if (searchRect.contains(x, y))
				{
					activeSearchCategory = hoveredCategory;
					return;
				}

				// Clicking in the list area selects an item (based on filtered list)
				rebuildFilteredIndicesIfNeeded(hoveredCategory);
				if (!hasCategory(hoveredCategory))
					return;
				const std::vector<ModelEntry>& modelList = categories[hoveredCategory].entries;
				const std::vector<int>& filtered = categories[hoveredCategory].filteredIndices;

				const float listTop = submenuRect.y + kTopPad + kSearchHeight + kSearchGap;
				const float listBottom = submenuRect.y + submenuRect.height - kBottomPad;

				if (y < listTop || y >= listBottom)
					return;

				updateSubmenuScrollBounds();

				float relativeY = y - listTop + scrollOffset;
				int itemPos = (int)(relativeY / kItemHeight);

				if (itemPos >= 0 && itemPos < (int)filtered.size())
				{
					const ModelEntry& entry = modelList[filtered[itemPos]];

					if (onModelSelected)
					{
						onModelSelected(entry);
						// Store selected model info
						for (auto& model : models)
						{
							if (model.name == entry.name)
							{
								selectedModel = &model;
								currentModelName = model.name;
								break;
							}
						}
					}

					dropdownOpen = false;
					submenuOpen = false;
					hoveredCategory = -1;
					hoveredSubmenuItem = -1;
					activeSearchCategory = -1;
				}
			}
			else if (dropdownOpen && dropdownRect.contains(x, y))
			{
				// Clicking on category doesn't do anything - must hover to open submenu
				// Just ignore the click
			}
			else if (hasMaterialPanel() && materialListRect.contains(x, y))
			{
				objectSearchActive = false;
				if (collapseButtonRect(materialListRect).contains(x, y))
				{
					materialListCollapsed = !materialListCollapsed;
					layoutMaterialList();
					return;
				}
				if (materialListCollapsed)
					return;
				// Handle mesh-part list clicks.
				// - Header: search, then bulk actions (show/hide all, or the filtered rows)
				// - Body: toggle individual parts
				rebuildMaterialFilter();
				const float yLocal = y - materialListRect.y;

				if (materialSearchRect().contains(x, y))
				{
					materialSearchActive = true;
					activeSearchCategory = -1;
					return;
				}
				materialSearchActive = false;

				const BulkButtonRects bulk = bulkButtonRects(materialListRect);

				if (yLocal >= 0.0f && yLocal < kMeshPartHeaderHeight)
				{
					auto eachListedMesh = [&](const auto& visit)
					{
						if (!materialSearch.empty())
						{
							for (int flatIndex : materialFiltered)
							{
								Mesh* mesh = materialMeshAt(flatIndex);
								if (mesh) visit(*mesh);
							}
							return;
						}
						const int count = materialMeshCount();
						for (int i = 0; i < count; i++)
						{
							Mesh* mesh = materialMeshAt(i);
							if (mesh) visit(*mesh);
						}
					};
					bool changed = false;
					if (bulk.showDefault.contains(x, y))
					{
						eachListedMesh([](Mesh& mesh) { mesh.setEnabled(mesh.defaultEnabled()); });
						changed = true;
					}
					else if (bulk.showAll.contains(x, y))
					{
						eachListedMesh([](Mesh& mesh) { mesh.setEnabled(true); });
						changed = true;
					}
					else if (bulk.hideAll.contains(x, y))
					{
						eachListedMesh([](Mesh& mesh) { mesh.setEnabled(false); });
						changed = true;
					}
					if (changed && onPartVisibilityChanged)
						onPartVisibilityChanged();
					return;
				}

				float relativeY = y - (materialListRect.y + kMeshPartHeaderHeight) + materialListScroll;
				int itemIndex = (int)(relativeY / kMeshPartItemHeight);
				
				if (itemIndex >= 0 && itemIndex < (int)materialFiltered.size())
				{
					Mesh* mesh = materialMeshAt(materialFiltered[itemIndex]);
					if (mesh == nullptr)
						return;
					mesh->setEnabled(!mesh->isEnabled());
					Debug::log("Toggled mesh part " + std::to_string(itemIndex) + ": " + mesh->getPartName() + " / " + mesh->getMaterialName() + " -> " + (mesh->isEnabled() ? "VISIBLE" : "HIDDEN"));
				}
			}
			else if (!levelObjectItems.empty() && objectListRect.contains(x, y))
			{
				materialSearchActive = false;
				if (collapseButtonRect(objectListRect).contains(x, y))
				{
					objectListCollapsed = !objectListCollapsed;
					layoutObjectList();
					return;
				}
				if (objectListCollapsed)
					return;
				rebuildObjectFilter();
				const float yLocal = y - objectListRect.y;

				if (objectSearchRect().contains(x, y))
				{
					objectSearchActive = true;
					activeSearchCategory = -1;
					return;
				}
				objectSearchActive = false;

				const BulkButtonRects bulk = bulkButtonRects(objectListRect);

				if (yLocal >= 0.0f && yLocal < kObjectListHeaderHeight)
				{
					if (bulk.showDefault.contains(x, y) || bulk.showAll.contains(x, y) || bulk.hideAll.contains(x, y))
					{
						for (int index : objectFiltered)
						{
							const LevelObjectItem& item = levelObjectItems[static_cast<size_t>(index)];
							const bool enabled = bulk.showDefault.contains(x, y)
								? item.defaultVisible
								: bulk.showAll.contains(x, y);
							setObjectVisible(index, enabled);
						}
					}
					else
					{
						for (int chip = 0; chip < kKindChipCount; chip++)
						{
							if (!objectKindChipRect(chip).contains(x, y))
								continue;
							if (kKindChips[chip].bit == 0)
								objectKindFilter = 0;
							else
								objectKindFilter ^= kKindChips[chip].bit;
							objectFilterDirty = true;
							objectListScroll = 0.0f;
							layoutObjectList();
							break;
						}
					}
					return;
				}

				float relativeY = y - (objectListRect.y + kObjectListHeaderHeight) + objectListScroll;
				int row = (int)(relativeY / kMeshPartItemHeight);
				if (row >= 0 && row < (int)objectFiltered.size())
				{
					const int index = objectFiltered[row];
					const float rowTop = objectListRect.y + kObjectListHeaderHeight - objectListScroll
						+ static_cast<float>(row) * kMeshPartItemHeight;
					const float bodyTop = objectListRect.y + kObjectListHeaderHeight;
					const float bodyBottom = objectListRect.y + objectListRect.height - 3.0f;
					if (!rowFits(rowTop, bodyTop, bodyBottom, 30.0f))
						return;
					const GuiRect checkbox = { objectListRect.x + 10.0f, rowTop + 9.0f, 12.0f, 12.0f };
					if (checkbox.contains(x, y))
					{
						const bool enabled = !levelObjectItems[static_cast<size_t>(index)].visible;
						setObjectVisible(index, enabled);
						return;
					}

					const double now = glfwGetTime();
					const bool doubleClick = index == lastObjectClickIndex && (now - lastObjectClickTime) <= 0.4;
					lastObjectClickIndex = index;
					lastObjectClickTime = now;
					selectLevelObject(index);
					if (doubleClick && onLevelObjectFocused)
						onLevelObjectFocused(index);
				}
			}
			else if (!levelObjectItems.empty() && objectInfoRect.contains(x, y))
			{
				objectSearchActive = false;
				materialSearchActive = false;
				activeSearchCategory = -1;
				rebuildInfoDrawLines();
				if (y < objectInfoRect.y + kInfoHeaderHeight)
					return;
				const float relativeY = y - (objectInfoRect.y + kInfoHeaderHeight) + objectInfoScroll;
				const int row = (int)(relativeY / kInfoLineHeight);
				if (row >= 0 && row < (int)infoDrawLines.size())
				{
					const int link = infoDrawLines[static_cast<size_t>(row)].link;
					if (link >= 0)
						selectLevelObject(link);
				}
			}
			else
			{
				// Clicked outside, close everything
				dropdownOpen = false;
				submenuOpen = false;
				hoveredCategory = -1;
				hoveredSubmenuItem = -1;
				activeSearchCategory = -1;
				materialSearchActive = false;
				objectSearchActive = false;
			}
		}
	}
}

void Gui::onMouseMove(float x, float y)
{
	mouseX = x;
	mouseY = y;
	
	hovering = buttonRect.contains(x, y) || exportButtonRect.contains(x, y) || exportRawButtonRect.contains(x, y) || recenterButtonRect.contains(x, y) || collisionButtonRect.contains(x, y) || boundsButtonRect.contains(x, y) ||
		(dropdownOpen && dropdownRect.contains(x, y)) || (submenuOpen && submenuRect.contains(x, y));
	
	// Track hovered submenu item
	hoveredSubmenuItem = -1;
	if (submenuOpen && submenuRect.contains(x, y))
	{
		// Mouse is in submenu - keep hoveredCategory as is (don't reset it)
		// This ensures the dropdown category stays highlighted

		const float kTopPad = 5.0f;
		const float kBottomPad = 5.0f;
		const float kSearchHeight = 25.0f;
		const float kSearchGap = 5.0f;
		const float kItemHeight = 25.0f;

		rebuildFilteredIndicesIfNeeded(hoveredCategory);
		if (!hasCategory(hoveredCategory))
			return;
		const std::vector<int>& filtered = categories[hoveredCategory].filteredIndices;

		const float listTop = submenuRect.y + kTopPad + kSearchHeight + kSearchGap;
		const float listBottom = submenuRect.y + submenuRect.height - kBottomPad;

		if (y >= listTop && y < listBottom)
		{
			float relativeY = y - listTop + scrollOffset;
			int itemPos = (int)(relativeY / kItemHeight);

			if (itemPos >= 0 && itemPos < (int)filtered.size())
				hoveredSubmenuItem = itemPos;
		}
		// Don't process dropdown hover detection when in submenu
		return;
	}
	
	// Track hovered object row
	hoveredObjectItem = -1;
	hoveredInfoRow = -1;
	if (!levelObjectItems.empty() && objectInfoRect.contains(x, y))
	{
		if (y >= objectInfoRect.y + kInfoHeaderHeight)
		{
			const float relativeY = y - (objectInfoRect.y + kInfoHeaderHeight) + objectInfoScroll;
			const int row = (int)(relativeY / kInfoLineHeight);
			if (row >= 0 && row < (int)infoDrawLines.size() && infoDrawLines[static_cast<size_t>(row)].link >= 0)
				hoveredInfoRow = row;
		}
	}
	if (!levelObjectItems.empty() && objectListRect.contains(x, y))
	{
		rebuildObjectFilter();
		const float yLocal = y - objectListRect.y;
		if (!objectListCollapsed && yLocal >= kObjectListHeaderHeight)
		{
			float relativeY = y - (objectListRect.y + kObjectListHeaderHeight) + objectListScroll;
			int row = (int)(relativeY / kMeshPartItemHeight);
			if (row >= 0 && row < (int)objectFiltered.size())
				hoveredObjectItem = row;
		}
	}

	// Track hovered material item
	hoveredMaterialItem = -1;
	if (hasMaterialPanel() && materialListRect.contains(x, y))
	{
		rebuildMaterialFilter();
		const float yLocal = y - materialListRect.y;
		if (materialListCollapsed || yLocal < kMeshPartHeaderHeight)
		{
			// Hovering header area; keep hoveredMaterialItem = -1 but continue processing other UI hovers.
		}
		else
		{
			float relativeY = y - (materialListRect.y + kMeshPartHeaderHeight) + materialListScroll;
			int itemIndex = (int)(relativeY / kMeshPartItemHeight);

			if (itemIndex >= 0 && itemIndex < (int)materialFiltered.size())
			{
				hoveredMaterialItem = itemIndex;
			}
		}
	}
	
	if (dropdownOpen && dropdownRect.contains(x, y))
	{
		float relativeY = y - dropdownRect.y;
		float yPos = 5.0f;

		for (int i = 0; i < kCategoryCount; i++)
		{
			if (categories[i].entries.empty())
				continue;

			if (relativeY >= yPos && relativeY < yPos + 25.0f)
			{
				hoverCategory(i);
				return;
			}
			yPos += 30.0f;
		}
	}
	else if (dropdownOpen && !dropdownRect.contains(x, y) && (!submenuOpen || !submenuRect.contains(x, y)))
	{
		// Mouse is outside both dropdown and submenu - close everything
		submenuOpen = false;
		hoveredCategory = -1;
		hoveredSubmenuItem = -1;
		activeSearchCategory = -1;
	}
}

void Gui::onScroll(float yoffset)
{
	// Scroll the submenu when it's open and mouse is in the general GUI area
	if (submenuOpen)
	{
		// Allow scrolling if mouse is over submenu OR over the dropdown (more forgiving)
		bool canScroll = submenuRect.contains(mouseX, mouseY) || 
		                 dropdownRect.contains(mouseX, mouseY) ||
		                 hovering;
		
		if (canScroll)
		{
			updateSubmenuScrollBounds();
			scrollOffset -= yoffset * 25.0f; // Scroll one item at a time
			if (scrollOffset < 0.0f) scrollOffset = 0.0f;
			if (scrollOffset > maxScroll) scrollOffset = maxScroll;
			return; // Don't process other scroll targets
		}
	}
	
	if (!levelObjectItems.empty() && objectInfoRect.contains(mouseX, mouseY))
	{
		objectInfoScroll -= yoffset * kInfoLineHeight * 3.0f;
		if (objectInfoScroll < 0.0f) objectInfoScroll = 0.0f;
		if (objectInfoScroll > maxObjectInfoScroll) objectInfoScroll = maxObjectInfoScroll;
		return;
	}

	if (!objectListCollapsed && !levelObjectItems.empty() && objectListRect.contains(mouseX, mouseY))
	{
		objectListScroll -= yoffset * kMeshPartItemHeight;
		if (objectListScroll < 0.0f) objectListScroll = 0.0f;
		if (objectListScroll > maxObjectListScroll) objectListScroll = maxObjectListScroll;
		return;
	}

	// Scroll the material list when mouse is over it
	if (!materialListCollapsed && hasMaterialPanel() && materialListRect.contains(mouseX, mouseY))
	{
		materialListScroll -= yoffset * kMeshPartItemHeight;
		if (materialListScroll < 0.0f) materialListScroll = 0.0f;
		if (materialListScroll > maxMaterialListScroll) materialListScroll = maxMaterialListScroll;
	}
}

void Gui::onKeyPress(int key)
{
	if (objectSearchActive)
	{
		if (key == GLFW_KEY_BACKSPACE)
		{
			if (!objectSearch.empty())
			{
				objectSearch.pop_back();
				objectFilterDirty = true;
				objectListScroll = 0.0f;
				layoutObjectList();
			}
			return;
		}
		if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)
		{
			objectSearchActive = false;
			return;
		}
		return;
	}

	if (materialSearchActive)
	{
		if (key == GLFW_KEY_BACKSPACE)
		{
			if (!materialSearch.empty())
			{
				materialSearch.pop_back();
				materialFilterDirty = true;
				materialListScroll = 0.0f;
				layoutMaterialList();
			}
			return;
		}
		if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)
		{
			materialSearchActive = false;
			return;
		}
		return;
	}

	if (!submenuOpen) return;

	// Search bar editing (special keys)
	if (hasCategory(activeSearchCategory))
	{
		std::string& search = categories[activeSearchCategory].search;

		if (key == GLFW_KEY_BACKSPACE)
		{
			if (!search.empty())
			{
				search.pop_back();
				markFilterDirty(activeSearchCategory);
				scrollOffset = 0.0f;
				updateSubmenuScrollBounds();
			}
			return;
		}
		if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)
		{
			activeSearchCategory = -1;
			return;
		}
	}
	
	// Arrow keys for scrolling submenu
	updateSubmenuScrollBounds();

	if (key == GLFW_KEY_UP)
	{
		scrollOffset -= 25.0f;
		if (scrollOffset < 0.0f) scrollOffset = 0.0f;
	}
	else if (key == GLFW_KEY_DOWN)
	{
		scrollOffset += 25.0f;
		if (scrollOffset > maxScroll) scrollOffset = maxScroll;
	}
	else if (key == GLFW_KEY_PAGE_UP)
	{
		scrollOffset -= submenuRect.height;
		if (scrollOffset < 0.0f) scrollOffset = 0.0f;
	}
	else if (key == GLFW_KEY_PAGE_DOWN)
	{
		scrollOffset += submenuRect.height;
		if (scrollOffset > maxScroll) scrollOffset = maxScroll;
	}
	else if (key == GLFW_KEY_HOME)
	{
		scrollOffset = 0.0f;
	}
	else if (key == GLFW_KEY_END)
	{
		scrollOffset = maxScroll;
	}
}

void Gui::onChar(unsigned int codepoint)
{
	if (objectSearchActive)
	{
		if (codepoint < 32 || codepoint > 126)
			return;
		char c = normalizeSearchChar((char)codepoint);
		if (c < 32 || c > 126)
			return;
		if (objectSearch.size() >= 64)
			return;
		objectSearch.push_back(c);
		objectFilterDirty = true;
		objectListScroll = 0.0f;
		layoutObjectList();
		return;
	}

	if (materialSearchActive)
	{
		if (codepoint < 32 || codepoint > 126)
			return;
		char c = normalizeSearchChar((char)codepoint);
		if (c < 32 || c > 126)
			return;
		if (materialSearch.size() >= 64)
			return;
		materialSearch.push_back(c);
		materialFilterDirty = true;
		materialListScroll = 0.0f;
		layoutMaterialList();
		return;
	}

	if (!submenuOpen) return;
	if (!hasCategory(activeSearchCategory)) return;

	// Limit to basic printable ASCII for now (matches built-in font range)
	if (codepoint < 32 || codepoint > 126)
		return;

	char c = normalizeSearchChar((char)codepoint);
	if (c < 32 || c > 126)
		return;

	std::string& search = categories[activeSearchCategory].search;

	// Hard cap to keep things reasonable
	if (search.size() >= 96)
		return;

	search.push_back(c);
	markFilterDirty(activeSearchCategory);
	scrollOffset = 0.0f;
	updateSubmenuScrollBounds();
}

void Gui::setCurrentModel(Model* model, const std::string& modelName)
{
	currentModel = model;
	levelModels.clear();
	clearObjectList();
	currentModelName = modelName;
	sceneLoaded = false;
	materialSearch.clear();
	materialSearchActive = false;
	materialFilterDirty = true;
	materialListScroll = 0.0f;
	hoveredMaterialItem = -1;
	layoutMaterialList();
}

void Gui::setLevelModels(const std::vector<Model*>& models, const std::string& name)
{
	currentModel = nullptr;
	levelModels = models;
	clearObjectList();
	currentModelName = name;
	sceneLoaded = !models.empty();
	materialSearch.clear();
	materialSearchActive = false;
	materialFilterDirty = true;
	materialListScroll = 0.0f;
	hoveredMaterialItem = -1;
	layoutMaterialList();
}

void Gui::setSceneLabel(const std::string& name, bool canRecenter)
{
	currentModel = nullptr;
	levelModels.clear();
	clearObjectList();
	currentModelName = name;
	sceneLoaded = canRecenter;
	materialSearch.clear();
	materialSearchActive = false;
	materialFilterDirty = true;
	materialListScroll = 0.0f;
	hoveredMaterialItem = -1;
	layoutMaterialList();
}

void Gui::clearCurrentModel()
{
	currentModel = nullptr;
	levelModels.clear();
	clearObjectList();
	sceneLoaded = false;
	materialSearch.clear();
	materialSearchActive = false;
	materialFilterDirty = true;
	materialListScroll = 0.0f;
	hoveredMaterialItem = -1;
	layoutMaterialList();
}

bool Gui::hasMaterialPanel() const
{
	return currentModel != nullptr || !levelModels.empty();
}

int Gui::materialMeshCount() const
{
	if (currentModel != nullptr)
		return currentModel->getMeshCount();

	int count = 0;
	for (const Model* model : levelModels)
	{
		if (model != nullptr)
			count += model->getMeshCount();
	}
	return count;
}

Mesh* Gui::materialMeshAt(int flatIndex)
{
	if (flatIndex < 0)
		return nullptr;

	if (currentModel != nullptr)
	{
		auto& meshes = currentModel->getMeshes();
		if (flatIndex >= static_cast<int>(meshes.size()))
			return nullptr;
		return meshes[flatIndex];
	}

	int cursor = 0;
	for (Model* model : levelModels)
	{
		if (model == nullptr)
			continue;
		auto& meshes = model->getMeshes();
		const int count = static_cast<int>(meshes.size());
		if (flatIndex < cursor + count)
			return meshes[flatIndex - cursor];
		cursor += count;
	}
	return nullptr;
}

void Gui::rebuildMaterialFilter()
{
	if (!materialFilterDirty)
		return;

	materialFilterDirty = false;
	materialFiltered.clear();
	const int count = materialMeshCount();
	for (int i = 0; i < count; i++)
	{
		Mesh* mesh = materialMeshAt(i);
		if (mesh == nullptr)
			continue;
		if (materialSearch.empty()
			|| containsCaseInsensitive(mesh->getPartName(), materialSearch)
			|| containsCaseInsensitive(mesh->getMaterialName(), materialSearch))
			materialFiltered.push_back(i);
	}
}

void Gui::layoutMaterialList()
{
	if (!hasMaterialPanel())
	{
		materialListRect = {(float)windowWidth - 310.0f, 170.0f, 300.0f, 200.0f};
		maxMaterialListScroll = 0.0f;
		return;
	}

	if (materialListCollapsed)
	{
		materialListRect = { (float)windowWidth - 310.0f, 170.0f, 300.0f, kCollapsedListHeight };
		return;
	}

	rebuildMaterialFilter();
	const float kBottomPad = 5.0f;
	const float contentHeight = kMeshPartHeaderHeight
		+ (static_cast<float>(materialFiltered.size()) * kMeshPartItemHeight)
		+ kBottomPad;
	const float maxHeight = windowHeight * 0.7f;
	const float panelHeight = (contentHeight < maxHeight) ? contentHeight : maxHeight;
	materialListRect = {(float)windowWidth - 310.0f, 170.0f, 300.0f, panelHeight};
	maxMaterialListScroll = (contentHeight > panelHeight) ? (contentHeight - panelHeight) : 0.0f;
	if (materialListScroll < 0.0f)
		materialListScroll = 0.0f;
	if (materialListScroll > maxMaterialListScroll)
		materialListScroll = maxMaterialListScroll;
}

GuiRect Gui::materialSearchRect() const
{
	return { materialListRect.x + 8.0f, materialListRect.y + 60.0f, materialListRect.width - 16.0f, 20.0f };
}

void Gui::clearObjectList()
{
	levelObjectItems.clear();
	objectSearch.clear();
	objectSearchActive = false;
	objectKindFilter = 0;
	objectFilterDirty = true;
	objectFiltered.clear();
	objectListScroll = 0.0f;
	maxObjectListScroll = 0.0f;
	hoveredObjectItem = -1;
	selectedObjectIndex = -1;
	lastObjectClickIndex = -1;
	lastObjectClickTime = 0.0;
	objectInfoLines.clear();
	infoDrawLines.clear();
	objectInfoScroll = 0.0f;
	maxObjectInfoScroll = 0.0f;
	hoveredInfoRow = -1;
	layoutObjectList();
}

void Gui::setLevelObjects(const std::vector<LevelObjectItem>& objects)
{
	levelObjectItems = objects;
	objectSearch.clear();
	objectSearchActive = false;
	objectKindFilter = 0;
	objectFilterDirty = true;
	objectListScroll = 0.0f;
	hoveredObjectItem = -1;
	selectedObjectIndex = -1;
	lastObjectClickIndex = -1;
	lastObjectClickTime = 0.0;
	objectInfoLines.clear();
	infoDrawLines.clear();
	objectInfoScroll = 0.0f;
	maxObjectInfoScroll = 0.0f;
	hoveredInfoRow = -1;
	if (!objects.empty())
		sceneLoaded = true;
	layoutObjectList();
}

void Gui::setObjectInfo(std::vector<ObjectInfoLine> lines)
{
	objectInfoLines = std::move(lines);
	objectInfoScroll = 0.0f;
	hoveredInfoRow = -1;
	layoutObjectList();
}

void Gui::setOnLevelObjectToggled(std::function<void(int, bool)> callback)
{
	onLevelObjectToggled = std::move(callback);
}

void Gui::setOnPartVisibilityChanged(std::function<void()> callback)
{
	onPartVisibilityChanged = std::move(callback);
}

void Gui::setOnLevelObjectSelected(std::function<void(int)> callback)
{
	onLevelObjectSelected = std::move(callback);
}

void Gui::setOnLevelObjectFocused(std::function<void(int)> callback)
{
	onLevelObjectFocused = std::move(callback);
}

void Gui::setObjectVisible(int index, bool visible)
{
	if (index < 0 || index >= static_cast<int>(levelObjectItems.size()))
		return;
	levelObjectItems[static_cast<size_t>(index)].visible = visible;
	if (onLevelObjectToggled)
		onLevelObjectToggled(index, visible);
}

void Gui::setRenderStats(int drawCalls, int instancedBatches, int instancesVisited, int instancesCulled,
	int partsVisited, int partsCulled, long long trianglesDrawn, float sceneMs)
{
	statsValid = true;
	statDrawCalls = drawCalls;
	statInstancedBatches = instancedBatches;
	statInstancesVisited = instancesVisited;
	statInstancesCulled = instancesCulled;
	statPartsVisited = partsVisited;
	statPartsCulled = partsCulled;
	statTrianglesDrawn = trianglesDrawn;
	statSceneMs = sceneMs;
}

void Gui::renderSceneStatsBar()
{
	if (!statsValid)
		return;

	const float height = kSceneStatsBarHeight;
	const float y = static_cast<float>(windowHeight) - height;

	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	drawRect(0.0f, y, static_cast<float>(windowWidth), height, glm::vec4(0.08f, 0.08f, 0.09f, 0.92f));
	drawRect(0.0f, y, static_cast<float>(windowWidth), 1.0f, glm::vec4(0.4f, 0.4f, 0.4f, 1.0f));

	char buf[224];
	std::snprintf(buf, sizeof(buf),
		"SCENE  objects %d (%d culled)  parts %d (%d culled)  draws %d (%d batched)  tris %lld  %.2fms",
		statInstancesVisited, statInstancesCulled, statPartsVisited, statPartsCulled,
		statDrawCalls, statInstancedBatches, statTrianglesDrawn, statSceneMs);
	drawText(buf, 10.0f, y + 6.0f, glm::vec4(0.75f, 0.9f, 0.8f, 1.0f));
}

void Gui::rebuildObjectFilter()
{
	if (!objectFilterDirty)
		return;

	objectFilterDirty = false;
	objectFiltered.clear();
	for (int i = 0; i < static_cast<int>(levelObjectItems.size()); i++)
	{
		const LevelObjectItem& item = levelObjectItems[static_cast<size_t>(i)];
		if (objectKindFilter != 0 && (kindBitForLabel(item.kindLabel) & objectKindFilter) == 0)
			continue;
		if (objectSearch.empty()
			|| containsCaseInsensitive(item.typeName, objectSearch)
			|| containsCaseInsensitive(item.modelFile, objectSearch)
			|| containsCaseInsensitive(item.idLabel, objectSearch))
			objectFiltered.push_back(i);
	}
}

void Gui::layoutObjectList()
{
	if (levelObjectItems.empty())
	{
		objectListRect = { 0.0f, 0.0f, 0.0f, 0.0f };
		objectInfoRect = { 0.0f, 0.0f, 0.0f, 0.0f };
		maxObjectListScroll = 0.0f;
		maxObjectInfoScroll = 0.0f;
		infoDrawLines.clear();
		return;
	}

	rebuildObjectFilter();
	const float kBottomPad = 5.0f;
	const float kGap = 8.0f;
	// Leave room for the scene stats bar (Gui::renderSceneStatsBar), which is
	// always drawn across the bottom of the window once a level has rendered a
	// frame, so the OBJECTS/INFO column never sits behind it.
	const float bottom = (float)windowHeight - 12.0f - kSceneStatsBarHeight;
	const float available = std::max(0.0f, bottom - kObjectColumnTop);
	// INFO only has something to show once an object has been clicked. Hiding it
	// otherwise (zero-size rect; renderObjectInfo already skips anything under 8px)
	// also frees that space back to the list instead of leaving a blank gap.
	const bool showInfo = selectedObjectIndex >= 0;

	if (objectListCollapsed)
	{
		const float listHeight = kCollapsedListHeight;
		objectListRect = { 10.0f, kObjectColumnTop, kObjectColumnWidth, listHeight };
		if (showInfo)
		{
			const float inspectorHeight = std::max(0.0f, available - kGap - listHeight);
			objectInfoRect = {
				10.0f,
				kObjectColumnTop + listHeight + kGap,
				kObjectColumnWidth,
				inspectorHeight
			};
		}
		else
		{
			objectInfoRect = { 0.0f, 0.0f, 0.0f, 0.0f };
		}
		rebuildInfoDrawLines();
		return;
	}

	const float contentHeight = kObjectListHeaderHeight
		+ (static_cast<float>(objectFiltered.size()) * kMeshPartItemHeight)
		+ kBottomPad;
	const float listMin = kObjectListHeaderHeight;

	// renderObjectList's rowFits() only draws a row that fits entirely inside the
	// panel, so a height that isn't a whole number of rows below the header leaves
	// blank background under the last full row. Round a capped budget down to the
	// nearest row boundary (reusing the same header + N*item + pad shape the
	// uncapped, exact-fit case already produces) so that remainder never shows.
	auto quantizeListHeight = [&](float budget) -> float
	{
		if (budget <= kObjectListHeaderHeight + kBottomPad)
			return budget;
		const float rows = std::floor((budget - kObjectListHeaderHeight - kBottomPad) / kMeshPartItemHeight);
		return kObjectListHeaderHeight + std::max(0.0f, rows) * kMeshPartItemHeight + kBottomPad;
	};

	if (!showInfo)
	{
		float listHeight = contentHeight;
		if (contentHeight > available)
			listHeight = quantizeListHeight(available);
		objectListRect = { 10.0f, kObjectColumnTop, kObjectColumnWidth, listHeight };
		objectInfoRect = { 0.0f, 0.0f, 0.0f, 0.0f };
		maxObjectListScroll = (contentHeight > listHeight) ? (contentHeight - listHeight) : 0.0f;
		if (objectListScroll < 0.0f)
			objectListScroll = 0.0f;
		if (objectListScroll > maxObjectListScroll)
			objectListScroll = maxObjectListScroll;
		rebuildInfoDrawLines();
		return;
	}

	float inspectorHeight = available * 0.38f;
	if (available >= 360.0f)
		inspectorHeight = std::max(220.0f, inspectorHeight);
	if (inspectorHeight > available - kGap - listMin)
		inspectorHeight = std::max(60.0f, available - kGap - listMin);

	const float listRoom = std::max(0.0f, available - kGap - inspectorHeight);
	const float listHeight = (contentHeight < listRoom) ? contentHeight : quantizeListHeight(listRoom);
	// Hand back whatever a short list or the row-quantization above didn't use, so
	// INFO gets the leftover instead of it sitting empty at the bottom of OBJECTS.
	inspectorHeight = std::max(0.0f, available - kGap - listHeight);

	objectListRect = { 10.0f, kObjectColumnTop, kObjectColumnWidth, listHeight };
	objectInfoRect = {
		10.0f,
		kObjectColumnTop + listHeight + kGap,
		kObjectColumnWidth,
		inspectorHeight
	};
	maxObjectListScroll = (contentHeight > listHeight) ? (contentHeight - listHeight) : 0.0f;
	if (objectListScroll < 0.0f)
		objectListScroll = 0.0f;
	if (objectListScroll > maxObjectListScroll)
		objectListScroll = maxObjectListScroll;
	rebuildInfoDrawLines();
}

GuiRect Gui::objectSearchRect() const
{
	return { objectListRect.x + 8.0f, objectListRect.y + 60.0f, objectListRect.width - 16.0f, 20.0f };
}

GuiRect Gui::objectKindChipRect(int index) const
{
	const int columns = 4;
	const float gap = 4.0f;
	const int col = index % columns;
	const int row = index / columns;
	const float width = (objectListRect.width - 16.0f - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
	return {
		objectListRect.x + 8.0f + static_cast<float>(col) * (width + gap),
		objectListRect.y + 84.0f + static_cast<float>(row) * 22.0f,
		width,
		18.0f
	};
}

void Gui::drawCollapseButton(const GuiRect& rect, bool collapsed)
{
	const bool hovered = rect.contains(mouseX, mouseY);
	drawRect(rect.x, rect.y, rect.width, rect.height,
		hovered ? glm::vec4(0.34f, 0.34f, 0.38f, 1.0f) : glm::vec4(0.20f, 0.20f, 0.22f, 1.0f));
	const glm::vec4 icon(0.92f, 0.92f, 0.92f, 1.0f);
	if (collapsed)
	{
		drawRect(rect.x + 3.0f, rect.y + rect.height * 0.5f - 1.0f, rect.width - 6.0f, 2.0f, icon);
		return;
	}
	const float x = rect.x + 4.0f;
	const float y = rect.y + 4.0f;
	const float size = rect.width - 8.0f;
	drawRect(x, y, size, 1.0f, icon);
	drawRect(x, y + size - 1.0f, size, 1.0f, icon);
	drawRect(x, y + 1.0f, 1.0f, size - 2.0f, icon);
	drawRect(x + size - 1.0f, y + 1.0f, 1.0f, size - 2.0f, icon);
}

void Gui::drawTextButton(const GuiRect& rect, const char* label, const glm::vec4& fill, const glm::vec4& hoverFill)
{
	const bool hovered = rect.contains(mouseX, mouseY);
	drawRect(rect.x, rect.y, rect.width, rect.height, hovered ? hoverFill : fill);
	const float textWidth = static_cast<float>(std::strlen(label)) * 8.0f;
	drawText(label, rect.x + (rect.width - textWidth) * 0.5f, rect.y + 5.0f, glm::vec4(0.95f, 0.95f, 0.95f, 1.0f));
}

void Gui::renderObjectList()
{
	if (levelObjectItems.empty())
		return;

	const float kItemBoxHeight = 30.0f;
	const float kNameLineY = 4.0f;
	const float kTagsLineY = 16.0f;

	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	auto bindRectShader = [&]()
	{
		glUseProgram(shaderProgram);
		glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	};

	drawRect(objectListRect.x, objectListRect.y, objectListRect.width, objectListRect.height, glm::vec4(0.12f, 0.12f, 0.14f, 0.95f));
	drawRect(objectListRect.x, objectListRect.y, objectListRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(objectListRect.x, objectListRect.y + objectListRect.height - 2.0f, objectListRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(objectListRect.x, objectListRect.y, 2.0f, objectListRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(objectListRect.x + objectListRect.width - 2.0f, objectListRect.y, 2.0f, objectListRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));

	rebuildObjectFilter();
	int visibleCount = 0;
	for (const LevelObjectItem& item : levelObjectItems)
	{
		if (item.visible)
			visibleCount++;
	}

	drawText("OBJECTS", objectListRect.x + 10.0f, objectListRect.y + 10.0f, glm::vec4(0.6f, 1.0f, 0.7f, 1.0f));
	bindRectShader();
	drawCollapseButton(collapseButtonRect(objectListRect), objectListCollapsed);
	if (objectListCollapsed)
		return;

	drawText("Visible: " + std::to_string(visibleCount) + "/" + std::to_string(levelObjectItems.size()),
		objectListRect.x + 10.0f, objectListRect.y + 24.0f,
		glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));

	bindRectShader();
	const BulkButtonRects bulk = bulkButtonRects(objectListRect);
	const glm::vec4 defaultFill(0.20f, 0.32f, 0.48f, 0.95f);
	const glm::vec4 defaultHover(0.28f, 0.44f, 0.64f, 0.98f);
	const glm::vec4 showFill(0.18f, 0.42f, 0.18f, 0.95f);
	const glm::vec4 showHover(0.25f, 0.55f, 0.25f, 0.98f);
	const glm::vec4 hideFill(0.45f, 0.18f, 0.18f, 0.95f);
	const glm::vec4 hideHover(0.62f, 0.25f, 0.25f, 0.98f);
	drawTextButton(bulk.showDefault, "SHOW DEFAULT", defaultFill, defaultHover);
	bindRectShader();
	drawTextButton(bulk.showAll, "SHOW ALL", showFill, showHover);
	bindRectShader();
	drawTextButton(bulk.hideAll, "HIDE ALL", hideFill, hideHover);
	bindRectShader();
	const GuiRect searchRect = objectSearchRect();
	const glm::vec4 searchBg = objectSearchActive ? glm::vec4(0.22f, 0.22f, 0.22f, 1.0f) : glm::vec4(0.18f, 0.18f, 0.18f, 1.0f);
	const glm::vec4 searchBorder = objectSearchActive ? glm::vec4(0.7f, 0.7f, 0.7f, 1.0f) : glm::vec4(0.45f, 0.45f, 0.45f, 1.0f);
	drawRect(searchRect.x, searchRect.y, searchRect.width, searchRect.height, searchBg);
	drawRect(searchRect.x, searchRect.y, searchRect.width, 2.0f, searchBorder);
	drawRect(searchRect.x, searchRect.y + searchRect.height - 2.0f, searchRect.width, 2.0f, searchBorder);
	drawRect(searchRect.x, searchRect.y, 2.0f, searchRect.height, searchBorder);
	drawRect(searchRect.x + searchRect.width - 2.0f, searchRect.y, 2.0f, searchRect.height, searchBorder);

	std::string searchText = objectSearch;
	if (searchText.empty())
		searchText = objectSearchActive ? "_" : "Search object or model";
	else if (objectSearchActive)
		searchText += "_";
	const int maxChars = (int)((searchRect.width - 12.0f) / 8.0f);
	if ((int)searchText.size() > maxChars && maxChars > 3)
		searchText = "..." + searchText.substr(searchText.size() - (maxChars - 3));
	drawText(searchText, searchRect.x + 6.0f, searchRect.y + 6.0f, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
	bindRectShader();

	for (int chip = 0; chip < kKindChipCount; chip++)
	{
		const GuiRect chipRect = objectKindChipRect(chip);
		const bool active = kKindChips[chip].bit == 0
			? objectKindFilter == 0
			: (objectKindFilter & kKindChips[chip].bit) != 0;
		const bool hovered = chipRect.contains(mouseX, mouseY);
		glm::vec4 fill = active
			? glm::vec4(kKindChips[chip].color.r * 0.45f, kKindChips[chip].color.g * 0.45f, kKindChips[chip].color.b * 0.45f, 0.98f)
			: glm::vec4(0.18f, 0.18f, 0.18f, 1.0f);
		if (hovered)
			fill += glm::vec4(0.08f, 0.08f, 0.08f, 0.0f);
		drawRect(chipRect.x, chipRect.y, chipRect.width, chipRect.height, fill);
		const glm::vec4 edge = active ? kKindChips[chip].color : glm::vec4(0.35f, 0.35f, 0.35f, 1.0f);
		drawRect(chipRect.x, chipRect.y, chipRect.width, 1.0f, edge);
		drawRect(chipRect.x, chipRect.y + chipRect.height - 1.0f, chipRect.width, 1.0f, edge);
		drawRect(chipRect.x, chipRect.y, 1.0f, chipRect.height, edge);
		drawRect(chipRect.x + chipRect.width - 1.0f, chipRect.y, 1.0f, chipRect.height, edge);
		const std::string label = kKindChips[chip].label;
		const float textWidth = static_cast<float>(label.size()) * 8.0f;
		const float textX = chipRect.x + std::max(2.0f, (chipRect.width - textWidth) * 0.5f);
		const glm::vec4 textColor = active ? kKindChips[chip].color : glm::vec4(0.55f, 0.55f, 0.55f, 1.0f);
		drawText(label, textX, chipRect.y + 5.0f, textColor);
		bindRectShader();
	}

	if (objectListScroll < 0.0f)
		objectListScroll = 0.0f;
	if (objectListScroll > maxObjectListScroll)
		objectListScroll = maxObjectListScroll;

	const float bodyTop = objectListRect.y + kObjectListHeaderHeight;
	const float bodyBottom = objectListRect.y + objectListRect.height - 3.0f;
	float yOffset = bodyTop - objectListScroll;
	for (size_t row = 0; row < objectFiltered.size(); row++)
	{
		if (rowFits(yOffset, bodyTop, bodyBottom, kItemBoxHeight))
		{
			const int index = objectFiltered[row];
			const LevelObjectItem& item = levelObjectItems[static_cast<size_t>(index)];
			const bool isEnabled = item.visible;
			const bool isHovered = (hoveredObjectItem == static_cast<int>(row));
			const bool isSelected = (index == selectedObjectIndex);
			glm::vec4 bgColor = glm::vec4(0.18f, 0.18f, 0.2f, 1.0f);
			if (isSelected)
				bgColor = isHovered ? glm::vec4(0.55f, 0.28f, 0.22f, 1.0f) : glm::vec4(0.42f, 0.20f, 0.16f, 1.0f);
			else if (isHovered)
				bgColor = glm::vec4(0.25f, 0.25f, 0.28f, 1.0f);

			bindRectShader();
			drawRect(objectListRect.x + 5.0f, yOffset, objectListRect.width - 10.0f, kItemBoxHeight, bgColor);

			const glm::vec4 checkboxColor = isEnabled ? glm::vec4(0.3f, 0.7f, 0.3f, 1.0f) : glm::vec4(0.7f, 0.3f, 0.3f, 1.0f);
			drawRect(objectListRect.x + 10.0f, yOffset + 9.0f, 12.0f, 12.0f, checkboxColor);
			if (isEnabled)
				drawText("X", objectListRect.x + 11.0f, yOffset + 11.0f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
			bindRectShader();

			const int maxChars = std::max(8, (int)((objectListRect.width - 40.0f) / 8.0f));
			std::string displayType = item.typeName.empty() ? "object" : item.typeName;
			if ((int)displayType.length() > maxChars)
				displayType = displayType.substr(0, maxChars - 3) + "...";
			const glm::vec4 textColor = isEnabled ? glm::vec4(0.9f, 0.9f, 0.9f, 1.0f) : glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);
			drawText(displayType, objectListRect.x + 28.0f, yOffset + kNameLineY, textColor);

			std::string secondary;
			if (!item.kindLabel.empty())
				secondary += item.kindLabel;
			if (!item.idLabel.empty())
			{
				if (!secondary.empty())
					secondary += "  ";
				secondary += item.idLabel;
			}
			if (!item.modelFile.empty())
			{
				if (!secondary.empty())
					secondary += "  ";
				secondary += item.modelFile;
			}
			if (secondary.empty())
				secondary = "no model";
			glm::vec4 secondaryColor = item.modelFile.empty() && item.idLabel.empty()
				? glm::vec4(0.55f, 0.55f, 0.55f, 1.0f)
				: (isEnabled ? glm::vec4(0.65f, 0.85f, 0.7f, 1.0f) : glm::vec4(0.45f, 0.55f, 0.48f, 1.0f));
			if (!item.kindLabel.empty())
			{
				glm::vec4 kindColor = objectKindColour(item.kindLabel);
				if (!isEnabled)
					kindColor.a = 0.55f;
				secondaryColor = kindColor;
			}
			if ((int)secondary.length() > maxChars)
				secondary = secondary.substr(0, maxChars - 3) + "...";
			drawText(secondary, objectListRect.x + 28.0f, yOffset + kTagsLineY, secondaryColor);
			bindRectShader();
		}
		yOffset += kMeshPartItemHeight;
	}
}

void Gui::selectLevelObject(int index)
{
	if (index < 0 || index >= static_cast<int>(levelObjectItems.size()))
		return;
	selectedObjectIndex = index;
	rebuildObjectFilter();
	bool listed = false;
	for (int filtered : objectFiltered)
	{
		if (filtered == index)
		{
			listed = true;
			break;
		}
	}
	if (!listed && (!objectSearch.empty() || objectKindFilter != 0))
	{
		objectSearch.clear();
		objectSearchActive = false;
		objectKindFilter = 0;
		objectFilterDirty = true;
	}
	revealLevelObject(index);
	if (onLevelObjectSelected)
		onLevelObjectSelected(index);
}

void Gui::revealLevelObject(int index)
{
	rebuildObjectFilter();
	int row = -1;
	for (int i = 0; i < static_cast<int>(objectFiltered.size()); i++)
	{
		if (objectFiltered[static_cast<size_t>(i)] == index)
		{
			row = i;
			break;
		}
	}
	if (row < 0)
		return;
	const float rowTop = static_cast<float>(row) * kMeshPartItemHeight;
	if (objectListCollapsed)
		return;
	const float view = objectListRect.height - kObjectListHeaderHeight;
	if (view <= 0.0f)
		return;
	if (rowTop < objectListScroll)
		objectListScroll = rowTop;
	else if (rowTop + kMeshPartItemHeight > objectListScroll + view)
		objectListScroll = rowTop + kMeshPartItemHeight - view;
	if (objectListScroll < 0.0f)
		objectListScroll = 0.0f;
	if (objectListScroll > maxObjectListScroll)
		objectListScroll = maxObjectListScroll;
}

void Gui::rebuildInfoDrawLines()
{
	infoDrawLines.clear();
	if (objectInfoRect.width < 16.0f)
	{
		maxObjectInfoScroll = 0.0f;
		return;
	}

	auto wrap = [](const std::string& text, int maxChars, std::vector<std::string>& out)
	{
		if (maxChars < 8)
			maxChars = 8;
		if (text.empty())
		{
			out.emplace_back();
			return;
		}
		size_t i = 0;
		while (i < text.size())
		{
			size_t count = std::min(static_cast<size_t>(maxChars), text.size() - i);
			if (i + count < text.size())
			{
				const size_t space = text.rfind(' ', i + count);
				if (space != std::string::npos && space > i)
					count = space - i;
			}
			if (count == 0)
				count = 1;
			out.push_back(text.substr(i, count));
			i += count;
			while (i < text.size() && text[i] == ' ')
				i++;
		}
	};

	const float inner = objectInfoRect.width - 20.0f;
	if (selectedObjectIndex < 0 || objectInfoLines.empty())
	{
		InfoDrawLine row;
		row.text = "Select an object";
		row.dim = true;
		infoDrawLines.push_back(row);
	}
	else
	{
		for (const ObjectInfoLine& line : objectInfoLines)
		{
			const float indent = static_cast<float>(std::max(0, line.indent)) * 12.0f;
			int maxChars = (int)((inner - indent) / 8.0f);
			std::vector<std::string> pieces;
			wrap(line.text, maxChars, pieces);
			for (const std::string& piece : pieces)
			{
				InfoDrawLine row;
				row.text = piece;
				row.link = line.link;
				row.dim = line.dim;
				row.heading = line.heading;
				row.indent = indent;
				infoDrawLines.push_back(std::move(row));
			}
		}
	}

	const float contentHeight = kInfoHeaderHeight
		+ static_cast<float>(infoDrawLines.size()) * kInfoLineHeight
		+ 8.0f;
	maxObjectInfoScroll = (contentHeight > objectInfoRect.height) ? (contentHeight - objectInfoRect.height) : 0.0f;
	if (objectInfoScroll < 0.0f)
		objectInfoScroll = 0.0f;
	if (objectInfoScroll > maxObjectInfoScroll)
		objectInfoScroll = maxObjectInfoScroll;
}

void Gui::renderObjectInfo()
{
	if (levelObjectItems.empty() || objectInfoRect.height < 8.0f)
		return;

	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	auto bindRectShader = [&]()
	{
		glUseProgram(shaderProgram);
		glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	};

	drawRect(objectInfoRect.x, objectInfoRect.y, objectInfoRect.width, objectInfoRect.height, glm::vec4(0.12f, 0.12f, 0.14f, 0.95f));
	drawRect(objectInfoRect.x, objectInfoRect.y, objectInfoRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(objectInfoRect.x, objectInfoRect.y + objectInfoRect.height - 2.0f, objectInfoRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(objectInfoRect.x, objectInfoRect.y, 2.0f, objectInfoRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(objectInfoRect.x + objectInfoRect.width - 2.0f, objectInfoRect.y, 2.0f, objectInfoRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawText("INFO", objectInfoRect.x + 10.0f, objectInfoRect.y + 8.0f, glm::vec4(1.0f, 1.0f, 0.55f, 1.0f));
	bindRectShader();

	if (objectInfoScroll < 0.0f)
		objectInfoScroll = 0.0f;
	if (objectInfoScroll > maxObjectInfoScroll)
		objectInfoScroll = maxObjectInfoScroll;

	const float listTop = objectInfoRect.y + kInfoHeaderHeight;
	const float listBottom = objectInfoRect.y + objectInfoRect.height - 4.0f;
	float y = listTop - objectInfoScroll;
	for (size_t row = 0; row < infoDrawLines.size(); row++)
	{
		const InfoDrawLine& line = infoDrawLines[row];
		if (y >= listTop - 1.0f && y + kInfoLineHeight <= listBottom + 1.0f)
		{
			const bool hovered = hoveredInfoRow == static_cast<int>(row);
			if (line.link >= 0)
			{
				bindRectShader();
				const glm::vec4 rowColor = hovered
					? glm::vec4(0.22f, 0.32f, 0.42f, 1.0f)
					: glm::vec4(0.16f, 0.2f, 0.26f, 1.0f);
				drawRect(objectInfoRect.x + 4.0f, y - 1.0f, objectInfoRect.width - 8.0f, kInfoLineHeight, rowColor);
			}
			glm::vec4 color(0.88f, 0.88f, 0.88f, 1.0f);
			if (line.heading)
				color = glm::vec4(0.75f, 0.9f, 1.0f, 1.0f);
			else if (line.link >= 0)
				color = hovered ? glm::vec4(0.75f, 0.95f, 1.0f, 1.0f) : glm::vec4(0.55f, 0.82f, 1.0f, 1.0f);
			else if (line.dim)
				color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
			drawText(line.text, objectInfoRect.x + 10.0f + line.indent, y, color);
			bindRectShader();
		}
		y += kInfoLineHeight;
	}
}

void Gui::renderModelInfo()
{
	if (!currentModel) return;
	
	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	
	// Draw background
	drawRect(modelInfoRect.x, modelInfoRect.y, modelInfoRect.width, modelInfoRect.height, glm::vec4(0.15f, 0.15f, 0.15f, 0.95f));
	
	// Draw border
	drawRect(modelInfoRect.x, modelInfoRect.y, modelInfoRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(modelInfoRect.x, modelInfoRect.y + modelInfoRect.height - 2.0f, modelInfoRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(modelInfoRect.x, modelInfoRect.y, 2.0f, modelInfoRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(modelInfoRect.x + modelInfoRect.width - 2.0f, modelInfoRect.y, 2.0f, modelInfoRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	
	float yOffset = modelInfoRect.y + 10.0f;
	
	// Title
	drawText("MODEL INFO", modelInfoRect.x + 10.0f, yOffset, glm::vec4(1.0f, 1.0f, 0.5f, 1.0f));
	yOffset += 15.0f;
	
	// Model name (truncated if too long)
	std::string displayName = currentModelName;
	if (displayName.length() > 32)
		displayName = displayName.substr(0, 29) + "...";
	drawText("Name: " + displayName, modelInfoRect.x + 10.0f, yOffset, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
	yOffset += 15.0f;
	
	// Mesh count
	{
		int visibleCount = 0;
		for (const Mesh* m : currentModel->getMeshes())
		{
			if (m && m->isEnabled()) visibleCount++;
		}
		drawText("Parts: " + std::to_string(currentModel->getMeshCount()), modelInfoRect.x + 10.0f, yOffset, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
		yOffset += 15.0f;
		drawText("Visible: " + std::to_string(visibleCount), modelInfoRect.x + 10.0f, yOffset, glm::vec4(0.75f, 0.75f, 0.75f, 1.0f));
		yOffset += 15.0f;
	}
	
	// Vertex count
	drawText("Vertices: " + std::to_string(currentModel->getTotalVertexCount()), modelInfoRect.x + 10.0f, yOffset, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
	yOffset += 15.0f;
	
	// Triangle count
	drawText("Triangles: " + std::to_string(currentModel->getTotalTriangleCount()), modelInfoRect.x + 10.0f, yOffset, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
	yOffset += 15.0f;
	
	// Bounds info
	std::string boundsStr = "Bounds: " + 
		std::to_string((int)currentModel->bounds_size.x) + "x" + 
		std::to_string((int)currentModel->bounds_size.y) + "x" + 
		std::to_string((int)currentModel->bounds_size.z);
	drawText(boundsStr, modelInfoRect.x + 10.0f, yOffset, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
	yOffset += 15.0f;
	
	// Collider count
	drawText("Colliders: " + std::to_string(currentModel->colliders.size()), modelInfoRect.x + 10.0f, yOffset, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
}

void Gui::renderMaterialList()
{
	if (!hasMaterialPanel()) return;

	const float kItemBoxHeight = 30.0f;  // background fill for the item
	const float kNameLineY = 4.0f;
	const float kTagsLineY = 16.0f;
	
	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	auto bindRectShader = [&]()
	{
		glUseProgram(shaderProgram);
		glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	};
	
	// Draw background
	drawRect(materialListRect.x, materialListRect.y, materialListRect.width, materialListRect.height, glm::vec4(0.12f, 0.12f, 0.12f, 0.95f));
	
	// Draw border
	drawRect(materialListRect.x, materialListRect.y, materialListRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(materialListRect.x, materialListRect.y + materialListRect.height - 2.0f, materialListRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(materialListRect.x, materialListRect.y, 2.0f, materialListRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(materialListRect.x + materialListRect.width - 2.0f, materialListRect.y, 2.0f, materialListRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	
	// Header text + bulk actions
	rebuildMaterialFilter();
	int visibleCount = 0;
	const int meshTotal = materialMeshCount();
	for (int i = 0; i < meshTotal; i++)
	{
		const Mesh* mesh = materialMeshAt(i);
		if (mesh && mesh->isEnabled()) visibleCount++;
	}

	const char* title = levelModels.empty() ? "MESH PARTS" : "LEVEL PARTS";
	drawText(title, materialListRect.x + 10.0f, materialListRect.y + 10.0f, glm::vec4(1.0f, 1.0f, 0.5f, 1.0f));
	bindRectShader();
	drawCollapseButton(collapseButtonRect(materialListRect), materialListCollapsed);
	if (materialListCollapsed)
		return;

	drawText("Visible: " + std::to_string(visibleCount) + "/" + std::to_string(meshTotal),
	         materialListRect.x + 10.0f, materialListRect.y + 24.0f,
	         glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));

	// Bulk action buttons (keep geometry in sync with onMouseButton)
	bindRectShader();
	const BulkButtonRects bulk = bulkButtonRects(materialListRect);
	const glm::vec4 defaultFill(0.20f, 0.32f, 0.48f, 0.95f);
	const glm::vec4 defaultHover(0.28f, 0.44f, 0.64f, 0.98f);
	const glm::vec4 showFill(0.18f, 0.42f, 0.18f, 0.95f);
	const glm::vec4 showHover(0.25f, 0.55f, 0.25f, 0.98f);
	const glm::vec4 hideFill(0.45f, 0.18f, 0.18f, 0.95f);
	const glm::vec4 hideHover(0.62f, 0.25f, 0.25f, 0.98f);
	drawTextButton(bulk.showDefault, "SHOW DEFAULT", defaultFill, defaultHover);
	bindRectShader();
	drawTextButton(bulk.showAll, "SHOW ALL", showFill, showHover);
	bindRectShader();
	drawTextButton(bulk.hideAll, "HIDE ALL", hideFill, hideHover);
	bindRectShader();
	const GuiRect searchRect = materialSearchRect();
	const glm::vec4 searchBg = materialSearchActive ? glm::vec4(0.22f, 0.22f, 0.22f, 1.0f) : glm::vec4(0.18f, 0.18f, 0.18f, 1.0f);
	const glm::vec4 searchBorder = materialSearchActive ? glm::vec4(0.7f, 0.7f, 0.7f, 1.0f) : glm::vec4(0.45f, 0.45f, 0.45f, 1.0f);
	drawRect(searchRect.x, searchRect.y, searchRect.width, searchRect.height, searchBg);
	drawRect(searchRect.x, searchRect.y, searchRect.width, 2.0f, searchBorder);
	drawRect(searchRect.x, searchRect.y + searchRect.height - 2.0f, searchRect.width, 2.0f, searchBorder);
	drawRect(searchRect.x, searchRect.y, 2.0f, searchRect.height, searchBorder);
	drawRect(searchRect.x + searchRect.width - 2.0f, searchRect.y, 2.0f, searchRect.height, searchBorder);

	std::string searchText = materialSearch;
	if (searchText.empty())
		searchText = materialSearchActive ? "_" : "Search part or material";
	else if (materialSearchActive)
		searchText += "_";
	const int maxChars = (int)((searchRect.width - 12.0f) / 8.0f);
	if ((int)searchText.size() > maxChars && maxChars > 3)
		searchText = "..." + searchText.substr(searchText.size() - (maxChars - 3));
	drawText(searchText, searchRect.x + 6.0f, searchRect.y + 6.0f, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
	bindRectShader();
	
	// Clamp scroll in case panel was resized
	if (materialListScroll < 0.0f) materialListScroll = 0.0f;
	if (materialListScroll > maxMaterialListScroll) materialListScroll = maxMaterialListScroll;

	const float bodyTop = materialListRect.y + kMeshPartHeaderHeight;
	const float bodyBottom = materialListRect.y + materialListRect.height - 3.0f;
	float yOffset = bodyTop - materialListScroll;
	
	for (size_t row = 0; row < materialFiltered.size(); row++)
	{
		if (rowFits(yOffset, bodyTop, bodyBottom, kItemBoxHeight))
		{
			Mesh* mesh = materialMeshAt(materialFiltered[row]);
			if (mesh == nullptr)
			{
				yOffset += kMeshPartItemHeight;
				continue;
			}
			bool isEnabled = mesh->isEnabled();
			bool isHovered = (hoveredMaterialItem == (int)row);
			
			// Background color
			glm::vec4 bgColor;
			if (isHovered)
				bgColor = glm::vec4(0.25f, 0.25f, 0.25f, 1.0f);
			else
				bgColor = glm::vec4(0.18f, 0.18f, 0.18f, 1.0f);
			
			bindRectShader();
			drawRect(materialListRect.x + 5.0f, yOffset, materialListRect.width - 10.0f, kItemBoxHeight, bgColor);
			
			// Checkbox
			glm::vec4 checkboxColor = isEnabled ? glm::vec4(0.3f, 0.7f, 0.3f, 1.0f) : glm::vec4(0.7f, 0.3f, 0.3f, 1.0f);
			drawRect(materialListRect.x + 10.0f, yOffset + 9.0f, 12.0f, 12.0f, checkboxColor);
			
			// Checkbox check mark (if enabled)
			if (isEnabled)
			{
				drawText("X", materialListRect.x + 11.0f, yOffset + 11.0f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
			}
			bindRectShader();
			
			// Part/component name (primary) + material/texture slot (secondary)
			std::string partName = mesh->getPartName();
			if (partName.empty()) partName = "part_" + std::to_string(materialFiltered[row]);

			std::string matName = mesh->getMaterialName();
			if (matName.empty()) matName = "unknown";

			ParsedMaterialName parsed = parseMaterialName(matName);
			std::string tagText;
			if (parsed.flags & MAT_GLASS)
			{
				if (!tagText.empty()) tagText += ",";
				tagText += " GLASS";
			}
			if (parsed.flags & MAT_SPEC)
			{
				if (!tagText.empty()) tagText += ",";
				tagText += " SPEC";
			}
			if (parsed.flags & MAT_OVERLAY)
			{
				if (!tagText.empty()) tagText += ",";
				tagText += " OVERLAY";
			}
			if (!tagText.empty())
			{
				tagText = tagText.substr(1); // remove leading space
			}

			std::string displayPart = partName;
			if (displayPart.length() > 26)
				displayPart = displayPart.substr(0, 23) + "...";
			
			glm::vec4 textColor = isEnabled ? glm::vec4(0.9f, 0.9f, 0.9f, 1.0f) : glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);
			drawText(displayPart, materialListRect.x + 28.0f, yOffset + kNameLineY, textColor);

			// Secondary line: material name + tags
			std::string secondary = matName;
			if (!tagText.empty())
				secondary += "  " + tagText;
			if (secondary.length() > 30)
				secondary = secondary.substr(0, 27) + "...";
			glm::vec4 secondaryColor = isEnabled ? glm::vec4(0.65f, 0.75f, 1.0f, 1.0f) : glm::vec4(0.45f, 0.5f, 0.6f, 1.0f);
			drawText(secondary, materialListRect.x + 28.0f, yOffset + kTagsLineY, secondaryColor);

			// Counts on the right
			std::string counts = std::to_string(mesh->getVertexCount()) + "v " + std::to_string(mesh->getTriangleCount()) + "t";
			const float countsX = materialListRect.x + materialListRect.width - 10.0f - ((float)counts.size() * 8.0f);
			drawText(counts, countsX, yOffset + kNameLineY, glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));
			
			// Restore rectangle shader after text rendering (items draw rectangles each iteration).
			bindRectShader();
		}
		yOffset += kMeshPartItemHeight;
	}
}

void Gui::drawText(const std::string& text, float x, float y, const glm::vec4& color)
{
	glUseProgram(textShaderProgram);
	glUniformMatrix4fv(glGetUniformLocation(textShaderProgram, "projection"), 1, GL_FALSE, 
		glm::value_ptr(glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f)));
	glUniform4fv(glGetUniformLocation(textShaderProgram, "textColor"), 1, glm::value_ptr(color));
	
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, fontTexture);
	glUniform1i(glGetUniformLocation(textShaderProgram, "fontTexture"), 0);
	
	glBindVertexArray(textVAO);
	
	const float charWidth = 8.0f;
	const float charHeight = 8.0f;
	const float texCharWidth = 8.0f / 128.0f; // 16 chars per row * 8 = 128
	const float texCharHeight = 8.0f / 48.0f;  // 6 rows * 8 = 48
	
	float xPos = x;
	for (char c : text)
	{
		if (c < 32 || c > 126) continue; // Skip non-printable chars
		
		int charIndex = c - 32;
		int charCol = charIndex % 16;
		int charRow = charIndex / 16;
		
		float texX = charCol * texCharWidth;
		float texY = charRow * texCharHeight;
		
		float vertices[6][4] = {
			{ xPos, y, texX, texY },
			{ xPos + charWidth, y, texX + texCharWidth, texY },
			{ xPos + charWidth, y + charHeight, texX + texCharWidth, texY + texCharHeight },
			
			{ xPos, y, texX, texY },
			{ xPos + charWidth, y + charHeight, texX + texCharWidth, texY + texCharHeight },
			{ xPos, y + charHeight, texX, texY + texCharHeight }
		};
		
		glBindBuffer(GL_ARRAY_BUFFER, textVBO);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		
		xPos += charWidth;
	}
	
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
}
