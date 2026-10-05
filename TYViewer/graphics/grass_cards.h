#pragma once

#include <functional>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "mesh.h"

class Content;
class Model;
class Shader;
class Texture;

// TY1 grass (Grass_DrawGC). Every triangle of a material with "effect = grass,N"
// gets type N's blade cards, including guide meshes the viewer hides.
// Cards stay vertical and spread along the camera's right axis in XZ.
class GrassCards
{
public:
	GrassCards() = default;
	~GrassCards();

	void build(const std::vector<Model*>& models, Content& content);
	void clear();
	// `visible` is the frustum test for a world AABB.
	void draw(const glm::mat4& vpMatrix, const glm::vec3& cameraRight,
		const std::function<bool(const glm::vec3&, const glm::vec3&)>& visible);

	size_t cardCount() const { return totalCards; }

private:
	struct Card
	{
		glm::vec3 base;
		float height;
		float halfWidth;
		float u0;
		glm::vec4 colour;
	};

	struct Batch
	{
		unsigned int vao = 0;
		unsigned int vbo = 0;
		int count = 0;
		Texture* texture = nullptr;
		float alphaRef = 0.5f;
		MeshBlend blend = MeshBlend::Opaque;
		bool clampU = false;
		bool clampV = false;
		glm::vec3 boxMin{ 0.0f };
		glm::vec3 boxMax{ 0.0f };
	};

	void ensureShader();
	void upload(Batch& batch, const std::vector<Card>& cards);

	std::vector<Batch> batches;
	size_t totalCards = 0;
	Shader* shader = nullptr;
	unsigned int cornerVbo = 0;
};
