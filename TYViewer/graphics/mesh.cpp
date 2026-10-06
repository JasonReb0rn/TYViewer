#include "mesh.h"

#include <cctype>
#include <limits>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/mat3x3.hpp>
#include <glm/common.hpp>

#include "content.h"
#include "render_stats.h"

namespace
{
	// One GPU buffer, reused by every mesh's instanced draw. Every Mesh's VAO wires
	// attributes 5-8 to this buffer in setup(); drawInstanced() re-uploads it with
	// that mesh's world matrices right before each instanced draw call. Instances
	// from two meshes are never interleaved between the upload and the draw, so
	// sharing one buffer across all meshes is safe.
	//
	// Ordinary draws leave those attributes enabled. glDrawElements still fetches
	// instance 0, so the buffer must already have storage. One identity matrix is
	// enough; the shader ignores it while useInstancing is 0.
	unsigned int sharedInstanceBuffer()
	{
		static unsigned int buffer = 0;
		if (buffer == 0)
		{
			glGenBuffers(1, &buffer);
			const glm::mat4 identity(1.0f);
			glBindBuffer(GL_ARRAY_BUFFER, buffer);
			glBufferData(GL_ARRAY_BUFFER, sizeof(identity), &identity[0][0], GL_DYNAMIC_DRAW);
		}
		return buffer;
	}

	std::string toLowerAscii(const std::string& value)
	{
		std::string lower = value;
		for (char& c : lower)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return lower;
	}
}

Mesh::Mesh() :
	Drawable(),
	Transformable(glm::vec3(0, 0, 0)),
	m_vertices(),
	m_indices(),
	m_texture(NULL),
	m_materialName(""),
	m_materialNameLower(""),
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
	m_materialNameLower(toLowerAscii(materialName)),
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
	computeLocalAabb();
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

	// Instance matrix, one vec4 attribute per column (locations 5-8). Enabled on
	// every draw. The shader reads modelMatrix instead while useInstancing is 0;
	// the fetch of instance 0 still happens, from the identity matrix stored in
	// the shared buffer until drawInstanced() replaces it.
	const unsigned int instanceBuffer = sharedInstanceBuffer();
	glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
	for (int column = 0; column < 4; column++)
	{
		const unsigned int location = 5 + static_cast<unsigned int>(column);
		glEnableVertexAttribArray(location);
		glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4),
			reinterpret_cast<const void*>(sizeof(glm::vec4) * static_cast<size_t>(column)));
		glVertexAttribDivisor(location, 1);
	}

	glBindVertexArray(0);
}

void Mesh::computeLocalAabb()
{
	m_hasLocalAabb = false;
	if (m_vertices.empty())
		return;

	glm::vec3 minCorner(std::numeric_limits<float>::max());
	glm::vec3 maxCorner(-std::numeric_limits<float>::max());
	for (const Vertex& vertex : m_vertices)
	{
		const glm::vec3 point(vertex.position);
		minCorner = glm::min(minCorner, point);
		maxCorner = glm::max(maxCorner, point);
	}

	// Same 1-unit pad as Application::computeInstanceWorldBounds, so a part sitting
	// exactly on a frustum edge does not flicker as the camera turns.
	const glm::vec3 pad(1.0f);
	m_localAabbMin = minCorner - pad;
	m_localAabbMax = maxCorner + pad;
	m_hasLocalAabb = true;
}

void Mesh::draw(Shader& shader) const
{
	draw(shader, glm::mat4(1.0f), MeshDrawStyle{});
}

