#include "content.h"
#include <algorithm>
#include <cctype>
#include <cmath>
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

	// "scroll = 0.1, -0.2" and "scroll 0.1 -0.2" both yield the floats.
	std::vector<float> collectFloats(const std::vector<std::string>& words)
	{
		std::vector<float> values;
		for (size_t i = 1; i < words.size(); i++)
		{
			std::string token = words[i];
			if (!token.empty() && token.back() == ',')
				token.pop_back();
			if (token.empty() || token == "=")
				continue;
			char* end = nullptr;
			const float parsed = std::strtof(token.c_str(), &end);
			if (end == token.c_str())
				continue;
			values.push_back(parsed);
		}
		return values;
	}

	float wrapUnit(float value)
	{
		float wrapped = std::fmod(value, 1.0f);
		if (wrapped < 0.0f)
			wrapped += 1.0f;
		return wrapped;
	}

	// animate steps UV by `step` every `period` seconds, then snaps a component
	// back to 0 once it reaches 0.99. The snap drops the remainder.
	float steppedOffset(float timeSeconds, float step, float period)
	{
		if (step == 0.0f)
			return 0.0f;

		float stepsF;
		if (period > 0.0f)
		{
			// The game steps only once angle is strictly greater than the period.
			stepsF = std::ceil(timeSeconds / period) - 1.0f;
			if (stepsF < 0.0f)
				stepsF = 0.0f;
		}
		else
		{
			stepsF = std::floor(timeSeconds * 60.0f);
		}

		const int steps = static_cast<int>(stepsF);
		if (steps <= 0)
			return 0.0f;
		if (step < 0.0f)
			return wrapUnit(static_cast<float>(steps) * step);

		int cycle = 1;
		if (step < 0.99f)
		{
			float acc = 0.0f;
			cycle = 0;
			while (cycle < 100000)
			{
				acc += step;
				cycle++;
				if (acc >= 0.99f)
					break;
			}
		}

		const int index = steps % cycle;
		const float offset = static_cast<float>(index) * step;
		return offset >= 0.99f ? 0.0f : offset;
	}
}

