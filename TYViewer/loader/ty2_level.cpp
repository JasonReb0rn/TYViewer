#include "ty2_level.h"

#include <cctype>
#include <cstdio>
#include <sstream>

namespace
{
	std::string lowerCopy(std::string value)
	{
		for (char& c : value)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return value;
	}

	std::string joinValues(const std::vector<std::string>& values)
	{
		std::string out;
		for (size_t i = 0; i < values.size(); i++)
		{
			if (i != 0)
				out += ", ";
			out += values[i];
		}
		return out;
	}

	// "ra1_chunk_01.lv3.bni" and "Ra1_Chunk_01" both become "ra1_chunk_01".
	std::string chunkStem(std::string name)
	{
		const size_t slash = name.find_last_of("/\\");
		if (slash != std::string::npos)
			name = name.substr(slash + 1);
		const size_t dot = name.find('.');
		if (dot != std::string::npos)
			name = name.substr(0, dot);
		return lowerCopy(name);
	}

	// Level id shared by every chunk of one map: the text before "_chunk".
	std::string chunkLevelKey(const std::string& name)
	{
		std::string stem = chunkStem(name);
		const size_t cut = stem.find("_chunk");
		if (cut != std::string::npos)
			stem = stem.substr(0, cut);
		return stem;
	}

	bool sameText(const std::string& a, const std::string& b)
	{
		return lowerCopy(a) == lowerCopy(b);
	}

	bool isStructuralSection(const std::string& section)
	{
		const std::string key = lowerCopy(section);
		return key == "header" || key == "bounding_region" || key == "swap_group_info"
			|| key == "graphics_effects";
	}

	std::vector<Ty1Field> readFields(const std::vector<BniLine>& lines, int start, int depth)
	{
		std::vector<Ty1Field> fields;
		if (depth > 6)
			return fields;
		for (int i = start; i >= 0 && i < static_cast<int>(lines.size()); i++)
		{
			const BniLine& line = lines[static_cast<size_t>(i)];
			if (line.valueCount < 0)
				break;
			Ty1Field field;
			field.key = !line.field.empty() ? line.field : line.section;
			field.value = joinValues(line.values);
			if (line.subline >= 0)
				field.children = readFields(lines, line.subline, depth + 1);
			if (!field.key.empty() || !field.value.empty())
				fields.push_back(std::move(field));
		}
		return fields;
	}

	const Ty1Field* findField(const std::vector<Ty1Field>& fields, const char* key)
	{
		for (const Ty1Field& field : fields)
		{
			if (sameText(field.key, key))
				return &field;
		}
		return nullptr;
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

	bool isTrueText(const std::string& text)
	{
		std::string cleaned = lowerCopy(text);
		for (char& c : cleaned)
		{
			if (c == ',')
				c = ' ';
		}
		std::istringstream stream(cleaned);
		std::string token;
		while (stream >> token)
		{
			if (token == "1" || token == "true" || token == "yes")
				return true;
		}
		return false;
	}

	std::string numberText(float value)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.3f", value);
		std::string text(buf);
		const size_t dot = text.find('.');
		if (dot != std::string::npos)
		{
			while (!text.empty() && text.back() == '0')
				text.pop_back();
			if (!text.empty() && text.back() == '.')
				text.pop_back();
		}
		if (text.empty() || text == "-")
			return "0";
		return text;
	}

	std::string withMdl(std::string name)
	{
		if (name.size() >= 4 && lowerCopy(name).compare(name.size() - 4, 4, ".mdl") == 0)
			return name;
		name += ".mdl";
		return name;
	}

	void addRoom(std::vector<Ty2RoomModel>& rooms, const std::string& role, const std::string& modelName, bool collision)
	{
		if (modelName.empty() || modelName == "none")
			return;
		for (const Ty2RoomModel& room : rooms)
		{
			if (sameText(room.modelName, modelName))
				return;
		}
		Ty2RoomModel room;
		room.role = role;
		room.modelName = modelName;
		room.collision = collision;
		rooms.push_back(std::move(room));
	}

	void readChunkRooms(const std::vector<BniLine>& lines, int start, std::vector<Ty2RoomModel>& rooms)
	{
		for (int i = start; i >= 0 && i < static_cast<int>(lines.size()); i++)
		{
			const BniLine& line = lines[static_cast<size_t>(i)];
			if (line.valueCount < 0)
				break;
			if (line.values.empty())
				continue;
			const std::string key = lowerCopy(line.field);
			if (key == "ground")
				addRoom(rooms, "ground", line.values[0], true);
			else if (key == "overlay")
				addRoom(rooms, "overlay", line.values[0], false);
			else if (key == "fftwater" || key == "envdomewater")
				addRoom(rooms, "water", line.values[0], false);
			else if (key == "envdome")
				addRoom(rooms, "dome", line.values[0], false);
		}
	}

