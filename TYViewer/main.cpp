#include <iostream>
#include <string>

#include <GLAD/glad.h>
#include <GLFW/glfw3.h>

#include "application.h"
#include "config.h"
#include "debug.h"

Application* application;

void onWindowResized(GLFWwindow* window, int width, int height)
{
	application->resize(width, height);
}

int main(int argc, char* argv[])
{
	Debug::log("TYViewer starting...");
	
	if (!Config::load(Application::APPLICATION_PATH + "config.cfg"))
	{
		Debug::log("Config file not found, creating default config");
		std::cout << "Failed to load config file." << std::endl <<
			"A config file will now be created where you can enter which model to load." << std::endl <<
			"Please relaunch after!" << std::endl << std::endl;

		if (!Config::save(Application::APPLICATION_PATH + "config.cfg"))
		{
			std::cout << "Failed to create config!" << std::endl <<
				"Check if program has write permission to executable folder." << std::endl;

			std::cin.get();

			return -1;
		}

		std::cout << "Created config file." << std::endl << std::endl;

		std::cout << "Press enter to exit program..." << std::endl;
		std::cin.get();

		return -1;
	}

	GLFWwindow* window;

	if (!glfwInit())
	{
		std::cout << "[FATAL] Failed to initialize GLFW!" << std::endl;
		return -1;
	}

	Debug::log("Initializing GLFW...");
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	window = glfwCreateWindow(Config::windowResolutionX, Config::windowResolutionY, "TYViewer", NULL, NULL);
	if (!window)
	{
		Debug::log("[FATAL] Failed to create GLFWwindow!");
		std::cout << "[FATAL] Failed to create GLFWwindow!" << std::endl;
		glfwTerminate();
		return -1;
	}

	Debug::log("Initializing OpenGL context...");
	glfwMakeContextCurrent(window);
	gladLoadGL();

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		Debug::log("[FATAL] Failed to initialize GLAD!");
		std::cout << "[FATAL] Failed to initialize GLAD!" << std::endl;
		glfwTerminate();
		return -1;
	}

	Debug::log("OpenGL initialized successfully");
	glfwSetWindowSizeCallback(window, onWindowResized);

	Debug::log("Creating application instance...");
	application = new Application(window);
	Debug::log("Initializing application...");
	application->initialize();
	Debug::log("Starting main loop...");
	application->run();

	return 0;
}