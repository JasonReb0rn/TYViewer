#pragma once

#include <vector>
#include <string>
#include <limits>

#include "vertex.h"
#include "texture.h"
#include "shader.h"

#include "drawable.h"
#include "transformable.h"

class Content;

// How a mesh composites. Opaque stays in the depth-writing pass.
// Alpha, additive, and subtractive are drawn after every opaque mesh.
enum class MeshBlend
{
	Opaque = 0,
	Alpha,
	Additive,
	Subtractive
};

// solid skips the mesh's blend and depth setup so a stencil outline can own that state.
// reflection draws with the reflection program, which has none of the water uniforms.
struct MeshDrawStyle
{
	glm::vec4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };
	glm::vec2 clipOffset{ 0.0f, 0.0f };
	bool solid = false;
	bool reflection = false;
	// Outline a part the list has hidden. The main pass still skips disabled meshes.
	bool drawHidden = false;
	// Reflection cutout program. Opaque reflections have no alpha test.
	bool cutout = false;
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
	void draw(Shader& shader, const glm::mat4& world, const MeshDrawStyle& style = {}) const;
	// One draw call for every entry in `worlds`. Used when many level objects share
	// this mesh (the common case for props). Skips the call when disabled or empty.
	void drawInstanced(Shader& shader, const std::vector<glm::mat4>& worlds, const MeshDrawStyle& style = {}) const;
	// Reflection bucket already bound this program and set the view-projection.
	// Binds the texture when it changes, then the VAO and the draw. No water,
	// skinning, or ripple work, and no material-name hash.
	void drawReflection(Shader& shader, bool cutout) const;
	void drawReflectionInstanced(Shader& shader, const std::vector<glm::mat4>& worlds, bool cutout) const;
	// TY1 skinned draw. `bones` are model-space matrices (0 is the model root) and
	// `boneParents[i]` is the parent matrix of bones[i]. Falls back to draw() past kMaxSkinBones.
	static const int kMaxSkinBones = 64;
	// `textureOverride` replaces the mesh texture for this draw. Blend and alpha test stay.
	void drawSkinned(Shader& shader, const glm::mat4& world, const glm::mat4* bones, const int* boneParents,
		int boneCount, const MeshDrawStyle& style = {}, Texture* textureOverride = nullptr) const;

	// Water surfaces bind this instead of the world program. Null keeps the caller's shader.
	static void setWaterSurfaceShader(Shader* shader);
	// GameCube ripple plus wave displacement, used when reflections are off.
	static void setSimpleWaterShader(Shader* shader);
	static void setSimpleWater(bool enabled);
	// Drop the cached program, blend, texture, wrap, and matrices. Call after any draw
	// that changes that GL state without going through Mesh.
	static void invalidateDrawState();
	Texture* getTexture() const { return m_texture; }
	// Masked, blended, and faded-vertex meshes keep an alpha test in the reflection.
	// Opaque ones do not.
	bool reflectionCutsOut() const;

	// Raw vertex access (debug/overlay). Order matches parsed file order.
	const std::vector<Vertex>& getVertices() const { return m_vertices; }
	// Raw index access (triangulated). Indices are into `getVertices()` and are in groups of 3.
	const std::vector<unsigned int>& getIndices() const { return m_indices; }

	// Local-space AABB of this mesh's vertices, padded by 1 unit (computed once
	// at construction). false when the mesh has no vertices; such a mesh is never
	// culled since there is nothing to draw anyway. Room meshes pass identity as
	// `world`, so this box is already in the same space as the frustum test.
	bool hasLocalAabb() const { return m_hasLocalAabb; }
	const glm::vec3& getLocalAabbMin() const { return m_localAabbMin; }
	const glm::vec3& getLocalAabbMax() const { return m_localAabbMax; }
	
	// "Material" here is the texture/material slot name as provided by the game formats.
	// Multiple mesh parts can share the same material name.
	std::string getMaterialName() const { return m_materialName; }
	// Human-facing "mesh part"/component/subobject name (when available).
	std::string getPartName() const { return m_partName; }
	// Part name Env_Sky, decided once. Reflections never distance-cull it.
	bool isEnvSky() const { return m_envSky; }
	// Meshes created from one TY1 subobject share a group. -1 means ungrouped.
	void setSubobjectGroup(int group) { m_subobjectGroup = group; }
	int getSubobjectGroup() const { return m_subobjectGroup; }
	void setEnabled(bool enabled) { m_enabled = enabled; }
	bool isEnabled() const { return m_enabled; }
	// Remember the visibility chosen when the model or level was opened.
	void captureDefaultEnabled() { m_defaultEnabled = m_enabled; }
	bool defaultEnabled() const { return m_defaultEnabled; }
	void setBlend(MeshBlend blend) { m_blend = blend; }
	MeshBlend getBlend() const { return m_blend; }
	// Fragments below this alpha are discarded. Masked cards use the material aref.
	void setAlphaRef(float alphaRef) { m_alphaRef = alphaRef; }
	float getAlphaRef() const { return m_alphaRef; }
	// A vertex alpha below 1. The main pass is unchanged. Reflections alpha-test these
	// so a transparent sky card does not fill the reflection before the sky is drawn.
	void setFadedVertexAlpha(bool faded) { m_fadedVertexAlpha = faded; }
	// TY1 materials look up their UV animation from this. Null stays untransformed.
	void setContent(Content* content) { m_content = content; m_materialReady = false; }
	// water_types.ini section for this mesh. Empty meshes are not displaced.
	void setWaterType(const std::string& typeName);
	const std::string& getWaterType() const { return m_waterType; }
	// True for a Room_*_water chunk. Those are skipped while drawing a reflection.
	bool isWaterSurface() const { return !m_waterType.empty(); }
	// Header surfaceY. Reflection planes match this.
	void setWaterSurfaceY(float y);
	bool hasWaterSurface() const { return m_hasWaterSurface; }
	float getWaterSurfaceY() const { return m_waterSurfaceY; }
	// Grow the cached bounds. Water verts move by the wave amplitude after the box is built.
	void expandLocalAabb(float padY);
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
	void computeLocalAabb();
	// Shared uniform/texture/blend state for both draw paths. Leaves modelMatrix,
	// useInstancing, the VAO bind, and the draw call itself to the caller.
	void prepareDraw(Shader& shader, const MeshDrawStyle& style) const;
	// First use copies cutout, wrap, and whether the UV matrix moves. Later frames
	// read those fields. Animated materials still look up the clocked matrix.
	void ensureMaterial() const;
	const void* materialDraw() const;
	void bindCachedTexture(Texture* textureOverride = nullptr) const;
	// Identity when the material does not scroll. Animated materials build the clocked matrix.
	glm::mat3 cachedUvMatrix() const;
	// Water surfaces draw with the water program. Reflection draws keep the caller's shader.
	Shader& programFor(Shader& passed, const MeshDrawStyle& style) const;

	static Shader* s_waterSurfaceShader;
	static Shader* s_simpleWaterShader;
	static bool s_simpleWater;

	unsigned int vao, vbo, ebo;

	std::vector<Vertex> m_vertices;
	std::vector<unsigned int> m_indices;

	Texture* m_texture;
	std::string m_materialName;
	// Lowercased once at construction. draw()/drawInstanced() look up global.mad
	// state by this every frame; the lowercase + hash lookup used to happen three
	// times per draw call (uv matrix, wrap, water), each re-lowercasing the name.
	std::string m_materialNameLower;
	std::string m_partName;
	bool m_envSky = false;
	std::string m_waterType;
	bool m_hasWaterSurface = false;
	float m_waterSurfaceY = 0.0f;
	int m_subobjectGroup = -1;
	bool m_hasLocalAabb = false;
	glm::vec3 m_localAabbMin{ 0.0f, 0.0f, 0.0f };
	glm::vec3 m_localAabbMax{ 0.0f, 0.0f, 0.0f };
	bool m_enabled;
	bool m_defaultEnabled = true;
	MeshBlend m_blend = MeshBlend::Opaque;
	float m_alphaRef = 0.01f;
	bool m_fadedVertexAlpha = false;
	Content* m_content = nullptr;

	mutable bool m_materialReady = false;
	// Content::Ty1MaterialDraw*, stored without the type so mesh.h does not include content.h.
	mutable const void* m_materialDraw = nullptr;
	mutable bool m_materialCutout = false;
	mutable bool m_uvAnimated = false;
	mutable int m_wrapS = 0x2901;
	mutable int m_wrapT = 0x2901;
};
