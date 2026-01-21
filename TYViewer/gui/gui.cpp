#include "gui.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include "../debug.h"
#include "../model.h"
#include "../graphics/mesh.h"

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
	hoveredCategory(0),
	hoveredSubmenuItem(-1),
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
}

Gui::~Gui()
{
	cleanupGL();
}

void Gui::initialize(int width, int height)
{
	windowWidth = width;
	windowHeight = height;
	
	// Button at top of screen
	buttonRect = {10.0f, 10.0f, 200.0f, 30.0f};
	
	// Model info panel on the right
	modelInfoRect = {(float)width - 310.0f, 10.0f, 300.0f, 150.0f};
	
	// Material list panel below model info (default size, will resize when model is loaded)
	materialListRect = {(float)width - 310.0f, 170.0f, 300.0f, 200.0f};
	
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
	ty1Models.clear();
	ty2Models.clear();
	
	for (const auto& model : models)
	{
		if (model.archiveName == "TY1")
			ty1Models.push_back(model);
		else if (model.archiveName == "TY2")
			ty2Models.push_back(model);
	}
	
	// Simple dropdown with just 2 category items
	int categoryCount = 0;
	if (!ty1Models.empty()) categoryCount++;
	if (!ty2Models.empty()) categoryCount++;
	
	// Calculate height: tight padding + items
	float dropdownHeight = (categoryCount * 30.0f) + 6.0f; // 3px top + 3px bottom padding
	dropdownRect = {buttonRect.x, buttonRect.y + buttonRect.height + 2.0f, 200.0f, dropdownHeight};
	
	// Submenu will be calculated dynamically when hovering
}

void Gui::setOnModelSelected(std::function<void(const ModelEntry&)> callback)
{
	onModelSelected = callback;
}

void Gui::resize(int width, int height)
{
	windowWidth = width;
	windowHeight = height;
	
	// Update panel positions
	modelInfoRect = {(float)width - 310.0f, 10.0f, 300.0f, 150.0f};
	
	// Recalculate material list size if we have a model
	if (currentModel)
	{
		int materialCount = currentModel->getMeshCount();
		float contentHeight = 40.0f + (materialCount * 25.0f) + 10.0f;
		float maxHeight = height * 0.7f;
		float panelHeight = (contentHeight < maxHeight) ? contentHeight : maxHeight;
		
		materialListRect = {(float)width - 310.0f, 170.0f, 300.0f, panelHeight};
		maxMaterialListScroll = (contentHeight > panelHeight) ? (contentHeight - panelHeight + 40.0f) : 0.0f;
	}
	else
	{
		// Default size when no model
		materialListRect = {(float)width - 310.0f, 170.0f, 300.0f, 200.0f};
	}
}

