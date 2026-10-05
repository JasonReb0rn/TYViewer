#include "content.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

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
		ty1GrassTypesReady = false;
		ty1GrassTypeList.clear();
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

	// Water_Update's RandomFR. The seed is the IEEE bits of a coordinate hash.
	float randomFR(std::uint32_t seedBits, float minValue, float maxValue)
	{
		const std::uint32_t curr = seedBits * 0x343FDu + 0x269EC3u;
		const std::uint32_t bits = ((curr >> 8) & 0x7FFFFFu) | 0x3F800000u;
		float unit = 0.0f;
		std::memcpy(&unit, &bits, sizeof(unit));
		unit -= 1.0f;
		return unit * (maxValue - minValue) + minValue;
	}

	float waterPhase(float texelX, float texelY, bool second)
	{
		const float f16 = texelY + 57.0f;
		const float f24 = texelX + 30.0f;
		const float hash = second
			? (0.05f + f24) * (1.2f + f24) * (2.15f + f16) * (1.2f + f16)
			: (f24 * f16) * f16 * (0.2f + f24) * (0.8f + f16);
		std::uint32_t seedBits = 0;
		std::memcpy(&seedBits, &hash, sizeof(seedBits));
		return randomFR(seedBits, 0.0f, 6.28318530718f);
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
		int grassIndex = -1;
		bool masked = false;
		float alphaRef = -1.0f;
		Content::Ty1UvAnim uvAnim = Content::Ty1UvAnim::None;
		float uvParam[6] = {};
		bool clampU = false;
		bool clampV = false;
		bool indirectWater = false;
		bool waterCameraUv = false;
		bool waterWorldUv = false;
		float waterX = 25.0f;
		float waterY = 50.0f;
		float waterZ = 0.005f;
		float waterW = 0.005f;
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
		draw.grassIndex = block.grassIndex;
		draw.masked = block.masked;
		draw.uvAnim = block.uvAnim;
		for (int i = 0; i < 6; i++)
			draw.uvParam[i] = block.uvParam[i];
		draw.clampU = block.clampU;
		draw.clampV = block.clampV;
		draw.indirectWater = block.indirectWater;
		draw.waterCameraUv = block.waterCameraUv;
		draw.waterWorldUv = block.waterWorldUv;
		draw.waterX = block.waterX;
		draw.waterY = block.waterY;
		draw.waterZ = block.waterZ;
		draw.waterW = block.waterW;
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
			// Order matches Material::InitFromMatDefs: flag, flag, z, w, x, y.
			// A short line keeps the game defaults already stored on the block.
			block.depthWrite = false;
			block.indirectWater = true;
			const std::vector<float> values = collectFloats(words);
			if (values.size() > 0)
				block.waterCameraUv = values[0] != 0.0f;
			if (values.size() > 1)
				block.waterWorldUv = values[1] != 0.0f;
			if (values.size() > 2)
				block.waterZ = values[2];
			if (values.size() > 3)
				block.waterW = values[3];
			if (values.size() > 4)
				block.waterX = values[4];
			if (values.size() > 5)
				block.waterY = values[5];
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
			// Material::InitFromMatDefs reads the name, then the int after it.
			std::string rest;
			for (size_t i = 1; i < words.size(); i++)
				rest += lowerCopy(words[i]) + " ";
			const size_t at = rest.find("grass");
			if (at != std::string::npos)
			{
				block.grassEffect = true;
				size_t pos = at + 5;
				while (pos < rest.size() && (rest[pos] == ',' || rest[pos] == ' ' || rest[pos] == '='))
					pos++;
				int index = 0;
				bool any = false;
				while (pos < rest.size() && std::isdigit(static_cast<unsigned char>(rest[pos])))
				{
					index = index * 10 + (rest[pos] - '0');
					any = true;
					pos++;
				}
				block.grassIndex = any ? index : 0;
			}
		}
		else if (key == "masked" && words.size() >= 2)
		{
			int enabled = 1;
			if (!parseIntWord(words[1], enabled))
				return;
			block.masked = enabled != 0;
		}
		else if (key == "clampuv" || key == "address")
		{
			// clampUV sets each axis. address uses its first value for both.
			std::vector<int> values;
			for (size_t i = 1; i < words.size(); i++)
			{
				std::string token = words[i];
				if (!token.empty() && token.back() == ',')
					token.pop_back();
				if (token.empty() || token == "=")
					continue;
				int value = 0;
				if (!parseIntWord(token, value))
					continue;
				values.push_back(value);
			}
			if (values.empty())
				return;
			if (key == "address")
			{
				block.clampU = values[0] != 0;
				block.clampV = values[0] != 0;
			}
			else
			{
				block.clampU = values[0] != 0;
				if (values.size() > 1)
					block.clampV = values[1] != 0;
			}
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

	// ManuallyScrollTextures, once per 60 Hz logic tick. Stored as UV per second of that tick.
	// Playback is 30 Hz because the animation clock runs at half real time.
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

const std::vector<Content::Ty1GrassType>& Content::ty1GrassTypes()
{
	if (ty1GrassTypesReady || archives[0] == nullptr)
		return ty1GrassTypeList;
	ty1GrassTypesReady = true;

	std::vector<char> data;
	if (!archives[0]->getFileData("grass_types.ini", data) || data.empty())
	{
		Debug::log("grass_types.ini missing; TY1 grass is skipped");
		return ty1GrassTypeList;
	}

	// GrassInfoGC is memset to 0, so a section without density emits no blades.
	struct Raw
	{
		std::string name;
		int clumpSize = 0;
		int density = 0;
		float minHeight = 0.0f;
		float maxHeight = 0.0f;
		float width = 0.0f;
		float upVector = 0.0f;
		int numTextures = 0;
		float maxVisibleRadius = 0.0f;
		std::string material;
	};
	std::vector<Raw> raws;

	auto trim = [](std::string value)
	{
		size_t start = 0;
		while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
			start++;
		size_t end = value.size();
		while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
			end--;
		return value.substr(start, end - start);
	};

	std::string text(data.begin(), data.end());
	size_t pos = 0;
	while (pos <= text.size())
	{
		size_t eol = text.find('\n', pos);
		if (eol == std::string::npos)
			eol = text.size();
		std::string line = text.substr(pos, eol - pos);
		pos = eol + 1;

		const size_t comment = line.find("//");
		if (comment != std::string::npos)
			line.erase(comment);
		line = trim(line);
		if (line.empty())
			continue;

		if (line[0] == '[')
		{
			Raw raw;
			const size_t close = line.find(']');
			raw.name = line.substr(1, close == std::string::npos ? std::string::npos : close - 1);
			raws.push_back(raw);
			continue;
		}
		if (raws.empty())
			continue;

		const size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		const std::string key = lowerCopy(trim(line.substr(0, eq)));
		const std::string value = trim(line.substr(eq + 1));
		const float number = std::strtof(value.c_str(), nullptr);
		const int whole = static_cast<int>(std::strtol(value.c_str(), nullptr, 10));

		Raw& raw = raws.back();
		if (key == "material")
			raw.material = value;
		else if (key == "clumpsize")
			raw.clumpSize = whole;
		else if (key == "density")
			raw.density = whole;
		else if (key == "minheight")
			raw.minHeight = number;
		else if (key == "maxheight")
			raw.maxHeight = number;
		else if (key == "width")
			raw.width = number;
		else if (key == "upvector")
			raw.upVector = number;
		else if (key == "numtextures")
			raw.numTextures = whole;
		else if (key == "maxvisibleradius")
			raw.maxVisibleRadius = number;
	}

	for (const Raw& raw : raws)
	{
		Ty1GrassType type;
		type.name = raw.name;
		type.bladesPerTriangle = std::min(raw.clumpSize * raw.density, 32);
		type.minHeight = raw.minHeight * 100.0f;
		type.maxHeight = raw.maxHeight * 100.0f;
		type.width = raw.width * 100.0f;
		type.upVector = raw.upVector;
		type.numTextures = raw.numTextures;
		type.maxVisibleRadius = raw.maxVisibleRadius * 100.0f;
		type.material = raw.material;
		ty1GrassTypeList.push_back(type);
	}
	Debug::log("grass_types.ini: " + std::to_string(ty1GrassTypeList.size()) + " grass types");
	return ty1GrassTypeList;
}

const Content::Ty1MaterialDraw* Content::findTy1MaterialLower(const std::string& lowerMaterialName) const
{
	const auto it = ty1Materials.find(lowerMaterialName);
	return (it == ty1Materials.end()) ? nullptr : &it->second;
}

void Content::ty1UvWrap(const std::string& materialName, bool& clampU, bool& clampV) const
{
	ty1UvWrapFor(findTy1MaterialLower(lowerCopy(materialName)), clampU, clampV);
}

void Content::ty1UvWrapFor(const Ty1MaterialDraw* draw, bool& clampU, bool& clampV) const
{
	clampU = false;
	clampV = false;

	if (draw == nullptr)
		return;

	// Static materials keep the repeat they had before the clamp experiment.
	// Animated ones clamp only on the axes global.mad names.
	if (draw->uvAnim == Ty1UvAnim::None && !draw->manualScroll)
		return;

	clampU = draw->clampU;
	clampV = draw->clampV;
}

bool Content::ty1IndirectWater(const std::string& materialName, glm::vec4& scale) const
{
	return ty1IndirectWaterFor(findTy1MaterialLower(lowerCopy(materialName)), scale);
}

bool Content::ty1IndirectWaterFor(const Ty1MaterialDraw* draw, glm::vec4& scale) const
{
	if (draw == nullptr || !draw->indirectWater)
		return false;

	scale = glm::vec4(draw->waterX, draw->waterY, draw->waterZ, draw->waterW);
	return true;
}

void Content::updateTy1WaterRipple()
{
	if (!waterPhasesReady)
	{
		// i is the texel row and j is the column, matching Water_Update's walk.
		for (int y = 0; y < 16; y++)
		{
			for (int x = 0; x < 16; x++)
			{
				const int index = y * 16 + x;
				waterPhaseA[index] = waterPhase(static_cast<float>(x), static_cast<float>(y), false);
				waterPhaseB[index] = waterPhase(static_cast<float>(x), static_cast<float>(y), true);
			}
		}
		waterPhasesReady = true;
	}

	// 0.1 rad per 60 Hz tick. The clock is already half real time, so this is one step per 30 Hz frame.
	const float angle = std::fmod(ty1AnimTimeSeconds * 6.0f, 6.28318530718f);
	unsigned char pixels[16 * 16 * 4];
	for (int i = 0; i < 256; i++)
	{
		int dat0 = static_cast<int>(128.0f + 127.0f * std::sin(waterPhaseA[i] + angle));
		int dat1 = static_cast<int>(128.0f + 127.0f * std::sin(waterPhaseB[i] + angle));
		if (dat0 < 0)
			dat0 = 0;
		if (dat0 > 255)
			dat0 = 255;
		if (dat1 < 0)
			dat1 = 0;
		if (dat1 > 255)
			dat1 = 255;
		// Indirect unit reads alpha, blue, green. Red and green stay 0.
		unsigned char* pixel = pixels + i * 4;
		pixel[0] = 0;
		pixel[1] = 0;
		pixel[2] = static_cast<unsigned char>(dat1);
		pixel[3] = static_cast<unsigned char>(dat0);
	}

	if (waterRipple == nullptr)
		waterRipple = Texture::createRGBA(16, 16, pixels);
	else
		waterRipple->updateRGBA(pixels);
}

void Content::setTy1AnimClock(float timeSeconds, float yawRadians, float pitchRadians)
{
	ty1AnimTimeSeconds = timeSeconds;
	ty1AnimYawRadians = yawRadians;
	ty1AnimPitchRadians = pitchRadians;
}

glm::mat3 Content::ty1UvMatrix(const std::string& materialName, float timeSeconds, float yawRadians, float pitchRadians) const
{
	return ty1UvMatrixFor(findTy1MaterialLower(lowerCopy(materialName)), timeSeconds, yawRadians, pitchRadians);
}

glm::mat3 Content::ty1UvMatrixFor(const Ty1MaterialDraw* drawPtr, float timeSeconds, float yawRadians, float pitchRadians) const
{
	if (drawPtr == nullptr)
		return glm::mat3(1.0f);

	const Ty1MaterialDraw& draw = *drawPtr;
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
