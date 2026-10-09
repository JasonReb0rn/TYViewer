#include "mesh.h"

#include <cctype>
#include <cmath>
#include <limits>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/mat3x3.hpp>
#include <glm/common.hpp>

#include "content.h"
#include "render_stats.h"
#include "water_reflection.h"

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

	// Last GL state written by Mesh. A repeat of the same program, blend, texture,
	// wrap, or matrix skips the call. Invalidated when something else touches that state.
	struct DrawState
	{
		bool blendValid = false;
		MeshBlend blend = MeshBlend::Opaque;
		const Texture* texture = nullptr;
		int wrapS = -1;
		int wrapT = -1;
		int activeUnit = -1;
		const Shader* uvShader = nullptr;
		glm::mat3 uv{ 1.0f };
		bool uvValid = false;
		const Shader* modelShader = nullptr;
		glm::mat4 model{ 1.0f };
		bool modelValid = false;
	};

	DrawState g_drawState;

	void uploadModelMatrix(Shader& program, const glm::mat4& model)
	{
		if (g_drawState.modelValid && g_drawState.modelShader == &program && g_drawState.model == model)
			return;
		program.setUniformMat4("modelMatrix", model);
		g_drawState.model = model;
		g_drawState.modelShader = &program;
		g_drawState.modelValid = true;
	}

	const glm::mat4 kIdentity(1.0f);

	void applyMeshBlend(MeshBlend blend)
	{
		if (g_drawState.blendValid && g_drawState.blend == blend)
			return;

		glBlendEquation(GL_FUNC_ADD);
		glDepthMask(GL_TRUE);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		if (blend == MeshBlend::Additive)
		{
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			glDepthMask(GL_FALSE);
		}
		else if (blend == MeshBlend::Subtractive)
		{
			glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
			glBlendFunc(GL_ONE, GL_ONE);
			glDepthMask(GL_FALSE);
		}
		else if (blend == MeshBlend::Alpha)
		{
			glDepthMask(GL_FALSE);
		}
		g_drawState.blend = blend;
		g_drawState.blendValid = true;
	}

	void uploadModelAt(Shader& program, int location, const glm::mat4& model)
	{
		if (location < 0)
			return;
		if (g_drawState.modelValid && g_drawState.modelShader == &program && g_drawState.model == model)
			return;
		program.setUniformMat4(location, model);
		g_drawState.model = model;
		g_drawState.modelShader = &program;
		g_drawState.modelValid = true;
	}

	// world is the placed instance. An identity room part does not rebuild or re-upload.
	void uploadWorldModel(Shader& program, const glm::mat4& world, bool localIdentity, const glm::mat4& local)
	{
		if (localIdentity)
		{
			if (world == kIdentity)
				uploadModelMatrix(program, kIdentity);
			else
				uploadModelMatrix(program, world);
		}
		else if (world == kIdentity)
			uploadModelMatrix(program, local);
		else
			uploadModelMatrix(program, world * local);
	}

	struct ReflectLocs
	{
		const Shader* shader = nullptr;
		int uvMatrix = -1;
		int alphaRef = -1;
		int useInstancing = -1;
		int modelMatrix = -1;
	};

	const ReflectLocs& reflectLocs(Shader& shader, bool cutout)
	{
		static ReflectLocs slots[4];
		for (const ReflectLocs& slot : slots)
		{
			if (slot.shader == &shader)
				return slot;
		}
		ReflectLocs* dest = nullptr;
		for (ReflectLocs& slot : slots)
		{
			if (slot.shader == nullptr)
			{
				dest = &slot;
				break;
			}
		}
		if (dest == nullptr)
			dest = &slots[0];
		dest->shader = &shader;
		dest->uvMatrix = shader.uniformLocation("uvMatrix");
		dest->useInstancing = shader.uniformLocation("useInstancing");
		dest->modelMatrix = shader.uniformLocation("modelMatrix");
		dest->alphaRef = cutout ? shader.uniformLocation("alphaRef") : -1;
		return *dest;
	}

	void uploadReflectInstances(const glm::mat4* matrices, size_t count)
	{
		glBindBuffer(GL_ARRAY_BUFFER, sharedInstanceBuffer());
		glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(count * sizeof(glm::mat4)), matrices, GL_DYNAMIC_DRAW);
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
	m_envSky(false),
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
	m_envSky(toLowerAscii(partName) == "env_sky"),
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