void Mesh::prepareDraw(Shader& shader, const MeshDrawStyle& style) const
{
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
	shader.setUniform1i("useSkinning", 0);

	// One hash lookup (on the name this mesh already lowercased at construction)
	// feeds the uv matrix, wrap, and water queries below, instead of each of them
	// separately lowercasing m_materialName and hashing it again.
	const auto* materialDraw = m_content ? m_content->findTy1MaterialLower(m_materialNameLower) : nullptr;

	const glm::mat3 uv = m_content
		? m_content->ty1UvMatrixFor(materialDraw, m_content->ty1AnimTime(), m_content->ty1AnimYaw(), m_content->ty1AnimPitch())
		: glm::mat3(1.0f);
	shader.setUniformMat3("uvMatrix", uv);
	
	m_texture->bind();
	// Wrap is per material and textures are shared, so set it on every draw.
	// Waterfalls repeat. clampUV / address clamp an axis on their own.
	bool clampU = false;
	bool clampV = false;
	if (m_content)
		m_content->ty1UvWrapFor(materialDraw, clampU, clampV);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clampU ? GL_CLAMP_TO_EDGE : GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clampV ? GL_CLAMP_TO_EDGE : GL_REPEAT);

	glm::vec4 waterScale(0.0f);
	int water = 0;
	if (m_content && m_content->ty1IndirectWaterFor(materialDraw, waterScale))
	{
		Texture* ripple = m_content->ty1WaterRipple();
		if (ripple != nullptr)
		{
			water = 1;
			ripple->bind(1);
			shader.setUniform1i("waterRipple", 1);
			shader.setUniform4f("waterScale", waterScale);
		}
	}
	shader.setUniform1i("water", water);
}

void Mesh::draw(Shader& shader, const glm::mat4& world, const MeshDrawStyle& style) const
{
	// Disabled mesh parts should be fully hidden (skip draw call).
	if (!m_enabled)
	{
		return;
	}

	prepareDraw(shader, style);
	shader.setUniform1i("useInstancing", 0);
	shader.setUniformMat4("modelMatrix", world * getMatrix());

	glBindVertexArray(vao);
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr);

	RenderStats::drawCalls++;
	RenderStats::trianglesDrawn += static_cast<long long>(m_indices.size() / 3);
}

void Mesh::drawSkinned(Shader& shader, const glm::mat4& world, const glm::mat4* bones, const int* boneParents,
	int boneCount, const MeshDrawStyle& style) const
{
	if (!m_enabled)
		return;
	if (bones == nullptr || boneParents == nullptr || boneCount <= 0 || boneCount > kMaxSkinBones)
	{
		draw(shader, world, style);
		return;
	}

	prepareDraw(shader, style);
	shader.setUniform1i("useInstancing", 0);
	shader.setUniformMat4("modelMatrix", world * getMatrix());
	shader.setUniformMat4Array("bones", bones, boneCount);
	shader.setUniform1iArray("boneParents", boneParents, boneCount);
	shader.setUniform1i("useSkinning", 1);

	glBindVertexArray(vao);
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr);

	shader.setUniform1i("useSkinning", 0);

	RenderStats::drawCalls++;
	RenderStats::trianglesDrawn += static_cast<long long>(m_indices.size() / 3);
}

void Mesh::drawInstanced(Shader& shader, const std::vector<glm::mat4>& worlds, const MeshDrawStyle& style) const
{
	if (!m_enabled || worlds.empty())
	{
		return;
	}

	prepareDraw(shader, style);
	shader.setUniform1i("useInstancing", 1);

	// The mesh-local transform is identity for every TY1 part today (nothing calls
	// Transformable::setPosition/setRotation/setScale on a Mesh), but fold it in so a
	// future per-part offset keeps working without touching call sites.
	const glm::mat4 local = getMatrix();
	std::vector<glm::mat4> combined(worlds.size());
	for (size_t i = 0; i < worlds.size(); i++)
		combined[i] = worlds[i] * local;

	glBindBuffer(GL_ARRAY_BUFFER, sharedInstanceBuffer());
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(combined.size() * sizeof(glm::mat4)), combined.data(), GL_DYNAMIC_DRAW);

	glBindVertexArray(vao);
	glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(combined.size()));

	// Other draw paths (room meshes, debug/text draws reusing this shader program)
	// never touch useInstancing. Leaving it at 1 would make their next modelMatrix
	// uniform update silently do nothing, since the vertex shader would keep
	// reading the instance attributes instead.
	shader.setUniform1i("useInstancing", 0);

	RenderStats::drawCalls++;
	RenderStats::instancedBatches++;
	RenderStats::trianglesDrawn += static_cast<long long>(m_indices.size() / 3) * static_cast<long long>(combined.size());
}
