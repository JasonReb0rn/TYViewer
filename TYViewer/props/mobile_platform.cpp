#include "mobile_platform.h"

#include "loader/ty1_level.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>

namespace
{
	constexpr float kTickSeconds = 1.0f / 30.0f;
	constexpr float kMoveOneEnd = 0.99f;

	std::string lowerCopy(std::string text)
	{
		for (char& c : text)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return text;
	}

	std::string trimCopy(const std::string& text)
	{
		size_t begin = 0;
		while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
			begin++;
		size_t end = text.size();
		while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
			end--;
		return text.substr(begin, end - begin);
	}

	bool readFloat(const std::string& text, float& out)
	{
		const std::string trimmed = trimCopy(text);
		if (trimmed.empty())
			return false;
		char* end = nullptr;
		const float value = std::strtof(trimmed.c_str(), &end);
		if (end == trimmed.c_str())
			return false;
		out = value;
		return true;
	}

	// `1589,cogplatform` or `0,none`. 0 is none.
	bool readId(const std::string& text, int& out)
	{
		const std::string trimmed = trimCopy(text);
		const size_t comma = trimmed.find(',');
		const std::string number = comma == std::string::npos ? trimmed : trimCopy(trimmed.substr(0, comma));
		if (number.empty())
			return false;
		char* end = nullptr;
		const long value = std::strtol(number.c_str(), &end, 10);
		if (end == number.c_str())
			return false;
		out = static_cast<int>(value);
		return true;
	}

	// Level files write these as `0,false` or `1,true`.
	bool readBool(const std::string& text, bool fallback)
	{
		std::string value = lowerCopy(trimCopy(text));
		const size_t comma = value.find(',');
		if (comma != std::string::npos)
			value = trimCopy(value.substr(0, comma));
		if (value == "true" || value == "1")
			return true;
		if (value == "false" || value == "0")
			return false;
		return fallback;
	}

	const Ty1Field* fieldNamed(const Ty1Instance& instance, const char* name)
	{
		const std::string key = lowerCopy(name);
		for (const Ty1Field& field : instance.fields)
		{
			if (lowerCopy(field.key) == key)
				return &field;
		}
		return nullptr;
	}

	const Ty1Field* childNamed(const Ty1Field& field, const char* name)
	{
		const std::string key = lowerCopy(name);
		for (const Ty1Field& child : field.children)
		{
			if (lowerCopy(child.key) == key)
				return &child;
		}
		return nullptr;
	}

	float fieldFloat(const Ty1Instance& instance, const char* name, float fallback)
	{
		const Ty1Field* field = fieldNamed(instance, name);
		float value = fallback;
		if (field != nullptr)
			readFloat(field->value, value);
		return value;
	}

	bool fieldBool(const Ty1Instance& instance, const char* name, bool fallback)
	{
		const Ty1Field* field = fieldNamed(instance, name);
		if (field == nullptr)
			return fallback;
		return readBool(field->value, fallback);
	}

	bool readVec3(const std::string& text, glm::vec3& out)
	{
		float value[3] = {};
		size_t cursor = 0;
		for (int axis = 0; axis < 3; axis++)
		{
			const size_t comma = text.find(',', cursor);
			const std::string part = comma == std::string::npos
				? text.substr(cursor)
				: text.substr(cursor, comma - cursor);
			if (!readFloat(part, value[axis]))
				return false;
			if (axis == 2)
			{
				out = glm::vec3(value[0], value[1], value[2]);
				return true;
			}
			if (comma == std::string::npos)
				return false;
			cursor = comma + 1;
		}
		return false;
	}

	float wrapPi(float angle)
	{
		const float pi = glm::pi<float>();
		angle = std::fmod(angle + pi, 2.0f * pi);
		if (angle < 0.0f)
			angle += 2.0f * pi;
		return angle - pi;
	}

	glm::vec3 unitOrZero(const glm::vec3& v)
	{
		const float length = glm::length(v);
		if (length <= 1.0e-8f)
			return glm::vec3(0.0f);
		return v / length;
	}