void Mesh::setWaterType(const std::string& typeName)
{
	m_waterType = toLowerAscii(typeName);
}

void Mesh::setWaterSurfaceY(float y)
{
	m_waterSurfaceY = y;
	m_hasWaterSurface = true;
}

void Mesh::expandLocalAabb(float padY)
{
	if (!m_hasLocalAabb || padY == 0.0f)
		return;
	const float pad = std::fabs(padY);
	m_localAabbMin.y -= pad;
	m_localAabbMax.y += pad;
}

void Mesh::draw(Shader& shader) const
{
	draw(shader, glm::mat4(1.0f), MeshDrawStyle{});
}

Shader* Mesh::s_waterSurfaceShader = nullptr;
Shader* Mesh::s_simpleWaterShader = nullptr;
bool Mesh::s_simpleWater = false;

void Mesh::setWaterSurfaceShader(Shader* shader)
{
	s_waterSurfaceShader = shader;
}

void Mesh::setSimpleWaterShader(Shader* shader)
{
	s_simpleWaterShader = shader;
}

void Mesh::setSimpleWater(bool enabled)
{
	s_simpleWater = enabled;
}

void Mesh::invalidateDrawState()
{
	g_drawState = DrawState{};
	Shader::invalidateBind();
}

bool Mesh::reflectionCutsOut() const
{
	if (m_blend != MeshBlend::Opaque || m_alphaRef > 0.02f)
		return true;
	ensureMaterial();
	return m_materialCutout;
}

void Mesh::ensureMaterial() const
{
	if (m_materialReady)
		return;

	m_materialReady = true;
	m_materialDraw = nullptr;
	m_materialCutout = false;
	m_uvAnimated = false;
	m_wrapS = GL_REPEAT;
	m_wrapT = GL_REPEAT;
	if (m_content == nullptr)
		return;

	const Content::Ty1MaterialDraw* draw = m_content->findTy1MaterialLower(m_materialNameLower);
	m_materialDraw = draw;
	if (draw == nullptr)
		return;

	m_materialCutout = draw->masked || draw->blend != MeshBlend::Opaque;
	m_uvAnimated = draw->uvAnim != Content::Ty1UvAnim::None || draw->manualScroll;
	bool clampU = false;
	bool clampV = false;
	m_content->ty1UvWrapFor(draw, clampU, clampV);
	m_wrapS = clampU ? GL_CLAMP_TO_EDGE : GL_REPEAT;
	m_wrapT = clampV ? GL_CLAMP_TO_EDGE : GL_REPEAT;
}

const void* Mesh::materialDraw() const
{
	ensureMaterial();
	return m_materialDraw;
}

void Mesh::bindCachedTexture() const
{
	ensureMaterial();
	if (g_drawState.texture != m_texture)
	{
		m_texture->bind();
		g_drawState.texture = m_texture;
		g_drawState.activeUnit = 0;
		g_drawState.wrapS = -1;
		g_drawState.wrapT = -1;
	}
	if (g_drawState.wrapS != m_wrapS || g_drawState.wrapT != m_wrapT)
	{
		if (g_drawState.activeUnit != 0)
		{
			glActiveTexture(GL_TEXTURE0);
			g_drawState.activeUnit = 0;
		}
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, m_wrapS);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, m_wrapT);
		g_drawState.wrapS = m_wrapS;
		g_drawState.wrapT = m_wrapT;
	}
}

glm::mat3 Mesh::cachedUvMatrix() const
{
	ensureMaterial();
	if (!m_uvAnimated || m_content == nullptr)
		return glm::mat3(1.0f);

	return m_content->ty1UvMatrixFor(
		static_cast<const Content::Ty1MaterialDraw*>(m_materialDraw),
		m_content->ty1AnimTime(),
		m_content->ty1AnimYaw(),
		m_content->ty1AnimPitch());
}

