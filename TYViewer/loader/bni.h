#pragma once

#include <string>
#include <vector>

// One line of a Krome MKIni binary (the payload of a .bni). A separator has
// valueCount -1. A section's fields are the run that starts at subline and
// ends at the next separator. Nested blocks use the same link.
struct BniLine
{
	int index = 0;
	int valueCount = 0;
	std::string section;
	std::string field;
	std::vector<std::string> values;
	int subline = -1;
};

// Data File version 3.0. The 0x44-byte wrapper holds the original path and the
// offsets; the lines, the uint16 string-index table, and the 4-aligned strings
// follow it.
struct BniFile
{
	std::string path;
	std::vector<BniLine> lines;
};

// False when the wrapper is too small or the tables run past the buffer.
bool loadBni(const char* data, size_t size, BniFile& out);