	float boundsRadius(const Ty1Instance& instance)
	{
		if (!instance.hasAabb)
			return 0.0f;
		float radius = 0.0f;
		const glm::vec3& mn = instance.worldAabbMin;
		const glm::vec3& mx = instance.worldAabbMax;
		for (int corner = 0; corner < 8; corner++)
		{
			const glm::vec3 point(
				(corner & 1) ? mx.x : mn.x,
				(corner & 2) ? mx.y : mn.y,
				(corner & 4) ? mx.z : mn.z);
			radius = std::max(radius, glm::length(point - instance.position));
		}
		return radius;
	}

	void expandAround(Ty1Instance& instance, const glm::vec3& center, float radius)
	{
		const glm::vec3 pad(radius);
		instance.worldAabbMin = glm::min(instance.worldAabbMin, center - pad);
		instance.worldAabbMax = glm::max(instance.worldAabbMax, center + pad);
		instance.hasAabb = true;
	}
}

void MobilePlatformSystem::addPoint(std::vector<Node>& nodes, int& count, const glm::vec3& point)
{
	if (count == 0)
	{
		nodes[0].position = point;
		nodes[0].tangent = glm::vec3(0.0f);
		nodes[0].length = 1.0f;
		count = 1;
		return;
	}

	const glm::vec3 delta = point - nodes[count - 1].position;
	const float mag = glm::length(delta);
	const glm::vec3 unitOut = mag > 1.0e-8f ? delta / mag : glm::vec3(0.0f);
	nodes[count - 1].length = mag;
	nodes[count].position = point;
	nodes[count].tangent = unitOut;
	nodes[count].length = 0.0f;
	if (count == 1)
	{
		nodes[0].tangent = unitOut;
		count = 2;
		return;
	}

	glm::vec3 inDir(0.0f);
	const float prevLen = nodes[count - 2].length;
	if (prevLen > 1.0e-8f)
		inDir = (nodes[count - 1].position - nodes[count - 2].position) / prevLen;
	nodes[count - 1].tangent = unitOrZero(0.5f * (unitOut + inDir));
	nodes[count].tangent = unitOrZero(3.0f * unitOut - nodes[count - 1].tangent);
	count++;
}

void MobilePlatformSystem::closeLoop(std::vector<Node>& nodes, int count)
{
	if (count < 3)
		return;
	glm::vec3 inDir(0.0f);
	const float inLen = nodes[count - 2].length;
	if (inLen > 1.0e-8f)
		inDir = (nodes[count - 1].position - nodes[count - 2].position) / inLen;
	glm::vec3 outDir(0.0f);
	const float outLen = nodes[0].length;
	if (outLen > 1.0e-8f)
		outDir = (nodes[1].position - nodes[0].position) / outLen;
	const glm::vec3 tangent = unitOrZero(0.5f * (inDir + outDir));
	nodes[0].tangent = tangent;
	nodes[count - 1].tangent = tangent;
}

float MobilePlatformSystem::segmentLength(const Node& a, const Node& b)
{
	const float chord = glm::length(b.position - a.position);
	const float mag0 = glm::length(a.tangent);
	const float mag1 = glm::length(b.tangent);
	const float avg = 0.5f * (mag0 + mag1);
	const float avg2 = avg * avg;
	if (avg2 <= 1.0e-12f)
		return a.length > 0.0f ? a.length : chord;

	const glm::vec3 tsum = a.tangent + b.tangent;
	const glm::vec3 delta = a.position - b.position;
	const float quadraticA = glm::dot(tsum, tsum) - 16.0f * avg2;
	const float quadraticB = 12.0f * glm::dot(delta, tsum);
	const float quadraticC = 36.0f * glm::dot(delta, delta);
	const float disc = quadraticB * quadraticB - 4.0f * quadraticA * quadraticC;
	if (std::abs(quadraticA) <= 1.0e-8f || disc < 0.0f)
		return chord;
	const float solved = (-quadraticB - std::sqrt(disc)) / (2.0f * quadraticA);
	if (!std::isfinite(solved) || solved <= 1.0e-6f)
		return chord;
	return solved;
}

