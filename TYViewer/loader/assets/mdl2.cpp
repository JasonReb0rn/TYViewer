#include "mdl2.h"

#include "util/bitconverter.h"
#include "util/stringext.h"
#include "debug.h"

#include <iostream>
#include <stdexcept>

bool mdl2::load(const char* buffer, size_t offset)
{
	isMDL3Format = false; // TY 1 format
	
	if (from_bytes<uint32_t>(buffer, 0) != 843859021)
	{
		// Signature check
		return false;
	}

	unsigned int frag_count = from_bytes<uint16_t>(buffer, 4);
	unsigned int subobject_count = from_bytes<uint16_t>(buffer, 6);
	unsigned int collider_count = from_bytes<uint16_t>(buffer, 8);
	unsigned int bone_count = from_bytes<uint16_t>(buffer, 10);

	size_t subobject_offset = from_bytes<uint32_t>(buffer, 12);
	size_t collider_offset = from_bytes<uint32_t>(buffer, 16);
	size_t bone_offset = from_bytes<uint32_t>(buffer, 20);

	bounds =
	{
		from_bytes<float>(buffer, offset + 32),	from_bytes<float>(buffer, offset + 36),	from_bytes<float>(buffer, offset + 40),
		from_bytes<float>(buffer, offset + 48), from_bytes<float>(buffer, offset + 52), from_bytes<float>(buffer, offset + 56),
		from_bytes<float>(buffer, offset + 64), from_bytes<float>(buffer, offset + 68), from_bytes<float>(buffer, offset + 72)
	};

	name = nts(buffer, from_bytes<uint32_t>(buffer, 68));

	subobjects = std::vector<Subobject>(subobject_count);
	for (unsigned int i = 0; i < subobject_count; i++)
	{
		subobjects[i] = parse_subobject(buffer, offset + subobject_offset);

		subobject_offset += 80;
	}

	return true;
}

bool mdl2::loadTY2(const char* buffer, size_t offset)
{
	// First try MDL3 format (newer TY 2 structure)
	if (loadTY2MDL3(buffer, offset))
	{
		return true;
	}

	// Fallback: TY 2 format with same structure as TY 1 but different signature
	try
	{
		Debug::log("loadTY2: Reading header values...");
		// Read header values
		unsigned int frag_count = from_bytes<uint16_t>(buffer, 4);
		unsigned int subobject_count = from_bytes<uint16_t>(buffer, 6);
		unsigned int collider_count = from_bytes<uint16_t>(buffer, 8);
		unsigned int bone_count = from_bytes<uint16_t>(buffer, 10);

		Debug::log("loadTY2: Header - frag:" + std::to_string(frag_count) + 
			" subobj:" + std::to_string(subobject_count) + 
			" collider:" + std::to_string(collider_count) + 
			" bone:" + std::to_string(bone_count));

		// Validate reasonable values
		if (subobject_count > 1000 || collider_count > 1000 || bone_count > 1000)
		{
			Debug::log("loadTY2: Failed validation - counts too high");
			return false;
		}

		Debug::log("loadTY2: Reading offsets...");
		size_t subobject_offset = from_bytes<uint32_t>(buffer, 12);
		size_t collider_offset = from_bytes<uint32_t>(buffer, 16);
		size_t bone_offset = from_bytes<uint32_t>(buffer, 20);

		Debug::log("loadTY2: Offsets - subobj:" + std::to_string(subobject_offset) + 
			" collider:" + std::to_string(collider_offset) + 
			" bone:" + std::to_string(bone_offset));

		// For TY 2, if offsets look invalid (too large), the structure might be different
		// Since TY 2 uses .mdg for mesh data, we can skip parsing subobjects if offsets are invalid
		// Check if offset is suspiciously large (more than 10KB suggests invalid data)
		bool skipSubobjectParsing = false;
		if (subobject_offset > 10000 || (subobject_offset == 0 && subobject_count > 0))
		{
			Debug::log("loadTY2: Warning - subobject_offset looks invalid (" + std::to_string(subobject_offset) + "), skipping subobject parsing");
			Debug::log("loadTY2: TY 2 format may have different structure - will use MDG data only");
			skipSubobjectParsing = true;
		}

		Debug::log("loadTY2: Reading bounds...");
		bounds =
		{
			from_bytes<float>(buffer, offset + 32),	from_bytes<float>(buffer, offset + 36),	from_bytes<float>(buffer, offset + 40),
			from_bytes<float>(buffer, offset + 48), from_bytes<float>(buffer, offset + 52), from_bytes<float>(buffer, offset + 56),
			from_bytes<float>(buffer, offset + 64), from_bytes<float>(buffer, offset + 68), from_bytes<float>(buffer, offset + 72)
		};

		Debug::log("loadTY2: Reading name...");
		uint32_t name_offset = from_bytes<uint32_t>(buffer, 68);
		if (name_offset > 0 && name_offset < 1000000) // Reasonable limit
		{
			name = nts(buffer, name_offset);
		}
		else
		{
			name = "";
		}

		if (!skipSubobjectParsing)
		{
			Debug::log("loadTY2: Parsing " + std::to_string(subobject_count) + " subobjects...");
			subobjects = std::vector<Subobject>(subobject_count);
			for (unsigned int i = 0; i < subobject_count; i++)
			{
				Debug::log("loadTY2: Parsing subobject " + std::to_string(i) + " at offset " + std::to_string(offset + subobject_offset));
				try
				{
					subobjects[i] = parse_subobject(buffer, offset + subobject_offset);
					subobject_offset += 80;
				}
				catch (...)
				{
					Debug::log("loadTY2: Failed to parse subobject " + std::to_string(i) + ", creating empty subobject");
					// Create empty subobject
					subobjects[i] = { {0,0,0,0,0,0,0,0,0}, "", "", 0, {} };
					subobject_offset += 80;
				}
			}
		}
		else
		{
			// Create empty subobjects - we'll use MDG data directly
			Debug::log("loadTY2: Creating " + std::to_string(subobject_count) + " empty subobjects (will use MDG data)");
			subobjects = std::vector<Subobject>(subobject_count);
			for (unsigned int i = 0; i < subobject_count; i++)
			{
				subobjects[i] = { {0,0,0,0,0,0,0,0,0}, "", "", 0, {} };
			}
		}

		Debug::log("loadTY2: Successfully loaded TY 2 MDL");
		return true;
	}
	catch (const std::exception& e)
	{
		Debug::log("loadTY2: Exception caught: " + std::string(e.what()));
		return false;
	}
	catch (...)
	{
		Debug::log("loadTY2: Unknown exception caught");
		return false;
	}
}

