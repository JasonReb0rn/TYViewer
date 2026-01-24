#pragma once

#include <string>

struct GLFWwindow;

namespace Util
{
	// Opens a native folder picker. Returns empty string on cancel/failure.
	std::string pickFolderDialog(GLFWwindow* window, const std::string& title);
}

