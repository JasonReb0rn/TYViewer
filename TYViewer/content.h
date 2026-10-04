#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <cmath>

#include <unordered_map>
#include <vector>

#include "SOIL2/SOIL2.h"

#include "loader/archive.h"

#include "loader/assets/wfn.h"

#include "loader/assets/mdl2.h"
#include "loader/assets/mdg.h"

#include "graphics/texture.h"
#include "graphics/shader.h"
#include "util/font.h"
#include "model.h"

#include "util/bitconverter.h"
#include "util/stringext.h"

#include "debug.h"


#define MDL2_SUBMESH_SIZE 80

#define MDL2_SEGMENT_VERTEX_LIST_COUNT_OFFSET	12
#define MDL2_SEGMENT_VERTEX_LIST_OFFSET 52

class Content
{
public:
	// If specified texture cannot be loaded, return this.
	// This is your job to assign!
	//    Wait, who am I talking to..? myself..?
	//    I even assigned it, why is old me so aggresive..?
	Texture* defaultTexture = NULL;

	void initialize();
	bool loadRKV(const std::string& path, int archiveIndex = 0);
	
	// Get list of all .mdl files from a specific archive
	std::vector<std::string> getModelList(int archiveIndex);
	// TY1: .lv2 files. TY2: *.lv3.bni chunks (not other .bni files).
	std::vector<std::string> getLevelList(int archiveIndex);
	
	// Set active archive for loading
	void setActiveArchive(int archiveIndex);
	int getActiveArchive() const { return activeArchiveIndex; }
	// Read raw bytes from the currently-active archive (useful for exporters/tools).
	bool getActiveFileData(const std::string& name, std::vector<char>& data) const;
	// True when the active archive contains a file with this name (case-insensitive).
	bool hasActiveFile(const std::string& name) const
	{
		if (activeArchiveIndex < 0 || activeArchiveIndex > 1)
			return false;
		Archive* archive = archives[activeArchiveIndex];
		if (archive == NULL)
			return false;
		File file;
		return archive->getFile(name, file) && file.size != 0;
	}

	template<typename T>
	T* load(const std::string& name)
	{}

	template<>
	Texture* load<Texture>(const std::string& name);

	template<>
	Shader* load<Shader>(const std::string& name);

	template<>
	Model* load<Model>(const std::string& name);

	template<>
	Font* load<Font>(const std::string& name);

	// TY1 global.mad entry. Unknown names stay opaque with no texture alias.
	struct Ty1MaterialDraw
	{
		std::string textureAlias;
		MeshBlend blend = MeshBlend::Opaque;
		// global.mad "invisible 1": the game does not draw this material.
		bool invisible = false;
		// global.mad "effect = grass,...". The surface may still be drawn.
		bool grassEffect = false;
		// global.mad "masked 1" / "aref". Cutout cards (tree walls) discard below this.
		bool masked = false;
		float alphaRef = 0.01f;
	};
	Ty1MaterialDraw lookupTy1Material(const std::string& materialName);

private:
	void createDefaultTexture();
	void loadTy1Materials();

	Archive* archives[2]; // 0 = TY1, 1 = TY2
	int activeArchiveIndex;

	std::unordered_map<std::string, Texture*> textures;
	std::unordered_map<std::string, Shader*> shaders;
	std::unordered_map<std::string, Model*> models;
	std::unordered_map<std::string, Font*> fonts;

	bool ty1MaterialsReady = false;
	std::unordered_map<std::string, Ty1MaterialDraw> ty1Materials;
};

#include "content.inl"