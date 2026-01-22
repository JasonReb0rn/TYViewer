#include "mdg.h"

#include "util/bitconverter.h"
#include "util/stringext.h"
#include "debug.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <unordered_map>
#include <unordered_set>

// ============================================================================
// TY 2 MDG Loading (using MDL3 metadata)
// ============================================================================

bool mdg::loadWithMDL3Metadata(const char* buffer, size_t size, const mdl2::MDL3Metadata& mdl3Metadata, const char* mdlBuffer, size_t mdlOffset)
{
	Debug::log("MDG: Loading with MDL3 metadata using ObjectLookupTable approach");
	meshes.clear();

	// Parse MDG using ObjectLookupTable (based on reference converter)
	return parseMDGWithObjectLookupTable(buffer, size, mdl3Metadata, mdlBuffer, mdlOffset);
}

bool mdg::parseMDGWithObjectLookupTable(const char* buffer, size_t size, const mdl2::MDL3Metadata& mdl3Metadata, const char* mdlBuffer, size_t mdlOffset)
{
	Debug::log("MDG: Parsing using ObjectLookupTable");
	meshes.clear();

	// First check if this is a PS2 or PC MDG file by searching for the PS2 pattern
	bool isPS2Format = false;
	const char ps2Pattern[] = { '\x00', '\x80', '\x02', '\x6C' };
	for (size_t searchPos = 0; searchPos < std::min(size - 3, (size_t)1000); searchPos++)
	{
		if (buffer[searchPos] == 0x00 && buffer[searchPos + 1] == 0x80 && 
			buffer[searchPos + 2] == 0x02 && buffer[searchPos + 3] == 0x6C)
		{
			isPS2Format = true;
			Debug::log("MDG: Detected PS2 format (found VIF packet marker)");
			break;
		}
	}

	if (!isPS2Format)
	{
		Debug::log("MDG: No PS2 markers found, assuming PC format");
		// Try PC parser first
		bool pcSuccess = parseMDGPC(buffer, size, mdl3Metadata, mdlBuffer, mdlOffset);
		if (!pcSuccess || meshes.empty()) {
			Debug::log("MDG: PC parser failed, trying fallback pattern-based parser");
			return load(buffer, size);  // Fallback to generic parser
		}
		return pcSuccess;
	}

	Debug::log("MDG: Parsing PS2 format");

	// Generate anim node lists (needed for bone index remapping)
	std::vector<std::vector<uint8_t>> animNodeLists;
	if (mdl3Metadata.AnimNodeListsOffset > 0)
	{
		uint16_t animNodeListCount = from_bytes<uint16_t>(mdlBuffer, mdlOffset + 0x10);
		for (uint16_t i = 0; i < animNodeListCount; i++)
		{
			std::vector<uint8_t> list;
			size_t listOffset = mdlOffset + mdl3Metadata.AnimNodeListsOffset + (i * 0x80);
			if (listOffset + 1 < mdlOffset + 1000000) // Sanity check
			{
				uint8_t count = from_bytes<uint8_t>(mdlBuffer, listOffset);
				for (uint8_t x = 0; x < count && x < 0x80; x++)
				{
					list.push_back(from_bytes<uint8_t>(mdlBuffer, listOffset + 1 + x));
				}
			}
			animNodeLists.push_back(list);
		}
	}

	// Iterate through texture/component pairs
	for (uint16_t ti = 0; ti < mdl3Metadata.TextureCount; ti++)
	{
		for (uint16_t ci = 0; ci < mdl3Metadata.ComponentCount; ci++)
		{
			// Read mesh reference from ObjectLookupTable
			size_t lookupOffset = mdlOffset + mdl3Metadata.ObjectLookupTable + (ti * 4 * mdl3Metadata.ComponentCount) + (ci * 4);
			if (lookupOffset + 4 > mdlOffset + 1000000) continue; // Sanity check

			int32_t meshRef = from_bytes<int32_t>(mdlBuffer, lookupOffset);
			if (meshRef == 0) continue;

			// Follow linked list of mesh references
			while (meshRef != 0)
			{
				if (meshRef < 0 || (size_t)meshRef >= size)
				{
					Debug::log("MDG: Invalid mesh reference: " + std::to_string(meshRef));
					break;
				}

				// Read strip count from MDG
				uint16_t stripCount = from_bytes<uint16_t>(buffer, meshRef + 0x6);

				// Read animNodeListIndex (2 bytes at offset + 0x8)
				uint16_t animNodeListIndex = from_bytes<uint16_t>(buffer, meshRef + 0x8);
				
				size_t currentOffset = meshRef + 0xC;

				// Parse each strip
				for (uint16_t si = 0; si < stripCount; si++)
				{
					// Find 00 80 02 6C pattern
					size_t patternPos = SIZE_MAX;
					size_t searchLimit = std::min(currentOffset + 10000, size - 3);
					for (size_t searchPos = currentOffset; searchPos < searchLimit; searchPos++)
					{
						if (buffer[searchPos] == 0x00 && buffer[searchPos + 1] == 0x80 && 
							buffer[searchPos + 2] == 0x02 && buffer[searchPos + 3] == 0x6C)
						{
							patternPos = searchPos;
							break;
						}
					}
					
					if (patternPos == SIZE_MAX)
					{
						Debug::log("MDG: Could not find PS2 strip pattern for strip " + std::to_string(si));
						break;
					}

					currentOffset = patternPos + 4;

					// Read vertex count (1 byte)
					if (currentOffset + 1 > size) break;
					uint8_t vertexCount = from_bytes<uint8_t>(buffer, currentOffset);
					currentOffset += 1;

					// Skip 3 bytes padding
					currentOffset += 3;

					// Skip 32 bytes (unknown weight/data section)
					if (currentOffset + 32 > size) break;
					currentOffset += 32;

					// Skip 0x27 (39) bytes before reading positions
					if (currentOffset + 0x27 > size) break;
					currentOffset += 0x27;

					// Parse strip vertices
					std::vector<mdl2::Vertex> vertices;
					if (!parseStrip(buffer, size, currentOffset, vertexCount, vertices, animNodeListIndex, animNodeLists))
					{
						Debug::log("MDG: Failed to parse PS2 strip " + std::to_string(si));
						break;
					}

					// Create mesh data
					MeshData meshData;
					meshData.vertices = vertices;
					meshData.textureIndex = ti;
					meshData.componentIndex = ci;
					meshes.push_back(meshData);
				}

				// Follow next mesh reference
				if (meshRef + 0xC + 4 <= size)
				{
					meshRef = from_bytes<int32_t>(buffer, meshRef + 0xC);
				}
				else
				{
					meshRef = 0;
				}
			}
		}
	}

	Debug::log("MDG: Parsed " + std::to_string(meshes.size()) + " meshes (PS2 format)");
	return !meshes.empty();
}

