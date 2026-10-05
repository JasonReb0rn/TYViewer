#include "application.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <vector>
#include <filesystem>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/common.hpp>

#include "export/obj_exporter.h"
#include "export/raw_exporter.h"
#include "util/folder_picker.h"
#include "util/bitconverter.h"
#include "util/stringext.h"

#include <cctype>
#include <cstring>
#include <sstream>

std::string Application::APPLICATION_PATH = "";
std::string Application::ARCHIVE_PATH = "";

// -----------------------------------------------------------------------------
// Screen-space vertex index overlay ("V")
// - Uses a tiny built-in 8x8 bitmap font (same idea as Gui).
// - Draws in constant pixel size, independent of camera zoom.
// -----------------------------------------------------------------------------
static const char* kOverlayTextVertexShader = R"(
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

static const char* kOverlayTextFragmentShader = R"(
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

static unsigned int compileShader(GLenum type, const char* src)
{
	unsigned int shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, nullptr);
	glCompileShader(shader);
	return shader;
}

static unsigned int createProgram(const char* vs, const char* fs)
{
	unsigned int v = compileShader(GL_VERTEX_SHADER, vs);
	unsigned int f = compileShader(GL_FRAGMENT_SHADER, fs);
	unsigned int p = glCreateProgram();
	glAttachShader(p, v);
	glAttachShader(p, f);
	glLinkProgram(p);
	glDeleteShader(v);
	glDeleteShader(f);
	return p;
}

