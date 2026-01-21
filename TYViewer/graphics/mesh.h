#pragma once

#include <vector>
#include <string>

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
	
	// Material debugging
	std::string getMaterialName() const { return m_materialName; }
	void setEnabled(bool enabled) { m_enabled = enabled; }
	bool isEnabled() const { return m_enabled; }
	
	int getVertexCount() const { return m_vertices.size(); }
	int getTriangleCount() const { return m_indices.size() / 3; }

private:
	void setup();

	unsigned int vao, vbo, ebo;

	std::vector<Vertex> m_vertices;
	std::vector<unsigned int> m_indices;

	Texture* m_texture;
	std::string m_materialName;
	bool m_enabled;
};