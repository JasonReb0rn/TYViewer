#include "anm.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "util/bitconverter.h"

namespace
{
	bool fits(size_t offset, size_t count, size_t size)
	{
		return count <= size && offset <= size - count;
	}

	std::string readName(const char* data, size_t size, uint32_t offset)
	{
		if (offset >= size)
			return {};
		const void* nul = std::memchr(data + offset, '\0', size - offset);
		if (nul == nullptr)
			return {};
		return std::string(data + offset, static_cast<const char*>(nul));
	}

	// SlerpQuat in Animation.cpp: a normalised lerp along the shorter arc.
	glm::vec4 slerpQuat(const glm::vec4& a, const glm::vec4& b, float weight)
	{
		const float inv = 1.0f - weight;
		const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
		const glm::vec4 from = dot > 0.0f ? a : -a;
		const glm::vec4 lerp = from * inv + b * weight;
		const float length = std::sqrt(lerp.x * lerp.x + lerp.y * lerp.y + lerp.z * lerp.z + lerp.w * lerp.w);
		if (length <= 0.0f)
			return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		return lerp / length;
	}

	struct Sample
	{
		glm::vec3 position{ 0.0f };
		glm::vec4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
		glm::vec3 scale{ 1.0f };
	};

	// Animation_InterpolateFrame.
	Sample interpolate(const AnmNode& node, float frame)
	{
		Sample out;
		const std::vector<AnmKey>& keys = node.keys;
		if (keys.empty())
			return out;
		if (keys.size() == 1)
		{
			out.position = keys[0].position;
			out.rotation = keys[0].rotation;
			out.scale = keys[0].scale;
			return out;
		}

		int start = 0;
		int end = static_cast<int>(keys.size()) - 1;
		while (end - start > 1)
		{
			const int mid = (start + end) / 2;
			if (static_cast<float>(keys[static_cast<size_t>(mid)].frame) > frame)
				end = mid;
			else
				start = mid;
		}

		const AnmKey& a = keys[static_cast<size_t>(start)];
		const AnmKey& b = keys[static_cast<size_t>(end)];
		if (frame == static_cast<float>(a.frame))
		{
			out.position = a.position;
			out.rotation = a.rotation;
			out.scale = a.scale;
			return out;
		}
		if (frame == static_cast<float>(b.frame) || a.frame == b.frame)
		{
			out.position = b.position;
			out.rotation = b.rotation;
			out.scale = b.scale;
			return out;
		}

		// The game does not clamp. Ranges in the shipped .bad files stay inside the keys.
		const float t = std::clamp((frame - a.frame) / static_cast<float>(b.frame - a.frame), 0.0f, 1.0f);
		out.position = a.position + (b.position - a.position) * t;
		out.scale = a.scale + (b.scale - a.scale) * t;
		out.rotation = slerpQuat(a.rotation, b.rotation, t);
		return out;
	}
}

bool parseAnm(const char* data, size_t size, AnmData& out)
{
	out = AnmData{};
	if (data == nullptr || size < 0x18 || std::memcmp(data, "ANIM", 4) != 0)
		return false;

	out.frameCount = static_cast<int>(from_bytes<uint32_t>(data, 0x04));
	const uint32_t nodeCount = from_bytes<uint32_t>(data, 0x08);
	const uint32_t nodeTable = from_bytes<uint32_t>(data, 0x0C);
	if (nodeCount > 1024 || !fits(nodeTable, static_cast<size_t>(nodeCount) * 0x20, size))
		return false;

	out.nodes.resize(nodeCount);
	for (uint32_t i = 0; i < nodeCount; i++)
	{
		const size_t base = nodeTable + static_cast<size_t>(i) * 0x20;
		AnmNode& node = out.nodes[i];
		node.origin = glm::vec3(
			from_bytes<float>(data, base + 0x0),
			from_bytes<float>(data, base + 0x4),
			from_bytes<float>(data, base + 0x8));
		node.name = readName(data, size, from_bytes<uint32_t>(data, base + 0x10));
		node.parent = from_bytes<int32_t>(data, base + 0x14);
		if (node.parent < -1 || node.parent >= static_cast<int>(nodeCount))
			node.parent = -1;

		const uint32_t keyCount = from_bytes<uint32_t>(data, base + 0x18);
		const uint32_t keyTable = from_bytes<uint32_t>(data, base + 0x1C);
		if (keyCount > 100000 || !fits(keyTable, static_cast<size_t>(keyCount) * 12, size))
			return false;

		// KeyFrame is three pointers to 16-byte vectors. Identical vectors are shared.
		// pos.w holds the frame number as an int.
		node.keys.resize(keyCount);
		for (uint32_t k = 0; k < keyCount; k++)
		{
			const size_t entry = keyTable + static_cast<size_t>(k) * 12;
			const uint32_t pos = from_bytes<uint32_t>(data, entry);
			const uint32_t rot = from_bytes<uint32_t>(data, entry + 4);
			const uint32_t scale = from_bytes<uint32_t>(data, entry + 8);
			if (!fits(pos, 16, size) || !fits(rot, 16, size) || !fits(scale, 16, size))
				return false;

			AnmKey& key = node.keys[k];
			key.position = glm::vec3(from_bytes<float>(data, pos), from_bytes<float>(data, pos + 4), from_bytes<float>(data, pos + 8));
			key.frame = from_bytes<int32_t>(data, pos + 12);
			key.rotation = glm::vec4(
				from_bytes<float>(data, rot), from_bytes<float>(data, rot + 4),
				from_bytes<float>(data, rot + 8), from_bytes<float>(data, rot + 12));
			key.scale = glm::vec3(from_bytes<float>(data, scale), from_bytes<float>(data, scale + 4), from_bytes<float>(data, scale + 8));
		}
	}
	return true;
}