bool mdg::parseStrip(const char* buffer, size_t size, size_t& offset, uint8_t vertexCount, std::vector<mdl2::Vertex>& vertices, uint16_t animNodeListIndex, const std::vector<std::vector<uint8_t>>& animNodeLists)
{
	vertices.resize(vertexCount);

	// Read positions directly (already skipped 0x27 bytes before calling this function)
	// Reference: sr.Read(strip.floatData, 0, 0xC * strip.VertexCount);
	if (offset + (vertexCount * 12) > size)
	{
		Debug::log("MDG: Not enough space for positions");
		return false;
	}

	for (uint8_t i = 0; i < vertexCount; i++)
	{
		vertices[i].position[0] = from_bytes<float>(buffer, offset + (i * 12));
		vertices[i].position[1] = from_bytes<float>(buffer, offset + (i * 12) + 4);
		vertices[i].position[2] = from_bytes<float>(buffer, offset + (i * 12) + 8);
	}
	offset += vertexCount * 12;

	// Skip 2 bytes (matches reference: sr.Seek(0x2, SeekOrigin.Current))
	if (offset + 2 > size)
	{
		Debug::log("MDG: Not enough space after positions");
		return false;
	}
	offset += 2;
	
	// Read format marker (2 bytes) to determine format
	// Reference: sr.Read(buffer, 0, 2); then checks buffer[1]
	if (offset + 2 > size)
	{
		Debug::log("MDG: Not enough space for format marker");
		return false;
	}
	
	uint8_t formatMarker1 = from_bytes<uint8_t>(buffer, offset);
	uint8_t formatMarker2 = from_bytes<uint8_t>(buffer, offset + 1);
	offset += 2;
	
	// Determine format based on marker (reference checks buffer[1])
	bool format6A = (formatMarker2 == 0x6A);
	bool format65 = (formatMarker2 == 0x65);
	
	if (format6A)
	{
		// Format 6A: Read charData (normals, 3 bytes per vertex)
		// Reference: for(int i = 0; i < strip.VertexCount; i++) sr.Read(strip.charData, i * 0x4, 0x3);
		if (offset + (vertexCount * 3) > size)
		{
			Debug::log("MDG: Not enough space for charData (format 6A)");
			return false;
		}

		for (uint8_t i = 0; i < vertexCount; i++)
		{
			vertices[i].normal[0] = byte_to_single(buffer, offset + (i * 4));
			vertices[i].normal[1] = byte_to_single(buffer, offset + (i * 4) + 1);
			vertices[i].normal[2] = byte_to_single(buffer, offset + (i * 4) + 2);
		}
		offset += vertexCount * 4; // Stride is 4 bytes per vertex
		
		// Skip 4 bytes
		offset += 4;
		
		// Skip vertexCount % 4 bytes
		offset += vertexCount % 4;
		
		// Read shortData (4 bytes = 2 shorts per vertex) - contains UVs
		// Reference: for (int i = 0; i < strip.VertexCount; i++) sr.Read(strip.shortData, i * 8, 0x4);
		if (offset + (vertexCount * 4) > size)
		{
			Debug::log("MDG: Not enough space for shortData (format 6A)");
			return false;
		}
		
		for (uint8_t i = 0; i < vertexCount; i++)
		{
			// Read 2 shorts (UVs)
			int16_t u = from_bytes<int16_t>(buffer, offset + (i * 8));
			int16_t v = from_bytes<int16_t>(buffer, offset + (i * 8) + 2);
			vertices[i].texcoord[0] = u / 4096.0f;
			vertices[i].texcoord[1] = std::abs((v / 4096.0f) - 1.0f);
			
			// Initialize skin data to defaults
			vertices[i].skin[0] = 0.0f;
			vertices[i].skin[1] = 0.0f;
			vertices[i].skin[2] = 0.0f;
		}
		offset += vertexCount * 8; // Stride is 8 bytes per vertex
	}
	else if (format65)
	{
		// Format 65: Read shortData directly (4 bytes = 2 shorts per vertex) - UVs only
		// Reference: for (int i = 0; i < strip.VertexCount; i++) sr.Read(strip.shortData, i * 0x8, 0x4);
		if (offset + (vertexCount * 4) > size)
		{
			Debug::log("MDG: Not enough space for shortData (format 65)");
			return false;
		}
		
		for (uint8_t i = 0; i < vertexCount; i++)
		{
			// Read 2 shorts (UVs)
			int16_t u = from_bytes<int16_t>(buffer, offset + (i * 8));
			int16_t v = from_bytes<int16_t>(buffer, offset + (i * 8) + 2);
			vertices[i].texcoord[0] = u / 4096.0f;
			vertices[i].texcoord[1] = std::abs((v / 4096.0f) - 1.0f);
			
			// Initialize normals and skin to defaults
			vertices[i].normal[0] = 0.0f;
			vertices[i].normal[1] = 0.0f;
			vertices[i].normal[2] = 1.0f;
			vertices[i].skin[0] = 0.0f;
			vertices[i].skin[1] = 0.0f;
			vertices[i].skin[2] = 0.0f;
		}
		offset += vertexCount * 8; // Stride is 8 bytes per vertex
	}
	else
	{
		// Default format: Read charData (3 bytes + 1 byte bone index per vertex)
		// Reference: for (int i = 0; i < strip.VertexCount; i++) { sr.Read(strip.charData, i * 0x4, 0x3); ... }
		if (offset + (vertexCount * 4) > size)
		{
			Debug::log("MDG: Not enough space for charData");
			return false;
		}
		
		for (uint8_t i = 0; i < vertexCount; i++)
		{
			// Read 3 bytes (normals or other data)
			vertices[i].normal[0] = byte_to_single(buffer, offset + (i * 4));
			vertices[i].normal[1] = byte_to_single(buffer, offset + (i * 4) + 1);
			vertices[i].normal[2] = byte_to_single(buffer, offset + (i * 4) + 2);
			
			// Read bone index (4th byte)
			uint8_t boneIndex = from_bytes<uint8_t>(buffer, offset + (i * 4) + 3);
			boneIndex = boneIndex >> 1; // Extract bone index
			
			// Remap bone index if animNodeListIndex is valid
			if (animNodeListIndex != 0xFFFF && animNodeListIndex < animNodeLists.size())
			{
				auto& animNodeList = animNodeLists[animNodeListIndex];
				if (boneIndex < animNodeList.size())
				{
					boneIndex = (animNodeList[boneIndex] + 1) << 1;
				}
				else
				{
					boneIndex = boneIndex << 1;
				}
			}
			else
			{
				boneIndex = boneIndex << 1;
			}
			
			vertices[i].skin[1] = (float)boneIndex;
		}
		offset += vertexCount * 4;
		
		// Skip 4 bytes
		offset += 4;
		
		// Read shortData (6 bytes + 2 bytes bone index per vertex)
		// Reference: for(int i = 0; i < strip.VertexCount; i++) { sr.Read(strip.shortData, i * 8, 0x6); ... }
		if (offset + (vertexCount * 8) > size)
		{
			Debug::log("MDG: Not enough space for shortData");
			return false;
		}
		
		for (uint8_t i = 0; i < vertexCount; i++)
		{
			// Read 6 bytes (3 shorts) - first 2 are UVs
			int16_t u = from_bytes<int16_t>(buffer, offset + (i * 8));
			int16_t v = from_bytes<int16_t>(buffer, offset + (i * 8) + 2);
			vertices[i].texcoord[0] = u / 4096.0f;
			vertices[i].texcoord[1] = std::abs((v / 4096.0f) - 1.0f);
			
			// Read bone index (last 2 bytes)
			uint16_t boneIndexShort = from_bytes<uint16_t>(buffer, offset + (i * 8) + 6);
			uint16_t boneIndex = boneIndexShort >> 2; // Extract bone index
			
			// Remap bone index if animNodeListIndex is valid
			if (animNodeListIndex != 0xFFFF && animNodeListIndex < animNodeLists.size())
			{
				auto& animNodeList = animNodeLists[animNodeListIndex];
				if (boneIndex < animNodeList.size())
				{
					boneIndex = (animNodeList[boneIndex] + 1) << 2;
				}
				else
				{
					boneIndex = boneIndex << 2;
				}
			}
			else
			{
				boneIndex = boneIndex << 2;
			}
			
			vertices[i].skin[2] = (float)boneIndex;
		}
		offset += vertexCount * 8;
	}

	// Skip 4 bytes before reading colors
	// Reference: sr.Seek(0x4, SeekOrigin.Current);
	if (offset + 4 > size)
	{
		Debug::log("MDG: Not enough space for 4-byte skip before colors");
		return false;
	}
	offset += 4;

	// Read colors (byteData: 4 bytes per vertex, RGBA)
	// Reference: sr.Read(strip.byteData, 0, 0x4 * strip.VertexCount);
	if (offset + (vertexCount * 4) > size)
	{
		Debug::log("MDG: Not enough space for colors");
		return false;
	}

	for (uint8_t i = 0; i < vertexCount; i++)
	{
		vertices[i].colour[0] = byte_to_single(buffer, offset + (i * 4));
		vertices[i].colour[1] = byte_to_single(buffer, offset + (i * 4) + 1);
		vertices[i].colour[2] = byte_to_single(buffer, offset + (i * 4) + 2);
		vertices[i].colour[3] = byte_to_single(buffer, offset + (i * 4) + 3);
	}
	offset += vertexCount * 4;

	return true;
}

