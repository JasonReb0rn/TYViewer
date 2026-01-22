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

void onMouseButton(GLFWwindow* window, int button, int action, int mods)
{
	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);
	application->onMouseButton(button, action, xpos, ypos);
}

void onCursorPos(GLFWwindow* window, double xpos, double ypos)
{
	// Debug: Uncomment to verify callbacks are working
	// std::cout << "Mouse moved: " << xpos << ", " << ypos << std::endl;
	
	if (application)
	{
		application->onMouseMove(xpos, ypos);
	}
}

void onScroll(GLFWwindow* window, double xoffset, double yoffset)
{
	if (application)
	{
		application->onScroll(xoffset, yoffset);
	}
}

void onKeyPress(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (action == GLFW_PRESS || action == GLFW_REPEAT)
	{
		application->onKeyPress(key);
	}
}

void onChar(GLFWwindow* window, unsigned int codepoint)
{
	if (application)
	{
		application->onChar(codepoint);
	}
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
	glfwSetMouseButtonCallback(window, onMouseButton);
	glfwSetCursorPosCallback(window, onCursorPos);
	glfwSetScrollCallback(window, onScroll);
	glfwSetKeyCallback(window, onKeyPress);
	glfwSetCharCallback(window, onChar);

	Debug::log("Creating application instance...");
	application = new Application(window);
	Debug::log("Initializing application...");
	application->initialize();
	Debug::log("Starting main loop...");
	application->run();

	return 0;
}