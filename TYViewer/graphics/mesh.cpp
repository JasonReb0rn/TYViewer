#include "mesh.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

Mesh::Mesh() :
	Drawable(),
	Transformable(glm::vec3(0, 0, 0)),
	m_vertices(),
	m_indices(),
	m_texture(NULL),
	m_materialName(""),
	m_partName(""),
	m_subobjectGroup(-1),
	m_enabled(true),
	m_defaultEnabled(true),
	m_blend(MeshBlend::Opaque),
	vao(0),
	vbo(0),
	ebo(0)
{}

Mesh::Mesh(const std::vector<Vertex>& vertices,
           const std::vector<unsigned int>& indices,
           Texture* texture,
           const std::string& materialName,
           const std::string& partName) :
	Drawable(),
	Transformable({ 0.0f, 0.0f, 0.0f }),
	m_vertices(vertices),
	m_indices(indices),
	m_texture(texture),
	m_materialName(materialName),
	m_partName(partName),
	m_subobjectGroup(-1),
	m_enabled(true),
	m_defaultEnabled(true),
	m_blend(MeshBlend::Opaque),
	vao(0),
	vbo(0),
	ebo(0)
{
	setup();
}

Mesh::~Mesh()
{
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &vbo);
	glDeleteBuffers(1, &ebo);
}

void Mesh::setup()
{
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &ebo);

	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(Vertex), &m_vertices[0], GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(unsigned int), &m_indices[0], GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, position));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, normal));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, colour));

	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, texcoord));

	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, skin));

	glBindVertexArray(0);
}

void Mesh::draw(Shader& shader) const
{
	draw(shader, glm::mat4(1.0f), MeshDrawStyle{});
}

void Mesh::draw(Shader& shader, const glm::mat4& world, const MeshDrawStyle& style) const
{
	// Disabled mesh parts should be fully hidden (skip draw call).
	if (!m_enabled)
	{
		return;
	}

	shader.bind();

	// A solid pass is the selection outline. The caller owns depth, stencil, and blend.
	if (!style.solid)
	{
		// Opaque keeps the default blend and writes depth. Transparent modes are
		// drawn in a later pass and must not punch a hole through the world.
		glBlendEquation(GL_FUNC_ADD);
		glDepthMask(GL_TRUE);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		if (m_blend == MeshBlend::Additive)
		{
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			glDepthMask(GL_FALSE);
		}
		else if (m_blend == MeshBlend::Subtractive)
		{
			glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
			glBlendFunc(GL_ONE, GL_ONE);
			glDepthMask(GL_FALSE);
		}
		else if (m_blend == MeshBlend::Alpha)
		{
			glDepthMask(GL_FALSE);
		}
	}

	shader.setUniform4f("tintColour", style.solid ? style.tint : glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
	shader.setUniform2f("clipOffset", style.solid ? style.clipOffset : glm::vec2(0.0f));
	shader.setUniform1i("solidColour", style.solid ? 1 : 0);
	shader.setUniform1f("alphaRef", m_alphaRef);
	
	m_texture->bind();

	shader.setUniformMat4("modelMatrix", world * getMatrix());

	glBindVertexArray(vao);

	glDrawElements(GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, nullptr);
}
