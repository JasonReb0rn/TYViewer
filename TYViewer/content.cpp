#include "content.h"
#include <algorithm>
#include <cctype>

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
	return archives[archiveIndex]->load(path);
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
