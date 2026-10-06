#include "ty1_level.h"

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <unordered_set>

namespace
{
	std::string trimCopy(const std::string& value)
	{
		size_t start = 0;
		while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
			start++;
		size_t end = value.size();
		while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
			end--;
		return value.substr(start, end - start);
	}

	std::string lowerCopy(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
		{
			return static_cast<char>(std::tolower(c));
		});
		return value;
	}

	std::string upperCopy(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
		{
			return static_cast<char>(std::toupper(c));
		});
		return value;
	}

	std::string stripComment(const std::string& line)
	{
		const size_t comment = line.find("//");
		std::string value = (comment == std::string::npos) ? line : line.substr(0, comment);
		if (!value.empty() && value.back() == '\r')
			value.pop_back();
		return value;
	}

	std::string lettersOnly(const std::string& value)
	{
		std::string out;
		out.reserve(value.size());
		for (unsigned char c : value)
		{
			if (std::isalnum(c))
				out.push_back(static_cast<char>(std::tolower(c)));
		}
		return out;
	}

	// The game drops a leading "static" before it looks up the descriptor.
	std::string descriptorKey(const std::string& typeName)
	{
		std::string key = upperCopy(trimCopy(typeName));
		if (key.size() > 6 && key.compare(0, 6, "STATIC") == 0)
			key = key.substr(6);
		return key;
	}

	bool startsWith(const std::string& value, const char* prefix)
	{
		const size_t n = std::strlen(prefix);
		return value.size() >= n && value.compare(0, n, prefix) == 0;
	}

	// prop_0001_name / act_07_name / act1_11_name / item_01_name -> the name.
	std::string assetSuffix(const std::string& stem)
	{
		std::string lower = lowerCopy(stem);
		const char* prefixes[] = { "prop_", "act1_", "act_", "itemanim_", "item_" };
		for (const char* prefix : prefixes)
		{
			if (!startsWith(lower, prefix))
				continue;
			size_t i = std::strlen(prefix);
			if (i >= lower.size() || !std::isdigit(static_cast<unsigned char>(lower[i])))
				continue;
			while (i < lower.size() && std::isdigit(static_cast<unsigned char>(lower[i])))
				i++;
			if (i >= lower.size() || lower[i] != '_')
				continue;
			return lower.substr(i + 1);
		}
		return {};
	}

	bool isLodFile(const std::string& filename)
	{
		return lowerCopy(filename).find("lod") != std::string::npos;
	}

	bool isPrefixedAsset(const std::string& filename)
	{
		const std::string lower = lowerCopy(filename);
		return startsWith(lower, "prop") || startsWith(lower, "act") || startsWith(lower, "item");
	}

	std::string pickModelFile(const std::vector<std::string>& files)
	{
		std::vector<std::string> pool;
		pool.reserve(files.size());
		for (const std::string& file : files)
		{
			if (std::find(pool.begin(), pool.end(), file) == pool.end())
				pool.push_back(file);
		}

		std::vector<std::string> noLod;
		for (const std::string& file : pool)
		{
			if (!isLodFile(file))
				noLod.push_back(file);
		}
		if (!noLod.empty())
			pool = std::move(noLod);

		std::vector<std::string> prefixed;
		for (const std::string& file : pool)
		{
			if (isPrefixedAsset(file))
				prefixed.push_back(file);
		}
		if (prefixed.size() == 1)
			return prefixed[0];
		if (prefixed.size() > 1)
			return {};
		if (pool.size() == 1)
			return pool[0];
		return {};
	}

	// Gameplay markers. They stay in the object list and are not drawn.
	bool isLogicOnly(const std::string& key)
	{
		static const char* kNames[] =
		{
			"DIALOG", "TRIGGERBOX", "TRIGGERSPHERE", "SOUNDPROP",
			"SCRIPT", "PATH", "WATERVOLUME",
		};
		for (const char* name : kNames)
		{
			if (key == name)
				return true;
		}
		return key.size() >= 6 && key.compare(0, 6, "CAMERA") == 0;
	}

	// Names the catalog does not list. These are the descriptor strings the game
	// registers in code, plus a few props whose model stem is not the type name.
	const char* extraModel(const std::string& key)
	{
		struct Pair
		{
			const char* type;
			const char* model;
		};
		static const Pair kExtra[] =
		{
			{ "TY", "Act_01_ty" },
			{ "SIGNPOST", "Prop_0005_signpost" },
			{ "EXTRALIFE", "Prop_0534_Lifeup" },
			{ "TASIGNPOST", "Prop_0393_SignPost" },
			{ "DIRECTIONARROW", "prop_0124_arrow_01" },
			{ "DIRECTIONARROW2", "prop_0400_arrow02" },
			{ "CONSTRUCTIONSIGN", "prop_0080_construct_sign" },
			{ "PICTUREFRAME", "Prop_0590_PickupFrame" },
			{ "TORCH1", "prop_0011_torch" },
			{ "CRATE", "Prop_0001_WoodenCrate_01" },
			{ "B3CRATE", "Prop_0001_WoodenCrate_01_B3" },
			{ "INVISICRATE", "Prop_0345_InvisibleCrate" },
			{ "FRILLLIZARD", "Act_07_Frill" },
			{ "SWIMMINGCROC", "Act_02_croc" },
			{ "MUDCRAB", "act_03_muddie" },
			{ "RHINORUNNER", "Act_36_Rhino" },
			{ "RHINORUNNERGROUND", "Act_36_Rhino" },
			{ "TELEPORTER", "Prop_0365_WarpMushroom" },
			{ "BURNINGLOG", "prop_0372_FlameLog" },
			{ "BUNYIPELDER", "act_86_ElderBunyip" },
			{ "THUNDEGGCOLLECTOR", "Prop_0403_ThundEggGun" },
			{ "THUNDEREGGCOLLECTOR", "Prop_0403_ThundEggGun" },
			{ "AIRPLATFORM", "Prop_0099_Platform" },
			{ "FROG", "Act_34_Frog" },
			{ "WHIRLYWIND", "prop_0340_Whirlwind" },
			{ "REED1", "prop_0006_Reed_01" },
			{ "REED2", "Prop_0007_Reed_02" },
			{ "REED3", "prop_0008_Reed_03" },
			{ "SNOWROO", "Act_41_snowroos" },
			{ "WATERWHEEL", "prop_0388_Z1WaterWheel" },
			{ "ELEVATOR1", "Prop_0108_Elevator" },
			{ "FINISHLINE", "Prop_0088_FinishLine" },
			{ "TIMEATTACKRING", "Prop_0413_TimeAttackRing" },
			{ "RESTART", "Prop_0018_Thunderbox" },
		};
		for (const Pair& pair : kExtra)
		{
			if (key == pair.type)
				return pair.model;
		}
		return nullptr;
	}

	std::unordered_map<std::string, std::string> catalogFromGlobalModel(const std::string& text)
	{
		std::unordered_map<std::string, std::string> catalog;
		std::vector<std::string> body;
		bool inSection = false;

		auto flush = [&]()
		{
			if (!inSection)
				return;
			int equations = 0;
			std::vector<std::string> bare;
			for (const std::string& line : body)
			{
				if (line.find('=') != std::string::npos)
					equations++;
				else
					bare.push_back(line);
			}
			if (!bare.empty() && equations == 0)
			{
				for (const std::string& line : bare)
				{
					std::istringstream stream(line);
					std::string descr;
					std::string model;
					if (!(stream >> descr >> model))
						continue;
					if (!descr.empty() && descr[0] == '[')
						continue;
					// "BridgeFlat1 prop_0111_bridge_flat_01, instance"
					const size_t comma = model.find(',');
					if (comma != std::string::npos)
						model = model.substr(0, comma);
					if (model.empty())
						continue;
					catalog[upperCopy(descr)] = model;
				}
			}
			inSection = false;
			body.clear();
		};

		std::istringstream stream(text);
		std::string line;
		while (std::getline(stream, line))
		{
			std::string value = trimCopy(stripComment(line));
			if (value.size() >= 5 && lowerCopy(value.substr(0, 5)) == "name ")
			{
				flush();
				const std::string section = trimCopy(value.substr(5));
				inSection = !section.empty();
				body.clear();
				continue;
			}
			if (inSection && !value.empty())
				body.push_back(value);
		}
		flush();
		return catalog;
	}

	bool readVec3(const std::string& text, glm::vec3& out)
	{
		std::string cleaned = text;
		for (char& c : cleaned)
		{
			if (c == ',')
				c = ' ';
		}
		std::istringstream stream(cleaned);
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		if (!(stream >> x >> y >> z))
			return false;
		out = glm::vec3(x, y, z);
		return true;
	}

	bool isHiddenValue(std::string value)
	{
		value = lowerCopy(trimCopy(value));
		const size_t cut = value.find_first_of(", \t");
		if (cut != std::string::npos)
			value = value.substr(0, cut);
		return value == "0" || value == "false" || value == "no";
	}

	bool readFloat(const std::string& text, float& out)
	{
		std::istringstream stream(text);
		return static_cast<bool>(stream >> out);
	}

	bool isStackType(const std::string& key)
	{
		return key == "CRATE" || key == "B3CRATE" || key == "INVISICRATE";
	}

	// Level id from `a1.lv2` or `a1ex.lv2`: drop the path, extension, and a trailing "ex".
	std::string levelIdFromFile(const std::string& levelFile)
	{
		std::string name = levelFile;
		const size_t slash = name.find_last_of("/\\");
		if (slash != std::string::npos)
			name = name.substr(slash + 1);
		const size_t dot = name.find_last_of('.');
		if (dot != std::string::npos)
			name = name.substr(0, dot);
		name = lowerCopy(name);
		if (name.size() > 2 && name.compare(name.size() - 2, 2, "ex") == 0)
			name = name.substr(0, name.size() - 2);
		return name;
	}

	// Opal and thunder egg meshes for one level. The .lv2 type name is the same in
	// every world; the game picks the mesh from the level id. An id that is not in
	// the table keeps the fire pair. Rainbow thunder eggs stay the red mesh.
	struct CollectibleModels
	{
		const char* opal = "Prop_0270_FireOpal";
		const char* egg = "Prop_0084_ThunderEgg";
	};

	CollectibleModels collectibleModels(const std::string& levelFile)
	{
		CollectibleModels models;
		const std::string id = levelIdFromFile(levelFile);
		if (id.size() != 2 || id[1] < '1' || id[1] > '4')
			return models;

		const char zone = id[0];
		const char index = id[1];
		if (zone == 'z')
			models.opal = "Prop_0218_RainbowScale";
		else if (zone == 'b' || (zone == 'd' && index == '4'))
		{
			models.opal = "prop_0380_IceOpal";
			models.egg = "prop_0573_bluethunderegg";
		}
		else if (zone == 'c' || (zone == 'd' && index != '4'))
		{
			models.opal = "prop_0382_AirOpal";
			models.egg = "prop_0571_greenthunderegg";
		}
		else if (zone == 'e')
		{
			models.opal = "Prop_0381_EarthOpal";
			models.egg = "prop_0572_ylwthunderegg";
		}
		else if (zone != 'a')
			return CollectibleModels{};
		return models;
	}

	// `type = N,label` on CAGEDBILBY. Dad has no dad-named mesh; Act_04_Bilby is the remaining one.
	const char* cagedBilbyModel(const std::string& label)
	{
		if (label == "mum")
			return "Act_26_bilbymum";
		if (label == "girl")
			return "Act_50_Bilbygirl";
		if (label == "boy")
			return "Act_49_BilbyBoy";
		if (label == "grandma")
			return "Act_43_BilbyGrandma";
		return "Act_04_Bilby";
	}

	// `8,blank` or `610,Teleporter2`. `blank` and `none` are not names.
	struct ParsedId
	{
		int number = 0;
		std::string label;
		bool ok = false;
	};

	ParsedId parseIdValue(const std::string& rhs)
	{
		ParsedId parsed;
		std::string number = rhs;
		std::string label;
		const size_t comma = rhs.find(',');
		if (comma != std::string::npos)
		{
			number = trimCopy(rhs.substr(0, comma));
			label = trimCopy(rhs.substr(comma + 1));
		}
		size_t i = 0;
		while (i < number.size() && std::isspace(static_cast<unsigned char>(number[i])))
			i++;
		if (i >= number.size())
			return parsed;
		int sign = 1;
		if (number[i] == '-')
		{
			sign = -1;
			i++;
		}
		if (i >= number.size() || !std::isdigit(static_cast<unsigned char>(number[i])))
			return parsed;
		int value = 0;
		while (i < number.size() && std::isdigit(static_cast<unsigned char>(number[i])))
		{
			value = value * 10 + (number[i] - '0');
			i++;
		}
		parsed.ok = true;
		parsed.number = sign * value;
		const std::string lower = lowerCopy(label);
		if (!label.empty() && lower != "blank" && lower != "none")
			parsed.label = label;
		return parsed;
	}
}