MobilePlatformSystem::Path MobilePlatformSystem::buildPath(const std::vector<glm::vec3>& points, bool circular)
{
	Path path;
	if (points.empty())
		return path;

	const bool loop = circular && points.size() >= 3;
	const int capacity = static_cast<int>(points.size()) + (loop ? 1 : 0);
	path.nodes.resize(static_cast<size_t>(capacity));
	int count = 0;
	for (const glm::vec3& point : points)
		addPoint(path.nodes, count, point);
	if (loop)
	{
		addPoint(path.nodes, count, points.front());
		closeLoop(path.nodes, count);
	}
	else if (count > 0)
	{
		path.nodes[0].tangent = glm::vec3(0.0f);
		path.nodes[static_cast<size_t>(count - 1)].tangent = glm::vec3(0.0f);
	}

	path.nodes.resize(static_cast<size_t>(count));
	path.totalLength = 0.0f;
	for (int i = 0; i + 1 < count; i++)
	{
		path.nodes[static_cast<size_t>(i)].length = segmentLength(
			path.nodes[static_cast<size_t>(i)], path.nodes[static_cast<size_t>(i + 1)]);
		path.totalLength += path.nodes[static_cast<size_t>(i)].length;
	}
	return path;
}

void MobilePlatformSystem::hermite(const Node& a, const Node& b, float u, glm::vec3& position, glm::vec3& derivative)
{
	const float seg = a.length;
	const glm::vec3 m0 = a.tangent * seg;
	const glm::vec3 m1 = b.tangent * seg;
	const float u2 = u * u;
	const float u3 = u2 * u;
	const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
	const float h10 = u3 - 2.0f * u2 + u;
	const float h01 = -2.0f * u3 + 3.0f * u2;
	const float h11 = u3 - u2;
	position = h00 * a.position + h10 * m0 + h01 * b.position + h11 * m1;
	const float d00 = 6.0f * u2 - 6.0f * u;
	const float d10 = 3.0f * u2 - 4.0f * u + 1.0f;
	const float d01 = -6.0f * u2 + 6.0f * u;
	const float d11 = 3.0f * u2 - 2.0f * u;
	derivative = d00 * a.position + d10 * m0 + d01 * b.position + d11 * m1;
}

void MobilePlatformSystem::Path::sample(float t, glm::vec3& position, glm::vec3& derivative) const
{
	derivative = glm::vec3(0.0f);
	if (nodes.empty())
	{
		position = glm::vec3(0.0f);
		return;
	}
	if (nodes.size() == 1 || totalLength <= 1.0e-8f)
	{
		position = nodes.front().position;
		return;
	}

	t = glm::clamp(t, 0.0f, 1.0f);
	float distance = totalLength * t;
	const int last = static_cast<int>(nodes.size()) - 2;
	for (int i = 0; i <= last; i++)
	{
		const Node& a = nodes[static_cast<size_t>(i)];
		const Node& b = nodes[static_cast<size_t>(i + 1)];
		if (a.length <= 1.0e-8f)
			continue;
		if (distance > a.length && i != last)
		{
			distance -= a.length;
			continue;
		}
		const float u = glm::clamp(distance / a.length, 0.0f, 1.0f);
		hermite(a, b, u, position, derivative);
		return;
	}
	position = nodes.back().position;
}

void MobilePlatformSystem::clear()
{
	m_platforms.clear();
	m_riders.clear();
	m_platformOf.clear();
	m_riderOf.clear();
	m_triggerOn.clear();
	m_accumulator = 0.0f;
	m_tick = 0;
	m_paused = false;
}

glm::mat4 MobilePlatformSystem::deckMatrix(const Platform& platform, const glm::vec3& position, float yaw) const
{
	glm::mat4 matrix(1.0f);
	matrix = glm::translate(matrix, position);
	matrix = glm::rotate(matrix, -platform.roll, glm::vec3(0.0f, 0.0f, 1.0f));
	matrix = glm::rotate(matrix, -yaw, glm::vec3(0.0f, 1.0f, 0.0f));
	matrix = glm::rotate(matrix, -platform.pitch, glm::vec3(1.0f, 0.0f, 0.0f));
	matrix = glm::scale(matrix, platform.scale);
	return matrix;
}