Shader& Mesh::programFor(Shader& passed, const MeshDrawStyle& style) const
{
	if (!style.reflection && isWaterSurface()
		&& m_content != nullptr && m_content->ty1WaterTypeFor(m_waterType) != nullptr)
	{
		if (s_simpleWater && s_simpleWaterShader != nullptr)
			return *s_simpleWaterShader;
		if (s_waterSurfaceShader != nullptr)
			return *s_waterSurfaceShader;
	}
	return passed;
}

void Mesh::prepareDraw(Shader& shader, const MeshDrawStyle& style) const
{
	shader.bind();

	// A solid pass is the selection outline. The caller owns depth, stencil, and blend.
	if (!style.solid)
		applyMeshBlend(m_blend);
	else
		g_drawState.blendValid = false;

	const Content::Ty1WaterType* waterType = nullptr;
	if (!m_waterType.empty() && m_content)
		waterType = m_content->ty1WaterTypeFor(m_waterType);

	const bool pcWater = s_waterSurfaceShader != nullptr && &shader == s_waterSurfaceShader;
	const bool simpleWater = s_simpleWaterShader != nullptr && &shader == s_simpleWaterShader;
	const bool waterProgram = pcWater || simpleWater;
	const glm::vec4 waveColour = waterType != nullptr ? waterType->color : glm::vec4(1.0f);
	const glm::vec4 tint = style.solid ? style.tint : (waterProgram ? waveColour : glm::vec4(1.0f));
	shader.setUniform4f("tintColour", tint);
	// The opaque reflection program has no alpha test, and no alphaRef uniform.
	if (!style.reflection || style.cutout)
		shader.setUniform1f("alphaRef", m_alphaRef);

	if (pcWater && waterType != nullptr)
	{
		// 0x5c0060. 1a is dir x, dir z, frequency, phase. 1b.x is the negated height.
		shader.setUniform4f("waterWaveCoeffs1a", glm::vec4(waterType->wave0DirX, waterType->wave0DirZ, waterType->wave0Freq, waterType->wave0Phase));
		shader.setUniform4f("waterWaveCoeffs1b", glm::vec4(-waterType->wave0Height, 0.0f, 0.0f, 0.0f));
		shader.setUniform4f("waterWaveCoeffs2a", glm::vec4(waterType->wave1DirX, waterType->wave1DirZ, waterType->wave1Freq, waterType->wave1Phase));
		shader.setUniform4f("waterWaveCoeffs2b", glm::vec4(-waterType->wave1Height, 0.0f, 0.0f, 0.0f));
		shader.setUniform4f("waterWobbleCoeffs1", glm::vec4(waterType->distanceScale, 0.0f, 0.0f, waterType->time));
		shader.setUniform4f("waterWobbleCoeffs2", glm::vec4(waterType->wobbleUVScale, waterType->wobbleUVScale, waterType->noiseScale, waterType->reflectWobble));
		const float invSize = 1.0f / static_cast<float>(WaterReflection::resolution());
		shader.setUniform4f("waterReflectCoeff", glm::vec4(invSize, invSize, waterType->reflectMix, waterType->reflectAdd));
	}
	else if (simpleWater && waterType != nullptr)
	{
		// Same displacement as the PC shader. The fragment is the GameCube ripple, not the reflection.
		shader.setUniform4f("waterWaveCoeffs1a", glm::vec4(waterType->wave0DirX, waterType->wave0DirZ, waterType->wave0Freq, waterType->wave0Phase));
		shader.setUniform4f("waterWaveCoeffs1b", glm::vec4(-waterType->wave0Height, 0.0f, 0.0f, 0.0f));
		shader.setUniform4f("waterWaveCoeffs2a", glm::vec4(waterType->wave1DirX, waterType->wave1DirZ, waterType->wave1Freq, waterType->wave1Phase));
		shader.setUniform4f("waterWaveCoeffs2b", glm::vec4(-waterType->wave1Height, 0.0f, 0.0f, 0.0f));
		shader.setUniform2f("clipOffset", style.solid ? style.clipOffset : glm::vec2(0.0f));
		shader.setUniform1i("solidColour", style.solid ? 1 : 0);
	}
	else if (!style.reflection)
	{
		shader.setUniform2f("clipOffset", style.solid ? style.clipOffset : glm::vec2(0.0f));
		shader.setUniform1i("solidColour", style.solid ? 1 : 0);
		shader.setUniform1i("useSkinning", 0);
	}

	// Cutout, wrap, and whether the UV matrix moves were copied on the first use.
	// Static materials keep an identity UV and do not hash the material again.
	const glm::mat3 uv = cachedUvMatrix();
	if (!g_drawState.uvValid || g_drawState.uvShader != &shader || g_drawState.uv != uv)
	{
		shader.setUniformMat3("uvMatrix", uv);
		g_drawState.uv = uv;
		g_drawState.uvShader = &shader;
		g_drawState.uvValid = true;
	}

	// Wrap is per material and textures are shared. Waterfalls repeat.
	// Setting the parameter on a texture the GPU is already sampling flushes the
	// pipeline, so only do it when this texture's wrap actually changes.
	bindCachedTexture();

	if (pcWater && waterType != nullptr && m_hasWaterSurface && m_content != nullptr)
	{
		int reflectEnabled = 0;
		Texture* noise = m_content->ty1WaterNoise();
		if (noise != nullptr)
		{
			noise->bind(2);
			g_drawState.activeUnit = 2;
			shader.setUniform1i("noiseTexture", 2);
		}
		const int plane = m_content->ty1ReflectionPlaneFor(m_waterSurfaceY);
		const unsigned reflectTex = m_content->ty1ReflectionTexture(plane);
		if (plane >= 0 && reflectTex != 0)
		{
			reflectEnabled = 1;
			glActiveTexture(GL_TEXTURE3);
			g_drawState.activeUnit = 3;
			glBindTexture(GL_TEXTURE_2D, reflectTex);
			shader.setUniform1i("reflectTexture", 3);
		}
		shader.setUniform1i("reflectEnabled", reflectEnabled);
	}
	else if (!style.reflection)
	{
		glm::vec4 waterScale(0.0f);
		int water = 0;
		// A .wml/.wmh chunk uses the PC noise wobble instead of the GameCube ripple.
		if (m_content && m_content->ty1IndirectWaterFor(static_cast<const Content::Ty1MaterialDraw*>(materialDraw()), waterScale))
		{
			Texture* ripple = m_content->ty1WaterRipple();
			if (ripple != nullptr)
			{
				water = 1;
				ripple->bind(1);
				g_drawState.activeUnit = 1;
				shader.setUniform1i("waterRipple", 1);
				shader.setUniform4f("waterScale", waterScale);
			}
		}
		shader.setUniform1i("water", water);
	}
	if (g_drawState.activeUnit != 0)
	{
		glActiveTexture(GL_TEXTURE0);
		g_drawState.activeUnit = 0;
	}
}