static unsigned int createOverlayFontTexture()
{
	// Simple 8x8 bitmap font (ASCII 32-127), identical glyph set to Gui::createFontTexture().
	const int charWidth = 8;
	const int charHeight = 8;
	const int charsPerRow = 16;
	const int numChars = 96; // ASCII 32-127

	unsigned char fontData[96][8] = {
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // space
		{0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00}, // !
		{0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00}, // "
		{0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, // #
		{0x0C, 0x3F, 0x68, 0x3E, 0x0B, 0x7E, 0x18, 0x00}, // $
		{0x60, 0x66, 0x0C, 0x18, 0x30, 0x66, 0x06, 0x00}, // %
		{0x38, 0x6C, 0x6C, 0x38, 0x6D, 0x66, 0x3B, 0x00}, // &
		{0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, // '
		{0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00}, // (
		{0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00}, // )
		{0x00, 0x18, 0x7E, 0x3C, 0x7E, 0x18, 0x00, 0x00}, // *
		{0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00}, // +
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30}, // ,
		{0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00}, // -
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00}, // .
		{0x00, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x00, 0x00}, // /
		{0x3C, 0x66, 0x6E, 0x7E, 0x76, 0x66, 0x3C, 0x00}, // 0
		{0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}, // 1
		{0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00}, // 2
		{0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00}, // 3
		{0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00}, // 4
		{0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00}, // 5
		{0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00}, // 6
		{0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00}, // 7
		{0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00}, // 8
		{0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00}, // 9
		{0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00}, // :
		{0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x30}, // ;
		{0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00}, // <
		{0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00}, // =
		{0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0x00}, // >
		{0x3C, 0x66, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00}, // ?
		{0x3C, 0x66, 0x6E, 0x6A, 0x6E, 0x60, 0x3C, 0x00}, // @
		{0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00}, // A
		{0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00}, // B
		{0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00}, // C
		{0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00}, // D
		{0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00}, // E
		{0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00}, // F
		{0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00}, // G
		{0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00}, // H
		{0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}, // I
		{0x3E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00}, // J
		{0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00}, // K
		{0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00}, // L
		{0x63, 0x77, 0x7F, 0x6B, 0x6B, 0x63, 0x63, 0x00}, // M
		{0x66, 0x66, 0x76, 0x7E, 0x6E, 0x66, 0x66, 0x00}, // N
		{0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}, // O
		{0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00}, // P
		{0x3C, 0x66, 0x66, 0x66, 0x6A, 0x6C, 0x36, 0x00}, // Q
		{0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0x00}, // R
		{0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00}, // S
		{0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00}, // T
		{0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}, // U
		{0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00}, // V
		{0x63, 0x63, 0x6B, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // W
		{0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00}, // X
		{0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00}, // Y
		{0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00}, // Z
		{0x7C, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7C, 0x00}, // [
		{0x00, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x00, 0x00}, // backslash
		{0x3E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x3E, 0x00}, // ]
		{0x18, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00}, // ^
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00}, // _
		{0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00}, // `
		{0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00}, // a
		{0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00}, // b
		{0x00, 0x00, 0x3C, 0x66, 0x60, 0x66, 0x3C, 0x00}, // c
		{0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00}, // d
		{0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00}, // e
		{0x1C, 0x36, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x00}, // f
		{0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C}, // g
		{0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00}, // h
		{0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00}, // i
		{0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38}, // j
		{0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00}, // k
		{0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, // l
		{0x00, 0x00, 0x66, 0x7F, 0x7F, 0x6B, 0x6B, 0x00}, // m
		{0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00}, // n
		{0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00}, // o
		{0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60}, // p
		{0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x06}, // q
		{0x00, 0x00, 0x6C, 0x76, 0x60, 0x60, 0x60, 0x00}, // r
		{0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00}, // s
		{0x30, 0x30, 0x7C, 0x30, 0x30, 0x36, 0x1C, 0x00}, // t
		{0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00}, // u
		{0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00}, // v
		{0x00, 0x00, 0x63, 0x6B, 0x6B, 0x7F, 0x36, 0x00}, // w
		{0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x00}, // x
		{0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x3C}, // y
		{0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0x00}, // z
		{0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00}, // {
		{0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // |
		{0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00}, // }
		{0x31, 0x6B, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00}, // ~
	};

	const int texWidth = charsPerRow * charWidth; // 128
	const int texHeight = ((numChars + charsPerRow - 1) / charsPerRow) * charHeight; // 48

	std::vector<unsigned char> texData(texWidth * texHeight, 0);
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

	unsigned int tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, texWidth, texHeight, 0, GL_RED, GL_UNSIGNED_BYTE, texData.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);
	return tex;
}

void Application::resize(int width, int height)
{
	if (width == 0 || height == 0)
	{
		width = 1;
		height = 1;
	}

	Config::windowResolutionX = width;
	Config::windowResolutionY = height;

	camera.setAspectRatio((float)width / (float)height);

	glViewport(0, 0, width, height);
	
	if (gui)
	{
		gui->resize(width, height);
	}
}

Application::Application(GLFWwindow* window) :
	window(window),
	renderer(),
	camera(glm::vec3(0.0f, 20.0f, -100.0f), glm::vec3(90.0f, 0.0f, 0.0f), 70.0f, (float)Config::windowResolutionX / (float)Config::windowResolutionY, 0.2f, 30000.0f),
	mesh(NULL),
	grid(NULL),
	shader(NULL),
	gui(NULL)
{}


void Application::initialize()
{
	Debug::log("Initializing application...");
	content.initialize();
	
	// Load archives (optional - can start without any)
	bool ty1Loaded = false;
	bool ty2Loaded = false;
	
	if (!Config::ty1_archive.empty())
	{
		Debug::log("Loading TY1 archive: " + Config::ty1_archive);
		if (content.loadRKV(Config::ty1_archive, 0))
		{
			Debug::log("TY1 archive loaded successfully");
			ty1Loaded = true;
		}
		else
		{
			Debug::log("Warning: Failed to load TY1 archive: " + Config::ty1_archive);
		}
	}
	
	if (!Config::ty2_archive.empty())
	{
		Debug::log("Loading TY2 archive: " + Config::ty2_archive);
		if (content.loadRKV(Config::ty2_archive, 1))
		{
			Debug::log("TY2 archive loaded successfully");
			ty2Loaded = true;
		}
		else
		{
			Debug::log("Warning: Failed to load TY2 archive: " + Config::ty2_archive);
		}
	}
	
	// Set active archive to first loaded one
	if (ty1Loaded)
	{
		content.setActiveArchive(0);
	}
	else if (ty2Loaded)
	{
		content.setActiveArchive(1);
	}

	Mouse::initialize(window);
	Keyboard::initialize(window);

	renderer.initialize();

	//content.defaultTexture = content.load<Texture>("front_yellow.dds");

	grid = new Grid({ 800, 800 }, 50.0f, glm::vec4(0.4f, 0.4f, 0.4f, 1.0f));

	Font* font = content.load<Font>("font_frontend_pc.wfn");
	//labels.push_back(new Text("This is a longer sentence with spaces!", font, glm::vec3(0,0,0)));

	shader = content.load<Shader>("standard.shader");
	if (shader == nullptr)
	{
		Debug::log("ERROR: Failed to load shader: standard.shader");
		std::cout << "Failed to load shader file!" << std::endl;
		std::cin.get();
		terminate();
		return;
	}
	shader->bind();
	shader->setUniform4f("tintColour", glm::vec4(1, 1, 1, 1));
	shader->setUniform1f("alphaRef", 0.01f);
	shader->setUniform1i("diffuseTexture", 0);
	shader->setUniformMat3("uvMatrix", glm::mat3(1.0f));

	basic = content.load<Shader>("standard.shader");
	if (basic == nullptr)
	{
		Debug::log("ERROR: Failed to load basic shader: standard.shader");
		std::cout << "Failed to load shader file!" << std::endl;
		std::cin.get();
		terminate();
		return;
	}
	basic->bind();
	basic->setUniform4f("tintColour", glm::vec4(1, 1, 1, 1));

	// Initialize GUI
	gui = new Gui();
	gui->initialize(Config::windowResolutionX, Config::windowResolutionY);

	// Initialize screen-space debug overlay (vertex indices)
	initializeVertexIdOverlay();
	
	// Scan archives for models and levels and populate GUI
	std::vector<ModelEntry> modelEntries;

	auto addEntries = [&](const std::vector<std::string>& names, const char* archiveName, int archiveIndex, EntryKind kind)
	{
		for (const auto& entryName : names)
		{
			ModelEntry entry;
			entry.name = entryName;
			entry.archiveName = archiveName;
			entry.archiveIndex = archiveIndex;
			entry.kind = kind;
			modelEntries.push_back(entry);
		}
	};
	
	if (ty1Loaded)
	{
		std::vector<std::string> ty1Models = content.getModelList(0);
		Debug::log("Found " + std::to_string(ty1Models.size()) + " models in TY1 archive");
		addEntries(ty1Models, "TY1", 0, EntryKind::Model);

		std::vector<std::string> ty1Levels = content.getLevelList(0);
		Debug::log("Found " + std::to_string(ty1Levels.size()) + " levels in TY1 archive");
		addEntries(ty1Levels, "TY1", 0, EntryKind::Level);
	}
	
	if (ty2Loaded)
	{
		std::vector<std::string> ty2Models = content.getModelList(1);
		Debug::log("Found " + std::to_string(ty2Models.size()) + " models in TY2 archive");
		addEntries(ty2Models, "TY2", 1, EntryKind::Model);

		std::vector<std::string> ty2Levels = content.getLevelList(1);
		Debug::log("Found " + std::to_string(ty2Levels.size()) + " levels in TY2 archive");
		addEntries(ty2Levels, "TY2", 1, EntryKind::Level);
	}
	
	gui->setModelList(modelEntries);
	
	// Set callback for model and level selection
	gui->setOnModelSelected([this](const ModelEntry& entry) {
		Debug::log("Selected: " + entry.name + " from " + entry.archiveName);
		if (entry.kind == EntryKind::Level)
		{
			if (entry.archiveIndex == 0)
				loadTy1Level(entry.name);
			else
				inspectTy2Level(entry.name);
		}
		else
		{
			loadModel(entry.name, entry.archiveIndex);
		}
	});

	// Set callback for exporting the currently-loaded model
	gui->setOnExportRequested([this]() {
		exportCurrentModel();
	});
	gui->setOnExportRawRequested([this]() {
		exportCurrentModelRaw();
	});
	gui->setOnRecenterCamera([this]() {
		if (!models.empty() || !levelObjects.empty())
			frameCameraOnLoadedModels();
	});
	gui->setOnLevelObjectToggled([this](int index, bool visible) {
		if (index >= 0 && index < static_cast<int>(levelObjects.size()))
			levelObjects[static_cast<size_t>(index)].visible = visible;
	});
	gui->setOnPartVisibilityChanged([this]() {
		syncCollisionVisibility();
	});
	gui->setOnLevelObjectSelected([this](int index) {
		selectedLevelObject = index;
		refreshObjectInspector();
	});
	gui->setOnLevelObjectFocused([this](int index) {
		frameCameraOnInstance(index);
	});
	gui->setOnCollisionToggle([this]() {
		setCollisionMeshesVisible(!collisionMeshesVisible);
	});
	gui->setOnBoundsToggle([this]() {
		drawBounds = !drawBounds;
		gui->setBoundsVisible(drawBounds);
	});
	
	// Load initial model if specified in config
	if (!Config::model.empty() && (ty1Loaded || ty2Loaded))
	{
		Debug::log("Loading model from config: " + Config::model);
		// Try to load from active archive
		loadModel(Config::model, content.getActiveArchive());
	}
	else
	{
		Debug::log("No initial model specified, starting with empty viewport");
	}
}

void Application::loadModel(const std::string& modelName, int archiveIndex)
{
	// Clear existing models
	clearModels();
	viewingLevel = false;
	
	// Set active archive
	content.setActiveArchive(archiveIndex);
	currentModelArchiveIndex = archiveIndex;
	currentModelName = modelName;
	
	// Load the model
	Model* loadedModel = content.load<Model>(modelName);
	
	if (loadedModel != nullptr)
	{
		models.push_back(loadedModel);
		Debug::log("Successfully loaded model: " + modelName);
		if (gui)
		{
			gui->showNotification("Loaded: " + modelName, Gui::NotificationKind::Success, 2.5f);
		}
		
		// Update GUI with current model info
		if (gui)
		{
			gui->setCurrentModel(loadedModel, modelName);
		}

		frameCameraOnModel(loadedModel);
		setCollisionMeshesVisible(true);
		capturePartDefaults();
	}
	else
	{
		Debug::log("Failed to load model: " + modelName);
		if (gui)
		{
			gui->showNotification("Failed to load: " + modelName, Gui::NotificationKind::Error, 4.0f);
		}
		refreshCollisionToggle();
	}
}

namespace
{
	std::string trimCopy(std::string value)
	{
		size_t start = 0;
		while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
			start++;
		size_t end = value.size();
		while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
			end--;
		return value.substr(start, end - start);
	}

	std::string lowerCopy(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
		{
			return static_cast<char>(std::tolower(c));
		});
		return value;
	}

	bool isCollisionStem(const std::string& part)
	{
		// Collide / Collision / Colide / Collsion / collde.
		return part.find("collid") != std::string::npos
			|| part.find("collis") != std::string::npos
			|| part.find("colid") != std::string::npos
			|| part.find("colls") != std::string::npos
			|| part.find("colld") != std::string::npos;
	}

	bool isCollisionMaterialName(const std::string& material)
	{
		const std::string lower = lowerCopy(material);
		// Default Max material. Suffixed slots (T0103_01_*) have no DDS and draw white.
		// The unsuffixed name binds t0103_01.dds, the red/yellow collision bitmap.
		if (lower == "t0103_01" || lower.rfind("t0103_01_", 0) == 0)
			return true;
		if (lower == "collision")
			return true;
		return lower.rfind("material #", 0) == 0;
	}

	bool isUntexturedCollisionPrefix(const std::string& part)
	{
		return part.rfind("c_", 0) == 0
			|| part.rfind("c ", 0) == 0;
	}

	bool isCollisionMesh(const Mesh* mesh, Content& content)
	{
		if (mesh == nullptr)
			return false;

		const std::string part = lowerCopy(mesh->getPartName());
		const std::string material = mesh->getMaterialName();
		if (isCollisionStem(part) || isCollisionMaterialName(material))
			return true;

		// B2 perimeter shell. Vertical quads on TY_B2_001 sit in front of the
		// masked tree cards and hide them. E1 A_TreeWall does not match.
		// Sector parts ending in " trees" are real geometry, not this shell.
		if (part == "tree_walls")
			return true;

		// Walkable snow ribbon on Room_b2_01 (TY_B2_001 / TY_B2_005). The drawn
		// path is the sector meshes in Room_b2_02. path_rope_* does not match.
		if (part.find("snow_path") != std::string::npos)
			return true;

		// invis_* shells stay collision even when the material has a DDS.
		// ty_b2_029 and ty_b2_031 are grass emitters and are also the drawn dirt
		// on sector parts. The grass flag does not keep an invis_ shell visible.
		if (part.find("invis") != std::string::npos)
			return true;

		// A3 quicksand volume, the only one in the game. It uses the surrounding
		// ground textures, so the untextured C_ rule does not catch it. The mesh
		// tells the engine where the hazard is. The drawn pit is
		// "003 000 006 Geom_Quicksand_02" and must not match.
		if (part == "c_geom_quicksand")
			return true;

		// B1 collision shells left with Max's default names. Object02 is already
		// hidden by T0103_01_c. These two use TY_A1_024, which also textures the
		// real cavern, so the material alone is not collision. Other Object01 /
		// Object03 parts (A2 waterfalls, props) use different materials.
		const std::string materialLower = lowerCopy(material);
		if ((part == "object01" || part == "object03") && materialLower == "ty_a1_024")
			return true;

		// global.mad "invisible 1" is not drawn (C3 island skirts, B3 grass emitters).
		const Content::Ty1MaterialDraw draw = content.lookupTy1Material(material);
		if (draw.invisible)
			return true;

		if (!isUntexturedCollisionPrefix(part))
			return false;
		return !content.hasActiveFile(material + ".dds");
	}

	bool isEnvPartName(const std::string& name)
	{
		return lowerCopy(name).rfind("env", 0) == 0;
	}

	bool sameName(const std::string& a, const std::string& b)
	{
		return lowerCopy(a) == lowerCopy(b);
	}

	void appendMdlNames(const std::string& value, std::vector<std::string>& out)
	{
		const std::string lower = lowerCopy(value);
		size_t pos = 0;
		while (pos < lower.size())
		{
			const size_t ext = lower.find(".mdl", pos);
			if (ext == std::string::npos)
				break;

			size_t start = ext;
			while (start > 0)
			{
				const unsigned char prev = static_cast<unsigned char>(value[start - 1]);
				if (std::isspace(prev) || value[start - 1] == ',' || value[start - 1] == '=')
					break;
				start--;
			}

			const std::string name = trimCopy(value.substr(start, ext + 4 - start));
			if (!name.empty())
			{
				bool seen = false;
				for (const std::string& existing : out)
				{
					if (sameName(existing, name))
					{
						seen = true;
						break;
					}
				}
				if (!seen)
					out.push_back(name);
			}
			pos = ext + 4;
		}
	}

	std::vector<std::string> roomModelsFromLv2(const std::string& text)
	{
		std::vector<std::string> names;
		std::istringstream stream(text);
		std::string line;
		while (std::getline(stream, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			const size_t comment = line.find("//");
			if (comment != std::string::npos)
				line = line.substr(0, comment);

			const size_t eq = line.find('=');
			if (eq == std::string::npos)
				continue;

			const std::string key = lowerCopy(trimCopy(line.substr(0, eq)));
			const bool take = (key == "ground" || key == "envcube" || key.rfind("overlay", 0) == 0);
			if (!take)
				continue;

			appendMdlNames(line.substr(eq + 1), names);
		}
		return names;
	}

	bool looksLikePropName(const std::string& name)
	{
		if (name.size() < 4 || name.size() > 64)
			return false;
		if (name.find('_') == std::string::npos || name.find(' ') != std::string::npos)
			return false;
		if ((name[0] == 'P' || name[0] == 'p') && std::isdigit(static_cast<unsigned char>(name[1])))
			return true;
		return lowerCopy(name).rfind("prop", 0) == 0;
	}

	std::string versionStringIn(const char* data, size_t size)
	{
		const char needle[] = "Data File version";
		const size_t needleLen = sizeof(needle) - 1;
		for (size_t i = 0; i + needleLen <= size; i++)
		{
			if (std::memcmp(data + i, needle, needleLen) != 0)
				continue;

			size_t start = i;
			while (start > 0)
			{
				const unsigned char prev = static_cast<unsigned char>(data[start - 1]);
				if (prev < 32 || prev >= 127)
					break;
				start--;
			}
			size_t end = i + needleLen;
			while (end < size)
			{
				const unsigned char next = static_cast<unsigned char>(data[end]);
				if (next < 32 || next >= 127)
					break;
				end++;
			}
			return std::string(data + start, data + end);
		}
		return {};
	}

	std::vector<std::string> propNamesIn(const char* data, size_t size)
	{
		std::vector<std::string> names;
		for (size_t i = 0; i < size; )
		{
			const unsigned char c = static_cast<unsigned char>(data[i]);
			if (c < 32 || c >= 127)
			{
				i++;
				continue;
			}

			size_t j = i;
			while (j < size)
			{
				const unsigned char d = static_cast<unsigned char>(data[j]);
				if (d < 32 || d >= 127)
					break;
				j++;
			}

			const bool terminated = (j == size) || data[j] == '\0';
			if (terminated && j > i)
			{
				const std::string name(data + i, data + j);
				if (looksLikePropName(name))
				{
					bool seen = false;
					for (const std::string& existing : names)
					{
						if (sameName(existing, name))
						{
							seen = true;
							break;
						}
					}
					if (!seen)
						names.push_back(name);
				}
			}
			i = j + 1;
		}
		return names;
	}
}

void Application::refreshObjectInspector()
{
	if (!gui)
		return;
	if (selectedLevelObject < 0 || selectedLevelObject >= static_cast<int>(levelObjects.size()))
	{
		gui->setObjectInfo({});
		return;
	}

	const std::vector<Ty1InfoLine> described = describeTy1Instance(
		levelObjects, selectedLevelObject, levelObjectIds);
	std::vector<ObjectInfoLine> lines;
	lines.reserve(described.size());
	for (const Ty1InfoLine& src : described)
	{
		ObjectInfoLine line;
		line.text = src.text;
		line.link = src.link;
		line.dim = src.dim;
		line.heading = src.heading;
		line.indent = src.indent;
		lines.push_back(std::move(line));
	}
	gui->setObjectInfo(std::move(lines));
}

void Application::loadTy1Level(const std::string& levelName)
{
	clearModels();
	content.setActiveArchive(0);
	currentModelArchiveIndex = 0;
	currentModelName = levelName;

	std::vector<char> data;
	if (!content.getActiveFileData(levelName, data) || data.empty())
	{
		Debug::log("Failed to read TY1 level: " + levelName);
		if (gui)
		{
			gui->setSceneLabel(levelName, false);
			gui->showNotification("Failed to read: " + levelName, Gui::NotificationKind::Error, 4.0f);
		}
		return;
	}

	const std::string text(data.begin(), data.end());
	const std::vector<std::string> roomNames = roomModelsFromLv2(text);
	Debug::log("TY1 level " + levelName + " room meshes: " + std::to_string(roomNames.size()));

	for (const std::string& roomName : roomNames)
	{
		Model* loaded = content.load<Model>(roomName);
		if (loaded == nullptr)
		{
			Debug::log("Level room mesh missing: " + roomName);
			continue;
		}
		models.push_back(loaded);
		Debug::log("Loaded level room mesh: " + roomName);
	}

	std::string globalModelText;
	std::vector<char> globalModel;
	if (content.getActiveFileData("global.model", globalModel) && !globalModel.empty())
		globalModelText.assign(globalModel.begin(), globalModel.end());
	else
		Debug::log("global.model missing; prop catalogs will be skipped");

	levelObjects = parseTy1Instances(text, globalModelText, content.getModelList(0));
	levelObjectIds = ty1IdIndex(levelObjects);
	propModels.clear();
	std::unordered_set<Model*> seenProps;
	size_t propsWithMesh = 0;
	auto takeProp = [&](const std::string& file, Model*& slot, const std::string& typeName)
	{
		if (file.empty())
			return false;
		Model* loaded = content.load<Model>(file);
		slot = loaded;
		if (loaded == nullptr)
		{
			Debug::log("Prop model missing: " + file + " (" + typeName + ")");
			return false;
		}
		if (seenProps.insert(loaded).second)
			propModels.push_back(loaded);
		return true;
	};
	for (Ty1Instance& instance : levelObjects)
	{
		const bool gotModel = takeProp(instance.modelFile, instance.model, instance.typeName);
		const bool gotExtra = takeProp(instance.extraModelFile, instance.extraModel, instance.typeName);
		if (gotModel || gotExtra)
			propsWithMesh++;
	}
	Debug::log("TY1 level " + levelName + " objects: " + std::to_string(levelObjects.size())
		+ " with mesh: " + std::to_string(propsWithMesh));

	if (models.empty() && levelObjects.empty())
	{
		Debug::log("No room meshes in " + levelName);
		if (gui)
		{
			gui->setSceneLabel(levelName, false);
			gui->showNotification("No room meshes in " + levelName, Gui::NotificationKind::Info, 4.0f);
		}
		refreshCollisionToggle();
		return;
	}

	viewingLevel = true;
	setCollisionMeshesVisible(false);
	capturePartDefaults();
	frameCameraOnLoadedModels();
	if (gui)
	{
		gui->setLevelModels(models, levelName);
		std::vector<LevelObjectItem> items;
		items.reserve(levelObjects.size());
		for (const Ty1Instance& instance : levelObjects)
		{
			LevelObjectItem item;
			item.typeName = instance.typeName;
			item.modelFile = instance.modelFile;
			if (!instance.variantLabel.empty())
			{
				item.modelFile = instance.variantLabel;
				if (!instance.modelFile.empty())
					item.modelFile += "  " + instance.modelFile;
			}
			item.visible = instance.visible;
			item.defaultVisible = instance.visible;
			const char* kindName = ty1KindName(instance);
			if (std::strcmp(kindName, "prop") != 0)
				item.kindLabel = kindName;
			item.idLabel = instance.objectLabel;
			items.push_back(item);
		}
		gui->setLevelObjects(items);
		gui->showNotification(
			"Loaded " + std::to_string(models.size()) + " room meshes, " + std::to_string(propsWithMesh) + " props",
			Gui::NotificationKind::Success, 3.0f);
	}
}

void Application::inspectTy2Level(const std::string& levelName)
{
	clearModels();
	viewingLevel = false;
	content.setActiveArchive(1);
	currentModelArchiveIndex = 1;
	currentModelName = levelName;

	std::vector<char> data;
	if (!content.getActiveFileData(levelName, data) || data.size() < 0x44)
	{
		Debug::log("Failed to read TY2 level chunk: " + levelName);
		if (gui)
		{
			gui->setSceneLabel(levelName, false);
			gui->showNotification("Failed to read: " + levelName, Gui::NotificationKind::Error, 4.0f);
		}
		return;
	}

	const std::string path = nts(data.data(), 0, 32);
	const uint32_t constant = from_bytes<uint32_t>(data.data(), 0x20);
	const uint32_t recordCount = from_bytes<uint32_t>(data.data(), 0x24);
	const uint32_t payloadSize = from_bytes<uint32_t>(data.data(), 0x28);
	const uint32_t stringOffset = from_bytes<uint32_t>(data.data(), 0x2C);
	const uint32_t recordBytes = from_bytes<uint32_t>(data.data(), 0x30);

	const char* payload = data.data() + 0x44;
	const size_t payloadLen = data.size() - 0x44;
	const std::string version = versionStringIn(payload, payloadLen);
	const std::vector<std::string> props = propNamesIn(payload, payloadLen);

	Debug::log("TY2 level " + levelName + " (not drawn)");
	Debug::log("  path: " + path);
	Debug::log("  constant: " + std::to_string(constant));
	Debug::log("  records: " + std::to_string(recordCount));
	Debug::log("  payload bytes: " + std::to_string(payloadSize) + " (file payload " + std::to_string(payloadLen) + ")");
	Debug::log("  string table offset: " + std::to_string(stringOffset));
	Debug::log("  record bytes: " + std::to_string(recordBytes));
	Debug::log("  version: " + (version.empty() ? std::string("(not found)") : version));
	Debug::log("  prop names: " + std::to_string(props.size()));

	const size_t sampleCount = std::min<size_t>(props.size(), 8);
	for (size_t i = 0; i < sampleCount; i++)
		Debug::log("    " + props[i]);

	if (gui)
	{
		gui->setSceneLabel(levelName, false);
		gui->showNotification("Listed, not drawn: " + levelName, Gui::NotificationKind::Info, 4.0f);
	}
}

void Application::exportCurrentModel()
{
	if (models.empty() || models[0] == nullptr)
	{
		Debug::log("Export requested but no model is loaded");
		if (gui)
		{
			gui->showNotification("Export failed: no model loaded", Gui::NotificationKind::Error, 4.0f);
		}
		return;
	}

	// Ensure we're exporting from the same archive the model was loaded from.
	content.setActiveArchive(currentModelArchiveIndex);

	std::string folder = Util::pickFolderDialog(window, "Select export folder");
	if (folder.empty())
	{
		Debug::log("Export cancelled");
		if (gui)
		{
			gui->showNotification("Export cancelled", Gui::NotificationKind::Info, 2.0f);
		}
		return;
	}

	std::filesystem::path outDir(folder);
	std::string err;
	if (!Export::exportModelAsObj(*models[0], currentModelName, content, outDir, &err))
	{
		Debug::log("Export failed: " + err);
		if (gui)
		{
			std::string msg = "Export failed";
			if (!err.empty())
				msg += ": " + err;
			gui->showNotification(msg, Gui::NotificationKind::Error, 4.5f);
		}
		return;
	}

	Debug::log("Export finished");
	if (gui)
	{
		gui->showNotification("Export complete", Gui::NotificationKind::Success, 3.0f);
	}
}

void Application::exportCurrentModelRaw()
{
	if (models.empty() || models[0] == nullptr || currentModelName.empty())
	{
		Debug::log("Raw export requested but no model is loaded");
		if (gui)
		{
			gui->showNotification("Export Raw failed: no model loaded", Gui::NotificationKind::Error, 4.0f);
		}
		return;
	}

	content.setActiveArchive(currentModelArchiveIndex);

	std::string folder = Util::pickFolderDialog(window, "Select folder for raw .mdl/.mdg export");
	if (folder.empty())
	{
		Debug::log("Raw export cancelled");
		if (gui)
		{
			gui->showNotification("Export Raw cancelled", Gui::NotificationKind::Info, 2.0f);
		}
		return;
	}

	std::filesystem::path outDir(folder);
	std::string err;
	bool wroteMdg = false;
	if (!Export::exportModelRaw(currentModelName, content, outDir, &err, &wroteMdg))
	{
		Debug::log("Raw export failed: " + err);
		if (gui)
		{
			std::string msg = "Export Raw failed";
			if (!err.empty())
				msg += ": " + err;
			gui->showNotification(msg, Gui::NotificationKind::Error, 4.5f);
		}
		return;
	}

	Debug::log("Raw export finished");
	if (gui)
	{
		std::string msg = wroteMdg ? "Exported .mdl + .mdg" : "Exported .mdl (no .mdg)";
		gui->showNotification(msg, Gui::NotificationKind::Success, 3.0f);
	}
}

void Application::frameCameraOnModel(const Model* model)
{
	std::vector<const Model*> one;
	if (model != nullptr)
		one.push_back(model);
	frameCameraOnModels(one, false);
}

void Application::frameCameraOnLoadedModels()
{
	std::vector<const Model*> list;
	list.reserve(models.size());
	for (const Model* model : models)
		list.push_back(model);
	frameCameraOnModels(list, viewingLevel);
}

template <typename Fn>
static void eachPlacedModel(const Ty1Instance& instance, Fn&& visit)
{
	if (instance.model != nullptr)
		visit(*instance.model);
	if (instance.extraModel != nullptr)
		visit(*instance.extraModel);
}

void Application::frameCameraOnModels(const std::vector<const Model*>& list, bool levelFraming)
{
	const float kDefaultFar = 30000.0f;

	auto includeMesh = [&](const Mesh* mesh, int pass) -> bool
	{
		if (mesh == nullptr)
			return false;
		if (!levelFraming || pass >= 2)
			return true;
		const bool collision = isCollisionMesh(mesh, content);
		const bool env = isEnvPartName(mesh->getPartName());
		if (pass == 0)
			return !collision && !env;
		return !env;
	};

	auto accumulate = [&](int pass, glm::dvec3& sum, size_t& count)
	{
		sum = glm::dvec3(0.0);
		count = 0;
		for (const Model* model : list)
		{
			if (model == nullptr)
				continue;
			for (const Mesh* mesh : model->getMeshes())
			{
				if (!includeMesh(mesh, pass))
					continue;
				for (const Vertex& vertex : mesh->getVertices())
				{
					sum += glm::dvec3(vertex.position);
					++count;
				}
			}
		}
		if (!levelFraming)
			return;
		for (const Ty1Instance& instance : levelObjects)
		{
			if (!instance.visible || (instance.model == nullptr && instance.extraModel == nullptr))
				continue;
			const glm::mat4 world = ty1InstanceMatrix(instance);
			eachPlacedModel(instance, [&](const Model& placed)
			{
				for (const Mesh* mesh : placed.getMeshes())
				{
					if (!includeMesh(mesh, pass))
						continue;
					for (const Vertex& vertex : mesh->getVertices())
					{
						const glm::vec3 point(world * glm::vec4(glm::vec3(vertex.position), 1.0f));
						sum += glm::dvec3(point);
						++count;
					}
				}
			});
		}
	};

	int pass = levelFraming ? 0 : 2;
	glm::dvec3 sum(0.0);
	size_t count = 0;
	accumulate(pass, sum, count);
	if (levelFraming && count == 0)
	{
		pass = 1;
		accumulate(pass, sum, count);
	}
	if (count == 0)
	{
		pass = 2;
		accumulate(pass, sum, count);
	}

	if (count == 0)
	{
		camera.setPosition(glm::vec3(0.0f, 0.0f, -100.0f));
		camera.setRotation(glm::vec3(90.0f, 0.0f, 0.0f));
		camera.setClipPlaneFar(kDefaultFar);
		return;
	}

	const glm::vec3 center(sum / static_cast<double>(count));

	float radius = 0.0f;
	float sceneRadius = 0.0f;
	for (const Model* model : list)
	{
		if (model == nullptr)
			continue;
		for (const Mesh* mesh : model->getMeshes())
		{
			if (mesh == nullptr)
				continue;
			const bool inFrame = includeMesh(mesh, pass);
			for (const Vertex& vertex : mesh->getVertices())
			{
				const float distance = glm::length(glm::vec3(vertex.position) - center);
				if (inFrame)
					radius = std::max(radius, distance);
				sceneRadius = std::max(sceneRadius, distance);
			}
		}
	}
	if (levelFraming)
	{
		for (const Ty1Instance& instance : levelObjects)
		{
			if (!instance.visible || (instance.model == nullptr && instance.extraModel == nullptr))
				continue;
			const glm::mat4 world = ty1InstanceMatrix(instance);
			eachPlacedModel(instance, [&](const Model& placed)
			{
				for (const Mesh* mesh : placed.getMeshes())
				{
					if (mesh == nullptr)
						continue;
					const bool inFrame = includeMesh(mesh, pass);
					for (const Vertex& vertex : mesh->getVertices())
					{
						const glm::vec3 point(world * glm::vec4(glm::vec3(vertex.position), 1.0f));
						const float distance = glm::length(point - center);
						if (inFrame)
							radius = std::max(radius, distance);
						sceneRadius = std::max(sceneRadius, distance);
					}
				}
			});
		}
	}
	if (radius < 0.05f)
		radius = 0.05f;

	float aspect = camera.getAspectRatio();
	if (aspect < 0.01f)
		aspect = 16.0f / 9.0f;

	const float vFov = glm::radians(camera.getFieldOfView());
	const float hFov = 2.0f * std::atan(std::tan(vFov * 0.5f) * aspect);
	float sinHalf = std::sin(std::min(vFov, hFov) * 0.5f);
	if (sinHalf < 0.001f)
		sinHalf = 0.001f;

	// Sit just outside the bounding sphere so small, normal, and huge models all fill the view.
	float dist = (radius / sinHalf) * 1.2f;
	if (levelFraming)
	{
		// The skybox is huge. Even the playable terrain spans the whole map, so fitting
		// all of it puts the eye minutes of flight away. Stay near the terrain instead.
		const float cap = 6000.0f;
		if (dist > cap)
			dist = cap;
	}
	const float lift = dist * 0.12f;

	const glm::vec3 worldCam = center + glm::vec3(0.0f, lift, dist);
	// Eye is stored with world Z negated; render() flips Z before the view matrix.
	const glm::vec3 eye(worldCam.x, worldCam.y, -worldCam.z);
	const glm::vec3 look(center.x, center.y, -center.z);

	glm::vec3 dir = look - eye;
	const float dirLen = glm::length(dir);
	if (dirLen < 0.0001f)
		dir = glm::vec3(0.0f, 0.0f, 1.0f);
	else
		dir /= dirLen;

	float pitch = glm::degrees(std::asin(glm::clamp(dir.y, -1.0f, 1.0f)));
	const float yaw = glm::degrees(std::atan2(dir.z, dir.x));
	if (pitch > 89.0f)
		pitch = 89.0f;
	if (pitch < -89.0f)
		pitch = -89.0f;

	camera.setPosition(eye);
	camera.setRotation(glm::vec3(yaw, pitch, 0.0f));
	const float farRadius = std::max(radius, sceneRadius);
	camera.setClipPlaneFar(std::max(kDefaultFar, dist + farRadius * 3.0f + 50.0f));
}

void Application::frameCameraOnInstance(int index)
{
	if (index < 0 || index >= static_cast<int>(levelObjects.size()))
		return;

	const Ty1Instance& instance = levelObjects[static_cast<size_t>(index)];
	glm::dvec3 sum(0.0);
	size_t count = 0;
	const glm::mat4 world = ty1InstanceMatrix(instance);
	eachPlacedModel(instance, [&](const Model& placed)
	{
		for (const Mesh* mesh : placed.getMeshes())
		{
			if (mesh == nullptr)
				continue;
			for (const Vertex& vertex : mesh->getVertices())
			{
				const glm::vec3 point(world * glm::vec4(glm::vec3(vertex.position), 1.0f));
				sum += glm::dvec3(point);
				++count;
			}
		}
	});

	glm::vec3 center = instance.position;
	float radius = 80.0f;
	if (count > 0)
	{
		center = glm::vec3(sum / static_cast<double>(count));
		radius = 0.0f;
		eachPlacedModel(instance, [&](const Model& placed)
		{
			for (const Mesh* mesh : placed.getMeshes())
			{
				if (mesh == nullptr)
					continue;
				for (const Vertex& vertex : mesh->getVertices())
				{
					const glm::vec3 point(world * glm::vec4(glm::vec3(vertex.position), 1.0f));
					radius = std::max(radius, glm::length(point - center));
				}
			}
		});
		if (radius < 0.05f)
			radius = 0.05f;
	}

	const float kDefaultFar = 30000.0f;
	float aspect = camera.getAspectRatio();
	if (aspect < 0.01f)
		aspect = 16.0f / 9.0f;

	const float vFov = glm::radians(camera.getFieldOfView());
	const float hFov = 2.0f * std::atan(std::tan(vFov * 0.5f) * aspect);
	float sinHalf = std::sin(std::min(vFov, hFov) * 0.5f);
	if (sinHalf < 0.001f)
		sinHalf = 0.001f;

	const float dist = (radius / sinHalf) * 1.2f;
	const float lift = dist * 0.12f;
	const glm::vec3 worldCam = center + glm::vec3(0.0f, lift, dist);
	const glm::vec3 eye(worldCam.x, worldCam.y, -worldCam.z);
	const glm::vec3 look(center.x, center.y, -center.z);

	glm::vec3 dir = look - eye;
	const float dirLen = glm::length(dir);
	if (dirLen < 0.0001f)
		dir = glm::vec3(0.0f, 0.0f, 1.0f);
	else
		dir /= dirLen;

	float pitch = glm::degrees(std::asin(glm::clamp(dir.y, -1.0f, 1.0f)));
	const float yaw = glm::degrees(std::atan2(dir.z, dir.x));
	if (pitch > 89.0f)
		pitch = 89.0f;
	if (pitch < -89.0f)
		pitch = -89.0f;

	camera.setPosition(eye);
	camera.setRotation(glm::vec3(yaw, pitch, 0.0f));

	// The object is small, but the skybox is not. Measure the level from the new
	// eye and never pull the far plane in closer than it already was.
	float reach = 0.0f;
	for (const Model* model : models)
	{
		if (model == nullptr)
			continue;
		const glm::vec3 corner = model->bounds_crn;
		const glm::vec3 size = model->bounds_size;
		for (int cornerIndex = 0; cornerIndex < 8; cornerIndex++)
		{
			const glm::vec3 point(
				corner.x + ((cornerIndex & 1) ? size.x : 0.0f),
				corner.y + ((cornerIndex & 2) ? size.y : 0.0f),
				corner.z + ((cornerIndex & 4) ? size.z : 0.0f));
			reach = std::max(reach, glm::length(point - worldCam));
		}
	}
	const float needed = std::max(kDefaultFar, reach + 50.0f);
	camera.setClipPlaneFar(std::max(camera.getClipPlaneFar(), needed));
}

void Application::setCollisionMeshesVisible(bool visible)
{
	bool any = false;
	auto visit = [&](Model* model)
	{
		if (model == nullptr)
			return;
		for (Mesh* mesh : model->getMeshes())
		{
			if (isCollisionMesh(mesh, content))
			{
				any = true;
				mesh->setEnabled(visible);
			}
		}
	};
	for (Model* model : models)
		visit(model);
	for (Model* model : propModels)
		visit(model);
	collisionMeshesVisible = any ? visible : true;
	refreshCollisionToggle();
}

void Application::capturePartDefaults()
{
	auto visit = [](Model* model)
	{
		if (model == nullptr)
			return;
		for (Mesh* mesh : model->getMeshes())
		{
			if (mesh != nullptr)
				mesh->captureDefaultEnabled();
		}
	};
	for (Model* model : models)
		visit(model);
	for (Model* model : propModels)
		visit(model);
}

void Application::syncCollisionVisibility()
{
	bool any = false;
	bool anyEnabled = false;
	auto visit = [&](const Model* model)
	{
		if (model == nullptr)
			return;
		for (const Mesh* mesh : model->getMeshes())
		{
			if (mesh == nullptr || !isCollisionMesh(mesh, content))
				continue;
			any = true;
			if (mesh->isEnabled())
				anyEnabled = true;
		}
	};
	for (const Model* model : models)
		visit(model);
	for (const Model* model : propModels)
		visit(model);
	collisionMeshesVisible = any ? anyEnabled : true;
	refreshCollisionToggle();
}

void Application::refreshCollisionToggle()
{
	bool any = false;
	auto visit = [&](const Model* model)
	{
		if (model == nullptr || any)
			return;
		for (const Mesh* mesh : model->getMeshes())
		{
			if (isCollisionMesh(mesh, content))
			{
				any = true;
				break;
			}
		}
	};
	for (const Model* model : models)
		visit(model);
	for (const Model* model : propModels)
		visit(model);

	if (gui)
		gui->setCollisionToggle(any, any && collisionMeshesVisible);
}

void Application::clearModels()
{
	models.clear();
	levelObjects.clear();
	levelObjectIds.clear();
	propModels.clear();
	selectedLevelObject = -1;
	// Note: Models are managed by the Content system, so we don't delete them here
	
	// Clear GUI model info
	if (gui)
	{
		gui->clearCurrentModel();
		gui->setCollisionToggle(false, false);
	}
}
void Application::run()
{
	float elapsed = 0.0f;
	float previous = 0.0f;

	float dt = 0.0f;

	while (!glfwWindowShouldClose(window))
	{
		elapsed = static_cast<float>(glfwGetTime());
		dt = elapsed - previous;
		previous = elapsed;

		Keyboard::process(window, dt);
		Mouse::process(window, dt);

		update(dt);
		render(*shader);
		
		glfwPollEvents();
	}

	terminate();
}
void Application::terminate()
{
	cleanupVertexIdOverlay();
	Config::save(Application::APPLICATION_PATH + "config.cfg");
	glfwTerminate();
}

void Application::onMouseButton(int button, int action, double x, double y)
{
	if (gui)
	{
		gui->onMouseButton(button, action, (float)x, (float)y);
	}
}

void Application::onMouseMove(double x, double y)
{
	if (gui)
	{
		gui->onMouseMove((float)x, (float)y);
	}
}

void Application::onScroll(double xoffset, double yoffset)
{
	if (gui)
	{
		// Always forward scroll to GUI first
		gui->onScroll((float)yoffset);
	}
	// Note: We don't use scroll for camera in this app, only GUI
}

void Application::onKeyPress(int key)
{
	if (gui)
	{
		gui->onKeyPress(key);
	}
}

void Application::onChar(unsigned int codepoint)
{
	if (gui)
	{
		gui->onChar(codepoint);
	}
}

void Application::update(float dt)
{
	ty1AnimTime += dt;
	content.setTy1AnimClock(
		ty1AnimTime,
		glm::radians(camera.getRotation().x),
		glm::radians(camera.getRotation().y));

	float mouseInputX = Mouse::getMouseDelta().x;
	float mouseInputY = Mouse::getMouseDelta().y;

	// When typing in GUI text inputs (e.g. model search), don't process app hotkeys/movement.
	bool guiTyping = gui && gui->isTextInputActive();

	float horizontal	= 
		(Keyboard::isKeyHeld(GLFW_KEY_A)) ?  1.0f : 
		(Keyboard::isKeyHeld(GLFW_KEY_D)) ? -1.0f : 
		0.0f;
	float vertical		= 
		(Keyboard::isKeyHeld(GLFW_KEY_W)) ?  1.0f : 
		(Keyboard::isKeyHeld(GLFW_KEY_S)) ? -1.0f : 
		0.0f;

	if (guiTyping)
	{
		horizontal = 0.0f;
		vertical = 0.0f;
	}

	// Only allow camera control if GUI is not being interacted with
	bool guiInteracting = gui && gui->isInteracting();
	
	if (Mouse::isButtonHeld(GLFW_MOUSE_BUTTON_MIDDLE) && !guiInteracting)
	{
		camera.localRotate(glm::vec3(mouseInputX, -mouseInputY, 0.0f) * 0.1f);
	}

	if (!guiTyping && Keyboard::isKeyHeld(GLFW_KEY_LEFT_CONTROL))
		camera.localTranslate(glm::vec3(horizontal, 0.0f, vertical) * 120.0f * dt);
	else if (!guiTyping && Keyboard::isKeyHeld(GLFW_KEY_LEFT_SHIFT))
		camera.localTranslate(glm::vec3(horizontal, 0.0f, vertical) * 1520.0f * dt);
	else if (!guiTyping)
		camera.localTranslate(glm::vec3(horizontal, 0.0f, vertical) * 820.0f * dt);

	if (!guiTyping && Keyboard::isKeyPressed(GLFW_KEY_1))
	{
		drawGrid = !drawGrid;
	}
	if (!guiTyping && Keyboard::isKeyPressed(GLFW_KEY_2))
	{
		drawBounds = !drawBounds;
		if (gui)
			gui->setBoundsVisible(drawBounds);
	}
	if (!guiTyping && Keyboard::isKeyPressed(GLFW_KEY_3))
	{
		drawColliders = !drawColliders;
	}
	if (!guiTyping && Keyboard::isKeyPressed(GLFW_KEY_4))
	{
		drawBones = !drawBones;
	}

	if (!guiTyping && Keyboard::isKeyPressed(GLFW_KEY_C))
	{
		setCollisionMeshesVisible(!collisionMeshesVisible);
	}

	if (!guiTyping && Keyboard::isKeyPressed(GLFW_KEY_F))
	{
		wireframe = !wireframe;
	}

	if (!guiTyping && Keyboard::isKeyPressed(GLFW_KEY_V))
	{
		drawVertexIds = !drawVertexIds;
	}

	if (!guiTyping && Keyboard::isKeyPressed(GLFW_KEY_T))
	{
		Debug::log
		(
			"Camera Position : { " +
			std::to_string(camera.getPosition().x) +
			", " +
			std::to_string(camera.getPosition().y) +
			", " +
			std::to_string(camera.getPosition().z) + " }"
		);
		Debug::log
		(
			"Camera Rotation : { " +
			std::to_string(camera.getRotation().x) +
			", " +
			std::to_string(camera.getRotation().y) +
			", " +
			std::to_string(camera.getRotation().z) + " }"
		);
	}

	if (!guiTyping && Keyboard::isKeyHeld(GLFW_KEY_KP_ADD))
	{
		camera.setFieldOfView(camera.getFieldOfView() - 30.0f * dt);
		if (camera.getFieldOfView() < 1.0f)
			camera.setFieldOfView(1.0f);
	}
	else if (!guiTyping && Keyboard::isKeyHeld(GLFW_KEY_KP_SUBTRACT))
	{
		camera.setFieldOfView(camera.getFieldOfView() + 30.0f * dt);
		if (camera.getFieldOfView() > 120.0f)
			camera.setFieldOfView(120.0f);
	}
}

void Application::drawSelectedObjectOutline(Shader& shader, const Ty1Instance& instance)
{
	if (instance.model == nullptr && instance.extraModel == nullptr)
		return;

	const int width = static_cast<int>(Config::windowResolutionX);
	const int height = static_cast<int>(Config::windowResolutionY);
	if (width <= 0 || height <= 0)
		return;

	GLint polygonMode[2] = { GL_FILL, GL_FILL };
	glGetIntegerv(GL_POLYGON_MODE, polygonMode);
	GLint depthFunc = GL_LESS;
	glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
	const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
	GLboolean depthMask = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);

	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glClear(GL_STENCIL_BUFFER_BIT);

	// Equal depth marks the pixels the mesh already wrote. A closer wall stays empty.
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_FALSE);
	glDepthFunc(GL_LEQUAL);
	glStencilFunc(GL_ALWAYS, 1, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

	const glm::mat4 world = ty1InstanceMatrix(instance);
	auto drawParts = [&](const MeshDrawStyle& style)
	{
		eachPlacedModel(instance, [&](Model& placed)
		{
			placed.drawMeshes(shader, false, world, style);
			placed.drawMeshes(shader, true, world, style);
		});
	};

	MeshDrawStyle mark;
	mark.solid = true;
	drawParts(mark);

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glDisable(GL_DEPTH_TEST);

	// Two pixels is 4/size in NDC. Eight shifts fill the ring, including the diagonals.
	const float ax = 4.0f / static_cast<float>(width);
	const float ay = 4.0f / static_cast<float>(height);
	const float dx = ax * 0.70710678f;
	const float dy = ay * 0.70710678f;
	const glm::vec2 ring[8] =
	{
		glm::vec2(ax, 0.0f), glm::vec2(-ax, 0.0f), glm::vec2(0.0f, ay), glm::vec2(0.0f, -ay),
		glm::vec2(dx, dy), glm::vec2(dx, -dy), glm::vec2(-dx, dy), glm::vec2(-dx, -dy)
	};

	MeshDrawStyle rim;
	rim.solid = true;
	rim.tint = glm::vec4(1.0f, 0.5f, 0.05f, 1.0f);
	for (const glm::vec2& offset : ring)
	{
		rim.clipOffset = offset;
		drawParts(rim);
	}

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glDepthFunc(depthFunc);
	glStencilMask(0xFF);
	glStencilFunc(GL_ALWAYS, 0, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glDisable(GL_STENCIL_TEST);
	if (depthTest)
		glEnable(GL_DEPTH_TEST);
	else
		glDisable(GL_DEPTH_TEST);
	glDepthMask(depthMask);
	glPolygonMode(GL_FRONT, polygonMode[0]);
	glPolygonMode(GL_BACK, polygonMode[1]);

	shader.bind();
	shader.setUniform2f("clipOffset", glm::vec2(0.0f));
	shader.setUniform1i("solidColour", 0);
	shader.setUniform4f("tintColour", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
}

void Application::render(Shader& shader)
{
	renderer.clear(glm::vec4(Config::backgroundR, Config::backgroundG, Config::backgroundB, 1.0f));

	// Apply wireframe mode only for the 3D scene; GUI should always be solid.
	if (wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	else
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	// Display as a left-handed coordinate system.
	glm::mat4 view;
	view = camera.getViewMatrix();
	view = glm::scale(view, glm::vec3(1.0f, 1.0f, -1.0f));

	glm::mat4 projection;
	projection = camera.getProjectionMatrix();

	glm::mat4 vpmatrix = projection * view;

	shader.bind();
	shader.setUniformMat4("VPMatrix", vpmatrix);
	shader.setUniformMat3("uvMatrix", glm::mat3(1.0f));

	// Opaque world first, then alpha and additive sheets. A waterfall drawn in
	// file order writes depth and hides the cliff that is stored in a later room.
	for (auto& model : models)
		model->drawMeshes(shader, false);
	for (const Ty1Instance& instance : levelObjects)
	{
		if (!instance.visible)
			continue;
		const glm::mat4 world = ty1InstanceMatrix(instance);
		eachPlacedModel(instance, [&](Model& placed)
		{
			placed.drawMeshes(shader, false, world);
		});
	}
	for (auto& model : models)
		model->drawMeshes(shader, true);
	for (const Ty1Instance& instance : levelObjects)
	{
		if (!instance.visible)
			continue;
		const glm::mat4 world = ty1InstanceMatrix(instance);
		eachPlacedModel(instance, [&](Model& placed)
		{
			placed.drawMeshes(shader, true, world);
		});
	}

	if (selectedLevelObject >= 0 && selectedLevelObject < static_cast<int>(levelObjects.size()))
		drawSelectedObjectOutline(shader, levelObjects[static_cast<size_t>(selectedLevelObject)]);

	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_TRUE);

	// Reset tint color to white after drawing models (avoid shader state leaking into debug draws).
	shader.bind();
	shader.setUniform4f("tintColour", glm::vec4(1, 1, 1, 1));
	shader.setUniform2f("clipOffset", glm::vec2(0.0f));
	shader.setUniform1i("solidColour", 0);
	shader.setUniform1f("alphaRef", 0.01f);
	shader.setUniformMat3("uvMatrix", glm::mat3(1.0f));

	for (auto& label : labels)
	{
		label->draw(shader);
	}

	basic->bind();
	basic->setUniformMat4("VPMatrix", vpmatrix);
	basic->setUniformMat4("modelMatrix", glm::mat4(1.0f));
	basic->setUniformMat3("uvMatrix", glm::mat3(1.0f));
	// Also reset tint for basic shader (they might share the same shader program)
	basic->setUniform4f("tintColour", glm::vec4(1, 1, 1, 1));

	if (drawGrid)
	{
		renderer.draw(*grid, *basic);
	}

	for (auto& model : models)
	{
		if (drawBounds)
		{
			renderer.drawHollowBox(model->bounds_crn, model->bounds_size, glm::vec4(1, 1, 1, 1));

			for (auto& bounds : model->bounds)
			{
				renderer.drawHollowBox(bounds.corner, bounds.size, glm::vec4(1, 1, 1, 1));
			}
		}

		if (drawColliders)
		{
			for (auto& collider : model->colliders)
			{
				renderer.drawSphere(collider.position, collider.size / 2.0f, glm::vec4(1, 0, 0, 1));
			}
		}

		if (drawBones)
		{
			for (auto& bone : model->bones)
			{
				renderer.drawSphere(bone.defaultPosition, 2.0f, glm::vec4(1, 1, 1, 1));
			}
		}
	}

	if (drawBounds)
	{
		for (const Ty1Instance& instance : levelObjects)
		{
			if (!instance.visible)
				continue;
			const glm::mat4 world = ty1InstanceMatrix(instance);
			eachPlacedModel(instance, [&](const Model& placed)
			{
				const glm::vec3 corner = placed.bounds_crn;
				const glm::vec3 size = placed.bounds_size;
				glm::vec3 minCorner(std::numeric_limits<float>::max());
				glm::vec3 maxCorner(-std::numeric_limits<float>::max());
				for (int cornerIndex = 0; cornerIndex < 8; cornerIndex++)
				{
					const glm::vec3 local(
						corner.x + ((cornerIndex & 1) ? size.x : 0.0f),
						corner.y + ((cornerIndex & 2) ? size.y : 0.0f),
						corner.z + ((cornerIndex & 4) ? size.z : 0.0f));
					const glm::vec3 point(world * glm::vec4(local, 1.0f));
					minCorner = glm::min(minCorner, point);
					maxCorner = glm::max(maxCorner, point);
				}
				renderer.drawHollowBox(minCorner, maxCorner - minCorner, glm::vec4(1, 1, 1, 1));
			});
		}
	}

	if (selectedLevelObject >= 0 && selectedLevelObject < static_cast<int>(levelObjects.size()))
	{
		const Ty1Instance& instance = levelObjects[static_cast<size_t>(selectedLevelObject)];
		const glm::vec4 critterColour = objectKindColour("critter");
		const glm::vec4 waterColour = objectKindColour("water");
		const glm::vec4 triggerColour = objectKindColour("trigger");
		const glm::vec4 soundColour = objectKindColour("sound");
		const glm::vec4 patrolColour = objectKindColour("patrol");
		const glm::vec4 rangeColour = objectKindColour("range");

		// The mesh shader multiplies by the texture left bound from the last model.
		// A terrain texel turns these guides dull and they disappear into the world.
		// A white texture plus a solid tint writes the chip colour on its own.
		if (content.defaultTexture != nullptr)
			content.defaultTexture->bind();
		basic->setUniform1i("solidColour", 1);
		basic->setUniform1f("alphaRef", 0.0f);
		const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
		glDisable(GL_BLEND);

		auto drawBox = [&](const glm::vec3& pos, float yaw, float pitch, float roll, const glm::vec3& size, const glm::vec4& colour)
		{
			if (size.x <= 0.0f && size.y <= 0.0f && size.z <= 0.0f)
				return;
			glm::mat4 matrix(1.0f);
			matrix = glm::translate(matrix, pos);
			matrix = glm::rotate(matrix, -roll, glm::vec3(0.0f, 0.0f, 1.0f));
			matrix = glm::rotate(matrix, -yaw, glm::vec3(0.0f, 1.0f, 0.0f));
			matrix = glm::rotate(matrix, -pitch, glm::vec3(1.0f, 0.0f, 0.0f));
			matrix = glm::scale(matrix, glm::max(size, glm::vec3(0.01f)));
			basic->setUniformMat4("modelMatrix", matrix);
			basic->setUniform4f("tintColour", colour);
			renderer.drawHollowBox(glm::vec3(-0.5f), glm::vec3(1.0f), colour);
			basic->setUniformMat4("modelMatrix", glm::mat4(1.0f));
		};

		if (instance.roamSize.x > 0.0f || instance.roamSize.y > 0.0f || instance.roamSize.z > 0.0f)
		{
			const glm::vec4 colour = instance.kind == Ty1Kind::Water ? waterColour : critterColour;
			drawBox(instance.position, instance.rotation.y, instance.rotation.x, instance.rotation.z, instance.roamSize, colour);
		}
		if (instance.hasBox)
			drawBox(instance.boxPosition, instance.boxYaw, instance.boxPitch, 0.0f, instance.boxSize, triggerColour);
		if (instance.hasSphere && instance.sphereRadius > 0.0f)
		{
			glm::vec4 colour = triggerColour;
			if (instance.soundSphere)
				colour = soundColour;
			else if (instance.rangeSphere)
				colour = rangeColour;
			basic->setUniform4f("tintColour", colour);
			renderer.drawSphere(instance.spherePosition, instance.sphereRadius, colour);
		}
		if (!instance.waypoints.empty())
		{
			std::vector<glm::vec3> path;
			if (!instance.closePath)
				path.push_back(instance.position);
			path.insert(path.end(), instance.waypoints.begin(), instance.waypoints.end());
			if (instance.closePath && path.size() >= 2)
				path.push_back(path.front());
			basic->setUniform4f("tintColour", patrolColour);
			renderer.drawLineStrip(path, patrolColour);
			if (instance.pathWidth > 0.0f && path.size() >= 2)
			{
				std::vector<glm::vec3> left;
				std::vector<glm::vec3> right;
				const float half = instance.pathWidth * 0.5f;
				for (size_t i = 0; i + 1 < path.size(); i++)
				{
					glm::vec3 dir = path[i + 1] - path[i];
					dir.y = 0.0f;
					if (glm::length(dir) < 0.001f)
						continue;
					dir = glm::normalize(dir);
					const glm::vec3 side = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), dir)) * half;
					if (left.empty())
					{
						left.push_back(path[i] + side);
						right.push_back(path[i] - side);
					}
					left.push_back(path[i + 1] + side);
					right.push_back(path[i + 1] - side);
				}
				renderer.drawLineStrip(left, patrolColour);
				renderer.drawLineStrip(right, patrolColour);
			}
			const size_t markerCount = (instance.closePath && path.size() >= 2) ? path.size() - 1 : path.size();
			for (size_t pointIndex = 0; pointIndex < markerCount; pointIndex++)
				renderer.drawSphere(path[pointIndex], 18.0f, patrolColour, 8);
		}

		basic->setUniform1i("solidColour", 0);
		basic->setUniform4f("tintColour", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
		basic->setUniform1f("alphaRef", 0.01f);
		if (blendWasEnabled)
			glEnable(GL_BLEND);
	}

	// Vertex index overlay should be readable regardless of wireframe mode.
	if (drawVertexIds)
	{
		drawVertexIdOverlay(vpmatrix);
	}

	// Ensure the GUI is never affected by 3D polygon mode.
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	// Render GUI on top
	if (gui)
	{
		gui->render();
	}

	renderer.render(window);
}