void MobilePlatformSystem::place(Platform& platform, float elapsedTicks, bool applyTurn)
{
	float t = 0.0f;
	bool returning = false;
	if (platform.durationTicks > 1.0e-4f)
	{
		const float period = platform.circular ? platform.durationTicks : platform.durationTicks * 2.0f;
		float wrapped = std::fmod(elapsedTicks, period);
		if (wrapped < 0.0f)
			wrapped += period;
		t = wrapped / platform.durationTicks;
		if (!platform.circular && t > 1.0f)
		{
			t = 2.0f - t;
			returning = true;
		}
		t = glm::clamp(t, 0.0f, 1.0f);
	}

	glm::vec3 derivative(0.0f);
	platform.path.sample(t, platform.position, derivative);
	if (applyTurn && platform.turnSpeed > 0.0f)
	{
		const float horizontal = std::sqrt(derivative.x * derivative.x + derivative.z * derivative.z);
		if (horizontal > 1.0e-4f)
		{
			const float target = std::atan2(derivative.z, derivative.x) + glm::half_pi<float>();
			float delta = wrapPi(target - platform.yaw);
			const float step = std::abs(platform.turnSpeed);
			delta = glm::clamp(delta, -step, step);
			platform.yaw += delta;
		}
	}

	if (platform.moveOne && platform.active && !returning && t >= kMoveOneEnd)
	{
		platform.active = false;
		platform.finished = true;
	}
}

void MobilePlatformSystem::rewind(Platform& platform, bool active)
{
	platform.finished = false;
	platform.elapsedTicks = platform.startElapsed;
	platform.active = active;
	platform.yaw = platform.initialYaw;
	if (platform.drop)
	{
		platform.hanging = true;
		platform.falling = false;
		platform.fallTicks = 0.0f;
		platform.previewEnabled = false;
		platform.position = platform.hangPos;
		platform.prevPosition = platform.hangPos;
		platform.prevYaw = platform.yaw;
		return;
	}
	platform.previewEnabled = active;
	place(platform, platform.elapsedTicks, false);
	platform.prevPosition = platform.position;
	platform.prevYaw = platform.yaw;
}

void MobilePlatformSystem::beginFall(Platform& platform)
{
	if (!platform.drop || !platform.hanging)
		return;
	platform.hanging = false;
	platform.falling = true;
	platform.fallTicks = 0.0f;
	platform.finished = false;
	platform.active = true;
	platform.elapsedTicks = platform.startElapsed;
	platform.yaw = platform.initialYaw;
	platform.prevYaw = platform.yaw;
	glm::vec3 derivative(0.0f);
	platform.path.sample(0.0f, platform.fallTarget, derivative);
	const float distance = glm::length(platform.fallTarget - platform.hangPos);
	if (platform.speed > 1.0e-4f && distance > 1.0e-4f)
		platform.fallDuration = (distance / platform.speed) * 30.0f;
	else
		platform.fallDuration = 0.0f;
	platform.position = platform.hangPos;
	platform.prevPosition = platform.hangPos;
	platform.previewEnabled = true;
}

void MobilePlatformSystem::step(Platform& platform)
{
	platform.prevPosition = platform.position;
	platform.prevYaw = platform.yaw;
	if (platform.hanging)
	{
		platform.position = platform.hangPos;
		platform.yaw = platform.initialYaw;
		return;
	}
	if (platform.falling)
	{
		platform.fallTicks += 1.0f;
		const float span = platform.fallDuration;
		const float u = span <= 1.0e-4f ? 1.0f : glm::clamp(platform.fallTicks / span, 0.0f, 1.0f);
		platform.position = glm::mix(platform.hangPos, platform.fallTarget, u);
		platform.yaw = platform.initialYaw;
		if (u >= 1.0f)
		{
			platform.falling = false;
			platform.elapsedTicks = platform.startElapsed;
			place(platform, platform.elapsedTicks, false);
		}
		return;
	}
	if (platform.active && platform.durationTicks > 1.0e-4f)
		platform.elapsedTicks += 1.0f;
	place(platform, platform.elapsedTicks, true);
}

