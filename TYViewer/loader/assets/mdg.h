#pragma once

#include <string>
#include <vector>

#include "mdl2.h"

class mdg
{
public:
	struct MeshData
	{
		std::vector<mdl2::Vertex> vertices;
		uint32_t textureIndex; // Texture index from MDL3
		uint32_t componentIndex; // Component index from MDL3
	};

public:
	bool load(const char* buffer, size_t size);
	bool loadWithMDL3Metadata(const char* buffer, size_t size, const mdl2::MDL3Metadata& mdl3Metadata, const char* mdlBuffer, size_t mdlOffset);

public:
	std::vector<MeshData> meshes;

private:
	bool parseMDGWithObjectLookupTable(const char* buffer, size_t size, const mdl2::MDL3Metadata& mdl3Metadata, const char* mdlBuffer, size_t mdlOffset);
	bool parseMDGPC(const char* buffer, size_t size, const mdl2::MDL3Metadata& mdl3Metadata, const char* mdlBuffer, size_t mdlOffset);
	bool parseStripPC(const char* buffer, size_t size, size_t& offset, uint8_t vertexCount, std::vector<mdl2::Vertex>& vertices, uint16_t format);
	bool parseStrip(const char* buffer, size_t size, size_t& offset, uint8_t vertexCount, std::vector<mdl2::Vertex>& vertices, uint16_t animNodeListIndex = 0xFFFF, const std::vector<std::vector<uint8_t>>& animNodeLists = {});
	std::vector<size_t> findall(const char* buffer, size_t size, const char* pattern, size_t patternSize);
	size_t findNext(const char* buffer, size_t size, size_t startPos, const char* pattern, size_t patternSize);
};
