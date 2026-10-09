#include "critter_floor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>

#include <glm/geometric.hpp>

#include "graphics/mesh.h"
#include "model.h"

namespace
{
	// A downward query only walks the cell under the point, so the cell has to stay
	// small. The cap grows the cell on a huge level instead of allocating a giant grid.
	const int kMaxCellsPerAxis = 1024;
	const float kTargetCellSize = 128.0f;
	// A ground hit this close under a water plane is still the shore, not the seabed.
	const float kSubmergedSlack = 0.5f;

	// C_Water, Collide_Water, invis_waterplane. Waterfalls, slides, walls, and
	// water_ground stay walkable.
	bool isWaterSurfaceName(const std::string& part)
	{
		std::string lower = part;
		for (char& c : lower)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (lower.find("water") == std::string::npos)
			return false;
		return lower.find("fall") == std::string::npos
			&& lower.find("slide") == std::string::npos
			&& lower.find("wall") == std::string::npos
			&& lower.find("ground") == std::string::npos;
	}
}

void CritterFloor::clear()
{
	m_triangles.clear();
	m_cells.clear();
	m_stamps.clear();
	m_stamp = 0;
	m_cellsX = 0;
	m_cellsZ = 0;
}

void CritterFloor::build(const std::vector<Model*>& rooms, const std::function<bool(const Mesh*)>& include)
{
	clear();
	glm::vec3 lo(std::numeric_limits<float>::max());
	glm::vec3 hi(-std::numeric_limits<float>::max());
	for (const Model* model : rooms)
	{
		if (model == nullptr)
			continue;
		for (const Mesh* mesh : model->getMeshes())
		{
			if (mesh == nullptr || !include(mesh))
				continue;
			const bool water = isWaterSurfaceName(mesh->getPartName());
			const std::vector<Vertex>& vertices = mesh->getVertices();
			const std::vector<unsigned int>& indices = mesh->getIndices();
			for (size_t i = 0; i + 2 < indices.size(); i += 3)
			{
				if (indices[i] >= vertices.size() || indices[i + 1] >= vertices.size() || indices[i + 2] >= vertices.size())
					continue;
				const glm::vec3 a(vertices[indices[i]].position);
				const glm::vec3 b(vertices[indices[i + 1]].position);
				const glm::vec3 c(vertices[indices[i + 2]].position);
				const glm::vec3 cross = glm::cross(b - a, c - a);
				const float area = glm::length(cross);
				if (area <= 1e-6f)
					continue;
				m_triangles.push_back({ a, b - a, c - a, cross / area, water });
				lo = glm::min(lo, glm::min(a, glm::min(b, c)));
				hi = glm::max(hi, glm::max(a, glm::max(b, c)));
			}
		}
	}
	if (m_triangles.empty())
		return;

	m_min = lo;
	const float span = std::max(hi.x - lo.x, hi.z - lo.z);
	m_cellSize = std::max(kTargetCellSize, span / static_cast<float>(kMaxCellsPerAxis));
	m_cellsX = std::max(1, static_cast<int>(std::ceil((hi.x - lo.x) / m_cellSize)) + 1);
	m_cellsZ = std::max(1, static_cast<int>(std::ceil((hi.z - lo.z) / m_cellSize)) + 1);
	m_cells.assign(static_cast<size_t>(m_cellsX) * static_cast<size_t>(m_cellsZ), {});
	m_stamps.assign(m_triangles.size(), 0);

	for (uint32_t t = 0; t < static_cast<uint32_t>(m_triangles.size()); t++)
	{
		const Triangle& tri = m_triangles[t];
		const glm::vec3 b = tri.a + tri.edge1;
		const glm::vec3 c = tri.a + tri.edge2;
		int x0, z0, x1, z1;
		cellOf(std::min(tri.a.x, std::min(b.x, c.x)), std::min(tri.a.z, std::min(b.z, c.z)), x0, z0);
		cellOf(std::max(tri.a.x, std::max(b.x, c.x)), std::max(tri.a.z, std::max(b.z, c.z)), x1, z1);
		for (int z = z0; z <= z1; z++)
		{
			for (int x = x0; x <= x1; x++)
				m_cells[static_cast<size_t>(cellIndex(x, z))].push_back(t);
		}
	}
}

