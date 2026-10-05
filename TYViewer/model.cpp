#include "Model.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/common.hpp>

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
