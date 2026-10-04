#include "content.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

void Content::initialize()
{
	createDefaultTexture();
	archives[0] = nullptr;
	archives[1] = nullptr;
	activeArchiveIndex = 0;
}

bool Content::loadRKV(const std::string& path, int archiveIndex)
{
	if (archiveIndex < 0 || archiveIndex > 1)
		return false;
	
	archives[archiveIndex] = new Archive();
	if (archiveIndex == 0)
	{
		ty1MaterialsReady = false;
		ty1Materials.clear();
	}
	return archives[archiveIndex]->load(path);
}

namespace
{
	std::string lowerCopy(std::string value)
	{
		for (char& ch : value)
		{
			const unsigned char u = static_cast<unsigned char>(ch);
			if (u >= 'A' && u <= 'Z')
				ch = static_cast<char>(u - 'A' + 'a');
		}
		return value;
	}

	std::vector<std::string> splitWords(const std::string& line)
	{
		std::vector<std::string> words;
		std::string word;
		for (char ch : line)
		{
			if (ch == ' ' || ch == '\t' || ch == '\r')
			{
				if (!word.empty())
				{
					words.push_back(word);
					word.clear();
				}
			}
			else
			{
				word.push_back(ch);
			}
		}
		if (!word.empty())
			words.push_back(word);
		return words;
	}

	bool parseIntWord(const std::string& word, int& value)
	{
		if (word.empty())
			return false;
		char* end = nullptr;
		const long parsed = std::strtol(word.c_str(), &end, 10);
		if (end == word.c_str() || *end != '\0')
			return false;
		value = static_cast<int>(parsed);
		return true;
	}
}

void Content::loadTy1Materials()
{
	if (ty1MaterialsReady || archives[0] == nullptr)
		return;

	ty1MaterialsReady = true;

	std::vector<char> data;
	if (!archives[0]->getFileData("global.mad", data) || data.empty())
		return;

	struct Block
	{
		std::string name;
		std::string alias;
		bool depthWrite = true;
		int blendCode = -1;
		bool invisible = false;
		bool grassEffect = false;
		bool masked = false;
		float alphaRef = -1.0f;
		bool active = false;
	};

	Block block;
	auto commit = [&]()
	{
		if (!block.active || block.name.empty())
			return;

		const std::string key = lowerCopy(block.name);
		if (ty1Materials.find(key) != ty1Materials.end())
			return;

		Ty1MaterialDraw draw;
		draw.textureAlias = block.alias;
		draw.invisible = block.invisible;
		draw.grassEffect = block.grassEffect;
		draw.masked = block.masked;
		if (block.masked)
			draw.alphaRef = block.alphaRef >= 0.0f ? block.alphaRef : 0.5f;
		if (block.blendCode == 1)
			draw.blend = MeshBlend::Additive;
		else if (block.blendCode == 2)
			draw.blend = MeshBlend::Subtractive;
		else if (block.blendCode == 5 || !block.depthWrite)
			draw.blend = MeshBlend::Alpha;
		else
			draw.blend = MeshBlend::Opaque;
		ty1Materials.emplace(key, draw);
	};

	std::string line;
	line.reserve(128);
	auto flushLine = [&]()
	{
		const size_t comment = line.find("//");
		if (comment != std::string::npos)
			line.erase(comment);

		const std::vector<std::string> words = splitWords(line);
		line.clear();
		if (words.empty())
			return;

		const std::string key = lowerCopy(words[0]);
		if (key == "name" && words.size() >= 2)
		{
			commit();
			block = Block();
			block.active = true;
			block.name = words[1];
			if (words.size() >= 4 && lowerCopy(words[2]) == "alias")
				block.alias = words[3];
			return;
		}

		if (!block.active)
			return;

		if (key == "alias" && words.size() >= 2)
			block.alias = words[1];
		else if (key == "blend" && words.size() >= 2)
		{
			int code = 0;
			if (!parseIntWord(words[1], code))
				return;
			block.blendCode = code;
			if (code == 1 || code == 2 || code == 5)
				block.depthWrite = false;
			else if (code == 0 || code == 6)
				block.depthWrite = true;
		}
		else if (key == "zwrite" && words.size() >= 2)
		{
			int enabled = 1;
			if (!parseIntWord(words[1], enabled))
				return;
			block.depthWrite = enabled != 0;
		}
		else if (key == "type" && words.size() >= 2 && lowerCopy(words[1]) == "alphafog")
		{
			// Vertex alpha is the sheet opacity. Z2 (and A1, B1, E2) use the
			// unsuffixed waterfall_overlay_01, which has no zwrite line. The
			// _z variant used by Z1 sets zwrite 0 explicitly.
			block.depthWrite = false;
		}
		else if (key == "indirectwater")
		{
			// Floor water and cave puddles (Z2_water, and the same material on
			// the raised pool). Vertex alpha is the opacity. Depth writes hide
			// the ground under the surface.
			block.depthWrite = false;
		}
		else if (key == "invisible" && words.size() >= 2)
		{
			int enabled = 1;
			if (!parseIntWord(words[1], enabled))
				return;
			block.invisible = enabled != 0;
		}
		else if (key == "effect")
		{
			// "effect = grass,21" is a grass emitter. The mesh may still be drawn.
			for (size_t i = 1; i < words.size(); i++)
			{
				const std::string word = lowerCopy(words[i]);
				if (word == "grass" || word.rfind("grass", 0) == 0)
					block.grassEffect = true;
			}
		}
		else if (key == "masked" && words.size() >= 2)
		{
			int enabled = 1;
			if (!parseIntWord(words[1], enabled))
				return;
			block.masked = enabled != 0;
		}
		else if (key == "aref" && words.size() >= 2)
		{
			char* end = nullptr;
			const float parsed = std::strtof(words[1].c_str(), &end);
			if (end == words[1].c_str())
				return;
			block.alphaRef = parsed;
		}
	};

	for (char ch : data)
	{
		if (ch == '\n')
			flushLine();
		else
			line.push_back(ch);
	}
	if (!line.empty())
		flushLine();
	commit();
}

