#pragma once

#include <string>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

// One placed object from a TY1 .lv2 `name` block. `model` is filled by the caller.
struct Ty1Instance
{
	std::string typeName;
	std::string modelFile;
	class Model* model = nullptr;
	glm::vec3 position{ 0.0f, 0.0f, 0.0f };
	// Pitch, yaw, roll in radians, as stored in the level.
	glm::vec3 rotation{ 0.0f, 0.0f, 0.0f };
	glm::vec3 scale{ 1.0f, 1.0f, 1.0f };
	bool visible = true;
};

// Krome stores this as a row-vector matrix (scale, then pitch, yaw, roll, translation
// in the last row). The returned matrix is that transform for a column-vector shader.
glm::mat4 ty1InstanceMatrix(const Ty1Instance& instance);

// `globalModelText` is the archive file global.model. `mdlFiles` is every .mdl name.
// Instances with no mesh keep an empty modelFile and still appear in the list.
std::vector<Ty1Instance> parseTy1Instances(
	const std::string& levelText,
	const std::string& globalModelText,
	const std::vector<std::string>& mdlFiles);
