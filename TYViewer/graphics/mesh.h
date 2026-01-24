#pragma once

#include <vector>
#include <string>
#include <limits>

#include "vertex.h"
#include "texture.h"
#include "shader.h"

#include "drawable.h"
#include "transformable.h"

class Mesh : public Drawable, public Transformable
{
public:
	Mesh();
	Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices, Texture* texture, const std::string& materialName = "");
	~Mesh();

	virtual void draw(Shader& shader) const override;

	// Raw vertex access (debug/overlay). Order matches parsed file order.
	const std::vector<Vertex>& getVertices() const { return m_vertices; }
	// Raw index access (triangulated). Indices are into `getVertices()` and are in groups of 3.
	const std::vector<unsigned int>& getIndices() const { return m_indices; }
	
	// Material debugging
	std::string getMaterialName() const { return m_materialName; }
	void setEnabled(bool enabled) { m_enabled = enabled; }
	bool isEnabled() const { return m_enabled; }
	
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
	bool m_enabled;
};