void AnmPose::init(const AnmData* data, const std::vector<glm::vec3>& origins)
{
	m_data = data;
	m_frames.clear();
	m_matrices.clear();
	m_hasTarget = false;
	m_matricesReady = false;
	if (data == nullptr)
		return;

	const size_t count = data->nodes.size();
	m_frames.resize(count);
	m_matrices.assign(count + 1, glm::mat4(1.0f));
	for (size_t i = 0; i < count; i++)
		m_frames[i].origin = (i < origins.size()) ? origins[i] : data->nodes[i].origin;
	tween(0.0f, 1.0f);
}

void AnmPose::tween(float frame, float weight)
{
	const float clamped = std::clamp(weight, 0.0f, 1.0f);
	if (m_hasTarget && m_matricesReady && m_targetFrame == frame && m_targetWeight == clamped)
		return;

	m_hasTarget = true;
	m_targetFrame = frame;
	m_targetWeight = clamped;
	m_matricesReady = false;

	for (size_t i = 0; i < m_frames.size(); i++)
	{
		Frame& state = m_frames[i];
		if (!state.frameCalc && clamped != 1.0f)
			calculateFrame(static_cast<int>(i));
		state.targetFrame = frame;
		state.targetWeight = clamped;
		state.frameCalc = false;
		state.matrixCalc = false;
	}
}

void AnmPose::calculateMatrices()
{
	if (m_matricesReady)
		return;
	for (int i = 0; i < static_cast<int>(m_frames.size()); i++)
	{
		if (!m_frames[static_cast<size_t>(i)].matrixCalc)
			calculateNodeMatrix(i);
	}
	m_matricesReady = true;
}

// Animation_CalculateFrame.
void AnmPose::calculateFrame(int index)
{
	Frame& state = m_frames[static_cast<size_t>(index)];
	const Sample sample = interpolate(m_data->nodes[static_cast<size_t>(index)], state.targetFrame);
	if (state.targetWeight == 1.0f)
	{
		state.position = sample.position;
		state.rotation = sample.rotation;
		state.scale = sample.scale;
	}
	else
	{
		const float t = state.targetWeight;
		state.position = state.position + (sample.position - state.position) * t;
		state.scale = state.scale + (sample.scale - state.scale) * t;
		state.rotation = slerpQuat(state.rotation, sample.rotation, t);
	}
	state.frameCalc = true;
	state.matrixCalc = false;
}

// Animation::CalculateNodeMatrix. Krome matrices are row-vector with the translation
// in row 3; column r of the glm matrix is Krome row r, so child = parent * local.
void AnmPose::calculateNodeMatrix(int index)
{
	const AnmNode& node = m_data->nodes[static_cast<size_t>(index)];
	Frame& state = m_frames[static_cast<size_t>(index)];
	if (!state.frameCalc)
		calculateFrame(index);
	if (node.parent != -1 && !m_frames[static_cast<size_t>(node.parent)].matrixCalc)
		calculateNodeMatrix(node.parent);

	const float sqrt2 = 1.41421356f;
	const float x = sqrt2 * state.rotation.x;
	const float y = sqrt2 * state.rotation.y;
	const float z = sqrt2 * state.rotation.z;
	const float w = sqrt2 * state.rotation.w;
	const float m00 = 1.0f - (y * y + z * z);
	const float m01 = x * y - w * z;
	const float m02 = x * z + w * y;
	const float m10 = x * y + w * z;
	const float m11 = 1.0f - (x * x + z * z);
	const float m12 = y * z - w * x;
	const float m20 = x * z - w * y;
	const float m21 = y * z + w * x;
	const float m22 = 1.0f - (x * x + y * y);

	const glm::vec3 s = state.scale;
	const glm::vec3 o = state.origin;
	glm::mat4 local(1.0f);
	local[0] = glm::vec4(s.x * m00, s.x * m01, s.x * m02, 0.0f);
	local[1] = glm::vec4(s.y * m10, s.y * m11, s.y * m12, 0.0f);
	local[2] = glm::vec4(s.z * m20, s.z * m21, s.z * m22, 0.0f);
	local[3] = glm::vec4(
		-o.x * s.x * m00 - o.y * s.y * m10 - o.z * s.z * m20 + o.x,
		-o.x * s.x * m01 - o.y * s.y * m11 - o.z * s.z * m21 + o.y,
		-o.x * s.x * m02 - o.y * s.y * m12 - o.z * s.z * m22 + o.z,
		1.0f);

	const glm::vec3 p = state.position;
	if (p.x != 0.0f || p.y != 0.0f || p.z != 0.0f)
	{
		glm::vec3 offset = p;
		if (node.parent != -1)
			offset = p - (o - m_frames[static_cast<size_t>(node.parent)].origin);
		local[3] += glm::vec4(offset, 0.0f);
	}

	const glm::mat4& parent = m_matrices[static_cast<size_t>(node.parent + 1)];
	m_matrices[static_cast<size_t>(index) + 1] = parent * local;
	state.matrixCalc = true;
}
