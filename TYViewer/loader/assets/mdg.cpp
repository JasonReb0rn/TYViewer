#include "mdg.h"

#include "util/bitconverter.h"
#include "util/stringext.h"
#include "debug.h"

#include <algorithm>
#include <cmath>

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
	//   +0-7:   UV (2 floats)
	//   +8-19:  Position (3 floats)
	//   +20-23: Weight (1 float)
	//   +24-31: BBox max (2 floats, constant)
	//   +32-43: Normal (3 floats)
	//   +44-47: Padding (often NaN/0xFFFFFFFF)
	
	size_t globalVertexDataStart = 0;
	const size_t VERTEX_STRIDE = 48;
	
	// Search for vertex data by looking for a sequence of valid 48-byte vertices
	// We validate both positions AND UVs to avoid false positives in mesh headers
	for (size_t searchOffset = 100; searchOffset < std::min(size - 1000, (size_t)5000); searchOffset += 4)
	{
		// Look for at least 5 consecutive valid vertices with 48-byte stride
		int validCount = 0;
		for (int v = 0; v < 5; v++) {
			size_t vertexOffset = searchOffset + (v * VERTEX_STRIDE);
			if (vertexOffset + VERTEX_STRIDE > size) break;
			
			// Check UV at +0
			float u = from_bytes<float>(buffer, vertexOffset);
			float v_coord = from_bytes<float>(buffer, vertexOffset + 4);
			bool uvValid = !std::isnan(u) && !std::isinf(u) && std::abs(u) < 100.0f &&
			               !std::isnan(v_coord) && !std::isinf(v_coord) && std::abs(v_coord) < 100.0f;
			
			// Check Position at +8
			float x = from_bytes<float>(buffer, vertexOffset + 8);
			float y = from_bytes<float>(buffer, vertexOffset + 12);
			float z = from_bytes<float>(buffer, vertexOffset + 16);
			bool hasNonZero = (std::abs(x) > 0.0001f || std::abs(y) > 0.0001f || std::abs(z) > 0.0001f);
			bool posValid = !std::isnan(x) && !std::isinf(x) && std::abs(x) < 1000.0f &&
			                !std::isnan(y) && !std::isinf(y) && std::abs(y) < 1000.0f &&
			                !std::isnan(z) && !std::isinf(z) && std::abs(z) < 1000.0f;
			
			// Both UV and position must be valid
			if (uvValid && posValid && hasNonZero) validCount++;
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
	size_t currentVertexDataOffset = globalVertexDataStart;

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

				// Read mesh header
				uint16_t stripCount = from_bytes<uint16_t>(buffer, meshRef + 0x6);
				int32_t nextMesh = from_bytes<int32_t>(buffer, meshRef + 0xC);

				if (stripCount == 0 || stripCount > 1000)
				{
					Debug::log("MDG PC: Invalid strip count: " + std::to_string(stripCount));
					break;
				}

				Debug::log("MDG PC: Parsing mesh at offset " + std::to_string(meshRef) + 
					" with " + std::to_string(stripCount) + " strips (texture=" + std::to_string(ti) + 
					", component=" + std::to_string(ci) + ")");

				// Read strip descriptors
				std::vector<PCStripDescriptor> stripDescriptors(stripCount);
				size_t descriptorOffset = meshRef + 0x10;
				
				for (uint16_t si = 0; si < stripCount; si++)
				{
					if (descriptorOffset + 2 > size)
					{
						Debug::log("MDG PC: Not enough data for strip descriptor " + std::to_string(si));
						break;
					}
					
					stripDescriptors[si].format = from_bytes<uint16_t>(buffer, descriptorOffset);
					descriptorOffset += 2;
					
					Debug::log("MDG PC: Strip " + std::to_string(si) + 
						" - format=0x" + std::to_string(stripDescriptors[si].format) + 
						" vertices=" + std::to_string(stripDescriptors[si].vertexCount()) +
						" flags=0x" + std::to_string(stripDescriptors[si].formatFlags()));
				}

				// Calculate total expected vertices
				size_t totalVertices = 0;
				for (const auto& desc : stripDescriptors) {
					totalVertices += desc.vertexCount();
				}
				
				if (totalVertices == 0) {
					Debug::log("MDG PC: Mesh has 0 total vertices, skipping");
					meshRef = nextMesh;
					continue;
				}
				
				Debug::log("MDG PC: Total vertices expected: " + std::to_string(totalVertices));
				
				// Use current position in sequential vertex data block
				size_t vertexDataOffset = currentVertexDataOffset;
				
				// Calculate expected data size for this mesh (48 bytes per vertex)
				size_t expectedDataSize = totalVertices * VERTEX_STRIDE;
				
				// Verify we have enough data
				if (vertexDataOffset + expectedDataSize > size) {
					Debug::log("MDG PC: Not enough data at offset " + std::to_string(vertexDataOffset) + 
						" (need " + std::to_string(expectedDataSize) + " bytes, have " + std::to_string(size - vertexDataOffset) + ")");
					meshRef = nextMesh;
					continue;
				}
				
				// Log first vertex for debugging (position is at +8, not +0!)
				float u = from_bytes<float>(buffer, vertexDataOffset);
				float v = from_bytes<float>(buffer, vertexDataOffset + 4);
				float x = from_bytes<float>(buffer, vertexDataOffset + 8);
				float y = from_bytes<float>(buffer, vertexDataOffset + 12);
				float z = from_bytes<float>(buffer, vertexDataOffset + 16);
				Debug::log("MDG PC: Reading vertex data at offset " + std::to_string(vertexDataOffset) + 
					" (" + std::to_string(expectedDataSize) + " bytes needed)");
				Debug::log("MDG PC: First vertex UV: (" + std::to_string(u) + ", " + std::to_string(v) + ")");
				Debug::log("MDG PC: First vertex Pos: (" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")");

				// PC Format: Interleaved vertex data with 48-byte stride
				// Layout per vertex:
				//   +0-7:   UV (2 floats)
				//   +8-19:  Position (3 floats)
				//   +20-23: Weight (1 float)
				//   +24-31: BBox (2 floats)
				//   +32-43: Normal (3 floats)
				//   +44-47: Padding
				std::vector<mdl2::Vertex> allVertices(totalVertices);
				size_t currentOffset = vertexDataOffset;
				
				// Verify we have enough data
				size_t requiredSize = totalVertices * VERTEX_STRIDE;
				
				if (currentOffset + requiredSize > size) {
					Debug::log("MDG PC: Not enough data for vertices (need " + 
						std::to_string(requiredSize) + " bytes, have " + std::to_string(size - currentOffset) + ")");
					meshRef = nextMesh;
					continue;
				}
				
				// Read all vertices with interleaved layout
				for (size_t i = 0; i < totalVertices; i++) {
					size_t vertexOffset = currentOffset + (i * VERTEX_STRIDE);
					
					// UV at +0
					allVertices[i].texcoord[0] = from_bytes<float>(buffer, vertexOffset);
					allVertices[i].texcoord[1] = from_bytes<float>(buffer, vertexOffset + 4);
					
					// Position at +8
					allVertices[i].position[0] = from_bytes<float>(buffer, vertexOffset + 8);
					allVertices[i].position[1] = from_bytes<float>(buffer, vertexOffset + 12);
					allVertices[i].position[2] = from_bytes<float>(buffer, vertexOffset + 16);
					
					// Weight at +20 (store in skin[0] for now)
					allVertices[i].skin[0] = from_bytes<float>(buffer, vertexOffset + 20);
					allVertices[i].skin[1] = 0.0f;
					allVertices[i].skin[2] = 0.0f;
					
					// Normal at +32
					allVertices[i].normal[0] = from_bytes<float>(buffer, vertexOffset + 32);
					allVertices[i].normal[1] = from_bytes<float>(buffer, vertexOffset + 36);
					allVertices[i].normal[2] = from_bytes<float>(buffer, vertexOffset + 40);
					
					// Default color (white) - colors may be stored elsewhere or not present
					allVertices[i].colour[0] = 1.0f;
					allVertices[i].colour[1] = 1.0f;
					allVertices[i].colour[2] = 1.0f;
					allVertices[i].colour[3] = 1.0f;
				}
				currentOffset += totalVertices * VERTEX_STRIDE;
				
				Debug::log("MDG PC: Parsed " + std::to_string(totalVertices) + " vertices (ended at offset " + std::to_string(currentOffset) + ")");
				
				// Advance the global vertex data pointer for next mesh
				currentVertexDataOffset = currentOffset;
				
				// Validate that we got some non-zero positions
				int nonZeroCount = 0;
				for (size_t i = 0; i < totalVertices; i++) {
					if (std::abs(allVertices[i].position[0]) > 0.0001f ||
					    std::abs(allVertices[i].position[1]) > 0.0001f ||
					    std::abs(allVertices[i].position[2]) > 0.0001f) {
						nonZeroCount++;
					}
				}
				
				float nonZeroPercent = (float)nonZeroCount / (float)totalVertices * 100.0f;
				Debug::log("MDG PC: Non-zero vertices: " + std::to_string(nonZeroCount) + "/" + std::to_string(totalVertices) + 
					" (" + std::to_string((int)nonZeroPercent) + "%)");
				
				if (nonZeroCount < 3) {
					Debug::log("MDG PC: ERROR - All or most vertices are at origin, skipping mesh (data invalid or wrong offset)");
					meshRef = nextMesh;
					continue;
				}
				
				// Split vertices into strips and create mesh data
				size_t vertexIndex = 0;
				for (uint16_t si = 0; si < stripCount; si++)
				{
					uint8_t stripVertexCount = stripDescriptors[si].vertexCount();
					
					if (stripVertexCount == 0) {
						continue;  // Skip silently
					}
					
					if (stripVertexCount < 3) {
						Debug::log("MDG PC: Strip " + std::to_string(si) + " has only " + std::to_string(stripVertexCount) + " vertices (need 3 minimum for triangle strip), skipping");
						vertexIndex += stripVertexCount;
						continue;
					}
					
					if (vertexIndex + stripVertexCount > totalVertices) {
						Debug::log("MDG PC: Strip " + std::to_string(si) + " exceeds total vertex count");
						break;
					}
					
					// Extract vertices for this strip
					std::vector<mdl2::Vertex> stripVertices(stripVertexCount);
					for (uint8_t vi = 0; vi < stripVertexCount; vi++) {
						stripVertices[vi] = allVertices[vertexIndex + vi];
					}
					vertexIndex += stripVertexCount;
					
					// Create mesh data for this strip
					MeshData meshData;
					meshData.vertices = stripVertices;
					meshData.textureIndex = ti;
					meshData.componentIndex = ci;
					meshes.push_back(meshData);
					
					Debug::log("MDG PC: Created strip " + std::to_string(si) + " with " + std::to_string(stripVertexCount) + " vertices");
				}

				// Move to next mesh in linked list
				meshRef = nextMesh;
			}
		}
	}

	Debug::log("MDG PC: Parsed " + std::to_string(meshes.size()) + " mesh strips");
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
