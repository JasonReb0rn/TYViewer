#pragma once

#include <string>
#include <vector>

class mdl2
{
public:
	struct Bounds;
	struct Vertex;
	struct Mesh;
	struct Subobject;


	struct Bounds
	{
		float x, y, z;		// POSITION
		float sx, sy, sz;	// SIZE
		float ox, oy, oz;	// ORIGIN / UNKNOWN
	};

	struct Vertex
	{
		float position[3]; // XYZ
		float normal[3]; // XYZ
		float texcoord[2]; // UV (XY)
		float skin[3]; // MODIFIER; BONE_A; BONE_B
		float colour[4]; // RGBA
	};

	struct Segment
	{
		std::vector<Vertex> vertices;
	};

	struct Mesh
	{
		std::string material;
		std::vector<Segment> segments;
	};

	struct Subobject
	{
		Bounds bounds;

		std::string name;
		std::string material;

		size_t triangle_count;

		std::vector<Mesh> meshes;
	};

public:
	bool load(const char* buffer, size_t offset);
	bool loadTY2(const char* buffer, size_t offset); // Load TY 2 format with relaxed signature check
	bool loadTY2MDL3(const char* buffer, size_t offset); // Load TY 2 MDL3 format (newer structure)

public:
	Bounds bounds;
	std::string name;

	std::vector<Subobject> subobjects;

	// TY 2 MDL3 metadata (for MDG parsing)
	struct MDL3Metadata
	{
		uint16_t ComponentCount;
		uint16_t TextureCount;
		uint16_t RefPointCount;
		uint16_t AnimNodeCount;
		uint16_t MeshCount;
		uint16_t StripCount;
		uint16_t ComponentDescriptionsOffset;
		uint32_t TextureListOffset;
		uint32_t RefPointsOffsetsOffset;
		uint16_t AnimNodeDataOffset;
		uint32_t AnimNodeListsOffset;
		uint32_t ObjectLookupTable;
		uint16_t StringTableOffset;
		std::vector<std::string> TextureNames;
	};
	MDL3Metadata mdl3Metadata;
	bool isMDL3Format;

private:
	Subobject parse_subobject(const char* buffer, size_t offset);
	Mesh parse_mesh(const char* buffer, size_t offset);
	Segment parse_segment(const char* buffer, size_t offset, size_t& size);
};