// ============================================================================
// PC MDG Loading (inline strip data, no VIF packets)
// ============================================================================

// PC MDG Strip Descriptor structure
// Format is stored as little-endian: low byte = vertex count, high byte = format flags
struct PCStripDescriptor {
	uint16_t format;      
	uint8_t vertexCount() const { return format & 0xFF; }  // Low byte
	uint8_t formatFlags() const { return (format >> 8) & 0xFF; }  // High byte
};

bool mdg::parseMDGPC(const char* buffer, size_t size, const mdl2::MDL3Metadata& mdl3Metadata, const char* mdlBuffer, size_t mdlOffset)
{
	Debug::log("MDG: Parsing PC MDG format");
	meshes.clear();
	
	// Find where vertex data actually starts (after all mesh headers)
	// PC MDG format uses 48-byte stride per vertex with specific layout:
	//   +0-3:   Unknown/flag (often 0xFFFFFFFF)
	//   +4-11:  UV (2 floats)
	//   +12-23: Position (3 floats)
	//   +24-27: Weight (1 float)
	//   +28-35: Unknown (2 floats, often constant per mesh like 27.0, 27.0)
	//   +36-47: Normal (3 floats)
	
	size_t globalVertexDataStart = 0;
	const size_t VERTEX_STRIDE = 48;
	
	// Determine where mesh headers end so we don't mis-detect in header data
	size_t maxHeaderEnd = 0;
	std::unordered_set<size_t> visitedMeshes;
	for (uint16_t ti = 0; ti < mdl3Metadata.TextureCount; ti++)
	{
		for (uint16_t ci = 0; ci < mdl3Metadata.ComponentCount; ci++)
		{
			size_t lookupOffset = mdlOffset + mdl3Metadata.ObjectLookupTable + (ti * 4 * mdl3Metadata.ComponentCount) + (ci * 4);
			if (lookupOffset + 4 > mdlOffset + 1000000) continue;

			int32_t meshRef = from_bytes<int32_t>(mdlBuffer, lookupOffset);
			while (meshRef != 0)
			{
				if (meshRef < 0 || (size_t)meshRef >= size) break;
				if (!visitedMeshes.insert((size_t)meshRef).second) break;

				uint16_t stripCount = from_bytes<uint16_t>(buffer, meshRef + 0x6);
				size_t headerEnd = meshRef + 0x10 + (stripCount * 2);
				maxHeaderEnd = std::max(maxHeaderEnd, headerEnd);

				if (meshRef + 0xC + 4 > size) break;
				meshRef = from_bytes<int32_t>(buffer, meshRef + 0xC);
			}
		}
	}

	// Search for vertex data by looking for a sequence of valid 48-byte vertices
	// Use position + normal checks (UVs are not reliable in PC format)
	size_t searchStart = maxHeaderEnd & ~0x3;
	for (size_t searchOffset = searchStart; searchOffset + (VERTEX_STRIDE * 5) <= size; searchOffset += 4)
	{
		// Look for at least 5 consecutive valid vertices with 48-byte stride
		int validCount = 0;
		for (int v = 0; v < 5; v++) {
			size_t vertexOffset = searchOffset + (v * VERTEX_STRIDE);
			if (vertexOffset + VERTEX_STRIDE > size) break;
			
			// Check Position at +12
			float x = from_bytes<float>(buffer, vertexOffset + 12);
			float y = from_bytes<float>(buffer, vertexOffset + 16);
			float z = from_bytes<float>(buffer, vertexOffset + 20);
			bool hasNonZero = (std::abs(x) > 0.0001f || std::abs(y) > 0.0001f || std::abs(z) > 0.0001f);
			bool posValid = !std::isnan(x) && !std::isinf(x) && std::abs(x) < 1000.0f &&
			                !std::isnan(y) && !std::isinf(y) && std::abs(y) < 1000.0f &&
			                !std::isnan(z) && !std::isinf(z) && std::abs(z) < 1000.0f;

			// Check Normal at +36
			float nx = from_bytes<float>(buffer, vertexOffset + 36);
			float ny = from_bytes<float>(buffer, vertexOffset + 40);
			float nz = from_bytes<float>(buffer, vertexOffset + 44);
			float normalLen = std::sqrt((nx * nx) + (ny * ny) + (nz * nz));
			bool normalValid = !std::isnan(nx) && !std::isinf(nx) &&
				!std::isnan(ny) && !std::isinf(ny) &&
				!std::isnan(nz) && !std::isinf(nz) &&
				normalLen > 0.2f && normalLen < 1.8f;
			
			// Position and normal must be valid
			if (posValid && normalValid && hasNonZero) validCount++;
		}
		
		if (validCount >= 4) {
			globalVertexDataStart = searchOffset;
			Debug::log("MDG PC: Found global vertex data block starting at offset " + std::to_string(globalVertexDataStart));
			break;
		}
	}
	
	if (globalVertexDataStart == 0) {
		Debug::log("MDG PC: Could not find global vertex data block");
		return false;
	}
	
	// Track current position in vertex data as we parse meshes sequentially
	// IMPORTANT: Many TY2 PC MDL3 files reuse the same mesh header (`meshRef`) across multiple
	// texture/material entries in the ObjectLookupTable. The vertex data, however, is stored once
	// per unique mesh header in a single contiguous block (see Reverse_Engineering_Documentation.md).
	//
	// If we advance a global vertex cursor for every *reference* (ti,ci)->meshRef (old behavior),
	// we desynchronize after the first reuse and later "materials" read garbage vertex data.
	//
	// Fix: First build a unique meshRef order (first-seen traversal), assign each meshRef a stable
	// vertexDataOffset in the global vertex block, parse each meshRef exactly once, then emit
	// render meshes per (ti,ci) reference using the cached parsed vertex data.

	struct ParsedPCMesh
	{
		std::vector<mdl2::Vertex> vertices;
		std::vector<uint16_t> stripVertexCounts;
		bool validForRender = false;
		size_t totalVertices = 0;
		uint16_t stripCount = 0;
	};

	std::vector<int32_t> meshOrder;
	meshOrder.reserve(256);
	std::unordered_set<int32_t> seenMeshRefs;
	seenMeshRefs.reserve(512);

	// Helper to safely read next mesh pointer
	auto readNextMesh = [&](int32_t meshRef) -> int32_t
	{
		if (meshRef <= 0) return 0;
		if ((size_t)meshRef + 0xC + 4 > size) return 0;
		return from_bytes<int32_t>(buffer, meshRef + 0xC);
	};

	// Pass 1: Discover unique meshRefs in first-seen traversal order
	for (uint16_t ti = 0; ti < mdl3Metadata.TextureCount; ti++)
	{
		for (uint16_t ci = 0; ci < mdl3Metadata.ComponentCount; ci++)
		{
			size_t lookupOffset = mdlOffset + mdl3Metadata.ObjectLookupTable + (ti * 4 * mdl3Metadata.ComponentCount) + (ci * 4);
			if (lookupOffset + 4 > mdlOffset + 1000000) continue;

			int32_t meshRef = from_bytes<int32_t>(mdlBuffer, lookupOffset);
			while (meshRef != 0)
			{
				if (meshRef < 0 || (size_t)meshRef >= size) break;

				if (seenMeshRefs.insert(meshRef).second)
				{
					meshOrder.push_back(meshRef);
				}

				meshRef = readNextMesh(meshRef);
			}
		}
	}

	if (meshOrder.empty())
	{
		Debug::log("MDG PC: ObjectLookupTable traversal found 0 mesh references");
		return false;
	}

	Debug::log("MDG PC: Unique mesh headers discovered: " + std::to_string(meshOrder.size()));

	// Pass 2: Parse each unique mesh once, consuming vertex data sequentially
	std::unordered_map<int32_t, ParsedPCMesh> parsedMeshes;
	parsedMeshes.reserve(meshOrder.size() * 2);

	size_t cursorVertexDataOffset = globalVertexDataStart;
	for (int32_t meshRef : meshOrder)
	{
		if (meshRef < 0 || (size_t)meshRef >= size)
			continue;

		// Read mesh header
		uint16_t baseVertexCount = from_bytes<uint16_t>(buffer, meshRef + 0x0);
		uint16_t duplicateVertexCount = from_bytes<uint16_t>(buffer, meshRef + 0x4);
		uint16_t stripCount = from_bytes<uint16_t>(buffer, meshRef + 0x6);

		if (stripCount > 1000)
		{
			Debug::log("MDG PC: Invalid strip count: " + std::to_string(stripCount) + " at meshRef " + std::to_string(meshRef));
			// Still advance cursor by 0 (unknown size) — best we can do is bail out
			return false;
		}

		const size_t totalVertices = static_cast<size_t>(baseVertexCount) + static_cast<size_t>(duplicateVertexCount);
		const size_t expectedDataSize = totalVertices * VERTEX_STRIDE;

		ParsedPCMesh parsed;
		parsed.totalVertices = totalVertices;
		parsed.stripCount = stripCount;
		parsed.validForRender = false;

		// Always advance cursor for *unique* mesh headers (even if we later skip rendering),
		// because vertex data storage includes all meshes in this traversal order.
		if (expectedDataSize == 0)
		{
			cursorVertexDataOffset += 0;
			parsedMeshes.emplace(meshRef, std::move(parsed));
			continue;
		}

		if (cursorVertexDataOffset + expectedDataSize > size)
		{
			Debug::log("MDG PC: Not enough data for meshRef " + std::to_string(meshRef) +
				" at vertexDataOffset " + std::to_string(cursorVertexDataOffset) +
				" (need " + std::to_string(expectedDataSize) + " bytes, have " + std::to_string(size - cursorVertexDataOffset) + ")");
			return false;
		}

		// Read strip descriptor low-byte counts (optional; informational only unless they align)
		std::vector<uint16_t> stripVertexCounts;
		if (stripCount > 0 && (size_t)meshRef + 0x10 + (stripCount * 2) <= size)
		{
			stripVertexCounts.reserve(stripCount);
			for (uint16_t si = 0; si < stripCount; si++)
			{
				uint16_t descriptor = from_bytes<uint16_t>(buffer, meshRef + 0x10 + (si * 2));
				stripVertexCounts.push_back(static_cast<uint16_t>(descriptor & 0xFF));
			}
		}

		parsed.vertices.resize(totalVertices);
		std::vector<std::array<float, 2>> rawUvs(totalVertices);

		for (size_t i = 0; i < totalVertices; i++)
		{
			size_t vertexOffset = cursorVertexDataOffset + (i * VERTEX_STRIDE);

			// UV at +4/+8 (float2); V flip
			rawUvs[i][0] = from_bytes<float>(buffer, vertexOffset + 4);
			rawUvs[i][1] = 1.0f - from_bytes<float>(buffer, vertexOffset + 8);

			// Position at +12
			parsed.vertices[i].position[0] = from_bytes<float>(buffer, vertexOffset + 12);
			parsed.vertices[i].position[1] = from_bytes<float>(buffer, vertexOffset + 16);
			parsed.vertices[i].position[2] = from_bytes<float>(buffer, vertexOffset + 20);

			// Weight at +24 (store in skin[0] for now)
			parsed.vertices[i].skin[0] = from_bytes<float>(buffer, vertexOffset + 24);
			parsed.vertices[i].skin[1] = 0.0f;
			parsed.vertices[i].skin[2] = 0.0f;

			// Normal at +36
			parsed.vertices[i].normal[0] = from_bytes<float>(buffer, vertexOffset + 36);
			parsed.vertices[i].normal[1] = from_bytes<float>(buffer, vertexOffset + 40);
			parsed.vertices[i].normal[2] = from_bytes<float>(buffer, vertexOffset + 44);

			// Default color (white) - colors may be stored elsewhere or not present
			parsed.vertices[i].colour[0] = 1.0f;
			parsed.vertices[i].colour[1] = 1.0f;
			parsed.vertices[i].colour[2] = 1.0f;
			parsed.vertices[i].colour[3] = 1.0f;
		}

		// UV shift heuristic (+1) based on adjacent duplicate position pairs (see docs)
		size_t adjacentPairs = 0;
		size_t matchesShift0 = 0;
		size_t matchesShift1 = 0;
		for (size_t i = 0; i + 1 < totalVertices; i++)
		{
			bool samePos = std::abs(parsed.vertices[i].position[0] - parsed.vertices[i + 1].position[0]) < 0.00001f &&
				std::abs(parsed.vertices[i].position[1] - parsed.vertices[i + 1].position[1]) < 0.00001f &&
				std::abs(parsed.vertices[i].position[2] - parsed.vertices[i + 1].position[2]) < 0.00001f;
			if (!samePos) continue;

			adjacentPairs++;
			bool sameUv0 = std::abs(rawUvs[i][0] - rawUvs[i + 1][0]) < 0.00001f &&
				std::abs(rawUvs[i][1] - rawUvs[i + 1][1]) < 0.00001f;
			if (sameUv0) matchesShift0++;

			if (i + 2 < totalVertices)
			{
				bool sameUv1 = std::abs(rawUvs[i + 1][0] - rawUvs[i + 2][0]) < 0.00001f &&
					std::abs(rawUvs[i + 1][1] - rawUvs[i + 2][1]) < 0.00001f;
				if (sameUv1) matchesShift1++;
			}
		}

		bool useShiftedUvs = (adjacentPairs > 0 && matchesShift1 > matchesShift0);
		if (useShiftedUvs)
		{
			Debug::log("MDG PC: Using +1 UV shift based on duplicate matches (meshRef " + std::to_string(meshRef) + ")");
		}

		for (size_t i = 0; i < totalVertices; i++)
		{
			size_t uvIndex = i;
			if (useShiftedUvs && i + 1 < totalVertices)
				uvIndex = i + 1;
			parsed.vertices[i].texcoord[0] = rawUvs[uvIndex][0];
			parsed.vertices[i].texcoord[1] = rawUvs[uvIndex][1];
		}

		// Validate non-zero positions (reject obvious bad offsets)
		int nonZeroCount = 0;
		for (size_t i = 0; i < totalVertices; i++)
		{
			if (std::abs(parsed.vertices[i].position[0]) > 0.0001f ||
				std::abs(parsed.vertices[i].position[1]) > 0.0001f ||
				std::abs(parsed.vertices[i].position[2]) > 0.0001f)
			{
				nonZeroCount++;
			}
		}

		if (totalVertices >= 3 && nonZeroCount >= 3)
		{
			// Skip box-like debug meshes (likely bounds visualization)
			std::set<float> uniqueX;
			std::set<float> uniqueY;
			std::set<float> uniqueZ;
			std::vector<std::array<int, 3>> quantizedPositions;
			quantizedPositions.reserve(totalVertices);

			for (const auto& vtx : parsed.vertices)
			{
				uniqueX.insert(vtx.position[0]);
				uniqueY.insert(vtx.position[1]);
				uniqueZ.insert(vtx.position[2]);

				int qx = static_cast<int>(std::round(vtx.position[0] * 1000.0f));
				int qy = static_cast<int>(std::round(vtx.position[1] * 1000.0f));
				int qz = static_cast<int>(std::round(vtx.position[2] * 1000.0f));
				quantizedPositions.push_back({ qx, qy, qz });
			}

			std::sort(quantizedPositions.begin(), quantizedPositions.end());
			quantizedPositions.erase(std::unique(quantizedPositions.begin(), quantizedPositions.end()), quantizedPositions.end());
			bool boxLike = (quantizedPositions.size() <= 8 &&
				uniqueX.size() <= 2 && uniqueY.size() <= 2 && uniqueZ.size() <= 2);

			parsed.validForRender = !boxLike;
		}

		// Store strip counts if they align with totalVertices (same logic as before)
		if (!stripVertexCounts.empty())
		{
			size_t stripSum = 0;
			for (auto count : stripVertexCounts) stripSum += count;
			const size_t sc = stripVertexCounts.size();
			const size_t stripDegenerate2Sum = stripSum + (sc > 0 ? (sc - 1) * 2 : 0);
			const size_t stripDegenerate1Sum = stripSum + (sc > 0 ? (sc - 1) : 0);
			const bool countsIncludeDegenerates = (stripSum == totalVertices);
			const bool countsExcludeDegenerates2 = (stripDegenerate2Sum == totalVertices);
			const bool countsExcludeDegenerates1 = (!countsExcludeDegenerates2 && stripDegenerate1Sum == totalVertices);
			if (countsIncludeDegenerates || countsExcludeDegenerates2 || countsExcludeDegenerates1)
			{
				parsed.stripVertexCounts = std::move(stripVertexCounts);
			}
		}

		parsedMeshes.emplace(meshRef, std::move(parsed));
		cursorVertexDataOffset += expectedDataSize;
	}

	// PC MDG Structure:
	// 1. MDG3 header (4 bytes)
	// 2. Mesh headers referenced by ObjectLookupTable
	// 3. Each mesh header contains:
	//    - Strip count at offset +0x6 (2 bytes)
	//    - Next mesh pointer at offset +0xC (4 bytes)
	//    - Strip descriptors (2 bytes each) starting at offset +0x10
	// 4. Vertex data follows after all strip descriptors

	// Iterate through texture/component pairs using ObjectLookupTable
	for (uint16_t ti = 0; ti < mdl3Metadata.TextureCount; ti++)
	{
		for (uint16_t ci = 0; ci < mdl3Metadata.ComponentCount; ci++)
		{
			// Read mesh reference from ObjectLookupTable
			size_t lookupOffset = mdlOffset + mdl3Metadata.ObjectLookupTable + (ti * 4 * mdl3Metadata.ComponentCount) + (ci * 4);
			if (lookupOffset + 4 > mdlOffset + 1000000) continue;

			int32_t meshRef = from_bytes<int32_t>(mdlBuffer, lookupOffset);
			if (meshRef == 0) continue;

			// Follow linked list of mesh references
			while (meshRef != 0)
			{
				if (meshRef < 0 || (size_t)meshRef >= size)
				{
					Debug::log("MDG PC: Invalid mesh reference: " + std::to_string(meshRef));
					break;
				}

				const int32_t nextMesh = readNextMesh(meshRef);

				bool isCollisionTexture = false;
				if (ti < mdl3Metadata.TextureNames.size())
				{
					const std::string& textureName = mdl3Metadata.TextureNames[ti];
					if (textureName.rfind("CM_", 0) == 0 || textureName.rfind("cm_", 0) == 0)
					{
						isCollisionTexture = true;
					}
				}

				auto parsedIt = parsedMeshes.find(meshRef);
				if (parsedIt == parsedMeshes.end())
				{
					Debug::log("MDG PC: Missing parsed mesh for meshRef " + std::to_string(meshRef));
					meshRef = nextMesh;
					continue;
				}

				const ParsedPCMesh& parsed = parsedIt->second;
				if (!parsed.validForRender)
				{
					// Still advance through references, but don't emit renderable mesh data
					meshRef = nextMesh;
					continue;
				}

				if (isCollisionTexture)
				{
					// Collision meshes are kept out of render list for now, but their vertex data
					// has already been consumed in the unique-mesh parsing pass.
					meshRef = nextMesh;
					continue;
				}

				MeshData meshData;
				meshData.vertices = parsed.vertices;
				meshData.stripVertexCounts = parsed.stripVertexCounts;
				meshData.textureIndex = ti;
				meshData.componentIndex = ci;
				meshes.push_back(std::move(meshData));

				// Move to next mesh in linked list
				meshRef = nextMesh;
			}
		}
	}

	Debug::log("MDG PC: Parsed " + std::to_string(meshes.size()) + " meshes");
	return !meshes.empty();
}