void Content::loadTy1Materials()
{
	if (ty1MaterialsReady || archives[0] == nullptr)
		return;

	ty1MaterialsReady = true;

	std::vector<char> data;
	const bool haveMad = archives[0]->getFileData("global.mad", data) && !data.empty();
	if (haveMad)
	{

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
		Content::Ty1UvAnim uvAnim = Content::Ty1UvAnim::None;
		float uvParam[6] = {};
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
		draw.uvAnim = block.uvAnim;
		for (int i = 0; i < 6; i++)
			draw.uvParam[i] = block.uvParam[i];
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
		else if (block.uvAnim == Content::Ty1UvAnim::None &&
			(key == "scroll" || key == "animate" || key == "rotate" || key == "sinrotate" || key == "envscroll"))
		{
			// The game refuses to combine these. The first one on the material wins.
			const std::vector<float> values = collectFloats(words);
			if (key == "envscroll")
			{
				if (values.empty())
				{
					block.uvParam[0] = 1.0f;
					block.uvParam[1] = -1.0f;
					block.uvParam[2] = 0.25f;
					block.uvParam[3] = 0.5f;
					block.uvParam[4] = 0.0f;
					block.uvParam[5] = 0.25f;
				}
				else
				{
					for (size_t i = 0; i < values.size() && i < 6; i++)
						block.uvParam[i] = values[i];
				}
				block.uvAnim = Content::Ty1UvAnim::EnvScroll;
			}
			else if (!values.empty())
			{
				for (size_t i = 0; i < values.size() && i < 6; i++)
					block.uvParam[i] = values[i];
				if (key == "scroll")
					block.uvAnim = Content::Ty1UvAnim::Scroll;
				else if (key == "animate")
					block.uvAnim = Content::Ty1UvAnim::Animate;
				else if (key == "rotate")
					block.uvAnim = Content::Ty1UvAnim::Rotate;
				else
					block.uvAnim = Content::Ty1UvAnim::SinRotate;
			}
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

	// ManuallyScrollTextures, once per 60 Hz logic tick. Stored as UV units per second.
	struct ManualScroll
	{
		const char* name;
		float duPerTick;
		float dvPerTick;
		bool lockU;
		float lockedU;
	};
	const ManualScroll manualScrolls[] =
	{
		{ "waterfall_01", 0.0f, 1.0f / 60.0f, false, 0.0f },
		{ "waterfall_overlay_03", 0.0f, 7.0f / 6000.0f, true, 0.5f },
		{ "ty_z1_001", 1.0f / 120.0f, 1.0f / 120.0f, false, 0.0f },
		{ "ty_z1_001b", 0.0f, 1.0f / 2400.0f, false, 0.0f },
		{ "ty_a1_022", 0.0f, -(1.0f / 300.0f), false, 0.0f },
		{ "ty_a1_022_overlay", 0.0f, -0.0050000004f, true, 0.5f },
		{ "ty_a1_env_004", 1.0f / 2400.0f, 1.0f / 6000.0f, false, 0.0f },
		{ "ty_a1_env_005", 3.0f / 8000.0f, 1.0f / 6000.0f, false, 0.0f },
		{ "ty_a1_env_006", 1.0f / 3000.0f, 1.0f / 6000.0f, false, 0.0f },
		{ "smoke", 0.0f, 1.0f / 1200.0f, true, 0.0f },
		{ "ty_a4_env_002", 0.0f, 1.0f / 2400.0f, false, 0.0f },
	};
	for (const ManualScroll& manual : manualScrolls)
	{
		const std::string key = manual.name;
		auto it = ty1Materials.find(key);
		if (it == ty1Materials.end())
			it = ty1Materials.emplace(key, Ty1MaterialDraw{}).first;
		Ty1MaterialDraw& draw = it->second;
		draw.manualScroll = true;
		draw.manualDu = manual.duPerTick * 60.0f;
		draw.manualDv = manual.dvPerTick * 60.0f;
		draw.lockU = manual.lockU;
		draw.lockedU = manual.lockedU;
	}
}

Content::Ty1MaterialDraw Content::lookupTy1Material(const std::string& materialName)
{
	loadTy1Materials();

	const auto it = ty1Materials.find(lowerCopy(materialName));
	if (it == ty1Materials.end())
		return {};
	return it->second;
}

bool Content::ty1UvClamped(const std::string& materialName) const
{
	const auto it = ty1Materials.find(lowerCopy(materialName));
	if (it == ty1Materials.end())
		return false;
	const Ty1MaterialDraw& draw = it->second;
	return draw.uvAnim != Ty1UvAnim::None || draw.manualScroll;
}

void Content::setTy1AnimClock(float timeSeconds, float yawRadians, float pitchRadians)
{
	ty1AnimTimeSeconds = timeSeconds;
	ty1AnimYawRadians = yawRadians;
	ty1AnimPitchRadians = pitchRadians;
}

glm::mat3 Content::ty1UvMatrix(const std::string& materialName, float timeSeconds, float yawRadians, float pitchRadians) const
{
	const auto it = ty1Materials.find(lowerCopy(materialName));
	if (it == ty1Materials.end())
		return glm::mat3(1.0f);

	const Ty1MaterialDraw& draw = it->second;
	if (draw.uvAnim == Ty1UvAnim::None && !draw.manualScroll)
		return glm::mat3(1.0f);

	// Row-vector texture matrix, matching Material::Update / GXLoadTexMtxImm.
	// u' = m00*u + m10*v + tu, v' = m01*u + m11*v + tv.
	float m00 = 1.0f;
	float m01 = 0.0f;
	float m10 = 0.0f;
	float m11 = 1.0f;
	float tu = 0.0f;
	float tv = 0.0f;

	const float p0 = draw.uvParam[0];
	const float p1 = draw.uvParam[1];
	const float p2 = draw.uvParam[2];
	const float p3 = draw.uvParam[3];
	const float p4 = draw.uvParam[4];
	const float p5 = draw.uvParam[5];

	if (draw.uvAnim == Ty1UvAnim::Scroll)
	{
		tu = wrapUnit(timeSeconds * p0);
		tv = wrapUnit(timeSeconds * p1);
	}
	else if (draw.uvAnim == Ty1UvAnim::Animate)
	{
		tu = steppedOffset(timeSeconds, p0, p2);
		tv = steppedOffset(timeSeconds, p1, p2);
	}
	else if (draw.uvAnim == Ty1UvAnim::Rotate || draw.uvAnim == Ty1UvAnim::SinRotate)
	{
		const float angle = timeSeconds * p2;
		float roll = angle;
		if (draw.uvAnim == Ty1UvAnim::SinRotate)
			roll = std::sin(angle) * (p3 * 3.14159265f / 180.0f);
		const float c = std::cos(roll);
		const float s = std::sin(roll);
		// Translate to the pivot, roll, translate back. Row-vector product.
		m00 = c;
		m10 = s;
		tu = p0 - p0 * c - p1 * s;
		m01 = -s;
		m11 = c;
		tv = p1 + p0 * s - p1 * c;
	}
	else if (draw.uvAnim == Ty1UvAnim::EnvScroll)
	{
		const float turns = 1.0f / 6.283185307f;
		m00 = p2;
		m11 = p3;
		tu = p4 + turns * yawRadians * p0;
		tv = p5 + turns * pitchRadians * p1;
	}

	if (draw.manualScroll)
	{
		if (draw.lockU)
			tu = draw.lockedU;
		tu = wrapUnit(tu + timeSeconds * draw.manualDu);
		tv = wrapUnit(tv + timeSeconds * draw.manualDv);
	}

	// Mesh UVs are stored as (u, 1 - gameV). Apply the game matrix in game UV
	// space, then flip V back so a positive game V scroll moves the other way.
	glm::mat3 uv(1.0f);
	uv[0][0] = m00;
	uv[1][0] = -m10;
	uv[2][0] = tu + m10;
	uv[0][1] = -m01;
	uv[1][1] = m11;
	uv[2][1] = 1.0f - tv - m11;
	return uv;
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