bool mdl2::loadTY2MDL3(const char* buffer, size_t offset)
{
	try
	{
		Debug::log("loadTY2MDL3: Attempting to parse MDL3 format...");
		
		// Read MDL3 header structure (based on reference converter)
		mdl3Metadata.ComponentCount = from_bytes<uint16_t>(buffer, offset + 0x4);
		mdl3Metadata.TextureCount = from_bytes<uint16_t>(buffer, offset + 0x6);
		mdl3Metadata.AnimNodeCount = from_bytes<uint16_t>(buffer, offset + 0x8);
		mdl3Metadata.RefPointCount = from_bytes<uint16_t>(buffer, offset + 0xA);
		mdl3Metadata.MeshCount = from_bytes<uint16_t>(buffer, offset + 0xE);
		mdl3Metadata.StripCount = from_bytes<uint16_t>(buffer, offset + 0x1E);
		
		// Validate reasonable values
		if (mdl3Metadata.ComponentCount > 1000 || mdl3Metadata.TextureCount > 1000 || 
			mdl3Metadata.AnimNodeCount > 1000 || mdl3Metadata.RefPointCount > 1000)
		{
			Debug::log("loadTY2MDL3: Failed validation - counts too high");
			return false;
		}

		Debug::log("loadTY2MDL3: ComponentCount=" + std::to_string(mdl3Metadata.ComponentCount) +
			" TextureCount=" + std::to_string(mdl3Metadata.TextureCount) +
			" AnimNodeCount=" + std::to_string(mdl3Metadata.AnimNodeCount) +
			" RefPointCount=" + std::to_string(mdl3Metadata.RefPointCount) +
			" MeshCount=" + std::to_string(mdl3Metadata.MeshCount) +
			" StripCount=" + std::to_string(mdl3Metadata.StripCount));

		// Read bounding box
		bounds.x = from_bytes<float>(buffer, offset + 0x30);
		bounds.y = from_bytes<float>(buffer, offset + 0x34);
		bounds.z = from_bytes<float>(buffer, offset + 0x38);
		bounds.sx = from_bytes<float>(buffer, offset + 0x40);
		bounds.sy = from_bytes<float>(buffer, offset + 0x44);
		bounds.sz = from_bytes<float>(buffer, offset + 0x48);

		// Read offsets
		mdl3Metadata.ComponentDescriptionsOffset = from_bytes<uint16_t>(buffer, offset + 0x50);
		mdl3Metadata.TextureListOffset = from_bytes<uint32_t>(buffer, offset + 0x54);
		mdl3Metadata.RefPointsOffsetsOffset = from_bytes<uint32_t>(buffer, offset + 0x58);
		mdl3Metadata.AnimNodeDataOffset = from_bytes<uint16_t>(buffer, offset + 0x5C);
		mdl3Metadata.AnimNodeListsOffset = from_bytes<uint32_t>(buffer, offset + 0x64);
		mdl3Metadata.ObjectLookupTable = from_bytes<uint32_t>(buffer, offset + 0x68);

		Debug::log("loadTY2MDL3: ObjectLookupTable=" + std::to_string(mdl3Metadata.ObjectLookupTable) +
			" TextureListOffset=" + std::to_string(mdl3Metadata.TextureListOffset));

		// Read texture names
		mdl3Metadata.TextureNames.clear();
		for (uint16_t ti = 0; ti < mdl3Metadata.TextureCount; ti++)
		{
			uint32_t textureNameOffset = from_bytes<uint32_t>(buffer, offset + mdl3Metadata.TextureListOffset + (ti * 4));
			std::string textureName = nts(buffer, offset + textureNameOffset);
			mdl3Metadata.TextureNames.push_back(textureName);
			Debug::log("loadTY2MDL3: Texture[" + std::to_string(ti) + "]=" + textureName);
		}

		// Read string table offset from component descriptions
		if (mdl3Metadata.ComponentDescriptionsOffset > 0)
		{
			mdl3Metadata.StringTableOffset = from_bytes<uint16_t>(buffer, offset + mdl3Metadata.ComponentDescriptionsOffset + 0x34);
		}

		// Create empty subobjects - actual mesh data will come from MDG file
		subobjects = std::vector<Subobject>(mdl3Metadata.ComponentCount);
		for (uint16_t i = 0; i < mdl3Metadata.ComponentCount; i++)
		{
			// Read component bounds if available
			size_t componentOffset = offset + mdl3Metadata.ComponentDescriptionsOffset + (i * 0x40);
			if (componentOffset + 0x30 <= offset + 1000000) // Sanity check
			{
				subobjects[i].bounds.x = from_bytes<float>(buffer, componentOffset);
				subobjects[i].bounds.y = from_bytes<float>(buffer, componentOffset + 4);
				subobjects[i].bounds.z = from_bytes<float>(buffer, componentOffset + 8);
				subobjects[i].bounds.sx = from_bytes<float>(buffer, componentOffset + 16);
				subobjects[i].bounds.sy = from_bytes<float>(buffer, componentOffset + 20);
				subobjects[i].bounds.sz = from_bytes<float>(buffer, componentOffset + 24);
				subobjects[i].bounds.ox = from_bytes<float>(buffer, componentOffset + 32);
				subobjects[i].bounds.oy = from_bytes<float>(buffer, componentOffset + 36);
				subobjects[i].bounds.oz = from_bytes<float>(buffer, componentOffset + 40);

				// Read component name
				uint32_t componentNameOffset = from_bytes<uint32_t>(buffer, componentOffset + 0x30);
				if (componentNameOffset > 0)
				{
					subobjects[i].name = nts(buffer, offset + componentNameOffset);
				}
			}
		}

		isMDL3Format = true;
		Debug::log("loadTY2MDL3: Successfully parsed MDL3 format");
		return true;
	}
	catch (const std::exception& e)
	{
		Debug::log("loadTY2MDL3: Exception caught: " + std::string(e.what()));
		return false;
	}
	catch (...)
	{
		Debug::log("loadTY2MDL3: Unknown exception caught");
		return false;
	}
}

