#pragma once

#include <string>
#include <filesystem>

class Content;

namespace Export
{
	// Writes the original archive bytes for a model into outDirectory:
	//   <base>.mdl  (required)
	//   <base>.mdg  (optional; written when present in the active archive)
	//
	// Files are written flat into outDirectory (same layout as TEST_FILES).
	// Returns true if the .mdl was written successfully.
	bool exportModelRaw(const std::string& modelFileName,
	                    const Content& content,
	                    const std::filesystem::path& outDirectory,
	                    std::string* outError = nullptr,
	                    bool* outWroteMdg = nullptr);
}
