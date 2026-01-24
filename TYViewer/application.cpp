#include "application.h"

#include <vector>
#include <filesystem>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "export/obj_exporter.h"
#include "util/folder_picker.h"

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
	shader->setUniform1i("diffuseTexture", 0);

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
	
	// Scan archives for models and populate GUI
	std::vector<ModelEntry> modelEntries;
	
	if (ty1Loaded)
	{
		std::vector<std::string> ty1Models = content.getModelList(0);
		Debug::log("Found " + std::to_string(ty1Models.size()) + " models in TY1 archive");
		for (const auto& modelName : ty1Models)
		{
			ModelEntry entry;
			entry.name = modelName;
			entry.archiveName = "TY1";
			entry.archiveIndex = 0;
			modelEntries.push_back(entry);
		}
	}
	
	if (ty2Loaded)
	{
		std::vector<std::string> ty2Models = content.getModelList(1);
		Debug::log("Found " + std::to_string(ty2Models.size()) + " models in TY2 archive");
		for (const auto& modelName : ty2Models)
		{
			ModelEntry entry;
			entry.name = modelName;
			entry.archiveName = "TY2";
			entry.archiveIndex = 1;
			modelEntries.push_back(entry);
		}
	}
	
	gui->setModelList(modelEntries);
	
	// Set callback for model selection
	gui->setOnModelSelected([this](const ModelEntry& entry) {
		Debug::log("Model selected: " + entry.name + " from " + entry.archiveName);
		loadModel(entry.name, entry.archiveIndex);
	});

	// Set callback for exporting the currently-loaded model
	gui->setOnExportRequested([this]() {
		exportCurrentModel();
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
		
		// Update GUI with current model info
		if (gui)
		{
			gui->setCurrentModel(loadedModel, modelName);
		}
	}
	else
	{
		Debug::log("Failed to load model: " + modelName);
	}
}

void Application::exportCurrentModel()
{
	if (models.empty() || models[0] == nullptr)
	{
		Debug::log("Export requested but no model is loaded");
		return;
	}

	// Ensure we're exporting from the same archive the model was loaded from.
	content.setActiveArchive(currentModelArchiveIndex);

	std::string folder = Util::pickFolderDialog(window, "Select export folder");
	if (folder.empty())
	{
		Debug::log("Export cancelled");
		return;
	}

	std::filesystem::path outDir(folder);
	std::string err;
	if (!Export::exportModelAsObj(*models[0], currentModelName, content, outDir, &err))
	{
		Debug::log("Export failed: " + err);
		return;
	}

	Debug::log("Export finished");
}

void Application::clearModels()
{
	models.clear();
	// Note: Models are managed by the Content system, so we don't delete them here
	
	// Clear GUI model info
	if (gui)
	{
		gui->clearCurrentModel();
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
	}
	if (!guiTyping && Keyboard::isKeyPressed(GLFW_KEY_3))
	{
		drawColliders = !drawColliders;
	}
	if (!guiTyping && Keyboard::isKeyPressed(GLFW_KEY_4))
	{
		drawBones = !drawBones;
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

	for (auto& model : models)
	{
		renderer.draw(*model, shader);
	}
	
	// Reset tint color to white after drawing models (in case last mesh had pink tint from being disabled)
	shader.bind();
	shader.setUniform4f("tintColour", glm::vec4(1, 1, 1, 1));

	for (auto& label : labels)
	{
		label->draw(shader);
	}

	basic->bind();
	basic->setUniformMat4("VPMatrix", vpmatrix);
	basic->setUniformMat4("modelMatrix", glm::mat4(1.0f));
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
