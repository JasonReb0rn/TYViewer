#include "grass_cards.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include <glad/glad.h>

#include "content.h"
#include "model.h"
#include "render_stats.h"
#include "shader.h"
#include "texture.h"

namespace
{
	// StdMath RandomFR.
	float randomFR(std::uint32_t& seed, float minValue, float maxValue)
	{
		seed = seed * 0x343FDu + 0x269EC3u;
		const std::uint32_t bits = ((seed >> 8) & 0x7FFFFFu) | 0x3F800000u;
		float unit = 0.0f;
		std::memcpy(&unit, &bits, sizeof(unit));
		return (unit - 1.0f) * (maxValue - minValue) + minValue;
	}

	// MKGrass_InitTypes' randomTimes: 32 barycentric weights from seed 0. Every
	// triangle walks the same table, so blade placement repeats per triangle.
	struct Barycentric
	{
		float w[32][3];

		Barycentric()
		{
			std::uint32_t seed = 0;
			for (int i = 0; i < 32; i++)
			{
				float a = randomFR(seed, 0.0f, 1.0f);
				float b = randomFR(seed, 0.0f, 1.0f);
				float c = randomFR(seed, 0.0f, 1.0f);
				const float scale = 1.0f / (a + b + c);
				w[i][0] = a * scale;
				w[i][1] = b * scale;
				w[i][2] = c * scale;
			}
		}
	};

	std::int32_t floatBits(float value)
	{
		std::int32_t bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		return bits;
	}
}

GrassCards::~GrassCards()
{
	clear();
	if (cornerVbo != 0)
		glDeleteBuffers(1, &cornerVbo);
	delete shader;
}

void GrassCards::clear()
{
	for (Batch& batch : batches)
	{
		glDeleteVertexArrays(1, &batch.vao);
		glDeleteBuffers(1, &batch.vbo);
	}
	batches.clear();
	totalCards = 0;
}

void GrassCards::build(const std::vector<Model*>& models, Content& content)
{
	clear();

	const std::vector<Content::Ty1GrassType>& types = content.ty1GrassTypes();
	if (types.empty())
		return;

	static const Barycentric table;
	std::vector<Card> cards;
	size_t emitters = 0;

	for (const Model* model : models)
	{
		if (model == nullptr)
			continue;
		for (const Mesh* mesh : model->getMeshes())
		{
			if (mesh == nullptr)
				continue;
			const Content::Ty1MaterialDraw draw = content.lookupTy1Material(mesh->getMaterialName());
			if (!draw.grassEffect || draw.grassIndex < 0 || draw.grassIndex >= static_cast<int>(types.size()))
				continue;
			const Content::Ty1GrassType& type = types[static_cast<size_t>(draw.grassIndex)];
			const int blades = type.bladesPerTriangle;
			if (blades <= 0 || type.numTextures <= 0)
				continue;

			const std::vector<Vertex>& vertices = mesh->getVertices();
			const std::vector<unsigned int>& indices = mesh->getIndices();
			if (indices.size() < 3)
				continue;

			const float heightStep = (type.maxHeight - type.minHeight) / 7.0f;
			const float lastCell = static_cast<float>(type.numTextures - 1) * 0.125f;

			cards.clear();
			cards.reserve((indices.size() / 3) * static_cast<size_t>(blades));
			for (size_t t = 0; t + 2 < indices.size(); t += 3)
			{
				// The TY1 loader stores strip triangle k as (k, k+2, k+1).
				// Grass_DrawGC walks it as (k, k+1, k+2).
				const Vertex& v0 = vertices[indices[t]];
				const Vertex& v1 = vertices[indices[t + 2]];
				const Vertex& v2 = vertices[indices[t + 1]];
				const glm::vec3 p0(v0.position);
				const glm::vec3 p1(v1.position);
				const glm::vec3 p2(v2.position);

				const std::int32_t hash = floatBits(p0.x) ^ floatBits(p0.z);
				float u = static_cast<float>((type.numTextures - 1) & hash) * 0.125f;

				for (int j = 0; j < blades; j++)
				{
					const float* w = table.w[j];
					Card card;
					card.base = p0 * w[0] + p1 * w[1] + p2 * w[2];
					card.height = type.upVector * (type.minHeight + static_cast<float>(~j & 7) * heightStep);
					card.halfWidth = type.width;
					card.u0 = u;
					card.colour = v0.colour * w[0] + v1.colour * w[1] + v2.colour * w[2];
					cards.push_back(card);

					u += 0.125f;
					if (u >= lastCell)
						u = 0.0f;
				}
			}
			if (cards.empty())
				continue;

			Batch batch;
			batch.texture = content.load<Texture>(type.material + ".dds");
			const Content::Ty1MaterialDraw cardMaterial = content.lookupTy1Material(type.material);
			batch.alphaRef = cardMaterial.masked ? cardMaterial.alphaRef : 0.01f;
			batch.blend = cardMaterial.blend;
			batch.clampU = cardMaterial.clampU;
			batch.clampV = cardMaterial.clampV;

			glm::vec3 boxMin(std::numeric_limits<float>::max());
			glm::vec3 boxMax(-std::numeric_limits<float>::max());
			for (const Card& card : cards)
			{
				const glm::vec3 low(card.base.x - card.halfWidth, card.base.y + std::min(card.height, 0.0f), card.base.z - card.halfWidth);
				const glm::vec3 high(card.base.x + card.halfWidth, card.base.y + std::max(card.height, 0.0f), card.base.z + card.halfWidth);
				boxMin = glm::min(boxMin, low);
				boxMax = glm::max(boxMax, high);
			}
			batch.boxMin = boxMin;
			batch.boxMax = boxMax;

			upload(batch, cards);
			batches.push_back(batch);
			totalCards += cards.size();
			emitters++;
		}
	}

	Debug::log("TY1 grass: " + std::to_string(totalCards) + " cards from " + std::to_string(emitters) + " meshes");
}

