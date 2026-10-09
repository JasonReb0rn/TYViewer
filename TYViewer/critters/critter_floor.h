#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <glm/vec3.hpp>

class Mesh;
class Model;

// Ground ignores water sheets, so a crab walks the creek bed. Support returns the sheet
// when it is the closest hit, so a flyer stays above the river.
enum class FloorKind
{
	Ground,
	Support,
};

// Level triangles bucketed on an XZ grid. Stands in for the game's collision queries
// (CritterField2::GetFloor / TestFloor go through CollisionTool, which is not decompiled).
class CritterFloor
{
public:
	// Room meshes are world space. `include` picks which meshes count as solid.
	void build(const std::vector<Model*>& rooms, const std::function<bool(const Mesh*)>& include);
	void clear();
	bool empty() const { return m_triangles.empty(); }

	// Nearest hit along `dir` (unit length) within `maxDistance`. Both triangle sides hit.
	// Ground: water-surface collision is not a floor, and neither is ground under a nearer sheet.
	// Support: the closer of the sheet and the ground. The bed under a nearer sheet is not returned.
	// `outTriangle` receives the hit index when non-null. Ground never stores a water triangle.
	bool cast(const glm::vec3& from, const glm::vec3& dir, float maxDistance,
		float& outDistance, glm::vec3& outNormal, int* outTriangle = nullptr,
		FloorKind kind = FloorKind::Ground) const;
	// The same test against one triangle from a previous cast. False when `triangle` is stale,
	// a water plane on a ground query, or a surface the other kind would not return.
	bool testTriangle(int triangle, const glm::vec3& from, const glm::vec3& dir, float maxDistance,
		float& outDistance, glm::vec3& outNormal, FloorKind kind = FloorKind::Ground) const;
	// Highest ground below `from`, at most `maxDrop` down. The normal faces up.
	// Water planes, and ground underneath them, do not count.
	bool floorBelow(const glm::vec3& from, float maxDrop, float& outY, glm::vec3& outNormal) const;

private:
	struct Triangle
	{
		glm::vec3 a;
		glm::vec3 edge1;
		glm::vec3 edge2;
		glm::vec3 normal;
		// Ocean and pool collision planes. Kept so a ground hit under one can be rejected.
		bool water = false;
	};

	int cellIndex(int x, int z) const { return z * m_cellsX + x; }
	void cellOf(float x, float z, int& outX, int& outZ) const;
	void bumpStamp() const;
	static bool rayTriangle(const Triangle& tri, const glm::vec3& from, const glm::vec3& dir,
		float maxDistance, float& outDistance, glm::vec3& outNormal);
	void testCell(int cell, const glm::vec3& from, const glm::vec3& dir, float maxDistance,
		float& bestGround, glm::vec3& bestNormal, int& bestTriangle,
		float& bestWater, glm::vec3& bestWaterNormal, int& bestWaterTriangle) const;
	// True when a water plane is hit closer than `thanDistance` by more than the shore slack.
	bool nearerWater(const glm::vec3& from, const glm::vec3& dir, float maxDistance, float thanDistance) const;
	// True when ground is hit closer than `thanDistance`.
	bool nearerGround(const glm::vec3& from, const glm::vec3& dir, float maxDistance, float thanDistance) const;

	std::vector<Triangle> m_triangles;
	std::vector<std::vector<uint32_t>> m_cells;
	glm::vec3 m_min{ 0.0f };
	float m_cellSize = 128.0f;
	int m_cellsX = 0;
	int m_cellsZ = 0;
	// Per-query visit marks so a triangle spanning several cells is tested once.
	mutable std::vector<uint32_t> m_stamps;
	mutable uint32_t m_stamp = 0;
};
