#include "obj_exporter.h"

#include <fstream>
#include <unordered_set>
#include <vector>

#include "../debug.h"
#include "../model.h"
#include "../content.h"
#include "../graphics/mesh.h"

static std::string stripExtension(const std::string& filename)
{
	size_t dot = filename.find_last_of('.');
	if (dot == std::string::npos)
		return filename;
	return filename.substr(0, dot);
}

static std::string sanitizeMtlName(const std::string& s)
{
	// OBJ/MTL names are space-delimited; keep it simple.
	std::string out = s;
	for (char& c : out)
	{
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
			c = '_';
	}
	if (out.empty())
		out = "material";
	return out;
}

namespace Export
{
	bool exportModelAsObj(const Model& model,
	                      const std::string& modelFileName,
	                      const Content& content,
	                      const std::filesystem::path& outDirectory,
	                      std::string* outError)
	{
		const std::string baseName = stripExtension(modelFileName);
		if (baseName.empty())
		{
			if (outError) *outError = "Invalid model name.";
			return false;
		}

		std::error_code ec;
		std::filesystem::create_directories(outDirectory, ec);
		if (ec)
		{
			if (outError) *outError = "Failed to create output directory.";
			return false;
		}

		const std::filesystem::path modelDir = outDirectory / baseName;
		std::filesystem::create_directories(modelDir, ec);
		if (ec)
		{
			if (outError) *outError = "Failed to create model subdirectory.";
			return false;
		}

		const std::filesystem::path objPath = modelDir / (baseName + ".obj");
		const std::filesystem::path mtlPath = modelDir / (baseName + ".mtl");

		// Collect materials (best-effort texture export) first.
		std::unordered_set<std::string> materialNames;
		for (const auto& meshPtr : model.getMeshes())
		{
			if (!meshPtr) continue;
			std::string mat = meshPtr->getMaterialName();
			if (!mat.empty())
				materialNames.insert(mat);
		}

		// Write MTL (one entry per unique material name).
		{
			std::ofstream mtl(mtlPath, std::ios::out | std::ios::trunc);
			if (!mtl.is_open())
			{
				if (outError) *outError = "Failed to open MTL for writing.";
				return false;
			}

			mtl << "# TYViewer export\n";
			mtl << "# Model: " << baseName << "\n\n";

			for (const auto& matNameRaw : materialNames)
			{
				const std::string matName = sanitizeMtlName(matNameRaw);
				const std::string texFile = matNameRaw + ".dds";

				mtl << "newmtl " << matName << "\n";
				mtl << "Ka 1.000 1.000 1.000\n";
				mtl << "Kd 1.000 1.000 1.000\n";
				mtl << "Ks 0.000 0.000 0.000\n";
				mtl << "d 1.000\n";
				mtl << "illum 1\n";
				mtl << "map_Kd " << texFile << "\n\n";
			}
		}

		// Export texture bytes (best-effort).
		for (const auto& matNameRaw : materialNames)
		{
			const std::string texName = matNameRaw + ".dds";
			std::vector<char> bytes;
			if (!content.getActiveFileData(texName, bytes) || bytes.empty())
				continue;

			const std::filesystem::path outTexPath = modelDir / texName;
			std::ofstream tex(outTexPath, std::ios::binary | std::ios::out | std::ios::trunc);
			if (!tex.is_open())
				continue;

			tex.write(bytes.data(), (std::streamsize)bytes.size());
		}

		// Write OBJ.
		std::ofstream obj(objPath, std::ios::out | std::ios::trunc);
		if (!obj.is_open())
		{
			if (outError) *outError = "Failed to open OBJ for writing.";
			return false;
		}

		obj << "# TYViewer export\n";
		obj << "# Model: " << baseName << "\n";
		obj << "mtllib " << (baseName + ".mtl") << "\n\n";

		// Global streams; we keep v/vt/vn aligned so indices match.
		size_t globalVertexOffset = 0;

		for (size_t meshIndex = 0; meshIndex < model.getMeshes().size(); meshIndex++)
		{
			const Mesh* mesh = model.getMeshes()[meshIndex];
			if (!mesh) continue;

			const auto& verts = mesh->getVertices();
			const auto& indices = mesh->getIndices();

			const std::string matRaw = mesh->getMaterialName();
			const std::string matName = sanitizeMtlName(matRaw.empty() ? ("mesh_" + std::to_string(meshIndex)) : matRaw);

			obj << "g mesh_" << meshIndex << "\n";
			obj << "usemtl " << matName << "\n";

			// Emit vertices for this mesh.
			for (const auto& v : verts)
			{
				obj << "v " << v.position.x << " " << v.position.y << " " << v.position.z << "\n";
			}
			for (const auto& v : verts)
			{
				obj << "vt " << v.texcoord.x << " " << v.texcoord.y << "\n";
			}
			for (const auto& v : verts)
			{
				obj << "vn " << v.normal.x << " " << v.normal.y << " " << v.normal.z << "\n";
			}

			// Faces (triangles).
			if (indices.size() % 3 != 0)
			{
				Debug::log("OBJ export: mesh indices not divisible by 3 (mesh " + std::to_string(meshIndex) + ")");
			}

			for (size_t i = 0; i + 2 < indices.size(); i += 3)
			{
				const unsigned int i0 = indices[i + 0];
				const unsigned int i1 = indices[i + 1];
				const unsigned int i2 = indices[i + 2];

				// OBJ indices are 1-based.
				const size_t a = globalVertexOffset + (size_t)i0 + 1;
				const size_t b = globalVertexOffset + (size_t)i1 + 1;
				const size_t c = globalVertexOffset + (size_t)i2 + 1;

				obj << "f "
				    << a << "/" << a << "/" << a << " "
				    << b << "/" << b << "/" << b << " "
				    << c << "/" << c << "/" << c << "\n";
			}

			obj << "\n";
			globalVertexOffset += verts.size();
		}

		Debug::log("OBJ export complete: " + objPath.string());
		return true;
	}
}