void Mesh::draw(Shader& shader, const glm::mat4& world, const MeshDrawStyle& style) const
{
	// Disabled mesh parts should be fully hidden (skip draw call).
	if (!m_enabled)
	{
		return;
	}

	Shader& program = programFor(shader, style);
	prepareDraw(program, style);
	program.setUniform1i("useInstancing", 0);
	if (isIdentity())
		uploadWorldModel(program, world, true, kIdentity);
	else
		uploadWorldModel(program, world, false, getMatrix());

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

	Shader& program = programFor(shader, style);
	prepareDraw(program, style);
	program.setUniform1i("useInstancing", 0);
	if (isIdentity())
		uploadWorldModel(program, world, true, kIdentity);
	else
		uploadWorldModel(program, world, false, getMatrix());
	program.setUniformMat4Array("bones", bones, boneCount);
	program.setUniform1iArray("boneParents", boneParents, boneCount);
	program.setUniform1i("useSkinning", 1);

	glBindVertexArray(vao);
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr);

	program.setUniform1i("useSkinning", 0);

	RenderStats::drawCalls++;
	RenderStats::trianglesDrawn += static_cast<long long>(m_indices.size() / 3);
}

void Mesh::drawInstanced(Shader& shader, const std::vector<glm::mat4>& worlds, const MeshDrawStyle& style) const
{
	if (!m_enabled || worlds.empty())
	{
		return;
	}

	Shader& program = programFor(shader, style);
	prepareDraw(program, style);
	program.setUniform1i("useInstancing", 1);

	// The mesh-local transform is identity for every TY1 part today (nothing calls
	// Transformable::setPosition/setRotation/setScale on a Mesh), so the instance
	// matrices upload as-is. A future per-part offset still folds in through the
	// scratch vector, which stays allocated across draws.
	static std::vector<glm::mat4> combined;
	const glm::mat4* upload = worlds.data();
	if (!isIdentity())
	{
		const glm::mat4 local = getMatrix();
		combined.resize(worlds.size());
		for (size_t i = 0; i < worlds.size(); i++)
			combined[i] = worlds[i] * local;
		upload = combined.data();
	}

	uploadReflectInstances(upload, worlds.size());

	glBindVertexArray(vao);
	glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(worlds.size()));

	// Other draw paths (room meshes, debug/text draws reusing this shader program)
	// never touch useInstancing. Leaving it at 1 would make their next modelMatrix
	// uniform update silently do nothing, since the vertex shader would keep
	// reading the instance attributes instead.
	program.setUniform1i("useInstancing", 0);

	RenderStats::drawCalls++;
	RenderStats::instancedBatches++;
	RenderStats::trianglesDrawn += static_cast<long long>(m_indices.size() / 3) * static_cast<long long>(worlds.size());
}