void MobilePlatformSystem::load(std::vector<Ty1Instance>& instances)
{
	clear();
	m_platformOf.assign(instances.size(), -1);
	m_riderOf.assign(instances.size(), -1);
	m_triggerOn.assign(instances.size(), 0);

	for (int index = 0; index < static_cast<int>(instances.size()); index++)
	{
		Ty1Instance& instance = instances[static_cast<size_t>(index)];
		const std::string type = lowerCopy(instance.typeName);
		const bool drop = type == "droppontoon";
		if (type != "airplatform" && type != "pontoon" && !drop)
			continue;

		Platform platform;
		platform.instance = index;
		platform.objectId = instance.objectId;
		platform.drop = drop;
		platform.pitch = instance.rotation.x;
		platform.roll = instance.rotation.z;
		platform.yaw = instance.rotation.y;
		platform.scale = instance.scale;
		platform.speed = fieldFloat(instance, "speed", 0.0f);
		platform.turnSpeed = fieldFloat(instance, "turnSpeed", 0.0f);
		platform.active = fieldBool(instance, "bActive", true);
		platform.authoredActive = platform.active;
		platform.previewEnabled = drop ? false : platform.active;
		platform.moveOne = fieldBool(instance, "bMoveOne", false);
		const bool nonCircular = fieldBool(instance, "bNonCircular", false);
		float startTime = fieldFloat(instance, "startTime", 0.0f);
		if (startTime < 0.0f)
			startTime = 0.0f;
		if (startTime > 1.0f)
			startTime = 1.0f;

		std::vector<glm::vec3> points;
		points.reserve(instance.waypoints.size() + 1);
		points.push_back(instance.position);
		points.insert(points.end(), instance.waypoints.begin(), instance.waypoints.end());
		platform.circular = points.size() >= 3 && !nonCircular;
		platform.path = buildPath(points, platform.circular);
		if (platform.speed > 1.0e-4f && platform.path.totalLength > 1.0e-4f)
			platform.durationTicks = (platform.path.totalLength / platform.speed) * 30.0f;
		platform.elapsedTicks = startTime * platform.durationTicks;
		platform.startElapsed = platform.elapsedTicks;

		if (!drop && platform.turnSpeed > 0.0f && points.size() >= 2)
		{
			const glm::vec3 chord = points[1] - points[0];
			const float horizontal = std::sqrt(chord.x * chord.x + chord.z * chord.z);
			if (horizontal > 1.0e-4f)
				platform.yaw = std::atan2(chord.z, chord.x) + glm::half_pi<float>();
		}
		platform.initialYaw = platform.yaw;
		if (drop)
		{
			platform.hangPos = instance.position;
			const Ty1Field* hang = fieldNamed(instance, "hangPos");
			if (hang != nullptr)
				readVec3(hang->value, platform.hangPos);
			platform.hanging = true;
			platform.position = platform.hangPos;
			platform.yaw = platform.initialYaw;
		}
		else
			place(platform, platform.elapsedTicks, false);
		platform.prevPosition = platform.position;
		platform.prevYaw = platform.yaw;

		if (instance.hasAabb && drop)
			expandAround(instance, platform.hangPos, boundsRadius(instance));

		if (instance.hasAabb && platform.path.nodes.size() >= 2)
		{
			const float radius = boundsRadius(instance);
			const int segments = static_cast<int>(platform.path.nodes.size()) - 1;
			for (int segment = 0; segment < segments; segment++)
			{
				for (int step = 0; step <= 4; step++)
				{
					glm::vec3 position;
					glm::vec3 derivative;
					hermite(platform.path.nodes[static_cast<size_t>(segment)],
						platform.path.nodes[static_cast<size_t>(segment + 1)],
						step / 4.0f, position, derivative);
					expandAround(instance, position, radius);
				}
			}
		}

		m_platformOf[static_cast<size_t>(index)] = static_cast<int>(m_platforms.size());
		m_platforms.push_back(std::move(platform));
	}

	std::unordered_map<int, int> idToPlatform;
	for (int i = 0; i < static_cast<int>(m_platforms.size()); i++)
	{
		const int id = m_platforms[static_cast<size_t>(i)].objectId;
		if (id > 0)
			idToPlatform.emplace(id, i);
	}

	for (int index = 0; index < static_cast<int>(instances.size()); index++)
	{
		if (m_platformOf[static_cast<size_t>(index)] >= 0)
			continue;
		Ty1Instance& instance = instances[static_cast<size_t>(index)];
		const Ty1Field* field = fieldNamed(instance, "platformID");
		if (field == nullptr)
			continue;
		int platformId = 0;
		if (!readId(field->value, platformId) || platformId <= 0)
			continue;
		const auto found = idToPlatform.find(platformId);
		if (found == idToPlatform.end())
			continue;

		const Platform& platform = m_platforms[static_cast<size_t>(found->second)];
		const glm::vec3 authored = instance.position + glm::vec3(0.0f, instance.drawLiftY, 0.0f);
		const glm::mat4 deck = deckMatrix(platform, platform.position, platform.yaw);
		const glm::vec3 local(glm::inverse(deck) * glm::vec4(authored, 1.0f));

		Rider rider;
		rider.instance = index;
		rider.platform = found->second;
		rider.local = local;
		rider.pitch = instance.rotation.x;
		rider.roll = instance.rotation.z;
		rider.yawOffset = instance.rotation.y - platform.initialYaw;
		rider.drawLiftY = instance.drawLiftY;
		m_riderOf[static_cast<size_t>(index)] = static_cast<int>(m_riders.size());
		m_riders.push_back(rider);

		if (instance.hasAabb && platform.path.nodes.size() >= 2)
		{
			const float reach = glm::length(authored - platform.position) + boundsRadius(instance);
			const int segments = static_cast<int>(platform.path.nodes.size()) - 1;
			for (int segment = 0; segment < segments; segment++)
			{
				for (int step = 0; step <= 4; step++)
				{
					glm::vec3 position;
					glm::vec3 derivative;
					hermite(platform.path.nodes[static_cast<size_t>(segment)],
						platform.path.nodes[static_cast<size_t>(segment + 1)],
						step / 4.0f, position, derivative);
					expandAround(instance, position, reach);
				}
			}
		}
	}
}