void CritterFloor::cellOf(float x, float z, int& outX, int& outZ) const
{
	outX = std::clamp(static_cast<int>(std::floor((x - m_min.x) / m_cellSize)), 0, m_cellsX - 1);
	outZ = std::clamp(static_cast<int>(std::floor((z - m_min.z) / m_cellSize)), 0, m_cellsZ - 1);
}

void CritterFloor::bumpStamp() const
{
	if (++m_stamp == 0)
	{
		std::fill(m_stamps.begin(), m_stamps.end(), 0u);
		m_stamp = 1;
	}
}

bool CritterFloor::rayTriangle(const Triangle& tri, const glm::vec3& from, const glm::vec3& dir,
	float maxDistance, float& outDistance, glm::vec3& outNormal)
{
	// Moller-Trumbore, both faces.
	const glm::vec3 p = glm::cross(dir, tri.edge2);
	const float det = glm::dot(tri.edge1, p);
	if (std::abs(det) < 1e-8f)
		return false;
	const float inv = 1.0f / det;
	const glm::vec3 s = from - tri.a;
	const float u = glm::dot(s, p) * inv;
	if (u < 0.0f || u > 1.0f)
		return false;
	const glm::vec3 q = glm::cross(s, tri.edge1);
	const float v = glm::dot(dir, q) * inv;
	if (v < 0.0f || u + v > 1.0f)
		return false;
	const float distance = glm::dot(tri.edge2, q) * inv;
	if (distance < 0.0f || distance > maxDistance)
		return false;
	outDistance = distance;
	outNormal = glm::dot(tri.normal, dir) > 0.0f ? -tri.normal : tri.normal;
	return true;
}

void CritterFloor::testCell(int cell, const glm::vec3& from, const glm::vec3& dir, float maxDistance,
	float& bestGround, glm::vec3& bestNormal, int& bestTriangle,
	float& bestWater, glm::vec3& bestWaterNormal, int& bestWaterTriangle) const
{
	for (uint32_t t : m_cells[static_cast<size_t>(cell)])
	{
		if (m_stamps[t] == m_stamp)
			continue;
		m_stamps[t] = m_stamp;

		const Triangle& tri = m_triangles[t];
		float distance = 0.0f;
		glm::vec3 normal(0.0f);
		if (!rayTriangle(tri, from, dir, maxDistance, distance, normal))
			continue;
		if (tri.water)
		{
			if (distance >= bestWater)
				continue;
			bestWater = distance;
			bestWaterNormal = normal;
			bestWaterTriangle = static_cast<int>(t);
			continue;
		}
		if (distance >= bestGround)
			continue;
		bestGround = distance;
		bestNormal = normal;
		bestTriangle = static_cast<int>(t);
	}
}

bool CritterFloor::nearerWater(const glm::vec3& from, const glm::vec3& dir, float maxDistance, float thanDistance) const
{
	if (m_triangles.empty() || maxDistance <= 0.0f)
		return false;
	bumpStamp();

	const float flat = std::sqrt(dir.x * dir.x + dir.z * dir.z) * maxDistance;
	const int samples = std::max(1, static_cast<int>(std::ceil(flat / (m_cellSize * 0.5f)))) + 1;
	int lastCell = -1;
	for (int i = 0; i < samples; i++)
	{
		const float t = samples == 1 ? 0.0f : maxDistance * static_cast<float>(i) / static_cast<float>(samples - 1);
		const glm::vec3 point = from + dir * t;
		int x, z;
		cellOf(point.x, point.z, x, z);
		const int cell = cellIndex(x, z);
		if (cell == lastCell)
			continue;
		lastCell = cell;
		for (uint32_t index : m_cells[static_cast<size_t>(cell)])
		{
			if (m_stamps[index] == m_stamp)
				continue;
			m_stamps[index] = m_stamp;
			const Triangle& tri = m_triangles[index];
			if (!tri.water)
				continue;
			float distance = 0.0f;
			glm::vec3 normal(0.0f);
			if (rayTriangle(tri, from, dir, maxDistance, distance, normal) && distance + kSubmergedSlack < thanDistance)
				return true;
		}
	}
	return false;
}