Ty1LevelFiles ty1LevelFiles(const std::string& levelFile)
{
	Ty1LevelFiles files;
	const std::string id = levelIdFromFile(levelFile);
	if (id.size() != 2 || id[1] < '1' || id[1] > '4')
		return files;
	const char zone = id[0];
	if (zone < 'a' || (zone > 'e' && zone != 'z'))
		return files;
	files.baseFile = id + ".lv2";
	files.companionFile = id + "ex.lv2";
	return files;
}

glm::mat4 ty1InstanceMatrix(const Ty1Instance& instance, const glm::mat4* cameraView)
{
	// Transpose of (scale * Rx * Ry * Rz) with translation in the last row.
	glm::mat4 matrix(1.0f);
	glm::vec3 place = instance.position;
	place.y += instance.drawLiftY;
	matrix = glm::translate(matrix, place);
	if (instance.billboard && cameraView != nullptr)
	{
		// Inverse of the view rotation is camera right, up, and back in the
		// Z-negated space Camera stores. Flip each axis Z into prop world space.
		const glm::mat3 facing = glm::transpose(glm::mat3(*cameraView));
		glm::mat4 rotation(1.0f);
		rotation[0] = glm::vec4(facing[0].x, facing[0].y, -facing[0].z, 0.0f);
		rotation[1] = glm::vec4(facing[1].x, facing[1].y, -facing[1].z, 0.0f);
		rotation[2] = glm::vec4(facing[2].x, facing[2].y, -facing[2].z, 0.0f);
		matrix *= rotation;
	}
	else
	{
		matrix = glm::rotate(matrix, -instance.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
		matrix = glm::rotate(matrix, -instance.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
		matrix = glm::rotate(matrix, -instance.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
	}
	matrix = glm::scale(matrix, instance.scale);
	return matrix;
}

std::vector<Ty1Instance> parseTy1Instances(
	const std::string& levelText,
	const std::string& globalModelText,
	const std::vector<std::string>& mdlFiles,
	const std::string& levelFile)
{
	const CollectibleModels collectibles = collectibleModels(levelFile);
	const std::unordered_map<std::string, std::string> catalog = catalogFromGlobalModel(globalModelText);

	std::unordered_map<std::string, std::string> canonical;
	std::unordered_map<std::string, std::vector<std::string>> byLetters;
	for (const std::string& file : mdlFiles)
	{
		canonical[lowerCopy(file)] = file;
		const size_t dot = file.find_last_of('.');
		const std::string stem = (dot == std::string::npos) ? file : file.substr(0, dot);
		byLetters[lettersOnly(stem)].push_back(file);
		const std::string suffix = assetSuffix(stem);
		if (!suffix.empty())
			byLetters[lettersOnly(suffix)].push_back(file);
	}

	auto canonicalFile = [&](const std::string& token) -> std::string
	{
		if (token.empty())
			return {};
		std::string file = token;
		if (lowerCopy(file).size() < 4 || lowerCopy(file).compare(lowerCopy(file).size() - 4, 4, ".mdl") != 0)
			file += ".mdl";
		const auto it = canonical.find(lowerCopy(file));
		if (it == canonical.end())
			return {};
		return it->second;
	};

	auto modelForType = [&](const std::string& typeName) -> std::string
	{
		const std::string key = descriptorKey(typeName);
		if (key.empty() || isLogicOnly(key))
			return {};

		const auto catalogIt = catalog.find(key);
		if (catalogIt != catalog.end())
			return canonicalFile(catalogIt->second);

		if (const char* extra = extraModel(key))
		{
			const std::string file = canonicalFile(extra);
			if (!file.empty())
				return file;
		}

		const auto stemIt = byLetters.find(lettersOnly(key));
		if (stemIt == byLetters.end())
			return {};
		return pickModelFile(stemIt->second);
	};

	std::vector<Ty1Instance> instances;
	std::string type;
	bool setup = false;
	Ty1Instance current;
	bool open = false;
	bool hasPos = false;
	bool hasRot = false;
	bool hasScale = false;
	bool hasVisible = false;
	bool triggerSphere = false;
	bool soundSphere = false;
	bool spherePosSet = false;
	float pendingRange = 0.0f;
	enum class Nest { None, Box, Sphere, Sound, Waypoints };
	Nest nest = Nest::None;

	auto flush = [&]()
	{
		if (open && hasPos && !setup)
		{
			const std::string key = descriptorKey(type);
			if (pendingRange > 0.0f && !triggerSphere && !soundSphere)
			{
				current.hasSphere = true;
				current.rangeSphere = true;
				current.spherePosition = current.position;
				current.sphereRadius = pendingRange;
			}
			if (current.hasSphere && !spherePosSet)
				current.spherePosition = current.position;
			if (isStackType(key))
				current.waypoints.clear();

			current.typeName = type;
			current.modelFile = modelForType(type);
			if (key == "OPAL")
			{
				current.modelFile = canonicalFile(collectibles.opal);
				current.billboard = true;
			}
			else if (key == "THUNDEREGG")
				current.modelFile = canonicalFile(collectibles.egg);
			else if (key == "PORTAL")
			{
				current.billboard = true;
				current.seatBottom = true;
			}
			if (key == "CAGEDBILBY")
			{
				current.modelFile = canonicalFile(cagedBilbyModel(current.variantLabel));
				current.extraModelFile = canonicalFile("Prop_0044_cage");
			}
			else if (key == "RESTART")
			{
				current.extraModelFile = canonicalFile("Prop_0092_DunnyRoll");
				// CheckpointStruct::Dormant activates when Ty is inside this sphere.
				current.hasSphere = true;
				current.spherePosition = current.position;
				current.sphereRadius = 500.0f;
			}
			current.closePath = key == "PATH";
			// CritterField2 reads `scale` as the field size even when it is small
			// (a2 has a gecko field of scale 1), and the range critters (turtle,
			// soldier crab) and the water dragon have no scale at all.
			if (ty1IsCritterType(type))
			{
				current.critter = true;
				if (hasScale && current.roamSize == glm::vec3(0.0f))
				{
					current.roamSize = glm::abs(current.scale);
					current.scale = glm::vec3(1.0f);
				}
			}
			if (key == "WATERVOLUME")
				current.kind = Ty1Kind::Water;
			else if (current.hasBox || triggerSphere)
				current.kind = Ty1Kind::Trigger;
			else if (soundSphere)
				current.kind = Ty1Kind::Sound;
			else if (current.critter)
				current.kind = Ty1Kind::Critter;
			else if (!current.waypoints.empty())
				current.kind = Ty1Kind::Patrol;
			else
				current.kind = Ty1Kind::Prop;
			instances.push_back(current);
		}
		open = false;
		hasPos = false;
		hasRot = false;
		hasScale = false;
		hasVisible = false;
		triggerSphere = false;
		soundSphere = false;
		spherePosSet = false;
		pendingRange = 0.0f;
		nest = Nest::None;
		current = {};
	};

	std::istringstream stream(levelText);
	std::string line;
	while (std::getline(stream, line))
	{
		std::string raw = stripComment(line);
		if (!raw.empty() && raw.back() == '\r')
			raw.pop_back();
		const bool indented = !raw.empty() && std::isspace(static_cast<unsigned char>(raw[0]));
		const std::string value = trimCopy(raw);
		if (value.size() >= 5 && lowerCopy(value.substr(0, 5)) == "name ")
		{
			flush();
			type = trimCopy(value.substr(5));
			setup = lowerCopy(type) == "setup";
			continue;
		}
		if (value.empty())
		{
			flush();
			continue;
		}
		if (setup || type.empty())
			continue;

		const size_t eq = value.find('=');
		if (eq == std::string::npos)
			continue;

		const std::string keyText = trimCopy(value.substr(0, eq));
		const std::string key = lowerCopy(keyText);
		const std::string rhs = trimCopy(value.substr(eq + 1));
		if (!open)
		{
			open = true;
			current.scale = glm::vec3(1.0f);
			current.visible = true;
		}

		Ty1Field field;
		field.key = keyText;
		field.value = rhs;
		if (indented)
		{
			if (!current.fields.empty())
				current.fields.back().children.push_back(field);
		}
		else
			current.fields.push_back(field);

		if (!indented && key == "id" && current.objectId < 0)
		{
			const ParsedId parsed = parseIdValue(rhs);
			if (parsed.ok)
			{
				current.objectId = parsed.number;
				current.objectLabel = parsed.label;
			}
		}

		if (indented)
		{
			if (nest == Nest::Box)
			{
				if (key == "pos")
					readVec3(rhs, current.boxPosition);
				else if (key == "yaw")
					readFloat(rhs, current.boxYaw);
				else if (key == "pitch")
					readFloat(rhs, current.boxPitch);
				else if (key == "width")
					readFloat(rhs, current.boxSize.x);
				else if (key == "height")
					readFloat(rhs, current.boxSize.y);
				else if (key == "depth")
					readFloat(rhs, current.boxSize.z);
			}
			else if (nest == Nest::Sphere || nest == Nest::Sound)
			{
				if (key == "pos")
					spherePosSet = readVec3(rhs, current.spherePosition);
				else if (key == "radius")
					readFloat(rhs, current.sphereRadius);
			}
			else if (nest == Nest::Waypoints && key == "waypoint" && !isStackType(descriptorKey(type)))
			{
				glm::vec3 point(0.0f);
				if (readVec3(rhs, point))
					current.waypoints.push_back(point);
			}
			continue;
		}

		nest = Nest::None;
		if (key == "box")
		{
			nest = Nest::Box;
			current.hasBox = true;
		}
		else if (key == "sphere")
		{
			nest = Nest::Sphere;
			current.hasSphere = true;
			triggerSphere = true;
		}
		else if (key == "dummysphere")
		{
			nest = Nest::Sound;
			current.hasSphere = true;
			current.soundSphere = true;
			soundSphere = true;
		}
		else if (key == "waypoints")
		{
			nest = Nest::Waypoints;
		}
		else if ((key == "range" || key == "radius") && pendingRange <= 0.0f)
		{
			readFloat(rhs, pendingRange);
		}
		else if (key == "type" && descriptorKey(type) == "CAGEDBILBY")
		{
			std::string label = rhs;
			const size_t comma = label.find(',');
			if (comma != std::string::npos)
				label = trimCopy(label.substr(comma + 1));
			else
				label.clear();
			current.variantLabel = lowerCopy(label);
		}
		else if (key == "pathwidth")
		{
			float width = 0.0f;
			if (readFloat(rhs, width) && width > 0.0f)
				current.pathWidth = width;
		}
		else if (key == "pos" && !hasPos)
		{
			hasPos = readVec3(rhs, current.position);
		}
		else if (key == "rot" && !hasRot)
		{
			hasRot = readVec3(rhs, current.rotation);
		}
		else if (key == "yaw" && !hasRot)
		{
			// Teleporters face with `Yaw` and never write `rot`. Same slot as rot's yaw.
			float yaw = 0.0f;
			if (readFloat(rhs, yaw))
				current.rotation.y = yaw;
		}
		else if (key == "scale" && !hasScale)
		{
			glm::vec3 parsed(1.0f);
			if (!readVec3(rhs, parsed))
				continue;
			hasScale = true;
			// Placed props stay near 1 (rocks reach about 17). A larger scale is the
			// box a flock roams in, or the size of a water volume.
			const float largest = std::max(std::abs(parsed.x), std::max(std::abs(parsed.y), std::abs(parsed.z)));
			const glm::vec3 full(std::abs(parsed.x), std::abs(parsed.y), std::abs(parsed.z));
			const std::string typeKey = descriptorKey(type);
			if (largest <= 64.0f)
				current.scale = parsed;
			else if (typeKey == "WATERVOLUME")
				current.roamSize = full;
			else if (!isLogicOnly(typeKey))
			{
				current.critter = true;
				current.roamSize = full;
			}
		}
		else if (key == "bvisible" && !hasVisible)
		{
			hasVisible = true;
			current.visible = !isHiddenValue(rhs);
		}
	}
	flush();
	return instances;
}

bool ty1IsCritterType(const std::string& typeName)
{
	static const char* kNames[] =
	{
		"BIRDFLOCK", "BUTTERFLY", "CLOWNFISH", "COW", "CUTTLEFISH", "DRAGONFLY",
		"FIREFLY", "FISHSHOAL", "FLY", "FROG", "GECKO", "GRASSHOPPER", "GUPPY",
		"IBIS", "KINGFISHER", "KOOKABURRA", "LORIKEET", "MOTH", "SEAGULL",
		"SEAHORSE", "SHEEP", "SOLDIERCRAB", "SYNKERFROG", "TURTLE", "WALLABY",
		"WATERDRAGON", "WATERSKIMMER",
	};
	const std::string key = descriptorKey(typeName);
	for (const char* name : kNames)
	{
		if (key == name)
			return true;
	}
	return false;
}

const char* ty1KindName(const Ty1Instance& instance)
{
	switch (instance.kind)
	{
	case Ty1Kind::Critter: return "critter";
	case Ty1Kind::Water: return "water";
	case Ty1Kind::Trigger: return "trigger";
	case Ty1Kind::Sound: return "sound";
	case Ty1Kind::Patrol: return "patrol";
	case Ty1Kind::Prop: break;
	}
	if (instance.rangeSphere)
		return "range";
	return "prop";
}

std::unordered_map<int, int> ty1IdIndex(const std::vector<Ty1Instance>& instances)
{
	std::unordered_map<int, int> index;
	for (int i = 0; i < static_cast<int>(instances.size()); i++)
	{
		const int id = instances[static_cast<size_t>(i)].objectId;
		if (id > 0)
			index.emplace(id, i);
	}
	return index;
}

std::vector<Ty1InfoLine> describeTy1Instance(
	const std::vector<Ty1Instance>& instances,
	int index,
	const std::unordered_map<int, int>& idToIndex)
{
	std::vector<Ty1InfoLine> lines;
	if (index < 0 || index >= static_cast<int>(instances.size()))
		return lines;

	const Ty1Instance& instance = instances[static_cast<size_t>(index)];

	auto childNamed = [](const Ty1Field& field, const char* name) -> const Ty1Field*
	{
		for (const Ty1Field& child : field.children)
		{
			if (lowerCopy(child.key) == name)
				return &child;
		}
		return nullptr;
	};

	auto push = [&](std::string text, int link, bool dim, bool heading, int indent)
	{
		Ty1InfoLine line;
		line.text = std::move(text);
		line.link = link;
		line.dim = dim;
		line.heading = heading;
		line.indent = indent;
		lines.push_back(std::move(line));
	};

	push(instance.typeName.empty() ? "object" : instance.typeName, -1, false, true, 0);
	if (!instance.sourceFile.empty())
		push(instance.sourceFile, -1, false, false, 0);
	push(ty1KindName(instance), -1, false, false, 0);
	if (instance.modelFile.empty() && instance.extraModelFile.empty())
		push("no model", -1, true, false, 0);
	else
	{
		std::string model = instance.modelFile;
		if (!instance.extraModelFile.empty())
		{
			if (!model.empty())
				model += "  ";
			model += instance.extraModelFile;
		}
		push(model, -1, false, false, 0);
	}
	if (instance.objectId >= 0)
	{
		std::string idText = "ID " + std::to_string(instance.objectId);
		if (!instance.objectLabel.empty())
			idText += " " + instance.objectLabel;
		push(std::move(idText), -1, false, false, 0);
	}

	for (const Ty1Field& field : instance.fields)
	{
		if (lowerCopy(field.value) == "event")
		{
			const Ty1Field* target = childNamed(field, "targetid");
			const Ty1Field* message = childNamed(field, "message");
			const ParsedId targetId = target ? parseIdValue(target->value) : ParsedId{};
			const bool empty = !targetId.ok || targetId.number == 0;
			if (empty)
			{
				push(field.key + "  none", -1, true, false, 0);
			}
			else
			{
				std::string messageText = "none";
				if (message && !message->value.empty() && lowerCopy(message->value) != "none")
					messageText = message->value;
				std::string dest = targetId.label.empty()
					? ("(" + std::to_string(targetId.number) + ")")
					: (targetId.label + " (" + std::to_string(targetId.number) + ")");
				int link = -1;
				std::string targetType;
				const auto found = idToIndex.find(targetId.number);
				if (found != idToIndex.end()
					&& found->second >= 0
					&& found->second < static_cast<int>(instances.size()))
				{
					link = found->second;
					targetType = instances[static_cast<size_t>(link)].typeName;
				}
				std::string text = field.key + "  " + messageText + " -> " + dest;
				if (!targetType.empty())
					text += "  " + targetType;
				push(std::move(text), link, false, false, 0);
			}
			for (const Ty1Field& child : field.children)
			{
				const std::string childKey = lowerCopy(child.key);
				if (childKey == "targetid" || childKey == "message")
					continue;
				push(child.key + " = " + child.value, -1, false, false, 1);
			}
			continue;
		}

		push(field.key + " = " + field.value, -1, false, false, 0);
		for (const Ty1Field& child : field.children)
			push(child.key + " = " + child.value, -1, false, false, 1);
	}

	if (instance.objectId > 0)
	{
		std::vector<Ty1InfoLine> hits;
		for (int i = 0; i < static_cast<int>(instances.size()); i++)
		{
			const Ty1Instance& other = instances[static_cast<size_t>(i)];
			for (const Ty1Field& field : other.fields)
			{
				if (lowerCopy(field.value) != "event")
					continue;
				const Ty1Field* target = childNamed(field, "targetid");
				if (target == nullptr)
					continue;
				const ParsedId targetId = parseIdValue(target->value);
				if (!targetId.ok || targetId.number != instance.objectId)
					continue;
				const Ty1Field* message = childNamed(field, "message");
				std::string messageText = "none";
				if (message && !message->value.empty() && lowerCopy(message->value) != "none")
					messageText = message->value;
				std::string who = other.typeName.empty() ? "object" : other.typeName;
				if (!other.objectLabel.empty())
					who += " " + other.objectLabel;
				else if (other.objectId > 0)
					who += " " + std::to_string(other.objectId);
				Ty1InfoLine hit;
				hit.text = who + "  " + field.key + "  " + messageText;
				hit.link = i;
				hits.push_back(std::move(hit));
			}
		}
		push("Targeted by", -1, false, true, 0);
		if (hits.empty())
			push("none", -1, true, false, 0);
		else
			lines.insert(lines.end(), hits.begin(), hits.end());
	}

	return lines;
}