int MobilePlatformSystem::update(float dt)
{
	if (m_paused || m_platforms.empty())
		return 0;
	m_accumulator += std::min(dt, kTickSeconds * 5.0f);
	if (m_accumulator < kTickSeconds)
		return 0;
	m_accumulator = std::fmod(m_accumulator, kTickSeconds);
	m_tick++;
	for (Platform& platform : m_platforms)
		step(platform);
	return 1;
}

void MobilePlatformSystem::setPaused(bool paused)
{
	m_paused = paused;
}

float MobilePlatformSystem::alpha() const
{
	return glm::clamp(m_accumulator / kTickSeconds, 0.0f, 1.0f);
}

float MobilePlatformSystem::propellerAngle() const
{
	const float seconds = (static_cast<float>(m_tick) + alpha()) * kTickSeconds;
	return std::fmod(seconds, 1.0f) * glm::two_pi<float>() * 10.0f;
}

int MobilePlatformSystem::platformIndexForId(int objectId) const
{
	if (objectId <= 0)
		return -1;
	for (int i = 0; i < static_cast<int>(m_platforms.size()); i++)
	{
		if (m_platforms[static_cast<size_t>(i)].objectId == objectId)
			return i;
	}
	return -1;
}

void MobilePlatformSystem::message(int objectId, const std::string& name)
{
	const int index = platformIndexForId(objectId);
	if (index < 0)
		return;
	const std::string key = lowerCopy(trimCopy(name));
	Platform& platform = m_platforms[static_cast<size_t>(index)];
	if (key == "activate")
	{
		if (!platform.finished)
			platform.active = true;
	}
	else if (key == "deactivate")
	{
		platform.active = false;
	}
	else if (key == "fall")
		beginFall(platform);
}

bool MobilePlatformSystem::platformPreview(int instanceIndex) const
{
	if (instanceIndex < 0 || instanceIndex >= static_cast<int>(m_platformOf.size()))
		return false;
	const int slot = m_platformOf[static_cast<size_t>(instanceIndex)];
	if (slot < 0)
		return false;
	return m_platforms[static_cast<size_t>(slot)].previewEnabled;
}

