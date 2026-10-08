#include "ty1_water_mesh.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "content.h"
#include "debug.h"
#include "graphics/mesh.h"
#include "graphics/texture.h"
#include "graphics/vertex.h"
#include "model.h"
#include "util/bitconverter.h"

namespace
{
	std::string levelIdFromWaterFile(const std::string& levelFile)
	{
		std::string name = levelFile;
		const size_t slash = name.find_last_of("/\\");
		if (slash != std::string::npos)
			name = name.substr(slash + 1);
		const size_t dot = name.find_last_of('.');
		if (dot != std::string::npos)
			name = name.substr(0, dot);
		for (char& ch : name)
		{
			const unsigned char u = static_cast<unsigned char>(ch);
			if (u >= 'A' && u <= 'Z')
				ch = static_cast<char>(u - 'A' + 'a');
		}
		if (name.size() > 2 && name.compare(name.size() - 2, 2, "ex") == 0)
			name = name.substr(0, name.size() - 2);
		return name;
	}

	bool readPaddedString(const char* data, size_t size, size_t& cursor, std::string& out)
	{
		if (cursor + 4 > size)
			return false;
		const std::uint32_t length = from_bytes<std::uint32_t>(data, cursor);
		cursor += 4;
		if (length > 256 || cursor + length > size)
			return false;
		out.assign(data + cursor, data + cursor + length);
		const size_t end = out.find('\0');
		if (end != std::string::npos)
			out.resize(end);
		cursor = (cursor + length + 3u) & ~size_t{ 3 };
		return cursor <= size;
	}

	struct WaterChunk
	{
		std::string material;
		std::string type;
		float surfaceY = 0.0f;
		std::vector<Vertex> vertices;
		std::vector<unsigned int> indices;
	};

	// Groups of chunks, back to back. Each group starts with a chunk count.
	// A chunk is material, type, index count, vertex count, 7 floats, then
	// 24-byte vertices and a 32-bit triangle list. Four trailing bytes are padding.
	bool parseWaterFile(const char* data, size_t size, std::vector<WaterChunk>& chunks)
	{
		size_t cursor = 0;
		while (cursor + 4 <= size)
		{
			const std::uint32_t chunkCount = from_bytes<std::uint32_t>(data, cursor);
			if (chunkCount == 0 || chunkCount > 16)
				break;
			cursor += 4;

			for (std::uint32_t chunkIndex = 0; chunkIndex < chunkCount; chunkIndex++)
			{
				WaterChunk chunk;
				if (!readPaddedString(data, size, cursor, chunk.material))
					return !chunks.empty();
				if (!readPaddedString(data, size, cursor, chunk.type))
					return !chunks.empty();
				if (cursor + 8 + 28 > size)
					return !chunks.empty();

				const std::uint32_t indexCount = from_bytes<std::uint32_t>(data, cursor);
				const std::uint32_t vertexCount = from_bytes<std::uint32_t>(data, cursor + 4);
				chunk.surfaceY = from_bytes<float>(data, cursor + 8);
				cursor += 8 + 28;
				if (vertexCount > 2000000u || indexCount > 8000000u)
					return !chunks.empty();

				const size_t vertexBytes = static_cast<size_t>(vertexCount) * 24u;
				const size_t indexBytes = static_cast<size_t>(indexCount) * 4u;
				if (cursor + vertexBytes + indexBytes > size)
					return !chunks.empty();

				chunk.vertices.reserve(vertexCount);
				for (std::uint32_t i = 0; i < vertexCount; i++)
				{
					const size_t at = cursor + static_cast<size_t>(i) * 24u;
					const float x = from_bytes<float>(data, at);
					const float y = from_bytes<float>(data, at + 4);
					const float z = from_bytes<float>(data, at + 8);
					const float u = from_bytes<float>(data, at + 12);
					// Same V flip as the MDL loader. SOIL inverts the DDS.
					const float v = 1.0f - from_bytes<float>(data, at + 16);
					const float r = static_cast<unsigned char>(data[at + 20]) / 255.0f;
					const float g = static_cast<unsigned char>(data[at + 21]) / 255.0f;
					const float b = static_cast<unsigned char>(data[at + 22]) / 255.0f;
					const float a = static_cast<unsigned char>(data[at + 23]) / 255.0f;
					chunk.vertices.emplace_back(
						glm::vec4(x, y, z, 1.0f),
						glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
						glm::vec4(r, g, b, a),
						glm::vec2(u, v),
						glm::vec3(0.0f));
				}
				cursor += vertexBytes;

				bool indicesFit = true;
				chunk.indices.reserve(indexCount);
				for (std::uint32_t i = 0; i < indexCount; i++)
				{
					const std::uint32_t index = from_bytes<std::uint32_t>(data, cursor + static_cast<size_t>(i) * 4u);
					if (index >= vertexCount)
					{
						indicesFit = false;
						break;
					}
					chunk.indices.push_back(index);
				}
				cursor += indexBytes;
				if (!indicesFit || chunk.vertices.empty() || chunk.indices.size() < 3)
					continue;
				chunk.indices.resize(chunk.indices.size() - (chunk.indices.size() % 3));
				chunks.push_back(std::move(chunk));
			}
		}
		return !chunks.empty();
	}
}

Model* loadTy1WaterModel(Content& content, const std::string& levelFile)
{
	content.loadTy1WaterTypes();

	const std::string id = levelIdFromWaterFile(levelFile);
	if (id.empty())
		return nullptr;

	// The PC loader opens the .wml (0x5bd8d1). .wmh is the same mesh, tessellated further.
	std::string fileName = "room_" + id + "_water.wml";
	if (!content.hasActiveFile(fileName))
	{
		fileName = "room_" + id + "_water.wmh";
		if (!content.hasActiveFile(fileName))
			return nullptr;
	}

	std::vector<char> bytes;
	if (!content.getActiveFileData(fileName, bytes) || bytes.empty())
		return nullptr;

	std::vector<WaterChunk> chunks;
	if (!parseWaterFile(bytes.data(), bytes.size(), chunks))
	{
		Debug::log("TY1 water mesh failed to parse: " + fileName);
		return nullptr;
	}

	std::vector<Mesh*> meshes;
	meshes.reserve(chunks.size());
	size_t vertices = 0;
	for (WaterChunk& chunk : chunks)
	{
		const Content::Ty1MaterialDraw draw = content.lookupTy1Material(chunk.material);
		Texture* texture = content.defaultTexture;
		if (!draw.textureAlias.empty())
			texture = content.load<Texture>(draw.textureAlias + ".dds");
		if (texture == content.defaultTexture)
			texture = content.load<Texture>(chunk.material + ".dds");

		Mesh* mesh = new Mesh(chunk.vertices, chunk.indices, texture, chunk.material, chunk.type);
		mesh->setContent(&content);
		// Vertex alpha is the shore fade. The reflection mix is what keeps the edge from cutting a hole.
		mesh->setBlend(MeshBlend::Alpha);
		mesh->setWaterType(chunk.type);
		mesh->setWaterSurfaceY(chunk.surfaceY);
		vertices += chunk.vertices.size();

		if (const Content::Ty1WaterType* type = content.ty1WaterTypeFor(chunk.type))
			mesh->expandLocalAabb(std::fabs(type->wave0Height) + std::fabs(type->wave1Height));
		meshes.push_back(mesh);
	}

	if (meshes.empty())
		return nullptr;

	Debug::log("TY1 water " + fileName + ": " + std::to_string(meshes.size()) + " chunks, "
		+ std::to_string(vertices) + " verts");
	return new Model(meshes);
}
