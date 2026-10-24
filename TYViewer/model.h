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

// One MDL subobject. The sector numbers live in the name ("002 000 004 Geom_Ground"):
// X page, vertical page, Z page. Names without that prefix are unsectioned.
struct SubobjectPart
{
	std::string name;
	std::string objectName;
	bool hasSector = false;
	int sectorX = 0;
	int sectorLayer = 0;
	int sectorZ = 0;
};

void parseSubobjectName(const std::string& name, SubobjectPart& part);

class Model : public Drawable
{
public:
	Model(const std::vector<Mesh*>& meshes);
	~Model();

	virtual void draw(Shader& shader) const override;
	// transparentPass false draws depth-writing meshes. true draws the rest, in file order.
	void drawMeshes(Shader& shader, bool transparentPass) const;
	void drawMeshes(Shader& shader, bool transparentPass, const glm::mat4& world, const MeshDrawStyle& style = {}) const;

	// Union of every mesh's local AABB (Mesh::hasLocalAabb), computed once in the
	// constructor. A coarse per-model test before testing each mesh; false when no
	// mesh in this model has vertices (nothing to draw, nothing to cull against).
	bool hasLocalAabb() const { return m_hasLocalAabb; }
	const glm::vec3& getLocalAabbMin() const { return m_localAabbMin; }
	const glm::vec3& getLocalAabbMax() const { return m_localAabbMax; }
	
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
	// Parallel to bounds: one entry per MDL subobject, in file order.
	std::vector<SubobjectPart> subobjects;
	std::vector<Bone> bones;

private:
	std::vector<Mesh*> meshes;
	bool m_hasLocalAabb = false;
	glm::vec3 m_localAabbMin{ 0.0f, 0.0f, 0.0f };
	glm::vec3 m_localAabbMax{ 0.0f, 0.0f, 0.0f };
};