void MobilePlatformSystem::setPlatformPreview(int instanceIndex, bool enabled)
{
	if (instanceIndex < 0 || instanceIndex >= static_cast<int>(m_platformOf.size()))
		return;
	const int slot = m_platformOf[static_cast<size_t>(instanceIndex)];
	if (slot < 0)
		return;
	Platform& platform = m_platforms[static_cast<size_t>(slot)];
	if (enabled && platform.drop)
	{
		rewind(platform, true);
		beginFall(platform);
		return;
	}
	rewind(platform, enabled);
	platform.previewEnabled = enabled;
}

bool MobilePlatformSystem::triggerPreview(int instanceIndex) const
{
	if (instanceIndex < 0 || instanceIndex >= static_cast<int>(m_triggerOn.size()))
		return false;
	return m_triggerOn[static_cast<size_t>(instanceIndex)] != 0;
}

void MobilePlatformSystem::setTriggerPreview(int instanceIndex, bool enabled, const std::vector<Ty1Instance>& instances)
{
	if (instanceIndex < 0 || instanceIndex >= static_cast<int>(instances.size()))
		return;
	if (instanceIndex >= static_cast<int>(m_triggerOn.size()))
		return;
	m_triggerOn[static_cast<size_t>(instanceIndex)] = enabled ? 1 : 0;

	const Ty1Instance& instance = instances[static_cast<size_t>(instanceIndex)];
	for (const Ty1Field& field : instance.fields)
	{
		if (lowerCopy(field.value) != "event")
			continue;
		const Ty1Field* target = childNamed(field, "targetid");
		const Ty1Field* messageField = childNamed(field, "message");
		if (target == nullptr || messageField == nullptr)
			continue;
		int targetId = 0;
		if (!readId(target->value, targetId) || targetId <= 0)
			continue;
		const int slot = platformIndexForId(targetId);
		if (slot < 0)
		{
			if (enabled)
			{
				const std::string text = trimCopy(messageField->value);
				if (!text.empty() && lowerCopy(text) != "none")
					message(targetId, text);
			}
			continue;
		}
		Platform& platform = m_platforms[static_cast<size_t>(slot)];
		rewind(platform, platform.authoredActive);
		if (enabled)
		{
			const std::string text = trimCopy(messageField->value);
			if (!text.empty() && lowerCopy(text) != "none")
				message(targetId, text);
			platform.previewEnabled = platform.active || platform.finished;
		}
		else if (platform.drop)
			platform.previewEnabled = false;
		else
			platform.previewEnabled = platform.authoredActive;
	}
}

bool MobilePlatformSystem::drawPose(int instanceIndex, float alpha, glm::vec3& position, glm::vec3& rotation) const
{
	if (instanceIndex < 0 || instanceIndex >= static_cast<int>(m_platformOf.size()))
		return false;

	const int platformIndex = m_platformOf[static_cast<size_t>(instanceIndex)];
	if (platformIndex >= 0)
	{
		const Platform& platform = m_platforms[static_cast<size_t>(platformIndex)];
		const float yawDelta = wrapPi(platform.yaw - platform.prevYaw);
		position = glm::mix(platform.prevPosition, platform.position, alpha);
		rotation = glm::vec3(platform.pitch, platform.prevYaw + yawDelta * alpha, platform.roll);
		return true;
	}

	const int riderIndex = m_riderOf[static_cast<size_t>(instanceIndex)];
	if (riderIndex < 0)
		return false;
	const Rider& rider = m_riders[static_cast<size_t>(riderIndex)];
	const Platform& platform = m_platforms[static_cast<size_t>(rider.platform)];
	const float yawDelta = wrapPi(platform.yaw - platform.prevYaw);
	const float yaw = platform.prevYaw + yawDelta * alpha;
	const glm::vec3 place = glm::mix(platform.prevPosition, platform.position, alpha);
	const glm::mat4 deck = deckMatrix(platform, place, yaw);
	const glm::vec3 origin(deck * glm::vec4(rider.local, 1.0f));
	position = origin - glm::vec3(0.0f, rider.drawLiftY, 0.0f);
	rotation = glm::vec3(rider.pitch, yaw + rider.yawOffset, rider.roll);
	return true;
}
