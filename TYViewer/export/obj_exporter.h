#pragma once

#include <string>
#include <filesystem>

class Model;
class Content;

namespace Export
{
	// Exports a model as OBJ+MTL into:
	//   outDirectory/<modelBaseName>/<modelBaseName>.obj
	//   outDirectory/<modelBaseName>/<modelBaseName>.mtl
	// and attempts to write referenced textures (typically "<material>.dds") alongside.
	//
	// Returns true on success (OBJ written). Texture export is best-effort.
	bool exportModelAsObj(const Model& model,
	                      const std::string& modelFileName,
	                      const Content& content,
	                      const std::filesystem::path& outDirectory,
	                      std::string* outError = nullptr);
}