bool mdg::parseStripPC(const char* buffer, size_t size, size_t& offset, uint8_t vertexCount, std::vector<mdl2::Vertex>& vertices, uint16_t format)
{
	// PC MDG format uses interleaved vertex data per strip
	// Format appears to be similar to PS2 but with floats instead of fixed-point in some cases
	// Each strip contains vertex attributes in sequence:
	// 1. Positions (12 bytes per vertex - 3 floats)
	// 2. UVs (8 bytes per vertex - 2 floats OR 2 shorts + padding)
	// 3. Normals (12 bytes per vertex - 3 floats OR packed format)
	// 4. Colors (4 bytes per vertex - RGBA)
	
	vertices.clear();
	vertices.resize(vertexCount);

	size_t startOffset = offset;
	
	// Read positions (always 12 bytes per vertex)
	if (offset + (vertexCount * 12) > size) {
		Debug::log("MDG PC: Not enough data for positions");
		return false;
	}
	
	for (uint8_t i = 0; i < vertexCount; i++) {
		vertices[i].position[0] = from_bytes<float>(buffer, offset + (i * 12));
		vertices[i].position[1] = from_bytes<float>(buffer, offset + (i * 12) + 4);
		vertices[i].position[2] = from_bytes<float>(buffer, offset + (i * 12) + 8);
	}
	offset += vertexCount * 12;
	
	// Read UVs (8 bytes per vertex - 2 floats)
	if (offset + (vertexCount * 8) > size) {
		Debug::log("MDG PC: Not enough data for UVs");
		return false;
	}
	
	for (uint8_t i = 0; i < vertexCount; i++) {
		// PC format uses floats for UVs
		vertices[i].texcoord[0] = from_bytes<float>(buffer, offset + (i * 8));
		vertices[i].texcoord[1] = from_bytes<float>(buffer, offset + (i * 8) + 4);
	}
	offset += vertexCount * 8;
	
	// Read normals (12 bytes per vertex - 3 floats)
	if (offset + (vertexCount * 12) > size) {
		Debug::log("MDG PC: Not enough data for normals");
		return false;
	}
	
	for (uint8_t i = 0; i < vertexCount; i++) {
		vertices[i].normal[0] = from_bytes<float>(buffer, offset + (i * 12));
		vertices[i].normal[1] = from_bytes<float>(buffer, offset + (i * 12) + 4);
		vertices[i].normal[2] = from_bytes<float>(buffer, offset + (i * 12) + 8);
	}
	offset += vertexCount * 12;
	
	// Read colors (4 bytes per vertex - RGBA)
	if (offset + (vertexCount * 4) > size) {
		Debug::log("MDG PC: Not enough data for colors");
		return false;
	}
	
	for (uint8_t i = 0; i < vertexCount; i++) {
		vertices[i].colour[0] = byte_to_single(buffer, offset + (i * 4));
		vertices[i].colour[1] = byte_to_single(buffer, offset + (i * 4) + 1);
		vertices[i].colour[2] = byte_to_single(buffer, offset + (i * 4) + 2);
		vertices[i].colour[3] = byte_to_single(buffer, offset + (i * 4) + 3);
	}
	offset += vertexCount * 4;
	
	// Initialize skin data to defaults (PC format may not have skinning data inline)
	for (uint8_t i = 0; i < vertexCount; i++) {
		vertices[i].skin[0] = 0.0f;
		vertices[i].skin[1] = 0.0f;
		vertices[i].skin[2] = 0.0f;
	}
	
	Debug::log("MDG PC: Parsed strip with " + std::to_string(vertexCount) + 
		" vertices (total bytes: " + std::to_string(offset - startOffset) + ")");
	
	return true;
}

