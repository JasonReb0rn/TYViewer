#include "application.h"

std::string Application::APPLICATION_PATH = "";
std::string Application::ARCHIVE_PATH = "";

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

	float horizontal	= 
		(Keyboard::isKeyHeld(GLFW_KEY_A)) ?  1.0f : 
		(Keyboard::isKeyHeld(GLFW_KEY_D)) ? -1.0f : 
		0.0f;
	float vertical		= 
		(Keyboard::isKeyHeld(GLFW_KEY_W)) ?  1.0f : 
		(Keyboard::isKeyHeld(GLFW_KEY_S)) ? -1.0f : 
		0.0f;

	// Only allow camera control if GUI is not being interacted with
	bool guiInteracting = gui && gui->isInteracting();
	
	if (Mouse::isButtonHeld(GLFW_MOUSE_BUTTON_MIDDLE) && !guiInteracting)
	{
		camera.localRotate(glm::vec3(mouseInputX, -mouseInputY, 0.0f) * 0.1f);
	}

	if (Keyboard::isKeyHeld(GLFW_KEY_LEFT_CONTROL))
		camera.localTranslate(glm::vec3(horizontal, 0.0f, vertical) * 120.0f * dt);
	else if (Keyboard::isKeyHeld(GLFW_KEY_LEFT_SHIFT))
		camera.localTranslate(glm::vec3(horizontal, 0.0f, vertical) * 1520.0f * dt);
	else
		camera.localTranslate(glm::vec3(horizontal, 0.0f, vertical) * 820.0f * dt);

	if (Keyboard::isKeyPressed(GLFW_KEY_1))
	{
		drawGrid = !drawGrid;
	}
	if (Keyboard::isKeyPressed(GLFW_KEY_2))
	{
		drawBounds = !drawBounds;
	}
	if (Keyboard::isKeyPressed(GLFW_KEY_3))
	{
		drawColliders = !drawColliders;
	}
	if (Keyboard::isKeyPressed(GLFW_KEY_4))
	{
		drawBones = !drawBones;
	}

	if (Keyboard::isKeyPressed(GLFW_KEY_F))
	{
		wireframe = !wireframe;
		if (wireframe)
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		}
		else
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}
	}

	if (Keyboard::isKeyPressed(GLFW_KEY_T))
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

	if (Keyboard::isKeyHeld(GLFW_KEY_KP_ADD))
	{
		camera.setFieldOfView(camera.getFieldOfView() - 30.0f * dt);
		if (camera.getFieldOfView() < 1.0f)
			camera.setFieldOfView(1.0f);
	}
	else if (Keyboard::isKeyHeld(GLFW_KEY_KP_SUBTRACT))
	{
		camera.setFieldOfView(camera.getFieldOfView() + 30.0f * dt);
		if (camera.getFieldOfView() > 120.0f)
			camera.setFieldOfView(120.0f);
	}
}

void Application::render(Shader& shader)
{
	renderer.clear(glm::vec4(Config::backgroundR, Config::backgroundG, Config::backgroundB, 1.0f));

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

	// Render GUI on top
	if (gui)
	{
		gui->render();
	}

	renderer.render(window);
}