	// TEXT_LEVEL_A1 Outback Oasis
	// TEXT_LEVEL_M2=950 Explosive Cargo
	std::unordered_map<std::string, std::string> englishLevelTitles(const std::string& text)
	{
		std::unordered_map<std::string, std::string> titles;
		const std::string prefix = "TEXT_LEVEL_";
		size_t i = 0;
		while (i < text.size())
		{
			size_t end = text.find_first_of("\r\n", i);
			if (end == std::string::npos)
				end = text.size();
			if (end - i > prefix.size() && text.compare(i, prefix.size(), prefix) == 0)
			{
				size_t p = i + prefix.size();
				const size_t idStart = p;
				while (p < end && text[p] != ' ' && text[p] != '\t' && text[p] != '=')
					p++;
				const std::string id = text.substr(idStart, p - idStart);
				if (p < end && text[p] == '=')
				{
					p++;
					while (p < end && std::isdigit(static_cast<unsigned char>(text[p])))
						p++;
				}
				while (p < end && (text[p] == ' ' || text[p] == '\t'))
					p++;
				std::string title = text.substr(p, end - p);
				while (!title.empty() && (title.back() == ' ' || title.back() == '\t'))
					title.pop_back();
				if (!id.empty() && !title.empty())
					titles.emplace(lowerCopy(id), std::move(title));
			}
			i = end + 1;
			if (end < text.size() && text[end] == '\r' && i < text.size() && text[i] == '\n')
				i++;
		}
		return titles;
	}
}

bool ty2DescribeChunk(const BniFile& levelsIni, const std::string& chunkFile, Ty2ChunkInfo& out)
{
	out = Ty2ChunkInfo{};
	const std::string want = chunkStem(chunkFile);
	if (want.empty())
		return false;

	const std::vector<BniLine>& lines = levelsIni.lines;
	for (int i = 0; i < static_cast<int>(lines.size()); i++)
	{
		const BniLine& line = lines[static_cast<size_t>(i)];
		if (line.valueCount < 0)
			break;
		if (!sameText(line.field, "level") || line.values.empty() || line.subline < 0)
			continue;

		bool hub = false;
		int chunkSub = -1;
		for (int f = line.subline; f >= 0 && f < static_cast<int>(lines.size()); f++)
		{
			const BniLine& field = lines[static_cast<size_t>(f)];
			if (field.valueCount < 0)
				break;
			if (sameText(field.field, "bHubLevel") && isTrueText(joinValues(field.values)))
				hub = true;
			if (chunkSub < 0 && sameText(field.field, "chunk") && !field.values.empty()
				&& field.subline >= 0 && chunkStem(field.values[0]) == want)
				chunkSub = field.subline;
		}
		if (chunkSub < 0)
			continue;
		out.found = true;
		out.levelId = line.values[0];
		out.hub = hub;
		readChunkRooms(lines, chunkSub, out.rooms);
		return true;
	}
	return false;
}

std::vector<Ty2LevelEntry> ty2ListLevels(const BniFile& levelsIni, const std::string& englishText)
{
	const std::unordered_map<std::string, std::string> titles = englishLevelTitles(englishText);
	std::vector<Ty2LevelEntry> levels;
	const std::vector<BniLine>& lines = levelsIni.lines;
	for (int i = 0; i < static_cast<int>(lines.size()); i++)
	{
		const BniLine& line = lines[static_cast<size_t>(i)];
		if (line.valueCount < 0)
			break;
		if (!sameText(line.field, "level") || line.values.empty())
			continue;
		Ty2LevelEntry entry;
		entry.id = line.values[0];
		const auto title = titles.find(lowerCopy(entry.id));
		if (title != titles.end())
			entry.title = title->second;
		levels.push_back(std::move(entry));
	}
	return levels;
}

bool ty2DescribeLevel(const BniFile& levelsIni, const std::string& levelId, Ty2LevelInfo& out)
{
	out = Ty2LevelInfo{};
	const std::string want = lowerCopy(levelId);
	if (want.empty())
		return false;

	const std::vector<BniLine>& lines = levelsIni.lines;
	for (int i = 0; i < static_cast<int>(lines.size()); i++)
	{
		const BniLine& line = lines[static_cast<size_t>(i)];
		if (line.valueCount < 0)
			break;
		if (!sameText(line.field, "level") || line.values.empty() || lowerCopy(line.values[0]) != want)
			continue;

		out.found = true;
		out.levelId = line.values[0];
		if (line.subline < 0)
			return true;
		for (int f = line.subline; f >= 0 && f < static_cast<int>(lines.size()); f++)
		{
			const BniLine& field = lines[static_cast<size_t>(f)];
			if (field.valueCount < 0)
				break;
			if (sameText(field.field, "bHubLevel") && isTrueText(joinValues(field.values)))
				out.hub = true;
			if (sameText(field.field, "chunk") && !field.values.empty())
			{
				out.chunks.push_back(field.values[0]);
				if (field.subline >= 0)
					readChunkRooms(lines, field.subline, out.rooms);
			}
		}
		return true;
	}
	return false;
}

std::string ty2LevelTitle(const std::string& englishText, const std::string& levelId)
{
	const std::unordered_map<std::string, std::string> titles = englishLevelTitles(englishText);
	const auto found = titles.find(lowerCopy(levelId));
	if (found == titles.end())
		return {};
	return found->second;
}