mdl2::Subobject mdl2::parse_subobject(const char* buffer, size_t offset)
{
	// Basic bounds check - ensure we can at least read the header (72 bytes minimum)
	// Note: This is a minimal check; full validation would require buffer size
	if (offset > 1000000) // Sanity check - offset way too large
	{
		Debug::log("parse_subobject: Offset too large: " + std::to_string(offset));
		throw std::runtime_error("Invalid subobject offset");
	}

	Bounds bounds =
	{
		from_bytes<float>(buffer, offset),		from_bytes<float>(buffer, offset + 4),	from_bytes<float>(buffer, offset + 8),
		from_bytes<float>(buffer, offset + 16), from_bytes<float>(buffer, offset + 20), from_bytes<float>(buffer, offset + 24),
		from_bytes<float>(buffer, offset + 32), from_bytes<float>(buffer, offset + 36), from_bytes<float>(buffer, offset + 40)
	};

	std::string name = nts(buffer, from_bytes<uint32_t>(buffer, offset + 48));
	std::string material = nts(buffer, from_bytes<uint32_t>(buffer, offset + 52));

	unsigned int triangle_count = from_bytes<uint32_t>(buffer, offset + 56);

	unsigned int mesh_count = from_bytes<uint16_t>(buffer, offset + 66);
	size_t mesh_offset = from_bytes<uint32_t>(buffer, offset + 68);

	std::vector<Mesh> meshes(mesh_count);
	for (unsigned int i = 0; i < mesh_count; i++)
	{
		meshes[i] = parse_mesh(buffer, mesh_offset);

		mesh_offset += 16;
	}

	return { bounds, name, material, triangle_count, meshes };
}