void GrassCards::ensureShader()
{
	if (shader != nullptr)
		return;

	const std::string vertexSource = R"(
		#version 330 core
		// x is the side (-1 left, +1 right). y is 0 at the ground and 1 at the tip.
		layout(location = 0) in vec2 corner;
		layout(location = 1) in vec3 base;
		// height, half width, first U of the atlas cell.
		layout(location = 2) in vec3 shape;
		layout(location = 3) in vec4 colour;

		uniform mat4 VPMatrix;
		uniform vec3 cameraRight;

		out vec4 v_colour;
		out vec2 v_texcoord;

		void main()
		{
			vec3 world = base;
			world.xz += cameraRight.xz * (shape.y * corner.x);
			world.y += shape.x * corner.y;
			gl_Position = VPMatrix * vec4(world, 1.0);
			v_colour = colour;
			// Game V is 0 at the tip. TY1 textures are sampled as (u, 1 - gameV).
			v_texcoord = vec2(shape.z + (corner.x > 0.0 ? 0.125 : 0.0), corner.y);
		}
	)";

	const std::string fragmentSource = R"(
		#version 330 core
		in vec4 v_colour;
		in vec2 v_texcoord;

		uniform sampler2D diffuseTexture;
		uniform float alphaRef;

		out vec4 color;

		void main()
		{
			vec4 shaded = texture(diffuseTexture, v_texcoord) * v_colour;
			if (shaded.a < alphaRef)
				discard;
			color = shaded;
		}
	)";

	shader = new Shader(vertexSource, fragmentSource);

	const float corners[8] =
	{
		-1.0f, 0.0f,
		 1.0f, 0.0f,
		-1.0f, 1.0f,
		 1.0f, 1.0f,
	};
	glGenBuffers(1, &cornerVbo);
	glBindBuffer(GL_ARRAY_BUFFER, cornerVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
}

void GrassCards::upload(Batch& batch, const std::vector<Card>& cards)
{
	ensureShader();

	glGenVertexArrays(1, &batch.vao);
	glGenBuffers(1, &batch.vbo);
	glBindVertexArray(batch.vao);

	glBindBuffer(GL_ARRAY_BUFFER, cornerVbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
	glVertexAttribDivisor(0, 0);

	glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cards.size() * sizeof(Card)), cards.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Card), (const void*)offsetof(Card, base));
	glVertexAttribDivisor(1, 1);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Card), (const void*)offsetof(Card, height));
	glVertexAttribDivisor(2, 1);
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Card), (const void*)offsetof(Card, colour));
	glVertexAttribDivisor(3, 1);

	glBindVertexArray(0);
	batch.count = static_cast<int>(cards.size());
}

void GrassCards::draw(const glm::mat4& vpMatrix, const glm::vec3& cameraRight,
	const std::function<bool(const glm::vec3&, const glm::vec3&)>& visible)
{
	if (batches.empty() || shader == nullptr)
		return;

	const GLboolean cull = glIsEnabled(GL_CULL_FACE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);

	shader->bind();
	shader->setUniformMat4("VPMatrix", vpMatrix);
	shader->setUniform3f("cameraRight", cameraRight);
	shader->setUniform1i("diffuseTexture", 0);

	for (const Batch& batch : batches)
	{
		if (batch.texture == nullptr || !visible(batch.boxMin, batch.boxMax))
			continue;

		glBlendEquation(GL_FUNC_ADD);
		if (batch.blend == MeshBlend::Additive)
		{
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			glDepthMask(GL_FALSE);
		}
		else
		{
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDepthMask(batch.blend == MeshBlend::Opaque ? GL_TRUE : GL_FALSE);
		}

		shader->setUniform1f("alphaRef", batch.alphaRef);
		batch.texture->bind();
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, batch.clampU ? GL_CLAMP_TO_EDGE : GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, batch.clampV ? GL_CLAMP_TO_EDGE : GL_REPEAT);

		glBindVertexArray(batch.vao);
		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, batch.count);

		RenderStats::drawCalls++;
		RenderStats::instancedBatches++;
		RenderStats::trianglesDrawn += static_cast<long long>(batch.count) * 2;
	}

	glBindVertexArray(0);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_TRUE);
	if (cull)
		glEnable(GL_CULL_FACE);
}