Content::Ty1MaterialDraw Content::lookupTy1Material(const std::string& materialName)
{
	loadTy1Materials();

	const auto it = ty1Materials.find(lowerCopy(materialName));
	if (it == ty1Materials.end())
		return {};
	return it->second;
}

std::vector<std::string> Content::getModelList(int archiveIndex)
{
	std::vector<std::string> modelList;
	
	if (archiveIndex < 0 || archiveIndex > 1 || archives[archiveIndex] == nullptr)
		return modelList;
	
	// Get all .mdl files from archive
	Archive* arc = archives[archiveIndex];
	modelList = arc->getFilesByExtension("mdl");
	
	return modelList;
}

std::vector<std::string> Content::getLevelList(int archiveIndex)
{
	std::vector<std::string> levelList;

	if (archiveIndex < 0 || archiveIndex > 1 || archives[archiveIndex] == nullptr)
		return levelList;

	Archive* arc = archives[archiveIndex];
	if (archiveIndex == 0)
	{
		levelList = arc->getFilesByExtension("lv2");
	}
	else
	{
		std::vector<std::string> bniFiles = arc->getFilesByExtension("bni");
		for (const std::string& name : bniFiles)
		{
			std::string lower = name;
			std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
			if (lower.find(".lv3.") != std::string::npos)
				levelList.push_back(name);
		}
	}

	std::sort(levelList.begin(), levelList.end(), [](const std::string& a, const std::string& b)
	{
		std::string al = a;
		std::string bl = b;
		std::transform(al.begin(), al.end(), al.begin(), ::tolower);
		std::transform(bl.begin(), bl.end(), bl.begin(), ::tolower);
		return al < bl;
	});
	return levelList;
}

void Content::setActiveArchive(int archiveIndex)
{
	if (archiveIndex >= 0 && archiveIndex <= 1)
		activeArchiveIndex = archiveIndex;
}

bool Content::getActiveFileData(const std::string& name, std::vector<char>& data) const
{
	if (activeArchiveIndex < 0 || activeArchiveIndex > 1)
		return false;
	Archive* archive = archives[activeArchiveIndex];
	if (archive == nullptr)
		return false;
	return archive->getFileData(name, data);
}

void Content::createDefaultTexture()
{
	// Gosh darn it, you said this map didn't need cs source!!!
	//const unsigned char* data = new unsigned char[16]
	//{ 
	//  // 4x4
	//	// PURPLE	BLACK
	//	// BLACK	PURPLE
	//	255, 0, 255, 255,	0, 0, 0, 0, 
	//	0, 0, 0, 0,			255, 0, 255, 255 
	//};

	// White
	const unsigned char* data = new unsigned char[16]
	{ 
		// 4x4
		// WHITE WHITE
		// WHITE WHITE

		255, 255, 255, 255, 255, 255, 255, 255, 
		255, 255, 255, 255, 255, 255, 255, 255 
	};

	int width = 2;
	int height = 2;

	defaultTexture = new Texture(SOIL_create_OGL_texture(data, &width, &height, 4, 0, 0));

	delete[] data;
}
