#include "archive.h"

#include <fstream>
#include <algorithm>

#include "util/bitconverter.h"
#include "util/stringext.h"

#include "debug.h"

bool Archive::load(const std::string& path)
{
	this->path = path;

	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	if (stream.fail())
	{
		Debug::log("ERROR: Failed to open archive file: " + path);
		stream.close();
		return false;
	}
	size = static_cast<unsigned long>(stream.tellg());
	stream.close();

	if (size == 0)
	{
		Debug::log("ERROR: Archive file is empty: " + path);
		return false;
	}

	identify();

	switch (version)
	{
	case Archive::RKV1:
		Debug::log("Identified archive as RKV1 format");
		loadAsRKV1();
		Debug::log("Loaded " + std::to_string(files.size()) + " files from RKV1 archive");
		return true;
		break;
	case Archive::RKV2:
		Debug::log("Identified archive as RKV2 format");
		loadAsRKV2();
		Debug::log("Loaded " + std::to_string(files.size()) + " files from RKV2 archive");
		return true;
		break;
	case Archive::UNKNOWN:
		Debug::log("ERROR: File isn't a recognized TY archive format: " + path);
		break;
	}

	return false;
}

bool Archive::getFile(const std::string& name, File& file)
{
	std::string key = name;
	std::transform(key.begin(), key.end(), key.begin(), ::tolower);

	if (files.find(key) != files.end())
	{
		file = files[key];
		return true;
	}
	return false;
}

bool Archive::getFileData(const std::string& name, std::vector<char>& data)
{
	std::string key = name;
	std::transform(key.begin(), key.end(), key.begin(), ::tolower);

	File file;
	if (files.find(key) != files.end())
	{
		file = files[key];
	}

	// File found!
	if (file.size != 0)
	{
		std::ifstream stream(path, std::ios::binary);

		data = std::vector<char>(file.size);
		stream.seekg(file.offset, std::ios::beg);
		stream.read(&data[0], file.size);

		stream.close();

		return true;
	}

	return false;
}

std::vector<std::string> Archive::getAllFiles() const
{
	std::vector<std::string> fileList;
	for (const auto& pair : files)
	{
		fileList.push_back(pair.second.name);
	}
	return fileList;
}

std::vector<std::string> Archive::getFilesByExtension(const std::string& ext) const
{
	std::vector<std::string> fileList;
	std::string lowerExt = ext;
	std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
	
	for (const auto& pair : files)
	{
		if (pair.second.extension() == lowerExt)
		{
			fileList.push_back(pair.second.name);
		}
	}
	return fileList;
}

void Archive::identify()
{
	std::ifstream stream(path, std::ios::binary | std::ios::beg);

	std::vector<char> ext(4);
	stream.read(&ext[0], 4);

	// There's no way to identify a RKV1 archive as far as I've found,
	// so we'll assume that if the archive is not RKV2 it's RKV1.

	if (std::string(ext.begin(), ext.end()) == "RKV2")
	{
		version = Archive::RKV2;
		return;
	}
	else
	{
		version = Archive::RKV1;
	}

	stream.close();
}

void Archive::loadAsRKV1()
{
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	stream.seekg(-((std::streampos)8), std::ios::end);

	// Last 4 bytes contain information 
	// regarding filecount and foldercount
	stream.seekg(-(8), std::ios::end);

	std::vector<char> info(8);
	stream.read(&info[0], 8);

	int filecount = from_bytes<uint32_t>(info.data(), 0);
	int foldercount = from_bytes<uint32_t>(info.data(), 4);

	// Goto beginning of table.
	stream.seekg(-(8 + foldercount * 256 + filecount * 64), std::ios::end);

	for (int i = 0; i < filecount; i++)
	{
		std::vector<char> buffer(64);
		stream.read(&buffer[0], 64);


		File file;
		file.name	= nts(buffer.data(), 0, 32);
		file.folder = from_bytes<uint32_t>(buffer.data(), 32);
		file.size	= from_bytes<uint32_t>(buffer.data(), 36);
		file.offset = from_bytes<uint32_t>(buffer.data(), 44);
		file.date	= from_bytes<uint32_t>(buffer.data(), 52);

		std::string key = file.name;
		std::transform(key.begin(), key.end(), key.begin(), ::tolower);

		files[key] = file;
	}

	stream.close();
}

void Archive::loadAsRKV2()
{
	std::ifstream stream(path, std::ios::binary);

	// Skip "RKV2" magic (4 bytes)
	stream.seekg(4, std::ios::beg);

	// Read initial header data (6 uint32_t values)
	std::vector<char> header(24);
	stream.read(&header[0], 24);

	uint32_t files_count = from_bytes<uint32_t>(header.data(), 0);
	uint32_t name_size = from_bytes<uint32_t>(header.data(), 4);
	uint32_t fullname_files = from_bytes<uint32_t>(header.data(), 8);
	// DUMMY1 at offset 12 - not used
	uint32_t info_off = from_bytes<uint32_t>(header.data(), 16);
	// DUMMY2 at offset 20 - not used

	// Calculate offsets
	uint32_t name_off = files_count * 20 + info_off;
	// uint32_t info2_off = name_off + name_size; // Not used in file reading
	// uint32_t fullname_off = files_count * 16 + info2_off; // Not used in file reading

	// Seek to INFO_OFF to start reading file entries
	uint32_t current_info_off = info_off;

	for (uint32_t i = 0; i < files_count; i++)
	{
		// Seek to current INFO entry position
		stream.seekg(current_info_off, std::ios::beg);

		// Read file entry (20 bytes: 5 uint32_t values)
		std::vector<char> entry(20);
		stream.read(&entry[0], 20);

		// Update current_info_off to next entry position
		current_info_off = static_cast<uint32_t>(stream.tellg());

		uint32_t nameoff = from_bytes<uint32_t>(entry.data(), 0);
		// DUMMY3 at offset 4 - not used
		uint32_t size = from_bytes<uint32_t>(entry.data(), 8);
		uint32_t offset = from_bytes<uint32_t>(entry.data(), 12);
		// CRC at offset 16 - not used

		// Read file name from NAME_OFF + nameoff
		uint32_t name_pos = name_off + nameoff;
		stream.seekg(name_pos, std::ios::beg);

		// Read name (up to 0x100 bytes, null-terminated)
		std::vector<char> name_buffer(0x100);
		stream.read(&name_buffer[0], 0x100);
		std::string name = nts(name_buffer.data(), 0, 0x100);

		// Create file entry
		File file;
		file.name = name;
		file.folder = 0; // RKV2 doesn't use folders
		file.size = size;
		file.offset = offset;
		file.date = 0; // RKV2 doesn't store date

		std::string key = file.name;
		std::transform(key.begin(), key.end(), key.begin(), ::tolower);

		files[key] = file;
	}

	stream.close();
}