void Gui::render()
{
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
	
	renderButton();
	
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
	{
		renderModelInfo();
		renderMaterialList();
	}
	
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
	
	// TY1 category
	if (!ty1Models.empty())
	{
		// Blue when hovered, gray when not
		glm::vec4 bgColor = (hoveredCategory == 1) ? glm::vec4(0.2f, 0.4f, 0.7f, 1.0f) : glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
		drawRect(dropdownRect.x + 5.0f, yOffset, dropdownRect.width - 10.0f, 25.0f, bgColor);
		
		std::string categoryText = "TY 1 Models (" + std::to_string(ty1Models.size()) + ") >";
		drawText(categoryText, dropdownRect.x + 10.0f, yOffset + 9.0f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
		yOffset += 30.0f;
	}
	
	// CRITICAL: Rebind the shader for rectangles after drawText switched to text shader
	glUseProgram(shaderProgram);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	
	// TY2 category
	if (!ty2Models.empty())
	{
		// Orange when hovered, gray when not
		glm::vec4 bgColor = (hoveredCategory == 2) ? glm::vec4(0.7f, 0.35f, 0.2f, 1.0f) : glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
		drawRect(dropdownRect.x + 5.0f, yOffset, dropdownRect.width - 10.0f, 25.0f, bgColor);
		
		std::string categoryText = "TY 2 Models (" + std::to_string(ty2Models.size()) + ") >";
		drawText(categoryText, dropdownRect.x + 10.0f, yOffset + 9.0f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
	}
}

void Gui::renderSubmenu()
{
	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	
	// Draw submenu background
	drawRect(submenuRect.x, submenuRect.y, submenuRect.width, submenuRect.height, glm::vec4(0.12f, 0.12f, 0.12f, 0.95f));
	
	// Draw border
	drawRect(submenuRect.x, submenuRect.y, submenuRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(submenuRect.x, submenuRect.y + submenuRect.height - 2.0f, submenuRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(submenuRect.x, submenuRect.y, 2.0f, submenuRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(submenuRect.x + submenuRect.width - 2.0f, submenuRect.y, 2.0f, submenuRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	
	float yOffset = submenuRect.y + 5.0f - scrollOffset;
	
	const std::vector<ModelEntry>* modelList = (hoveredCategory == 1) ? &ty1Models : &ty2Models;
	
	for (size_t i = 0; i < modelList->size(); i++)
	{
		if (yOffset >= submenuRect.y && yOffset < submenuRect.y + submenuRect.height)
		{
			glm::vec4 itemColor = glm::vec4(0.18f, 0.18f, 0.18f, 1.0f);
			glm::vec4 textColor = glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
			
			// Check if this is the currently selected model
			bool isSelected = (currentModelName == (*modelList)[i].name);
			// Check if this item is being hovered
			bool isHovered = (hoveredSubmenuItem == (int)i);
			
			if (isSelected)
			{
				// Selected item gets green color
				itemColor = glm::vec4(0.3f, 0.6f, 0.3f, 1.0f);
				textColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
			}
			else if (isHovered)
			{
				// Hovered item gets lighter background
				itemColor = glm::vec4(0.28f, 0.28f, 0.28f, 1.0f);
				textColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
			}
			
			drawRect(submenuRect.x + 5.0f, yOffset, submenuRect.width - 10.0f, 20.0f, itemColor);
			
			// Truncate long names
			std::string displayName = (*modelList)[i].name;
			if (displayName.length() > 40)
				displayName = displayName.substr(0, 37) + "...";
			
			drawText(displayName, submenuRect.x + 10.0f, yOffset + 6.0f, textColor);
		}
		yOffset += 25.0f;
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
			if (buttonRect.contains(x, y))
			{
				dropdownOpen = !dropdownOpen;
				submenuOpen = false;
				scrollOffset = 0.0f;
				hoveredCategory = 0;
				hoveredSubmenuItem = -1;
			}
			else if (submenuOpen && submenuRect.contains(x, y))
			{
				// Calculate which model was clicked in submenu
				float relativeY = y - submenuRect.y + scrollOffset;
				int itemIndex = (int)(relativeY / 25.0f);
				
				const std::vector<ModelEntry>* modelList = (hoveredCategory == 1) ? &ty1Models : &ty2Models;
				
				if (itemIndex >= 0 && itemIndex < modelList->size())
				{
					if (onModelSelected)
					{
						onModelSelected((*modelList)[itemIndex]);
						// Store selected model info
						for (auto& model : models)
						{
							if (model.name == (*modelList)[itemIndex].name)
							{
								selectedModel = &model;
								currentModelName = model.name;
								break;
							}
						}
					}
					dropdownOpen = false;
					submenuOpen = false;
					hoveredCategory = 0;
					hoveredSubmenuItem = -1;
				}
			}
			else if (dropdownOpen && dropdownRect.contains(x, y))
			{
				// Clicking on category doesn't do anything - must hover to open submenu
				// Just ignore the click
			}
			else if (currentModel && materialListRect.contains(x, y))
			{
				// Handle material list clicks - toggle material enabled/disabled
				float relativeY = y - (materialListRect.y + 45.0f) + materialListScroll;
				int itemIndex = (int)(relativeY / 25.0f);
				
				if (itemIndex >= 0 && itemIndex < currentModel->getMeshCount())
				{
					auto& meshes = currentModel->getMeshes();
					Mesh* mesh = meshes[itemIndex];
					mesh->setEnabled(!mesh->isEnabled());
					Debug::log("Toggled material " + std::to_string(itemIndex) + ": " + mesh->getMaterialName() + " -> " + (mesh->isEnabled() ? "ON" : "OFF"));
				}
			}
			else
			{
				// Clicked outside, close everything
				dropdownOpen = false;
				submenuOpen = false;
				hoveredCategory = 0;
				hoveredSubmenuItem = -1;
			}
		}
	}
}

void Gui::onMouseMove(float x, float y)
{
	mouseX = x;
	mouseY = y;
	
	hovering = buttonRect.contains(x, y) || (dropdownOpen && dropdownRect.contains(x, y)) || (submenuOpen && submenuRect.contains(x, y));
	
	// Track hovered submenu item
	hoveredSubmenuItem = -1;
	if (submenuOpen && submenuRect.contains(x, y))
	{
		// Mouse is in submenu - keep hoveredCategory as is (don't reset it)
		// This ensures the dropdown category stays highlighted
		
		float relativeY = y - submenuRect.y + scrollOffset - 5.0f; // -5.0f accounts for top padding
		int itemIndex = (int)(relativeY / 25.0f);
		
		const std::vector<ModelEntry>* modelList = (hoveredCategory == 1) ? &ty1Models : &ty2Models;
		
		if (itemIndex >= 0 && itemIndex < (int)modelList->size())
		{
			hoveredSubmenuItem = itemIndex;
		}
		// Don't process dropdown hover detection when in submenu
		return;
	}
	
	// Track hovered material item
	hoveredMaterialItem = -1;
	if (currentModel && materialListRect.contains(x, y))
	{
		float relativeY = y - (materialListRect.y + 45.0f) + materialListScroll;
		int itemIndex = (int)(relativeY / 25.0f);
		
		if (itemIndex >= 0 && itemIndex < currentModel->getMeshCount())
		{
			hoveredMaterialItem = itemIndex;
		}
	}
	
	if (dropdownOpen && dropdownRect.contains(x, y))
	{
		// Check which category is being hovered
		float relativeY = y - dropdownRect.y;
		
		int categoryIndex = 0;
		float yPos = 5.0f;
		
		// Check TY1
		if (!ty1Models.empty())
		{
			if (relativeY >= yPos && relativeY < yPos + 25.0f)
			{
				// Always set hoveredCategory to 1 when hovering over TY1
				// Only recalculate submenu if we just started hovering
				if (hoveredCategory != 1)
				{
					hoveredCategory = 1;
					submenuOpen = true;
					scrollOffset = 0.0f;
					
					// Calculate submenu position and size
					// Always align submenu with the top of the dropdown for easier mouse access
					float submenuYPos = dropdownRect.y;
					
					// Use 70% of window height for submenu
					float maxSubmenuHeight = windowHeight * 0.7f;
					float contentHeight = ty1Models.size() * 25.0f;
					float submenuHeight = (contentHeight < maxSubmenuHeight) ? contentHeight : maxSubmenuHeight;
					
					if (contentHeight > submenuHeight)
						maxScroll = contentHeight - submenuHeight;
					else
						maxScroll = 0.0f;
					
					submenuRect = {dropdownRect.x + dropdownRect.width + 2.0f, submenuYPos, 350.0f, submenuHeight};
					
					// Auto-scroll to selected model if one exists in this category
					if (!currentModelName.empty())
					{
						Debug::log("Looking for current model in TY1: " + currentModelName);
						// Find the index of the current model in TY1 list
						for (size_t i = 0; i < ty1Models.size(); i++)
						{
							if (ty1Models[i].name == currentModelName)
							{
								// Place the selected item at the TOP of the visible area
								float itemPosition = i * 25.0f;
								scrollOffset = itemPosition;
								
								// Clamp to valid scroll range
								if (scrollOffset < 0.0f) scrollOffset = 0.0f;
								if (scrollOffset > maxScroll) scrollOffset = maxScroll;
								
								Debug::log("Auto-scrolled TY1 submenu to model: " + currentModelName + " at index " + std::to_string(i) + " (scroll=" + std::to_string(scrollOffset) + ")");
								break;
							}
						}
					}
				}
				else
				{
					// Already hovering over TY1, just make sure it stays set
					hoveredCategory = 1;
				}
				return;
			}
			yPos += 30.0f;
		}
		
		// Check TY2
		if (!ty2Models.empty())
		{
			if (relativeY >= yPos && relativeY < yPos + 25.0f)
			{
				// Always set hoveredCategory to 2 when hovering over TY2
				// Only recalculate submenu if we just started hovering
				if (hoveredCategory != 2)
				{
					hoveredCategory = 2;
					submenuOpen = true;
					scrollOffset = 0.0f;
					
					// Calculate submenu position and size
					// Always align submenu with the top of the dropdown for easier mouse access
					float submenuYPos = dropdownRect.y;
					
					// Use 70% of window height for submenu
					float maxSubmenuHeight = windowHeight * 0.7f;
					float contentHeight = ty2Models.size() * 25.0f;
					float submenuHeight = (contentHeight < maxSubmenuHeight) ? contentHeight : maxSubmenuHeight;
					
					if (contentHeight > submenuHeight)
						maxScroll = contentHeight - submenuHeight;
					else
						maxScroll = 0.0f;
					
					submenuRect = {dropdownRect.x + dropdownRect.width + 2.0f, submenuYPos, 350.0f, submenuHeight};
					
					// Auto-scroll to selected model if one exists in this category
					if (!currentModelName.empty())
					{
						Debug::log("Looking for current model in TY2: " + currentModelName);
						// Find the index of the current model in TY2 list
						for (size_t i = 0; i < ty2Models.size(); i++)
						{
							if (ty2Models[i].name == currentModelName)
							{
								// Place the selected item at the TOP of the visible area
								float itemPosition = i * 25.0f;
								scrollOffset = itemPosition;
								
								// Clamp to valid scroll range
								if (scrollOffset < 0.0f) scrollOffset = 0.0f;
								if (scrollOffset > maxScroll) scrollOffset = maxScroll;
								
								Debug::log("Auto-scrolled TY2 submenu to model: " + currentModelName + " at index " + std::to_string(i) + " (scroll=" + std::to_string(scrollOffset) + ")");
								break;
							}
						}
					}
				}
				else
				{
					// Already hovering over TY2, just make sure it stays set
					hoveredCategory = 2;
				}
				return;
			}
		}
	}
	else if (dropdownOpen && !dropdownRect.contains(x, y) && (!submenuOpen || !submenuRect.contains(x, y)))
	{
		// Mouse is outside both dropdown and submenu - close everything
		submenuOpen = false;
		hoveredCategory = 0;
		hoveredSubmenuItem = -1;
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
			scrollOffset -= yoffset * 25.0f; // Scroll one item at a time
			if (scrollOffset < 0.0f) scrollOffset = 0.0f;
			if (scrollOffset > maxScroll) scrollOffset = maxScroll;
			return; // Don't process other scroll targets
		}
	}
	
	// Scroll the material list when mouse is over it
	if (currentModel && materialListRect.contains(mouseX, mouseY))
	{
		materialListScroll -= yoffset * 25.0f;
		if (materialListScroll < 0.0f) materialListScroll = 0.0f;
		if (materialListScroll > maxMaterialListScroll) materialListScroll = maxMaterialListScroll;
	}
}

void Gui::onKeyPress(int key)
{
	if (!submenuOpen) return;
	
	// Arrow keys for scrolling submenu
	if (key == 265) // Up arrow (GLFW_KEY_UP)
	{
		scrollOffset -= 25.0f;
		if (scrollOffset < 0.0f) scrollOffset = 0.0f;
	}
	else if (key == 264) // Down arrow (GLFW_KEY_DOWN)
	{
		scrollOffset += 25.0f;
		if (scrollOffset > maxScroll) scrollOffset = maxScroll;
	}
	else if (key == 266) // Page Up
	{
		scrollOffset -= submenuRect.height;
		if (scrollOffset < 0.0f) scrollOffset = 0.0f;
	}
	else if (key == 267) // Page Down
	{
		scrollOffset += submenuRect.height;
		if (scrollOffset > maxScroll) scrollOffset = maxScroll;
	}
	else if (key == 268) // Home
	{
		scrollOffset = 0.0f;
	}
	else if (key == 269) // End
	{
		scrollOffset = maxScroll;
	}
}

void Gui::setCurrentModel(Model* model, const std::string& modelName)
{
	currentModel = model;
	currentModelName = modelName;
	materialListScroll = 0.0f;
	hoveredMaterialItem = -1;
	
	// Calculate material list panel size based on content
	if (currentModel)
	{
		int materialCount = currentModel->getMeshCount();
		// Header (title + instructions) = 40px, each material = 25px, padding = 10px
		float contentHeight = 40.0f + (materialCount * 25.0f) + 10.0f;
		
		// Cap at a reasonable maximum height (70% of window height)
		float maxHeight = windowHeight * 0.7f;
		float panelHeight = (contentHeight < maxHeight) ? contentHeight : maxHeight;
		
		// Update material list rect
		materialListRect = {(float)windowWidth - 310.0f, 170.0f, 300.0f, panelHeight};
		
		// Calculate scroll
		maxMaterialListScroll = (contentHeight > panelHeight) ? (contentHeight - panelHeight + 40.0f) : 0.0f;
	}
}

void Gui::clearCurrentModel()
{
	currentModel = nullptr;
	materialListScroll = 0.0f;
	hoveredMaterialItem = -1;
	
	// Reset material list to default size
	materialListRect = {(float)windowWidth - 310.0f, 170.0f, 300.0f, 200.0f};
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
	drawText("Meshes: " + std::to_string(currentModel->getMeshCount()), modelInfoRect.x + 10.0f, yOffset, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
	yOffset += 15.0f;
	
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
	if (!currentModel) return;
	
	glUseProgram(shaderProgram);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	
	// Draw background
	drawRect(materialListRect.x, materialListRect.y, materialListRect.width, materialListRect.height, glm::vec4(0.12f, 0.12f, 0.12f, 0.95f));
	
	// Draw border
	drawRect(materialListRect.x, materialListRect.y, materialListRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(materialListRect.x, materialListRect.y + materialListRect.height - 2.0f, materialListRect.width, 2.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(materialListRect.x, materialListRect.y, 2.0f, materialListRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	drawRect(materialListRect.x + materialListRect.width - 2.0f, materialListRect.y, 2.0f, materialListRect.height, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
	
	// Title
	drawText("MATERIALS", materialListRect.x + 10.0f, materialListRect.y + 10.0f, glm::vec4(1.0f, 1.0f, 0.5f, 1.0f));
	
	// Instructions
	drawText("Click to toggle", materialListRect.x + 10.0f, materialListRect.y + 25.0f, glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));
	
	float yOffset = materialListRect.y + 45.0f - materialListScroll;
	
	auto& meshes = currentModel->getMeshes();
	
	for (size_t i = 0; i < meshes.size(); i++)
	{
		if (yOffset >= materialListRect.y + 40.0f && yOffset < materialListRect.y + materialListRect.height - 5.0f)
		{
			Mesh* mesh = meshes[i];
			bool isEnabled = mesh->isEnabled();
			bool isHovered = (hoveredMaterialItem == (int)i);
			
			// Background color
			glm::vec4 bgColor;
			if (isHovered)
				bgColor = glm::vec4(0.25f, 0.25f, 0.25f, 1.0f);
			else
				bgColor = glm::vec4(0.18f, 0.18f, 0.18f, 1.0f);
			
			drawRect(materialListRect.x + 5.0f, yOffset, materialListRect.width - 10.0f, 20.0f, bgColor);
			
			// Checkbox
			glm::vec4 checkboxColor = isEnabled ? glm::vec4(0.3f, 0.7f, 0.3f, 1.0f) : glm::vec4(0.7f, 0.3f, 0.3f, 1.0f);
			drawRect(materialListRect.x + 10.0f, yOffset + 4.0f, 12.0f, 12.0f, checkboxColor);
			
			// Checkbox check mark (if enabled)
			if (isEnabled)
			{
				drawText("X", materialListRect.x + 11.0f, yOffset + 6.0f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
			}
			
			// Material name (truncated if too long)
			std::string matName = mesh->getMaterialName();
			if (matName.empty()) matName = "unnamed_" + std::to_string(i);
			if (matName.length() > 30)
				matName = matName.substr(0, 27) + "...";
			
			glm::vec4 textColor = isEnabled ? glm::vec4(0.9f, 0.9f, 0.9f, 1.0f) : glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);
			drawText(matName, materialListRect.x + 28.0f, yOffset + 6.0f, textColor);
			
			// Show triangle count
			std::string triCount = "(" + std::to_string(mesh->getTriangleCount()) + " tri)";
			drawText(triCount, materialListRect.x + materialListRect.width - 80.0f, yOffset + 6.0f, glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));
		}
		yOffset += 25.0f;
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