bool CritterFloor::nearerGround(const glm::vec3& from, const glm::vec3& dir, float maxDistance, float thanDistance) const
{
	if (m_triangles.empty() || maxDistance <= 0.0f)
		return false;
	bumpStamp();

	const float flat = std::sqrt(dir.x * dir.x + dir.z * dir.z) * maxDistance;
	const int samples = std::max(1, static_cast<int>(std::ceil(flat / (m_cellSize * 0.5f)))) + 1;
	int lastCell = -1;
	for (int i = 0; i < samples; i++)
	{
		const float t = samples == 1 ? 0.0f : maxDistance * static_cast<float>(i) / static_cast<float>(samples - 1);
		const glm::vec3 point = from + dir * t;
		int x, z;
		cellOf(point.x, point.z, x, z);
		const int cell = cellIndex(x, z);
		if (cell == lastCell)
			continue;
		lastCell = cell;
		for (uint32_t index : m_cells[static_cast<size_t>(cell)])
		{
			if (m_stamps[index] == m_stamp)
				continue;
			m_stamps[index] = m_stamp;
			const Triangle& tri = m_triangles[index];
			if (tri.water)
				continue;
			float distance = 0.0f;
			glm::vec3 normal(0.0f);
			if (rayTriangle(tri, from, dir, maxDistance, distance, normal) && distance < thanDistance)
				return true;
		}
	}
	return false;
}

bool CritterFloor::cast(const glm::vec3& from, const glm::vec3& dir, float maxDistance,
	float& outDistance, glm::vec3& outNormal, int* outTriangle, FloorKind kind) const
{
	if (m_triangles.empty() || maxDistance <= 0.0f)
		return false;
	bumpStamp();

	float bestGround = std::numeric_limits<float>::max();
	float bestWater = std::numeric_limits<float>::max();
	glm::vec3 bestNormal(0.0f, 1.0f, 0.0f);
	glm::vec3 bestWaterNormal(0.0f, 1.0f, 0.0f);
	int bestTriangle = -1;
	int bestWaterTriangle = -1;

	// Visit each cell the XZ projection crosses, sampled at half a cell.
	const float flat = std::sqrt(dir.x * dir.x + dir.z * dir.z) * maxDistance;
	const int samples = std::max(1, static_cast<int>(std::ceil(flat / (m_cellSize * 0.5f)))) + 1;
	int lastCell = -1;
	for (int i = 0; i < samples; i++)
	{
		const float t = samples == 1 ? 0.0f : maxDistance * static_cast<float>(i) / static_cast<float>(samples - 1);
		const glm::vec3 point = from + dir * t;
		int x, z;
		cellOf(point.x, point.z, x, z);
		const int cell = cellIndex(x, z);
		if (cell == lastCell)
			continue;
		lastCell = cell;
		testCell(cell, from, dir, maxDistance, bestGround, bestNormal, bestTriangle,
			bestWater, bestWaterNormal, bestWaterTriangle);
	}

	// The sheet is above the bed. Shore and island ground sit above the sheet, or within the slack.
	const bool sheetFirst = bestWaterTriangle >= 0 && bestWater + kSubmergedSlack < bestGround;
	if (kind == FloorKind::Support && sheetFirst)
	{
		outDistance = bestWater;
		outNormal = bestWaterNormal;
		if (outTriangle != nullptr)
			*outTriangle = bestWaterTriangle;
		return true;
	}
	if (bestTriangle < 0 || sheetFirst)
		return false;
	outDistance = bestGround;
	outNormal = bestNormal;
	if (outTriangle != nullptr)
		*outTriangle = bestTriangle;
	return true;
}

bool CritterFloor::testTriangle(int triangle, const glm::vec3& from, const glm::vec3& dir, float maxDistance,
	float& outDistance, glm::vec3& outNormal, FloorKind kind) const
{
	if (triangle < 0 || static_cast<size_t>(triangle) >= m_triangles.size() || maxDistance <= 0.0f)
		return false;
	const Triangle& tri = m_triangles[static_cast<size_t>(triangle)];
	if (tri.water && kind != FloorKind::Support)
		return false;
	if (!rayTriangle(tri, from, dir, maxDistance, outDistance, outNormal))
		return false;
	if (tri.water)
	{
		// An island closer than this cached sheet has to win, including the shore slack.
		return !nearerGround(from, dir, maxDistance, outDistance + kSubmergedSlack);
	}
	// A cached bed triangle used to win for the whole plane, including under an island.
	return !nearerWater(from, dir, maxDistance, outDistance);
}

bool CritterFloor::floorBelow(const glm::vec3& from, float maxDrop, float& outY, glm::vec3& outNormal) const
{
	float distance = 0.0f;
	if (!cast(from, glm::vec3(0.0f, -1.0f, 0.0f), maxDrop, distance, outNormal))
		return false;
	outY = from.y - distance;
	return true;
}