std::string ty2LevelLabel(const std::string& levelId, const std::string& title)
{
	if (title.empty())
		return levelId;
	return levelId + "  " + title;
}

std::unordered_map<std::string, std::string> ty2ModelCatalog(const BniFile& globalModel)
{
	std::unordered_map<std::string, std::string> catalog;
	const std::vector<BniLine>& lines = globalModel.lines;
	for (int i = 0; i < static_cast<int>(lines.size()); i++)
	{
		const BniLine& section = lines[static_cast<size_t>(i)];
		if (section.section.empty())
			continue;
		for (int j = i + 1; j < static_cast<int>(lines.size()); j++)
		{
			const BniLine& field = lines[static_cast<size_t>(j)];
			if (field.valueCount < 0 || !field.section.empty())
				break;
			if (sameText(field.field, "model") && !field.values.empty() && !field.values[0].empty())
			{
				catalog.emplace(lowerCopy(section.section), field.values[0]);
				break;
			}
		}
	}
	return catalog;
}

bool ty2IsConnector(const Ty1Instance& instance)
{
	return sameText(instance.typeName, "Connector");
}

std::vector<Ty1Instance> parseTy2Instances(
	const BniFile& chunk,
	const std::string& chunkFile,
	const std::unordered_map<std::string, std::string>& catalog,
	const std::function<bool(const std::string&)>& hasModel)
{
	std::vector<Ty1Instance> instances;
	const std::vector<BniLine>& lines = chunk.lines;
	const std::string here = chunkLevelKey(chunkFile);
	// Instance sections sit inside a parent run (often HEADER). Each one still
	// has its own subline, and that run is the pos / rot / scale block.
	for (int i = 0; i < static_cast<int>(lines.size()); i++)
	{
		const BniLine& line = lines[static_cast<size_t>(i)];
		if (line.section.empty() || line.subline < 0)
			continue;
		if (isStructuralSection(line.section))
			continue;

		const bool connector = sameText(line.section, "Connector");
		std::vector<Ty1Field> fields = readFields(lines, line.subline, 0);
		const Ty1Field* posField = findField(fields, "pos");
		glm::vec3 position(0.0f);
		if (posField == nullptr || !readVec3(posField->value, position))
			continue;

		Ty1Instance instance;
		instance.typeName = line.section;
		instance.sourceFile = chunkFile;
		instance.position = position;
		instance.fields = std::move(fields);
		if (const Ty1Field* rotField = findField(instance.fields, "rot"))
			readVec3(rotField->value, instance.rotation);
		instance.scale = glm::vec3(1.0f);
		if (const Ty1Field* scaleField = findField(instance.fields, "scale"))
			readVec3(scaleField->value, instance.scale);
		if (const Ty1Field* idField = findField(instance.fields, "ID"))
		{
			std::string cleaned = idField->value;
			for (char& c : cleaned)
			{
				if (c == ',')
					c = ' ';
			}
			std::istringstream stream(cleaned);
			int number = 0;
			std::string label;
			if (stream >> number)
			{
				instance.objectId = number;
				if (stream >> label && !sameText(label, "none") && !sameText(label, "blank"))
					instance.objectLabel = label;
			}
		}
		if (const Ty1Field* visible = findField(instance.fields, "bVisible"))
			instance.visible = isTrueText(visible->value);

		if (connector)
		{
			std::string target;
			if (const Ty1Field* chunkField = findField(instance.fields, "chunk"))
				target = chunkField->value;
			const bool sameLevel = !target.empty() && chunkLevelKey(target) == here;
			glm::vec3 offset(0.0f);
			if (const Ty1Field* offsetField = findField(instance.fields, "offset"))
				readVec3(offsetField->value, offset);
			const Ty1Field* screen = findField(instance.fields, "useLoadingScreen");
			const bool loadingScreen = screen != nullptr && isTrueText(screen->value);

			instance.variantLabel = target.empty() ? "no target" : target;
			instance.variantLabel += sameLevel ? "  same level" : "  other level";
			instance.variantLabel += "  offset " + numberText(offset.x) + ", "
				+ numberText(offset.y) + ", " + numberText(offset.z);
			instance.variantLabel += loadingScreen ? "  loading screen" : "  no loading screen";

			Ty1Field span;
			span.key = "span";
			span.value = sameLevel ? "same level" : "other level";
			instance.fields.push_back(std::move(span));
			instances.push_back(std::move(instance));
			continue;
		}

		std::string modelFile = withMdl(line.section);
		if (!hasModel(modelFile))
		{
			modelFile.clear();
			const auto found = catalog.find(lowerCopy(line.section));
			if (found != catalog.end())
			{
				const std::string catalogFile = withMdl(found->second);
				if (hasModel(catalogFile))
					modelFile = catalogFile;
			}
		}
		if (modelFile.empty())
			continue;
		instance.modelFile = std::move(modelFile);
		instance.kind = Ty1Kind::Prop;
		instances.push_back(std::move(instance));
	}
	return instances;
}