// ============================================================================
// Fallback MDG Loading (for non-TY2 files)
// ============================================================================

bool mdg::load(const char* buffer, size_t size)
{
	if (buffer == nullptr || size == 0)
	{
		Debug::log("MDG: Invalid buffer or size");
		return false;
	}

	Debug::log("MDG: Attempting fallback pattern-based parsing (non-TY2 format)");
	meshes.clear();

	// Pattern to find: \x00\x80\x02\x6C
	const char pattern[] = { '\x00', '\x80', '\x02', '\x6C' };
	std::vector<size_t> positions = findall(buffer, size, pattern, 4);

	if (positions.empty())
	{
		Debug::log("MDG: No mesh patterns found in file");
		return false;
	}

	Debug::log("MDG: Found " + std::to_string(positions.size()) + " pattern(s)");

	// Basic parsing for fallback format (simplified)
	for (size_t i = 0; i < positions.size(); i++)
	{
		size_t offset = positions[i] + 4; // Skip pattern

		if (offset + 4 > size)
			continue;

		// Read vertex count (4 bytes for fallback format)
		uint32_t vnum = from_bytes<uint32_t>(buffer, offset);
		offset += 4;

		if (vnum == 0 || vnum > 100000)
			continue;

		// Skip 32 bytes
		if (offset + 32 > size)
			continue;
		offset += 32;

		// Skip UV tag
		if (offset + 4 > size)
			continue;
		offset += 4;

		// Read positions
		if (offset + (vnum * 12) > size)
			continue;

		std::vector<mdl2::Vertex> vertices(vnum);
		for (uint32_t j = 0; j < vnum; j++)
		{
			vertices[j].position[0] = from_bytes<float>(buffer, offset + (j * 12));
			vertices[j].position[1] = from_bytes<float>(buffer, offset + (j * 12) + 4);
			vertices[j].position[2] = from_bytes<float>(buffer, offset + (j * 12) + 8);
		}
		offset += vnum * 12;

		// Find normals marker
		const char normalPattern[] = { '\x03', '\x80' };
		size_t normalPos = findNext(buffer, size, offset, normalPattern, 2);
		if (normalPos == SIZE_MAX)
			continue;

		offset = normalPos + 4;

		// Read normals
		if (offset + (vnum * 4) > size)
			continue;

		for (uint32_t j = 0; j < vnum; j++)
		{
			vertices[j].normal[0] = byte_to_single(buffer, offset + (j * 4));
			vertices[j].normal[1] = byte_to_single(buffer, offset + (j * 4) + 1);
			vertices[j].normal[2] = byte_to_single(buffer, offset + (j * 4) + 2);
		}
		offset += vnum * 4;

		// Skip UV tag
		if (offset + 4 > size)
			continue;
		offset += 4;

		// Read UVs
		if (offset + (vnum * 8) > size)
			continue;

		for (uint32_t j = 0; j < vnum; j++)
		{
			int16_t u = from_bytes<int16_t>(buffer, offset + (j * 8));
			int16_t v = from_bytes<int16_t>(buffer, offset + (j * 8) + 2);
			vertices[j].texcoord[0] = u / 4096.0f;
			vertices[j].texcoord[1] = std::abs((v / 4096.0f) - 1.0f);
		}
		offset += vnum * 8;

		// Read colors
		if (offset + 4 > size)
			continue;
		offset += 4;

		if (offset + (vnum * 4) > size)
			continue;

		for (uint32_t j = 0; j < vnum; j++)
		{
			vertices[j].colour[0] = byte_to_single(buffer, offset + (j * 4));
			vertices[j].colour[1] = byte_to_single(buffer, offset + (j * 4) + 1);
			vertices[j].colour[2] = byte_to_single(buffer, offset + (j * 4) + 2);
			vertices[j].colour[3] = byte_to_single(buffer, offset + (j * 4) + 3);
		}

		// Initialize skin data
		for (uint32_t j = 0; j < vnum; j++)
		{
			vertices[j].skin[0] = 0.0f;
			vertices[j].skin[1] = 0.0f;
			vertices[j].skin[2] = 0.0f;
		}

		MeshData meshData;
		meshData.vertices = vertices;
		meshes.push_back(meshData);
	}

	Debug::log("MDG: Fallback parsing complete, found " + std::to_string(meshes.size()) + " mesh(es)");
	return !meshes.empty();
}

// ============================================================================
// Utility Functions
// ============================================================================

std::vector<size_t> mdg::findall(const char* buffer, size_t size, const char* pattern, size_t patternSize)
{
	std::vector<size_t> positions;
	size_t pos = 0;

	while (pos < size)
	{
		if (pos + patternSize > size)
			break;

		bool match = true;
		for (size_t i = 0; i < patternSize; i++)
		{
			if (buffer[pos + i] != pattern[i])
			{
				match = false;
				break;
			}
		}

		if (match)
			positions.push_back(pos);

		pos++;
	}

	return positions;
}

size_t mdg::findNext(const char* buffer, size_t size, size_t startPos, const char* pattern, size_t patternSize)
{
	for (size_t pos = startPos; pos < size; pos++)
	{
		if (pos + patternSize > size)
			break;

		bool match = true;
		for (size_t i = 0; i < patternSize; i++)
		{
			if (buffer[pos + i] != pattern[i])
			{
				match = false;
				break;
			}
		}

		if (match)
			return pos;
	}

	return SIZE_MAX;
}
