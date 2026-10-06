#include "anim_script.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include "anm.h"

namespace
{
	std::string trim(const std::string& value)
	{
		size_t start = 0;
		while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
			start++;
		size_t end = value.size();
		while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
			end--;
		return value.substr(start, end - start);
	}

	std::string lower(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}

	std::string stripExtension(const std::string& value)
	{
		const size_t dot = value.find_last_of('.');
		return dot == std::string::npos ? value : value.substr(0, dot);
	}

	// Returns the remainder after `keyword` and whitespace, or false when the line is another keyword.
	bool keyword(const std::string& line, const char* word, std::string& rest)
	{
		const std::string key(word);
		if (line.size() < key.size() || lower(line.substr(0, key.size())) != key)
			return false;
		if (line.size() > key.size() && !std::isspace(static_cast<unsigned char>(line[key.size()])))
			return false;
		rest = trim(line.substr(key.size()));
		return true;
	}

	float stepFor(const AnimScriptRange& range)
	{
		return range.speed != 0 ? 1.0f / static_cast<float>(range.speed) : 1.0f;
	}
}

int AnimScriptData::find(const std::string& name) const
{
	const std::string wanted = lower(name);
	for (size_t i = 0; i < anims.size(); i++)
	{
		if (lower(anims[i].name) == wanted)
			return static_cast<int>(i);
	}
	return -1;
}

bool parseAnimScript(const char* data, size_t size, AnimScriptData& out)
{
	out = AnimScriptData{};
	if (data == nullptr || size == 0)
		return false;

	std::istringstream stream(std::string(data, size));
	std::string raw;
	AnimScriptAnim* anim = nullptr;
	while (std::getline(stream, raw))
	{
		const size_t comment = raw.find("//");
		const std::string line = trim(comment == std::string::npos ? raw : raw.substr(0, comment));
		if (line.empty())
			continue;

		std::string rest;
		if (keyword(line, "mesh", rest))
			out.mesh = stripExtension(rest);
		else if (keyword(line, "skeleton", rest))
			out.skeleton = stripExtension(rest);
		else if (keyword(line, "anim", rest) || (line.front() == '[' && line.back() == ']'))
		{
			out.anims.push_back({});
			anim = &out.anims.back();
			anim->name = line.front() == '[' ? trim(line.substr(1, line.size() - 2)) : rest;
		}
		else if (keyword(line, "cycle", rest))
		{
			if (anim == nullptr)
				continue;
			const std::string mode = lower(rest);
			if (mode == "loop")
				anim->cycle = AnimCycle::Loop;
			else if (mode == "rebound")
				anim->cycle = AnimCycle::Rebound;
			else
				anim->cycle = AnimCycle::Stop;
		}
		else if (std::isdigit(static_cast<unsigned char>(line.front())) && anim != nullptr)
		{
			// MKAnimScript reads "%d-%d,%d". A missing end repeats the start; speed defaults to 1.
			AnimScriptRange range;
			const char* p = line.c_str();
			char* next = nullptr;
			range.start = static_cast<int>(std::strtol(p, &next, 10));
			range.end = range.start;
			if (*next == '-')
			{
				p = next + 1;
				range.end = static_cast<int>(std::strtol(p, &next, 10));
				if (next == p)
					range.end = range.start;
				if (*next == ',')
				{
					p = next + 1;
					const long speed = std::strtol(p, &next, 10);
					if (next != p)
						range.speed = static_cast<int>(speed);
				}
			}
			anim->ranges.push_back(range);
		}
	}

	out.anims.erase(std::remove_if(out.anims.begin(), out.anims.end(),
		[](const AnimScriptAnim& a) { return a.ranges.empty(); }), out.anims.end());
	return !out.anims.empty();
}

void AnimScriptPlayer::init(const AnimScriptData* data)
{
	*this = AnimScriptPlayer{};
	m_data = data;
	if (valid())
		setAnim(0);
}

void AnimScriptPlayer::setAnim(int anim)
{
	if (!valid() || anim < 0 || anim >= static_cast<int>(m_data->anims.size()))
		return;
	const AnimScriptAnim& entry = m_data->anims[static_cast<size_t>(anim)];
	m_anim = anim;
	m_range = 0;
	m_frame = static_cast<float>(entry.ranges[0].start);
	m_step = stepFor(entry.ranges[0]);
	m_loops = 0;
	m_nextAnim = -1;
	m_tweenCount = 0;
}

void AnimScriptPlayer::tweenAnim(int anim, int steps)
{
	if (!valid() || anim < 0 || anim >= static_cast<int>(m_data->anims.size()))
		return;
	if (steps <= 0 || m_anim < 0)
	{
		setAnim(anim);
		return;
	}
	m_nextAnim = anim;
	m_nextFrame = static_cast<float>(m_data->anims[static_cast<size_t>(anim)].ranges[0].start);
	m_tweenCount = steps;
}

void AnimScriptPlayer::play(int anim, int tweenSteps)
{
	if (anim < 0 || anim == m_nextAnim || (anim == m_anim && m_nextAnim < 0))
		return;
	tweenAnim(anim, tweenSteps);
}

void AnimScriptPlayer::animate(float advance)
{
	if (!valid() || m_anim < 0 || m_nextAnim >= 0 || m_step == 0.0f)
		return;

	const AnimScriptAnim& entry = m_data->anims[static_cast<size_t>(m_anim)];
	const float frame = m_frame + m_step * advance;
	const AnimScriptRange& range = entry.ranges[static_cast<size_t>(m_range)];

	if (m_step > 0.0f && frame > static_cast<float>(range.end))
	{
		if (m_range + 1 < static_cast<int>(entry.ranges.size()))
		{
			m_range++;
			m_frame = static_cast<float>(entry.ranges[static_cast<size_t>(m_range)].start);
			m_step = stepFor(entry.ranges[static_cast<size_t>(m_range)]);
			return;
		}
		switch (entry.cycle)
		{
		case AnimCycle::Stop:
			m_frame = static_cast<float>(range.end);
			m_step = 0.0f;
			break;
		case AnimCycle::Loop:
			m_range = 0;
			m_frame = static_cast<float>(entry.ranges[0].start);
			m_step = stepFor(entry.ranges[0]);
			m_loops++;
			break;
		case AnimCycle::Rebound:
			m_frame = static_cast<float>(range.end);
			m_step = -m_step;
			break;
		}
		return;
	}

	if (m_step < 0.0f && frame < static_cast<float>(range.start))
	{
		if (m_range > 0)
		{
			m_range--;
			m_frame = static_cast<float>(entry.ranges[static_cast<size_t>(m_range)].end);
			m_step = -stepFor(entry.ranges[static_cast<size_t>(m_range)]);
			return;
		}
		if (entry.cycle == AnimCycle::Stop)
		{
			m_frame = static_cast<float>(range.start);
			m_step = 0.0f;
		}
		else
		{
			m_frame = static_cast<float>(range.start);
			m_step = -m_step;
			m_loops++;
		}
		return;
	}

	m_frame = frame;
}

void AnimScriptPlayer::apply(AnmPose& pose)
{
	if (!valid() || !pose.valid())
		return;
	if (m_nextAnim >= 0)
	{
		pose.tween(m_nextFrame, 1.0f / static_cast<float>(m_tweenCount));
		if (--m_tweenCount <= 0)
			setAnim(m_nextAnim);
		return;
	}
	pose.tween(m_frame, 1.0f);
}
