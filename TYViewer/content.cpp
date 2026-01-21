#include "content.h"
#include <algorithm>

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

void Content::setActiveArchive(int archiveIndex)
{
	if (archiveIndex >= 0 && archiveIndex <= 1)
		activeArchiveIndex = archiveIndex;
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
