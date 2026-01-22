#pragma once

#include <vector>
#include <limits>

#include "graphics/drawable.h"
#include "graphics/transformable.h"

#include "graphics/mesh.h"

struct Collider 
{
	glm::vec3 position;
	float size;
};

struct Bounds
{
	glm::vec3 corner;
	glm::vec3 size;
};

struct Bone
{
	glm::vec3 defaultPosition;
};

class Model : public Drawable
{
public:
	Model(const std::vector<Mesh*>& meshes);
	~Model();

	virtual void draw(Shader& shader) const override;
	
	// For GUI access
	const std::vector<Mesh*>& getMeshes() const { return meshes; }
	std::vector<Mesh*>& getMeshes() { return meshes; }
	int getMeshCount() const
	{
		const size_t count = meshes.size();
		const size_t maxInt = static_cast<size_t>(std::numeric_limits<int>::max());
		return (count > maxInt) ? std::numeric_limits<int>::max() : static_cast<int>(count);
	}
	int getTotalVertexCount() const;
	int getTotalTriangleCount() const;

	glm::vec3 bounds_crn;
	glm::vec3 bounds_size;

	std::vector<Collider> colliders;
	std::vector<Bounds> bounds;
	std::vector<Bone> bones;

private:
	std::vector<Mesh*> meshes;
};