#include "bni.h"

#include "util/bitconverter.h"

namespace
{
	std::string readCString(const char* data, size_t size, size_t offset, size_t cap)
	{
		if (offset >= size)
			return {};
		size_t end = offset;
		const size_t limit = offset + cap < size ? offset + cap : size;
		while (end < limit && data[end] != '\0')
			end++;
		return std::string(data + offset, data + end);
	}

	std::string stringAt(const char* strings, size_t stringBytes, uint16_t index)
	{
		const size_t offset = static_cast<size_t>(index) * 4;
		if (offset >= stringBytes)
			return {};
		return readCString(strings, stringBytes, offset, 240);
	}
}

bool loadBni(const char* data, size_t size, BniFile& out)
{
	out = BniFile{};
	if (data == nullptr || size < 0x44)
		return false;

	const uint32_t lineCount = from_bytes<uint32_t>(data, 0x24);
	const uint32_t stringOffset = from_bytes<uint32_t>(data, 0x2C);
	const uint32_t indexOffset = from_bytes<uint32_t>(data, 0x30);
	const size_t payloadSize = size - 0x44;
	if (lineCount > 1000000)
		return false;
	const uint64_t lineBytes = static_cast<uint64_t>(lineCount) * 16;
	if (lineBytes > payloadSize || indexOffset > stringOffset || stringOffset > payloadSize)
		return false;
	if (((stringOffset - indexOffset) & 1u) != 0)
		return false;

	const char* payload = data + 0x44;
	const char* strings = payload + stringOffset;
	const size_t stringBytes = payloadSize - stringOffset;
	const size_t indexCount = (stringOffset - indexOffset) / 2;
	const char* indices = payload + indexOffset;

	out.path = readCString(data, size, 0, 32);
	out.lines.reserve(lineCount);
	for (uint32_t i = 0; i < lineCount; i++)
	{
		const size_t at = static_cast<size_t>(i) * 16;
		BniLine line;
		line.index = static_cast<int>(i);
		line.valueCount = from_bytes<int16_t>(payload, at);
		const uint16_t section = from_bytes<uint16_t>(payload, at + 2);
		const uint16_t field = from_bytes<uint16_t>(payload, at + 4);
		const uint16_t firstValue = from_bytes<uint16_t>(payload, at + 6);
		const int16_t subline = from_bytes<int16_t>(payload, at + 8);
		line.subline = subline;
		if (section != 0xFFFF)
			line.section = stringAt(strings, stringBytes, section);
		if (field != 0xFFFF)
			line.field = stringAt(strings, stringBytes, field);
		if (line.valueCount > 0)
		{
			const int count = line.valueCount > 48 ? 48 : line.valueCount;
			line.values.reserve(static_cast<size_t>(count));
			for (int v = 0; v < count; v++)
			{
				const size_t slot = static_cast<size_t>(firstValue) + static_cast<size_t>(v);
				if (slot >= indexCount)
					break;
				const uint16_t stringIndex = from_bytes<uint16_t>(indices, slot * 2);
				line.values.push_back(stringAt(strings, stringBytes, stringIndex));
			}
		}
		out.lines.push_back(std::move(line));
	}
	return true;
}
