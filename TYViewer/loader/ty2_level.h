#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "loader/bni.h"
#include "loader/ty1_level.h"

// One room mesh named by a levels.ini chunk entry.
struct Ty2RoomModel
{
	// overlay, ground, water, or dome.
	std::string role;
	// Archive stem, without .mdl. ground is the collision shell.
	std::string modelName;
	bool collision = false;
};

// The levels.ini entry for one *.lv3.bni. found is false for world.lv3.bni
// and any chunk the manifest does not name.
struct Ty2ChunkInfo
{
	bool found = false;
	std::string levelId;
	bool hub = false;
	std::vector<Ty2RoomModel> rooms;
};

// One playable map from levels.ini. title is empty when Translations.English.txt
// has no TEXT_LEVEL_ line for the id.
struct Ty2LevelEntry
{
	std::string id;
	std::string title;
};

// Every chunk of one level, and the room meshes those chunks name.
// Chunks of one level already share coordinates, so rooms and instances
// are placed with no extra offset.
struct Ty2LevelInfo
{
	bool found = false;
	std::string levelId;
	bool hub = false;
	// Chunk stems as written in levels.ini (ra1_chunk_01, not a filename).
	std::vector<std::string> chunks;
	std::vector<Ty2RoomModel> rooms;
};

// Match chunkFile (ra1_chunk_01.lv3.bni) to a chunk block. Room names come
// from that block: overlay and envDome are drawn, ground is collision,
// fftwater is the water mesh.
bool ty2DescribeChunk(const BniFile& levelsIni, const std::string& chunkFile, Ty2ChunkInfo& out);

// One entry per levels.ini level, in file order. englishText is
// Translations.English.txt. A TEXT_LEVEL_<id> line supplies the title.
// TEXT_LEVEL_M2=950 Explosive Cargo keeps the words after the =number.
std::vector<Ty2LevelEntry> ty2ListLevels(const BniFile& levelsIni, const std::string& englishText);

// levelId is the levels.ini id (a1), not a chunk filename.
bool ty2DescribeLevel(const BniFile& levelsIni, const std::string& levelId, Ty2LevelInfo& out);

// Title for levelId, or empty when the English file has no TEXT_LEVEL_ line.
std::string ty2LevelTitle(const std::string& englishText, const std::string& levelId);

// "a1" when title is empty, otherwise "a1  Outback Oasis".
std::string ty2LevelLabel(const std::string& levelId, const std::string& title);

// Type name to model stem, from the model field of each global.model.bni section.
// Keys are lowercase. A type with no model field is absent.
std::unordered_map<std::string, std::string> ty2ModelCatalog(const BniFile& globalModel);

// True when this placed section is a TY2 Connector. It is not a Ty1Kind.
bool ty2IsConnector(const Ty1Instance& instance);

// Placed props, plus every Connector. hasModel is true when the archive has
// that .mdl. A type with no mesh is left out, except Connector, which is a
// loading seam and has no mesh. Rotation is pitch, yaw, roll, the same order
// ty1InstanceMatrix applies.
std::vector<Ty1Instance> parseTy2Instances(
	const BniFile& chunk,
	const std::string& chunkFile,
	const std::unordered_map<std::string, std::string>& catalog,
	const std::function<bool(const std::string&)>& hasModel);
