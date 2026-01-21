#pragma once

#include <vector>

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
	int getMeshCount() const { return meshes.size(); }
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