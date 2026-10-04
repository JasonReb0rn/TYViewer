#pragma once

#include <vector>
#include <string>
#include <limits>

#include "vertex.h"
#include "texture.h"
#include "shader.h"

#include "drawable.h"
#include "transformable.h"

// How a mesh composites. Opaque stays in the depth-writing pass.
// Alpha, additive, and subtractive are drawn after every opaque mesh.
enum class MeshBlend
{
	Opaque = 0,
	Alpha,
	Additive,
	Subtractive
};

class Mesh : public Drawable, public Transformable
{
public:
	Mesh();
	Mesh(const std::vector<Vertex>& vertices,
	     const std::vector<unsigned int>& indices,
	     Texture* texture,
	     const std::string& materialName = "",
	     const std::string& partName = "");
	~Mesh();

	virtual void draw(Shader& shader) const override;
	// `world` is the placed instance. Room meshes pass identity.
	void draw(Shader& shader, const glm::mat4& world) const;

	// Raw vertex access (debug/overlay). Order matches parsed file order.
	const std::vector<Vertex>& getVertices() const { return m_vertices; }
	// Raw index access (triangulated). Indices are into `getVertices()` and are in groups of 3.
	const std::vector<unsigned int>& getIndices() const { return m_indices; }
	
	// "Material" here is the texture/material slot name as provided by the game formats.
	// Multiple mesh parts can share the same material name.
	std::string getMaterialName() const { return m_materialName; }
	// Human-facing "mesh part"/component/subobject name (when available).
	std::string getPartName() const { return m_partName; }
	// Meshes created from one TY1 subobject share a group. -1 means ungrouped.
	void setSubobjectGroup(int group) { m_subobjectGroup = group; }
	int getSubobjectGroup() const { return m_subobjectGroup; }
	void setEnabled(bool enabled) { m_enabled = enabled; }
	bool isEnabled() const { return m_enabled; }
	void setBlend(MeshBlend blend) { m_blend = blend; }
	MeshBlend getBlend() const { return m_blend; }
	// Fragments below this alpha are discarded. Masked cards use the material aref.
	void setAlphaRef(float alphaRef) { m_alphaRef = alphaRef; }
	float getAlphaRef() const { return m_alphaRef; }
	bool isTransparent() const { return m_blend != MeshBlend::Opaque; }
	
	// NOTE: UI/debug code expects `int`, but containers use `size_t`.
	// Clamp to avoid overflow and silence C4267 warnings on x64.
	int getVertexCount() const
	{
		const size_t count = m_vertices.size();
		const size_t maxInt = static_cast<size_t>(std::numeric_limits<int>::max());
		return (count > maxInt) ? std::numeric_limits<int>::max() : static_cast<int>(count);
	}
	int getTriangleCount() const
	{
		const size_t count = m_indices.size() / 3;
		const size_t maxInt = static_cast<size_t>(std::numeric_limits<int>::max());
		return (count > maxInt) ? std::numeric_limits<int>::max() : static_cast<int>(count);
	}

private:
	void setup();

	unsigned int vao, vbo, ebo;

	std::vector<Vertex> m_vertices;
	std::vector<unsigned int> m_indices;

	Texture* m_texture;
	std::string m_materialName;
	std::string m_partName;
	int m_subobjectGroup = -1;
	bool m_enabled;
	MeshBlend m_blend = MeshBlend::Opaque;
	float m_alphaRef = 0.01f;
};
