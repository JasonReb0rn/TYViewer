#include "Model.h"

#include <cctype>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/common.hpp>

void parseSubobjectName(const std::string& name, SubobjectPart& part)
{
	part.name = name;
	part.objectName = name;
	part.hasSector = false;
	part.sectorX = 0;
	part.sectorLayer = 0;
	part.sectorZ = 0;

	auto digits = [&](size_t at) -> bool
	{
		return at + 2 < name.size()
			&& std::isdigit(static_cast<unsigned char>(name[at]))
			&& std::isdigit(static_cast<unsigned char>(name[at + 1]))
			&& std::isdigit(static_cast<unsigned char>(name[at + 2]));
	};
	if (name.size() < 11 || !digits(0) || name[3] != ' ' || !digits(4) || name[7] != ' ' || !digits(8))
		return;
	if (name.size() > 11 && name[11] != ' ')
		return;

	auto value = [&](size_t at) -> int
	{
		return (name[at] - '0') * 100 + (name[at + 1] - '0') * 10 + (name[at + 2] - '0');
	};
	part.hasSector = true;
	part.sectorX = value(0);
	part.sectorLayer = value(4);
	part.sectorZ = value(8);
	part.objectName = name.size() > 12 ? name.substr(12) : std::string();
}

Model::Model(const std::vector<Mesh*>& meshes) :
	meshes(meshes)
{
	for (const Mesh* mesh : meshes)
	{
		if (mesh == nullptr || !mesh->hasLocalAabb())
			continue;
		if (!m_hasLocalAabb)
		{
			m_localAabbMin = mesh->getLocalAabbMin();
			m_localAabbMax = mesh->getLocalAabbMax();
			m_hasLocalAabb = true;
		}
		else
		{
			m_localAabbMin = glm::min(m_localAabbMin, mesh->getLocalAabbMin());
			m_localAabbMax = glm::max(m_localAabbMax, mesh->getLocalAabbMax());
		}
	}
}
Model::~Model()
{
	for (int i = 0; i < meshes.size(); i++)
	{
		delete meshes[i];
	}
	meshes.clear();
}

void Model::draw(Shader& shader) const
{
	drawMeshes(shader, false);
	drawMeshes(shader, true);
}

void Model::drawMeshes(Shader& shader, bool transparentPass) const
{
	drawMeshes(shader, transparentPass, glm::mat4(1.0f), MeshDrawStyle{});
}

void Model::drawMeshes(Shader& shader, bool transparentPass, const glm::mat4& world, const MeshDrawStyle& style) const
{
	for (auto& mesh : meshes)
	{
		if (mesh->isTransparent() != transparentPass)
			continue;
		mesh->draw(shader, world, style);
	}
}

int Model::getTotalVertexCount() const
{
	int total = 0;
	for (const auto& mesh : meshes)
	{
		total += mesh->getVertexCount();
	}
	return total;
}

int Model::getTotalTriangleCount() const
{
	int total = 0;
	for (const auto& mesh : meshes)
	{
		total += mesh->getTriangleCount();
	}
	return total;
}
