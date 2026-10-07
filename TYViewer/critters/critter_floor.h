#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <glm/vec3.hpp>

class Mesh;
class Model;

// Level triangles bucketed on an XZ grid. Stands in for the game's collision queries
// (CritterField2::GetFloor / TestFloor go through CollisionTool, which is not decompiled).
class CritterFloor
{
public:
	// Room meshes are world space. `include` picks which meshes count as solid.
	void build(const std::vector<Model*>& rooms, const std::function<bool(const Mesh*)>& include);
	void clear();
	bool empty() const { return m_triangles.empty(); }

	// Nearest ground hit along `dir` (unit length) within `maxDistance`. Both triangle sides hit.
	// Water-surface collision is not ground. A hit underneath a water plane is not ground either.
	// `outTriangle` receives the hit index when non-null, and it is never a water triangle.
	bool cast(const glm::vec3& from, const glm::vec3& dir, float maxDistance,
		float& outDistance, glm::vec3& outNormal, int* outTriangle = nullptr) const;
	// The same test against one triangle from a previous cast. False when `triangle` is stale,
	// a water plane, or ground that sits under a water plane.
	bool testTriangle(int triangle, const glm::vec3& from, const glm::vec3& dir, float maxDistance,
		float& outDistance, glm::vec3& outNormal) const;
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
		float& bestGround, glm::vec3& bestNormal, int& bestTriangle, float& bestWater) const;
	// True when a water plane is hit closer than `thanDistance` by more than the shore slack.
	bool nearerWater(const glm::vec3& from, const glm::vec3& dir, float maxDistance, float thanDistance) const;

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