void Application::initializeVertexIdOverlay()
{
	// (Re)create if needed
	cleanupVertexIdOverlay();

	vertexOverlayTextShaderProgram = createProgram(kOverlayTextVertexShader, kOverlayTextFragmentShader);

	// VAO/VBO for text quads (6 verts, each: pos(2), uv(2))
	glGenVertexArrays(1, &vertexOverlayTextVAO);
	glGenBuffers(1, &vertexOverlayTextVBO);

	glBindVertexArray(vertexOverlayTextVAO);
	glBindBuffer(GL_ARRAY_BUFFER, vertexOverlayTextVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

	glBindVertexArray(0);

	vertexOverlayFontTexture = createOverlayFontTexture();
}

void Application::cleanupVertexIdOverlay()
{
	if (vertexOverlayTextVAO) glDeleteVertexArrays(1, &vertexOverlayTextVAO);
	if (vertexOverlayTextVBO) glDeleteBuffers(1, &vertexOverlayTextVBO);
	if (vertexOverlayTextShaderProgram) glDeleteProgram(vertexOverlayTextShaderProgram);
	if (vertexOverlayFontTexture) glDeleteTextures(1, &vertexOverlayFontTexture);

	vertexOverlayTextVAO = 0;
	vertexOverlayTextVBO = 0;
	vertexOverlayTextShaderProgram = 0;
	vertexOverlayFontTexture = 0;
}

static bool projectToScreen(const glm::mat4& vpmatrix, const glm::mat4& modelMatrix, const glm::vec4& p,
                            int width, int height, glm::vec2& outPixel)
{
	glm::vec4 clip = vpmatrix * modelMatrix * p;
	if (clip.w <= 0.00001f)
		return false;

	glm::vec3 ndc = glm::vec3(clip) / clip.w;
	if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f || ndc.z < -1.0f || ndc.z > 1.0f)
		return false;

	float sx = (ndc.x * 0.5f + 0.5f) * (float)width;
	float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)height; // top-left origin
	outPixel = glm::vec2(sx, sy);
	return true;
}

