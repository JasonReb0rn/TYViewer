#include "critter_floor.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

#include "graphics/mesh.h"
#include "model.h"

namespace
{
	const int kMaxCellsPerAxis = 256;
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
				m_triangles.push_back({ a, b - a, c - a, cross / area });
				lo = glm::min(lo, glm::min(a, glm::min(b, c)));
				hi = glm::max(hi, glm::max(a, glm::max(b, c)));
			}
		}
	}
	if (m_triangles.empty())
		return;

	m_min = lo;
	const float span = std::max(hi.x - lo.x, hi.z - lo.z);
	m_cellSize = std::max(512.0f, span / static_cast<float>(kMaxCellsPerAxis));
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

bool CritterFloor::testCell(int cell, const glm::vec3& from, const glm::vec3& dir, float maxDistance,
	float& best, glm::vec3& bestNormal) const
{
	bool hit = false;
	for (uint32_t t : m_cells[static_cast<size_t>(cell)])
	{
		if (m_stamps[t] == m_stamp)
			continue;
		m_stamps[t] = m_stamp;

		// Moller-Trumbore, both faces.
		const Triangle& tri = m_triangles[t];
		const glm::vec3 p = glm::cross(dir, tri.edge2);
		const float det = glm::dot(tri.edge1, p);
		if (std::abs(det) < 1e-8f)
			continue;
		const float inv = 1.0f / det;
		const glm::vec3 s = from - tri.a;
		const float u = glm::dot(s, p) * inv;
		if (u < 0.0f || u > 1.0f)
			continue;
		const glm::vec3 q = glm::cross(s, tri.edge1);
		const float v = glm::dot(dir, q) * inv;
		if (v < 0.0f || u + v > 1.0f)
			continue;
		const float distance = glm::dot(tri.edge2, q) * inv;
		if (distance < 0.0f || distance > maxDistance || distance >= best)
			continue;
		best = distance;
		bestNormal = glm::dot(tri.normal, dir) > 0.0f ? -tri.normal : tri.normal;
		hit = true;
	}
	return hit;
}

bool CritterFloor::cast(const glm::vec3& from, const glm::vec3& dir, float maxDistance,
	float& outDistance, glm::vec3& outNormal) const
{
	if (m_triangles.empty() || maxDistance <= 0.0f)
		return false;
	if (++m_stamp == 0)
	{
		std::fill(m_stamps.begin(), m_stamps.end(), 0u);
		m_stamp = 1;
	}

	float best = std::numeric_limits<float>::max();
	glm::vec3 bestNormal(0.0f, 1.0f, 0.0f);
	bool hit = false;

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
		hit |= testCell(cell, from, dir, maxDistance, best, bestNormal);
	}

	if (!hit)
		return false;
	outDistance = best;
	outNormal = bestNormal;
	return true;
}

bool CritterFloor::floorBelow(const glm::vec3& from, float maxDrop, float& outY, glm::vec3& outNormal) const
{
	float distance = 0.0f;
	if (!cast(from, glm::vec3(0.0f, -1.0f, 0.0f), maxDrop, distance, outNormal))
		return false;
	outY = from.y - distance;
	return true;
}