mdl2::Mesh mdl2::parse_mesh(const char* buffer, size_t offset)
{
	std::string material = nts(buffer, from_bytes<uint32_t>(buffer, offset));
	uint32_t segment_offset = from_bytes<uint32_t>(buffer, offset + 4);

	unsigned int segment_count = from_bytes<uint32_t>(buffer, offset + 12);

	std::vector<Segment> segments(segment_count);
	for (unsigned int i = 0; i < segment_count; i++)
	{
		size_t size = 0;
		segments[i] = parse_segment(buffer, segment_offset, size);

		segment_offset += static_cast<uint32_t>(size);
	}

	return { material, segments };
}

mdl2::Segment mdl2::parse_segment(const char* buffer, size_t offset, size_t& size)
{
	unsigned int amount_of_vertices = from_bytes<uint32_t>(buffer, offset + 12);
	std::vector<Vertex> vertices(amount_of_vertices);

	size = 52 + (amount_of_vertices * 12) +
		4 + (amount_of_vertices * 4) +
		4 + (amount_of_vertices * 8) +
		4 + (amount_of_vertices * 4);


	// POSITIONS
	for (unsigned int i = 0; i < amount_of_vertices; i++)
	{
		size_t p = offset + 52 + (i * 12);

		float x = from_bytes<float>(buffer, p);
		float y = from_bytes<float>(buffer, p + 4);
		float z = from_bytes<float>(buffer, p + 8);

		vertices[i].position[0] = x;
		vertices[i].position[1] = y;
		vertices[i].position[2] = z;
	}

	// NORMALS
	for (unsigned int i = 0; i < amount_of_vertices; i++)
	{
		size_t p = offset + 52 + (amount_of_vertices * 12) + 4 + (i * 4);

		float x = byte_to_single(buffer, p);
		float y = byte_to_single(buffer, p + 1);
		float z = byte_to_single(buffer, p + 2);

		vertices[i].normal[0] = x;
		vertices[i].normal[1] = y;
		vertices[i].normal[2] = z;
	}

	// TEXCOORDS
	for (unsigned int i = 0; i < amount_of_vertices; i++)
	{
		size_t p = offset + 52 + (amount_of_vertices * 12) + 4 + (amount_of_vertices * 4) + 4 + (i * 8);

		float x = from_bytes<int16_t>(buffer, p) / 4096.0f;
		float y = std::abs((from_bytes<int16_t>(buffer, p + 2) / 4096.0f) - 1.0f);

		vertices[i].texcoord[0] = x;
		vertices[i].texcoord[1] = y;
	}

	// SKIN
	for (unsigned int i = 0; i < amount_of_vertices; i++)
	{
		size_t p = offset + 52 + (amount_of_vertices * 12) + 4 + (amount_of_vertices * 4) + 4 + (i * 8) + 4;

		float x = from_bytes<int16_t>(buffer, p) / 4096.0f;
		float y = (float)from_bytes<int8_t>(buffer, p + 2);
		float z = (float)from_bytes<int8_t>(buffer, p + 3);

		vertices[i].skin[0] = x;
		vertices[i].skin[1] = y;
		vertices[i].skin[2] = z;
	}

	// COLOUR
	for (unsigned int i = 0; i < amount_of_vertices; i++)
	{
		size_t p = offset + 52 + (amount_of_vertices * 12) +
			4 + (amount_of_vertices * 4) +
			4 + (amount_of_vertices * 8) +
			4 + (i * 4);

		float x = byte_to_single(buffer, p);
		float y = byte_to_single(buffer, p + 1);
		float z = byte_to_single(buffer, p + 2);
		float w = byte_to_single(buffer, p + 3);

		vertices[i].colour[0] = x;
		vertices[i].colour[1] = y;
		vertices[i].colour[2] = z;
		vertices[i].colour[3] = w;
	}

	return { vertices };
}
