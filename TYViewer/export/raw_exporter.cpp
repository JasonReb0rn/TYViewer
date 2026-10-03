#include "raw_exporter.h"

#include <fstream>
#include <vector>

#include "../debug.h"
#include "../content.h"

static std::string stripExtension(const std::string& filename)
{
	size_t dot = filename.find_last_of('.');
	if (dot == std::string::npos)
		return filename;
	return filename.substr(0, dot);
}

static bool writeBytes(const std::filesystem::path& path, const std::vector<char>& bytes, std::string* outError)
{
	std::ofstream out(path, std::ios::binary | std::ios::out | std::ios::trunc);
	if (!out.is_open())
	{
		if (outError) *outError = "Failed to open for writing: " + path.string();
		return false;
	}

	out.write(bytes.data(), (std::streamsize)bytes.size());
	if (!out)
	{
		if (outError) *outError = "Failed while writing: " + path.string();
		return false;
	}

	return true;
}

namespace Export
{
	bool exportModelRaw(const std::string& modelFileName,
	                    const Content& content,
	                    const std::filesystem::path& outDirectory,
	                    std::string* outError,
	                    bool* outWroteMdg)
	{
		if (outWroteMdg)
			*outWroteMdg = false;

		const std::string baseName = stripExtension(modelFileName);
		if (baseName.empty())
		{
			if (outError) *outError = "Invalid model name.";
			return false;
		}

		const std::string mdlName = baseName + ".mdl";
		const std::string mdgName = baseName + ".mdg";

		std::error_code ec;
		std::filesystem::create_directories(outDirectory, ec);
		if (ec)
		{
			if (outError) *outError = "Failed to create output directory.";
			return false;
		}

		std::vector<char> mdlBytes;
		if (!content.getActiveFileData(mdlName, mdlBytes) || mdlBytes.empty())
		{
			if (outError) *outError = "Could not read archive file: " + mdlName;
			return false;
		}

		const std::filesystem::path mdlPath = outDirectory / mdlName;
		if (!writeBytes(mdlPath, mdlBytes, outError))
			return false;

		Debug::log("Raw export wrote: " + mdlPath.string() + " (" + std::to_string(mdlBytes.size()) + " bytes)");

		std::vector<char> mdgBytes;
		if (content.getActiveFileData(mdgName, mdgBytes) && !mdgBytes.empty())
		{
			const std::filesystem::path mdgPath = outDirectory / mdgName;
			if (!writeBytes(mdgPath, mdgBytes, outError))
				return false;

			if (outWroteMdg)
				*outWroteMdg = true;

			Debug::log("Raw export wrote: " + mdgPath.string() + " (" + std::to_string(mdgBytes.size()) + " bytes)");
		}
		else
		{
			Debug::log("Raw export: no .mdg companion found for " + mdlName);
		}

		return true;
	}
}