void Mesh::drawReflection(Shader& shader, bool cutout) const
{
	if (!m_enabled)
		return;

	const ReflectLocs& locs = reflectLocs(shader, cutout);
	shader.bind();
	applyMeshBlend(m_blend);
	bindCachedTexture();

	const glm::mat3 uv = cachedUvMatrix();
	if (!g_drawState.uvValid || g_drawState.uvShader != &shader || g_drawState.uv != uv)
	{
		shader.setUniformMat3(locs.uvMatrix, uv);
		g_drawState.uv = uv;
		g_drawState.uvShader = &shader;
		g_drawState.uvValid = true;
	}
	if (cutout)
		shader.setUniform1f(locs.alphaRef, m_alphaRef);
	shader.setUniform1i(locs.useInstancing, 0);
	if (isIdentity())
		uploadModelAt(shader, locs.modelMatrix, kIdentity);
	else
		uploadModelAt(shader, locs.modelMatrix, getMatrix());

	glBindVertexArray(vao);
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr);

	RenderStats::drawCalls++;
	RenderStats::trianglesDrawn += static_cast<long long>(m_indices.size() / 3);
}

void Mesh::drawReflectionInstanced(Shader& shader, const std::vector<glm::mat4>& worlds, bool cutout) const
{
	if (!m_enabled || worlds.empty())
		return;

	const ReflectLocs& locs = reflectLocs(shader, cutout);
	shader.bind();
	applyMeshBlend(m_blend);
	bindCachedTexture();

	const glm::mat3 uv = cachedUvMatrix();
	if (!g_drawState.uvValid || g_drawState.uvShader != &shader || g_drawState.uv != uv)
	{
		shader.setUniformMat3(locs.uvMatrix, uv);
		g_drawState.uv = uv;
		g_drawState.uvShader = &shader;
		g_drawState.uvValid = true;
	}
	if (cutout)
		shader.setUniform1f(locs.alphaRef, m_alphaRef);
	shader.setUniform1i(locs.useInstancing, 1);

	static std::vector<glm::mat4> combined;
	const glm::mat4* upload = worlds.data();
	if (!isIdentity())
	{
		const glm::mat4 local = getMatrix();
		combined.resize(worlds.size());
		for (size_t i = 0; i < worlds.size(); i++)
			combined[i] = worlds[i] * local;
		upload = combined.data();
	}
	uploadReflectInstances(upload, worlds.size());

	glBindVertexArray(vao);
	glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(worlds.size()));
	shader.setUniform1i(locs.useInstancing, 0);

	RenderStats::drawCalls++;
	RenderStats::instancedBatches++;
	RenderStats::trianglesDrawn += static_cast<long long>(m_indices.size() / 3) * static_cast<long long>(worlds.size());
}