static void drawOverlayText(unsigned int program, unsigned int vao, unsigned int vbo, unsigned int fontTex,
                            const std::string& text, float x, float y, const glm::vec4& color, int windowWidth, int windowHeight)
{
	glUseProgram(program);
	glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(glGetUniformLocation(program, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	glUniform4fv(glGetUniformLocation(program, "textColor"), 1, glm::value_ptr(color));

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, fontTex);
	glUniform1i(glGetUniformLocation(program, "fontTexture"), 0);

	glBindVertexArray(vao);

	const float charW = 8.0f;
	const float charH = 8.0f;
	const float texCharW = 8.0f / 128.0f; // 16 chars/row * 8px = 128
	const float texCharH = 8.0f / 48.0f;  // 6 rows * 8px = 48

	float xPos = x;
	for (char c : text)
	{
		if (c < 32 || c > 126) { xPos += charW; continue; }
		int idx = c - 32;
		int col = idx % 16;
		int row = idx / 16;
		float tx = col * texCharW;
		float ty = row * texCharH;

		float verts[6][4] = {
			{ xPos,         y,          tx,           ty },
			{ xPos + charW, y,          tx + texCharW, ty },
			{ xPos + charW, y + charH,  tx + texCharW, ty + texCharH },

			{ xPos,         y,          tx,           ty },
			{ xPos + charW, y + charH,  tx + texCharW, ty + texCharH },
			{ xPos,         y + charH,  tx,           ty + texCharH }
		};

		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		xPos += charW;
	}

	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
}

void Application::drawVertexIdOverlay(const glm::mat4& vpmatrix)
{
	if (vertexOverlayTextShaderProgram == 0 || vertexOverlayFontTexture == 0 || vertexOverlayTextVAO == 0)
		return;

	const int w = Config::windowResolutionX;
	const int h = Config::windowResolutionY;
	if (w <= 0 || h <= 0)
		return;

	// Make overlay readable regardless of current 3D state.
	GLboolean depthTestEnabled = GL_FALSE;
	glGetBooleanv(GL_DEPTH_TEST, &depthTestEnabled);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	// Density limiter (reduces draw calls + clutter when zoomed out)
	const int cellSizePx = 12;
	const int cellsX = (w + cellSizePx - 1) / cellSizePx;
	const int cellsY = (h + cellSizePx - 1) / cellSizePx;
	std::vector<int> occupied;
	occupied.assign((size_t)cellsX * (size_t)cellsY, -1);

	size_t globalIndex = 0;
	for (auto& model : models)
	{
		if (!model) continue;
		for (const auto& meshPtr : model->getMeshes())
		{
			const Mesh* m = meshPtr;
			if (!m) continue;
			if (!m->isEnabled()) { globalIndex += m->getVertices().size(); continue; }

			const glm::mat4 modelMatrix = m->getMatrix();
			const auto& verts = m->getVertices();
			for (size_t i = 0; i < verts.size(); i++, globalIndex++)
			{
				glm::vec2 screen;
				if (!projectToScreen(vpmatrix, modelMatrix, verts[i].position, w, h, screen))
					continue;

				const int cx = (int)(screen.x / (float)cellSizePx);
				const int cy = (int)(screen.y / (float)cellSizePx);
				if (cx < 0 || cy < 0 || cx >= cellsX || cy >= cellsY)
					continue;
				const size_t cellIdx = (size_t)cy * (size_t)cellsX + (size_t)cx;
				if (occupied[cellIdx] != -1)
					continue;
				occupied[cellIdx] = (int)globalIndex;

				std::string label = std::to_string(globalIndex);
				const float textW = (float)label.size() * 8.0f;
				const float textH = 8.0f;
				float x = screen.x - textW * 0.5f;
				float y = screen.y - textH * 0.5f;

				// Tiny outline/shadow for legibility.
				drawOverlayText(vertexOverlayTextShaderProgram, vertexOverlayTextVAO, vertexOverlayTextVBO, vertexOverlayFontTexture,
					label, x + 1.0f, y + 1.0f, glm::vec4(0, 0, 0, 0.85f), w, h);
				drawOverlayText(vertexOverlayTextShaderProgram, vertexOverlayTextVAO, vertexOverlayTextVBO, vertexOverlayFontTexture,
					label, x, y, glm::vec4(1.0f, 0.95f, 0.2f, 1.0f), w, h);
			}
		}
	}

	// Restore depth test if it was enabled.
	if (depthTestEnabled)
		glEnable(GL_DEPTH_TEST);
}
