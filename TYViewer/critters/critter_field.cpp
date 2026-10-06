#include "critter_field.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include <glm/geometric.hpp>
#include <glm/common.hpp>

#include "debug.h"
#include "graphics/mesh.h"
#include "loader/ty1_level.h"
#include "model.h"

namespace
{
	const float kPi = 3.14159265f;
	const float kTickSeconds = 1.0f / 30.0f;
	// Guess. MKAnimScript advances by `advanceAmount` (0.5 by default) each Animate,
	// and the .bad files are authored at 30 fps for a lockTo30 game, so one frame per tick.
	const float kAnimAdvance = 1.0f;
	// Guess. Ticks to tween between two anims on a state change.
	const int kTweenTicks = 6;
	// Guess. Moth_IsFloorValid: a surface counts as a floor when it faces up this much.
	const float kFloorValidDot = 0.6f;
	const int kMaxCritters = 64;

	// Every speed, turn rate, and timer below is a guess tuned by eye. The game's
	// CritterDesc2 values come from global.model / the Desc constructors, which are
	// not decompiled.
	const CritterSpecies kSpecies[] =
	{
		// type          move                  script                     count speed run   turn  idle      idle anims                                    move anims                    alt anims         land   hop    hopH  align
		{ "IBIS",        CritterMove::Ground,  "Act_61_Ibis.bad",         1,    2.0f, 7.0f, 0.12f, 60, 240,  "Idle00|Idle01|Idle02",                      "walk cycle00|walk cycle01",  "run",            "",     0.0f,  0.0f, false },
		{ "GECKO",       CritterMove::Ground,  "Act_55_Gecko.bad",        1,    3.0f, 3.0f, 0.20f, 30, 150,  "idle01_breath",                             "walk",                       "",               "",     0.0f,  0.0f, true },
		{ "COW",         CritterMove::Ground,  "act_98_cow.bad",          1,    1.2f, 1.2f, 0.05f, 150, 450, "idle|eatgrass",                             "walk",                       "",               "",     0.0f,  0.0f, false },
		{ "SHEEP",       CritterMove::Ground,  "act_97_sheep.bad",        1,    1.8f, 5.0f, 0.08f, 90, 300,  "idle",                                      "walk",                       "run",            "",     0.0f,  0.0f, false },
		{ "SOLDIERCRAB", CritterMove::Ground,  "Act_45_SoldierCrab.bad",  1,    4.0f, 8.0f, 0.25f, 20, 90,   "idle01_look|idle02_eat",                    "run",                        "runAway",        "",     0.0f,  0.0f, false },
		{ "WALLABY",     CritterMove::Hopper,  "ACT_46_WALLABY.BAD",      1,    0.0f, 0.0f, 0.15f, 60, 200,  "idle01_look01|idle01_look02|idle01_look03", "",                           "hop|small_hop",  "",     180.0f, 50.0f, false },
		{ "FROG",        CritterMove::Hopper,  "critter_frog.bad",        1,    0.0f, 0.0f, 0.20f, 30, 120,  "idle",                                      "",                           "jump",           "",     90.0f, 40.0f, false },
		{ "GRASSHOPPER", CritterMove::Hopper,  "Act_14_Grasshopper.bad",  1,    0.0f, 0.0f, 0.30f, 20, 90,   "idle",                                      "",                           "jump",           "",     120.0f, 60.0f, false },
		{ "MOTH",        CritterMove::FlySit,  "act1_10_moth.bad",        1,    4.0f, 4.0f, 0.15f, 60, 240,  "sit",                                       "fly",                        "",               "",     0.0f,  80.0f, false },
		{ "BUTTERFLY",   CritterMove::FlySit,  "Act_09_Butterfly.bad",    1,    3.5f, 3.5f, 0.12f, 60, 240,  "sit",                                       "fly",                        "",               "",     0.0f,  80.0f, false },
		// dragonFlyDesc is a FlyDesc. The levels pack DFly_A3.dds and never Act_16_Dragonfly.mdl,
		// whose "hover" anim only turns z_root 180 degrees and snaps back.
		{ "DRAGONFLY",   CritterMove::Hover,   nullptr,                   1,    10.0f, 10.0f, 0.40f, 15, 90, "",                                          "",                           "",               "",     0.0f,  100.0f, false, "DFly_A3.dds", 40.0f, 16, true, false },
		{ "SEAGULL",     CritterMove::Flock,   "Act_33_Seagull.bad",      1,    10.0f, 10.0f, 0.04f, 0, 0,   "",                                          "flap",                       "glide",          "",     0.0f,  300.0f, false },
		{ "BIRDFLOCK",   CritterMove::Flock,   "ACT1_37_BIRDFLOCK.BAD",   1,    12.0f, 12.0f, 0.05f, 0, 0,   "",                                          "flap",                       "glide",          "",     0.0f,  300.0f, false },
		{ "KINGFISHER",  CritterMove::Perch,   "Act_21_Kingfisher.bad",   1,    9.0f, 9.0f, 0.08f, 90, 300,  "idle01",                                    "fly",                        "glide",          "land", 0.0f,  200.0f, false },
		{ "KOOKABURRA",  CritterMove::Perch,   "act1_04_kookaburra.bad",  1,    9.0f, 9.0f, 0.08f, 90, 300,  "idle01|idle02|idle03",                      "flap01",                     "glide01",        "land", 0.0f,  200.0f, false },
		{ "LORIKEET",    CritterMove::Perch,   "Act_59_Lorikeet.bad",     1,    8.0f, 8.0f, 0.10f, 60, 240,  "idle01_LR|idle02|idle03|idle04",            "fly",                        "fly",            "",     0.0f,  150.0f, false },
		{ "CUTTLEFISH",  CritterMove::Swim,    "act1_05_cuttlefish.bad",  1,    1.5f, 6.0f, 0.06f, 30, 120,  "swim",                                      "swim",                       "swim",           "",     0.0f,  30.0f, false },
		{ "SEAHORSE",    CritterMove::Swim,    "act1_01_seahorse.bad",    1,    0.6f, 0.6f, 0.04f, 60, 200,  "idle01",                                    "swim",                       "",               "",     0.0f,  30.0f, false },
		{ "FISHSHOAL",   CritterMove::Swim,    nullptr,                   1,    3.0f, 3.0f, 0.08f, 0, 0,     "",                                          "",                           "",               "",     0.0f,  30.0f, false },
		{ "CLOWNFISH",   CritterMove::Swim,    nullptr,                   1,    2.0f, 2.0f, 0.08f, 20, 80,   "",                                          "",                           "",               "",     0.0f,  30.0f, false },
		{ "GUPPY",       CritterMove::Swim,    nullptr,                   1,    2.5f, 2.5f, 0.10f, 20, 80,   "",                                          "",                           "",               "",     0.0f,  30.0f, false },
		{ "TURTLE",      CritterMove::Turtle,  "Act_32_Turtle.bad",       1,    2.0f, 2.0f, 0.02f, 0, 0,     "Swim",                                      "Swim",                       "",               "",     0.0f,  0.0f, false },
		{ "WATERSKIMMER",CritterMove::Surface, "Act_57_WaterSkimmer.bad", 1,    6.0f, 6.0f, 0.30f, 20, 80,   "idle",                                      "skim",                       "",               "",     0.0f,  0.0f, false },
		{ "WATERDRAGON", CritterMove::Point,   "Act_20_WaterDragon.bad",  1,    0.0f, 0.0f, 0.0f, 150, 400,  "idle",                                      "",                           "alert",          "",     0.0f,  0.0f, false },
		{ "SYNKERFROG",  CritterMove::Point,   "Act1_13_SynkerFrog.bad",  1,    0.0f, 0.0f, 0.0f, 150, 400,  "nothing",                                   "",                           "",               "",     0.0f,  0.0f, false },
		{ "FLY",         CritterMove::Sprite,  nullptr,                   8,    6.0f, 6.0f, 0.30f, 0, 0,     "",                                          "",                           "",               "",     0.0f,  0.0f, false },
		// fx_072 is packed by exactly the levels with fireflies; global.mad gives it blend 1 (additive).
		{ "FIREFLY",     CritterMove::Sprite,  nullptr,                   6,    3.0f, 3.0f, 0.15f, 0, 0,     "",                                          "",                           "",               "",     0.0f,  0.0f, false, "fx_072.dds", 20.0f, 8, false, true },
	};

	std::string upper(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		return value;
	}

	std::string lower(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}

	std::vector<int> resolveAnims(const AnimScriptData& script, const char* names)
	{
		std::vector<int> out;
		if (names == nullptr)
			return out;
		std::string list(names);
		size_t start = 0;
		while (start <= list.size())
		{
			const size_t bar = list.find('|', start);
			const std::string name = list.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
			if (!name.empty())
			{
				const int index = script.find(name);
				if (index >= 0)
					out.push_back(index);
			}
			if (bar == std::string::npos)
				break;
			start = bar + 1;
		}
		return out;
	}

	// Ticks one pass of an anim takes at kAnimAdvance.
	int animTicks(const AnimScriptData& script, int anim)
	{
		if (anim < 0 || anim >= static_cast<int>(script.anims.size()))
			return 15;
		float ticks = 0.0f;
		for (const AnimScriptRange& range : script.anims[static_cast<size_t>(anim)].ranges)
			ticks += std::abs(static_cast<float>(range.end - range.start)) * static_cast<float>(std::max(range.speed, 1));
		return std::max(1, static_cast<int>(ticks / kAnimAdvance));
	}

	// Turns `from` toward `to` (both unit) by at most `maxAngle`, about `fallbackAxis` when opposite.
	glm::vec3 rotateToward(const glm::vec3& from, const glm::vec3& to, float maxAngle, const glm::vec3& fallbackAxis)
	{
		const float dot = std::clamp(glm::dot(from, to), -1.0f, 1.0f);
		const float angle = std::acos(dot);
		if (angle <= maxAngle)
			return to;
		glm::vec3 axis = glm::cross(from, to);
		const float length = glm::length(axis);
		axis = length > 1e-5f ? axis / length : fallbackAxis;
		const float c = std::cos(maxAngle);
		const float s = std::sin(maxAngle);
		const glm::vec3 rotated = from * c + glm::cross(axis, from) * s + axis * glm::dot(axis, from) * (1.0f - c);
		return glm::normalize(rotated);
	}

	glm::vec3 flatten(const glm::vec3& v, const glm::vec3& up)
	{
		return v - up * glm::dot(v, up);
	}

	glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback)
	{
		const float length = glm::length(v);
		return length > 1e-5f ? v / length : fallback;
	}

	int fieldCount(const Ty1Instance& instance, int fallback)
	{
		for (const Ty1Field& field : instance.fields)
		{
			if (lower(field.key) == "count")
			{
				char* end = nullptr;
				const long value = std::strtol(field.value.c_str(), &end, 10);
				if (end != field.value.c_str())
					return static_cast<int>(value);
			}
		}
		return fallback;
	}
}

const CritterSpecies* findCritterSpecies(const std::string& typeName)
{
	std::string key = upper(typeName);
	if (key.size() > 6 && key.compare(0, 6, "STATIC") == 0)
		key = key.substr(6);
	for (const CritterSpecies& species : kSpecies)
	{
		if (key == species.type)
			return &species;
	}
	return nullptr;
}

CritterField::CritterField(const Ty1Instance& instance, int instanceIndex, const CritterSpecies& species,
	CritterAssets* assets, const CritterFloor* floor)
	: m_species(species),
	m_assets(assets),
	m_floor(floor),
	m_instanceIndex(instanceIndex),
	m_seed(static_cast<uint32_t>(instanceIndex) * 2654435761u + 0x9E3779B9u)
{
	m_center = instance.position;
	m_waypoints = instance.waypoints;

	Ty1Instance oriented;
	oriented.rotation = instance.rotation;
	m_axes = glm::mat3(ty1InstanceMatrix(oriented));
	for (int i = 0; i < 3; i++)
		m_axes[i] = safeNormalize(m_axes[i], glm::vec3(i == 0, i == 1, i == 2));

	if (instance.roamSize != glm::vec3(0.0f))
	{
		m_half = instance.roamSize * 0.5f;
	}
	else if (instance.rangeSphere && instance.sphereRadius > 0.0f)
	{
		// Guess at the height a range field spans. Ground critters search for floor
		// across it; swimmers stay near the placed height.
		const float r = instance.sphereRadius;
		m_round = true;
		m_axes = glm::mat3(1.0f);
		float vertical = r;
		if (species.move == CritterMove::Swim)
			vertical = std::min(r * 0.25f, 150.0f);
		else if (species.move == CritterMove::Turtle || species.move == CritterMove::Surface)
			vertical = 0.0f;
		m_half = glm::vec3(r, vertical, r);
	}

	spawn(std::clamp(fieldCount(instance, species.defaultCount), 0, kMaxCritters));
}

float CritterField::random01()
{
	// xorshift32: the same level always lays its critters out the same way.
	m_seed ^= m_seed << 13;
	m_seed ^= m_seed >> 17;
	m_seed ^= m_seed << 5;
	return static_cast<float>(m_seed & 0xFFFFFF) / static_cast<float>(0x1000000);
}

int CritterField::randomTicks(int lo, int hi)
{
	if (hi <= lo)
		return std::max(lo, 1);
	return lo + static_cast<int>(random01() * static_cast<float>(hi - lo + 1));
}

int CritterField::pickAnim(const std::vector<int>& anims)
{
	if (anims.empty())
		return -1;
	return anims[std::min(anims.size() - 1, static_cast<size_t>(random01() * static_cast<float>(anims.size())))];
}

void CritterField::playAnim(Critter& critter, const std::vector<int>& anims, bool restart)
{
	if (m_assets == nullptr || !m_assets->animated || anims.empty())
		return;
	// Compare against the tween target: re-issuing TweenAnim every tick would reset
	// the tween count and hold the pose on the new anim's first frame.
	const int current = critter.anim.target();
	const bool playing = std::find(anims.begin(), anims.end(), current) != anims.end();
	if (playing && !restart)
		return;
	const int next = pickAnim(anims);
	if (next == current)
		critter.anim.setAnim(next);
	else
		critter.anim.tweenAnim(next, restart ? 2 : kTweenTicks);
}

glm::vec3 CritterField::toLocal(const glm::vec3& world) const
{
	return glm::transpose(m_axes) * (world - m_center);
}

glm::vec3 CritterField::toWorld(const glm::vec3& local) const
{
	return m_center + m_axes * local;
}

glm::vec3 CritterField::clipPointToField(const glm::vec3& point) const
{
	glm::vec3 local = toLocal(point);
	if (m_round)
	{
		const float flat = std::sqrt(local.x * local.x + local.z * local.z);
		if (flat > m_half.x && flat > 0.0f)
		{
			local.x *= m_half.x / flat;
			local.z *= m_half.x / flat;
		}
		local.y = std::clamp(local.y, -m_half.y, m_half.y);
	}
	else
	{
		local = glm::clamp(local, -m_half, m_half);
	}
	return toWorld(local);
}

bool CritterField::getFloor(const glm::vec3& from, float above, float below, glm::vec3& outPoint, glm::vec3& outNormal) const
{
	if (m_floor == nullptr || m_floor->empty())
		return false;
	const glm::vec3 down = -fieldUp();
	const glm::vec3 start = from - down * above;
	float distance = 0.0f;
	glm::vec3 normal(0.0f);
	if (!m_floor->cast(start, down, above + below, distance, normal))
		return false;
	outPoint = start + down * distance;
	outNormal = normal;
	return true;
}

bool CritterField::generatePointInField(glm::vec3& out, bool onFloor)
{
	for (int attempt = 0; attempt < 8; attempt++)
	{
		glm::vec3 local;
		if (m_round)
		{
			const float r = m_half.x * std::sqrt(random01());
			const float a = random01() * 2.0f * kPi;
			local = glm::vec3(std::cos(a) * r, randomRange(-m_half.y, m_half.y), std::sin(a) * r);
		}
		else
		{
			local = glm::vec3(randomRange(-m_half.x, m_half.x), randomRange(-m_half.y, m_half.y), randomRange(-m_half.z, m_half.z));
		}

		if (!onFloor)
		{
			out = toWorld(local);
			return true;
		}

		// From the top of the field down past its bottom. The margins are guesses:
		// placed fields often sit slightly into the ground or above it.
		const float margin = 300.0f;
		local.y = m_half.y;
		glm::vec3 hit, normal;
		if (!getFloor(toWorld(local), margin, m_half.y * 2.0f + margin * 2.0f, hit, normal))
			continue;
		if (glm::dot(normal, fieldUp()) < kFloorValidDot)
			continue;
		out = hit;
		return true;
	}
	return false;
}

void CritterField::spawn(int count)
{
	const CritterMove move = m_species.move;
	const bool grounded = move == CritterMove::Ground || move == CritterMove::Hopper
		|| (move == CritterMove::Point && m_species.alignToFloor);
	m_shoalTarget = m_center;

	m_critters.resize(static_cast<size_t>(count));
	for (size_t i = 0; i < m_critters.size(); i++)
	{
		Critter& critter = m_critters[i];
		glm::vec3 point = m_center;
		if (move == CritterMove::Point)
		{
			// The water dragon is placed by hand. Extra synker frogs spread over the floor.
			if (i > 0 && !generatePointInField(point, true))
				point = m_center;
		}
		else if (grounded || move == CritterMove::Perch)
		{
			if (!generatePointInField(point, true))
			{
				glm::vec3 normal;
				if (!getFloor(m_center, 300.0f, 1000.0f, point, normal))
					point = m_center;
			}
		}
		else
		{
			generatePointInField(point, false);
		}
		if (move == CritterMove::Turtle || move == CritterMove::Surface)
		{
			glm::vec3 local = toLocal(point);
			local.y = 0.0f;
			point = toWorld(local);
		}

		critter.position = point;
		critter.up = fieldUp();
		if (move == CritterMove::Point && i == 0)
			critter.forward = -m_axes[2];
		else
		{
			const float yaw = random01() * 2.0f * kPi;
			critter.forward = safeNormalize(m_axes[0] * std::sin(yaw) - m_axes[2] * std::cos(yaw), -m_axes[2]);
		}
		critter.speed = m_species.speed;

		switch (move)
		{
		case CritterMove::FlySit:
		case CritterMove::Flock:
			critter.state = CritterState::Fly;
			break;
		case CritterMove::Hover:
		case CritterMove::Sprite:
			critter.state = CritterState::FindPoint;
			break;
		case CritterMove::Swim:
			critter.state = (std::string(m_species.type) == "FISHSHOAL") ? CritterState::Cruise : CritterState::Swim;
			break;
		case CritterMove::Turtle:
			critter.state = CritterState::Swim;
			break;
		default:
			critter.state = CritterState::Idle;
			break;
		}
		critter.timer = randomTicks(m_species.idleMin / 2, m_species.idleMax);
		critter.target = point;
		if (move == CritterMove::Flock || move == CritterMove::FlySit || move == CritterMove::Swim
			|| move == CritterMove::Turtle || move == CritterMove::Sprite)
			generatePointInField(critter.target, false);

		if (m_assets != nullptr && m_assets->animated)
		{
			critter.pose.init(&m_assets->anm, m_assets->origins);
			critter.anim.init(&m_assets->script);
			const std::vector<int>& first =
				(critter.state == CritterState::Idle && !m_assets->idleAnims.empty()) ? m_assets->idleAnims
				: !m_assets->moveAnims.empty() ? m_assets->moveAnims : m_assets->idleAnims;
			const int anim = pickAnim(first);
			if (anim >= 0)
				critter.anim.setAnim(anim);
			// Desynchronise a flock that all spawned on the same frame.
			const int skip = randomTicks(0, 45);
			for (int s = 0; s < skip; s++)
				critter.anim.animate(kAnimAdvance);
			critter.anim.apply(critter.pose);
			critter.pose.calculateMatrices();
		}

		critter.prevPosition = critter.position;
		critter.prevForward = critter.forward;
		critter.prevUp = critter.up;
	}
}

void CritterField::turnToward(Critter& critter, const glm::vec3& direction, float rate) const
{
	const glm::vec3 wanted = safeNormalize(direction, critter.forward);
	critter.forward = rotateToward(critter.forward, wanted, rate, critter.up);
}

// Moves along the forward direction. `keepUpright` keeps the motion in the field's plane.
void CritterField::stepToward(Critter& critter, float speed, bool keepUpright)
{
	glm::vec3 toTarget = critter.target - critter.position;
	if (keepUpright)
		toTarget = flatten(toTarget, critter.up);
	turnToward(critter, toTarget, m_species.turnRate);
	if (keepUpright)
		critter.forward = safeNormalize(flatten(critter.forward, critter.up), critter.forward);
	critter.position += critter.forward * speed;
}

bool CritterField::sampleFloor(Critter& critter, float above, float below, glm::vec3& outPoint, glm::vec3& outNormal)
{
	if (m_floor == nullptr || m_floor->empty())
		return false;
	const glm::vec3 down = -fieldUp();
	const glm::vec3 start = critter.position - down * above;
	const float reach = above + below;
	float distance = 0.0f;
	glm::vec3 normal(0.0f);
	if (m_floor->testTriangle(critter.floorTriangle, start, down, reach, distance, normal))
	{
		outPoint = start + down * distance;
		outNormal = normal;
		return true;
	}
	int triangle = -1;
	if (!m_floor->cast(start, down, reach, distance, normal, &triangle))
	{
		critter.floorTriangle = -1;
		return false;
	}
	critter.floorTriangle = triangle;
	outPoint = start + down * distance;
	outNormal = normal;
	return true;
}

bool CritterField::snapToFloor(Critter& critter)
{
	glm::vec3 hit, normal;
	// Guess: the step a walker climbs or drops in one tick.
	if (!sampleFloor(critter, 60.0f, 120.0f, hit, normal))
		return false;
	if (glm::dot(normal, fieldUp()) < 0.35f)
	{
		// Don't keep a wall. The next tick has to search again.
		critter.floorTriangle = -1;
		return false;
	}
	critter.position = hit;
	if (m_species.alignToFloor)
	{
		critter.up = safeNormalize(critter.up + (normal - critter.up) * 0.3f, normal);
		critter.forward = safeNormalize(flatten(critter.forward, critter.up), critter.forward);
	}
	return true;
}

void CritterField::keepAboveFloor(Critter& critter, float clearance)
{
	glm::vec3 hit, normal;
	if (sampleFloor(critter, clearance, clearance, hit, normal))
	{
		const float height = glm::dot(critter.position - hit, fieldUp());
		if (height < clearance)
			critter.position += fieldUp() * (clearance - height) * 0.5f;
	}
}

// Ibis_Idle / Ibis_Walk / Ibis_Run, Gecko_Idle / Gecko_Walk, SmallCrab_Scurry.
void CritterField::updateGround(Critter& critter)
{
	if (critter.state == CritterState::Idle)
	{
		playAnim(critter, m_assets ? m_assets->idleAnims : std::vector<int>{});
		if (m_assets && m_assets->animated && critter.anim.finished())
			playAnim(critter, m_assets->idleAnims, true);
		if (--critter.timer > 0)
			return;
		glm::vec3 point;
		if (!generatePointInField(point, true))
		{
			critter.timer = randomTicks(m_species.idleMin, m_species.idleMax);
			return;
		}
		critter.target = point;
		const bool run = m_species.runSpeed > m_species.speed && random01() < 0.2f
			&& m_assets && !m_assets->altAnims.empty();
		critter.state = run ? CritterState::Run : CritterState::Walk;
		critter.speed = run ? m_species.runSpeed : m_species.speed;
		// Long walks across a big field still end.
		critter.timer = 30 * 20;
		return;
	}

	playAnim(critter, critter.state == CritterState::Run ? m_assets->altAnims : m_assets->moveAnims);
	const glm::vec3 before = critter.position;
	stepToward(critter, critter.speed, true);
	const glm::vec3 clipped = clipPointToField(critter.position);
	critter.position = clipped;
	if (!snapToFloor(critter))
	{
		critter.position = before;
		critter.timer = 0;
	}

	const float remaining = glm::length(flatten(critter.target - critter.position, critter.up));
	if (remaining <= critter.speed * 2.0f || --critter.timer <= 0)
	{
		critter.state = CritterState::Idle;
		critter.timer = randomTicks(m_species.idleMin, m_species.idleMax);
	}
}

// Frog_Idle / Frog_StartJump. Wallabies chain a few hops between idles.
void CritterField::updateHopper(Critter& critter)
{
	const int jumpAnim = m_assets ? pickAnim(m_assets->altAnims) : -1;
	if (critter.state == CritterState::Idle)
	{
		playAnim(critter, m_assets ? m_assets->idleAnims : std::vector<int>{});
		if (m_assets && m_assets->animated && critter.anim.finished())
			playAnim(critter, m_assets->idleAnims, true);
		if (--critter.timer > 0)
			return;

		const float yaw = random01() * 2.0f * kPi;
		const glm::vec3 direction = m_axes[0] * std::sin(yaw) - m_axes[2] * std::cos(yaw);
		glm::vec3 landing = clipPointToField(critter.position + direction * m_species.hopDistance * randomRange(0.6f, 1.0f));
		glm::vec3 normal;
		if (!getFloor(landing, 150.0f, 300.0f, landing, normal) || glm::dot(normal, fieldUp()) < 0.35f)
		{
			critter.timer = randomTicks(10, 40);
			return;
		}
		critter.target = landing;
		critter.state = CritterState::Walk;
		critter.timer = 30;
		return;
	}

	if (critter.state == CritterState::Walk)
	{
		// Face the landing spot before leaving the ground.
		const glm::vec3 toTarget = flatten(critter.target - critter.position, critter.up);
		turnToward(critter, toTarget, m_species.turnRate);
		const float facing = glm::dot(critter.forward, safeNormalize(toTarget, critter.forward));
		if (facing < 0.95f && --critter.timer > 0)
			return;
		critter.state = CritterState::StartJump;
		critter.jumpFrom = critter.position;
		critter.jumpTicks = 0;
		critter.jumpLength = (m_assets && m_assets->animated && jumpAnim >= 0) ? animTicks(m_assets->script, jumpAnim) : 15;
		if (m_assets && m_assets->animated && jumpAnim >= 0)
		{
			if (critter.anim.current() == jumpAnim)
				critter.anim.setAnim(jumpAnim);
			else
				critter.anim.tweenAnim(jumpAnim, 2);
		}
		return;
	}

	critter.jumpTicks++;
	const float t = std::min(1.0f, static_cast<float>(critter.jumpTicks) / static_cast<float>(critter.jumpLength));
	critter.position = critter.jumpFrom + (critter.target - critter.jumpFrom) * t
		+ fieldUp() * (std::sin(t * kPi) * m_species.hopHeight);
	if (t >= 1.0f)
	{
		critter.position = critter.target;
		critter.state = CritterState::Idle;
		const bool chain = std::string(m_species.type) == "WALLABY" && random01() < 0.6f;
		critter.timer = chain ? 1 : randomTicks(m_species.idleMin, m_species.idleMax);
	}
}

// Moth_Fly / Moth_Idle with Moth_IsFloorValid choosing where to land.
void CritterField::updateFlySit(Critter& critter)
{
	if (critter.state == CritterState::Idle)
	{
		playAnim(critter, m_assets ? m_assets->idleAnims : std::vector<int>{});
		if (--critter.timer > 0)
			return;
		critter.state = CritterState::Fly;
		critter.timer = randomTicks(3, 6);
		generatePointInField(critter.target, false);
		critter.position += fieldUp() * 5.0f;
		return;
	}

	playAnim(critter, m_assets ? m_assets->moveAnims : std::vector<int>{});
	const bool landing = critter.state == CritterState::Land;
	glm::vec3 toTarget = critter.target - critter.position;
	const float distance = glm::length(toTarget);
	// A flutter on top of the straight line.
	toTarget += fieldUp() * std::sin(static_cast<float>(critter.timer + m_instanceIndex) * 0.7f) * 0.2f * distance;
	turnToward(critter, toTarget, m_species.turnRate * (landing ? 2.0f : 1.0f));
	critter.position += critter.forward * std::min(m_species.speed, std::max(distance, 0.5f));
	if (!landing)
	{
		critter.position = clipPointToField(critter.position);
		keepAboveFloor(critter, m_species.hopHeight);
	}

	if (distance > m_species.speed * 2.0f)
		return;
	if (landing)
	{
		critter.position = critter.target;
		critter.forward = safeNormalize(flatten(critter.forward, fieldUp()), critter.forward);
		critter.state = CritterState::Idle;
		critter.timer = randomTicks(m_species.idleMin, m_species.idleMax);
		return;
	}
	if (--critter.timer <= 0)
	{
		glm::vec3 seat;
		if (generatePointInField(seat, true))
		{
			critter.target = seat;
			critter.state = CritterState::Land;
			return;
		}
	}
	generatePointInField(critter.target, false);
}

// Fly_FindPoint / Fly_Moving / Fly_WaitingMove, the dart-and-hover pattern.
void CritterField::updateHover(Critter& critter)
{
	playAnim(critter, m_assets ? m_assets->moveAnims : std::vector<int>{});
	switch (critter.state)
	{
	case CritterState::FindPoint:
	{
		glm::vec3 point;
		if (generatePointInField(point, false))
		{
			critter.target = point;
			critter.state = CritterState::Moving;
		}
		break;
	}
	case CritterState::Moving:
	{
		const glm::vec3 toTarget = critter.target - critter.position;
		const float distance = glm::length(toTarget);
		turnToward(critter, flatten(toTarget, fieldUp()), m_species.turnRate);
		const glm::vec3 direction = safeNormalize(toTarget, critter.forward);
		critter.position += direction * std::min(distance, m_species.speed);
		keepAboveFloor(critter, m_species.hopHeight);
		if (distance <= m_species.speed)
		{
			critter.state = CritterState::WaitingMove;
			critter.timer = randomTicks(m_species.idleMin, m_species.idleMax);
		}
		break;
	}
	default:
		critter.position += fieldUp() * std::sin(static_cast<float>(critter.timer) * 0.3f) * 0.6f;
		if (--critter.timer <= 0)
			critter.state = CritterState::FindPoint;
		break;
	}
}

// Seagull and bird flock: one long cruise through the volume.
void CritterField::updateFlock(Critter& critter)
{
	glm::vec3 toTarget = critter.target - critter.position;
	if (glm::length(toTarget) < 250.0f)
	{
		generatePointInField(critter.target, false);
		toTarget = critter.target - critter.position;
	}

	const glm::vec3 before = critter.forward;
	turnToward(critter, toTarget, m_species.turnRate);
	// Guess: birds do not climb or dive steeper than about 25 degrees.
	glm::vec3 flat = flatten(critter.forward, fieldUp());
	float climb = glm::dot(critter.forward, fieldUp());
	climb = std::clamp(climb, -0.42f, 0.42f);
	critter.forward = safeNormalize(safeNormalize(flat, before) * std::sqrt(1.0f - climb * climb) + fieldUp() * climb, before);
	critter.position += critter.forward * m_species.speed;
	keepAboveFloor(critter, m_species.hopHeight);

	const float turn = glm::dot(glm::cross(before, critter.forward), fieldUp());
	critter.bank += (std::clamp(-turn * 12.0f, -0.6f, 0.6f) - critter.bank) * 0.1f;
	const bool flap = climb > 0.05f || std::abs(turn) > m_species.turnRate * 0.5f;
	if (m_assets)
		playAnim(critter, flap || m_assets->altAnims.empty() ? m_assets->moveAnims : m_assets->altAnims);
}

// KingFisher_Fly / KingFisher_Dive (used for the landing), Kookaburra and Lorikeet perching.
void CritterField::updatePerch(Critter& critter)
{
	if (critter.state == CritterState::Idle)
	{
		playAnim(critter, m_assets ? m_assets->idleAnims : std::vector<int>{});
		if (m_assets && m_assets->animated && critter.anim.finished())
			playAnim(critter, m_assets->idleAnims, true);
		if (--critter.timer > 0)
			return;
		glm::vec3 perch;
		bool found = false;
		if (!m_waypoints.empty())
		{
			perch = m_waypoints[std::min(m_waypoints.size() - 1, static_cast<size_t>(random01() * m_waypoints.size()))];
			found = glm::length(perch - critter.position) > 50.0f;
		}
		if (!found)
			found = generatePointInField(perch, true);
		if (!found)
		{
			critter.timer = randomTicks(m_species.idleMin, m_species.idleMax);
			return;
		}
		critter.target = perch;
		critter.state = CritterState::Fly;
		critter.timer = 30 * 30;
		return;
	}

	const glm::vec3 toPerch = critter.target - critter.position;
	const float flat = glm::length(flatten(toPerch, fieldUp()));
	const float approach = 300.0f;
	if (critter.state == CritterState::Fly)
	{
		// Cruise at hopHeight over the perch until close, then glide down to it.
		const glm::vec3 aim = flat > approach ? critter.target + fieldUp() * m_species.hopHeight : critter.target;
		const glm::vec3 before = critter.forward;
		turnToward(critter, aim - critter.position, m_species.turnRate * (flat > approach ? 1.0f : 2.5f));
		critter.position += critter.forward * m_species.speed;
		const float climb = glm::dot(critter.forward, fieldUp());
		const float turn = glm::dot(glm::cross(before, critter.forward), fieldUp());
		critter.bank += (std::clamp(-turn * 12.0f, -0.6f, 0.6f) - critter.bank) * 0.1f;
		if (m_assets)
			playAnim(critter, (climb > -0.1f || m_assets->altAnims.empty()) ? m_assets->moveAnims : m_assets->altAnims);
		if (glm::length(toPerch) < m_species.speed * 8.0f || --critter.timer <= 0)
		{
			critter.state = CritterState::Land;
			critter.timer = 30 * 3;
			if (m_assets)
				playAnim(critter, m_assets->landAnims.empty() ? m_assets->altAnims : m_assets->landAnims);
		}
		return;
	}

	// Land: brake onto the perch.
	critter.bank *= 0.8f;
	const float distance = glm::length(toPerch);
	critter.position += safeNormalize(toPerch, critter.forward) * std::min(distance, std::max(1.0f, distance * 0.15f));
	critter.forward = safeNormalize(flatten(critter.forward, fieldUp()), critter.forward);
	if (distance < 2.0f || --critter.timer <= 0)
	{
		critter.position = critter.target;
		critter.bank = 0.0f;
		critter.state = CritterState::Idle;
		critter.timer = randomTicks(m_species.idleMin, m_species.idleMax);
	}
}

// CuttleFish_Wait / CuttleFish_Swim, ShoalFish_Cruise.
void CritterField::updateSwim(Critter& critter)
{
	if (critter.state == CritterState::Cruise)
	{
		critter.target = m_shoalTarget;
		const glm::vec3 offset = glm::vec3(
			std::sin(static_cast<float>(&critter - m_critters.data()) * 2.4f),
			std::cos(static_cast<float>(&critter - m_critters.data()) * 1.7f) * 0.3f,
			std::cos(static_cast<float>(&critter - m_critters.data()) * 2.4f)) * 120.0f;
		turnToward(critter, m_shoalTarget + offset - critter.position, m_species.turnRate);
		critter.position = clipPointToField(critter.position + critter.forward * m_species.speed);
		keepAboveFloor(critter, m_species.hopHeight);
		return;
	}

	if (critter.state == CritterState::Idle)
	{
		playAnim(critter, m_assets && !m_assets->idleAnims.empty() ? m_assets->idleAnims : std::vector<int>{});
		critter.position += fieldUp() * std::sin(static_cast<float>(critter.timer) * 0.1f) * 0.2f;
		if (--critter.timer <= 0)
		{
			generatePointInField(critter.target, false);
			critter.state = CritterState::Swim;
		}
		return;
	}

	playAnim(critter, m_assets ? m_assets->moveAnims : std::vector<int>{});
	const glm::vec3 toTarget = critter.target - critter.position;
	turnToward(critter, toTarget, m_species.turnRate);
	critter.forward = safeNormalize(flatten(critter.forward, fieldUp()) + fieldUp() * std::clamp(glm::dot(critter.forward, fieldUp()), -0.3f, 0.3f), critter.forward);
	critter.position = clipPointToField(critter.position + critter.forward * m_species.speed);
	keepAboveFloor(critter, m_species.hopHeight);
	if (glm::length(toTarget) < 60.0f)
	{
		if (m_species.idleMax > 0 && random01() < 0.5f)
		{
			critter.state = CritterState::Idle;
			critter.timer = randomTicks(m_species.idleMin, m_species.idleMax);
		}
		else
			generatePointInField(critter.target, false);
	}
}

// Turtle_Swimming. HeadingForLand / SunBaking need the beach logic, which is not recovered.
void CritterField::updateTurtle(Critter& critter)
{
	playAnim(critter, m_assets ? m_assets->moveAnims : std::vector<int>{});
	glm::vec3 toTarget = flatten(critter.target - critter.position, fieldUp());
	if (glm::length(toTarget) < 80.0f)
	{
		generatePointInField(critter.target, false);
		toTarget = flatten(critter.target - critter.position, fieldUp());
	}
	turnToward(critter, toTarget, m_species.turnRate);
	critter.forward = safeNormalize(flatten(critter.forward, fieldUp()), critter.forward);
	critter.position = clipPointToField(critter.position + critter.forward * m_species.speed);
}

void CritterField::updateSurface(Critter& critter)
{
	if (critter.state == CritterState::Idle)
	{
		playAnim(critter, m_assets ? m_assets->idleAnims : std::vector<int>{});
		if (--critter.timer > 0)
			return;
		glm::vec3 point;
		generatePointInField(point, false);
		critter.target = point;
		critter.state = CritterState::Moving;
		return;
	}
	playAnim(critter, m_assets ? m_assets->moveAnims : std::vector<int>{});
	const glm::vec3 toTarget = flatten(critter.target - critter.position, fieldUp());
	turnToward(critter, toTarget, m_species.turnRate);
	critter.forward = safeNormalize(flatten(critter.forward, fieldUp()), critter.forward);
	critter.position = clipPointToField(critter.position + critter.forward * m_species.speed);
	if (glm::length(toTarget) < m_species.speed * 3.0f)
	{
		critter.state = CritterState::Idle;
		critter.timer = randomTicks(m_species.idleMin, m_species.idleMax);
	}
}

void CritterField::updateSprite(Critter& critter)
{
	const glm::vec3 toTarget = critter.target - critter.position;
	if (glm::length(toTarget) < m_species.speed * 3.0f)
		generatePointInField(critter.target, false);
	turnToward(critter, toTarget, m_species.turnRate);
	const glm::vec3 jitter(randomRange(-1.0f, 1.0f), randomRange(-1.0f, 1.0f), randomRange(-1.0f, 1.0f));
	critter.position = clipPointToField(critter.position + critter.forward * m_species.speed + jitter * m_species.speed * 0.3f);
}

void CritterField::update()
{
	if (m_species.move == CritterMove::Swim && --m_shoalTimer <= 0)
	{
		generatePointInField(m_shoalTarget, false);
		m_shoalTimer = randomTicks(90, 240);
	}

	m_ticks++;
	for (Critter& critter : m_critters)
	{
		critter.prevPosition = critter.position;
		critter.prevForward = critter.forward;
		critter.prevUp = critter.up;
		critter.prevBank = critter.bank;

		switch (m_species.move)
		{
		case CritterMove::Ground: updateGround(critter); break;
		case CritterMove::Hopper: updateHopper(critter); break;
		case CritterMove::FlySit: updateFlySit(critter); break;
		case CritterMove::Hover: updateHover(critter); break;
		case CritterMove::Flock: updateFlock(critter); break;
		case CritterMove::Perch: updatePerch(critter); break;
		case CritterMove::Swim: updateSwim(critter); break;
		case CritterMove::Turtle: updateTurtle(critter); break;
		case CritterMove::Surface: updateSurface(critter); break;
		case CritterMove::Sprite: updateSprite(critter); break;
		case CritterMove::Point:
			if (m_assets && m_assets->animated)
			{
				if (critter.state == CritterState::Idle)
				{
					playAnim(critter, m_assets->idleAnims);
					if (--critter.timer <= 0 && !m_assets->altAnims.empty())
					{
						critter.state = CritterState::Run;
						critter.timer = randomTicks(60, 120);
						playAnim(critter, m_assets->altAnims);
					}
				}
				else if (--critter.timer <= 0)
				{
					critter.state = CritterState::Idle;
					critter.timer = randomTicks(m_species.idleMin, m_species.idleMax);
				}
			}
			break;
		}

		if (m_assets != nullptr && m_assets->animated)
		{
			// Bone matrices are built at draw time, and only for critters in view.
			critter.anim.animate(kAnimAdvance);
			critter.anim.apply(critter.pose);
		}
	}
}

glm::vec3 CritterField::critterPosition(const Critter& critter, float alpha) const
{
	return critter.prevPosition + (critter.position - critter.prevPosition) * alpha;
}

glm::mat4 CritterField::critterMatrix(const Critter& critter, float alpha) const
{
	const glm::vec3 position = critterPosition(critter, alpha);
	const glm::vec3 forward = safeNormalize(critter.prevForward + (critter.forward - critter.prevForward) * alpha, critter.forward);
	glm::vec3 up = safeNormalize(critter.prevUp + (critter.up - critter.prevUp) * alpha, critter.up);
	const float bank = critter.prevBank + (critter.bank - critter.prevBank) * alpha;
	if (bank != 0.0f)
		up = safeNormalize(up * std::cos(bank) + glm::cross(forward, up) * std::sin(bank), up);

	// Models face -Z.
	const glm::vec3 back = -forward;
	const glm::vec3 x = safeNormalize(glm::cross(up, back), m_axes[0]);
	const glm::vec3 y = glm::cross(back, x);

	glm::mat4 world(1.0f);
	world[0] = glm::vec4(x, 0.0f);
	world[1] = glm::vec4(y, 0.0f);
	world[2] = glm::vec4(back, 0.0f);
	world[3] = glm::vec4(position, 1.0f);
	return world;
}

void CritterSystem::clear()
{
	m_fields.clear();
	m_fieldByInstance.clear();
	m_assets.clear();
	m_floor.clear();
	m_accumulator = 0.0f;
}

CritterAssets* CritterSystem::assetsFor(Model* model, const CritterSpecies& species, const FileReader& readFile)
{
	const std::string key = std::string(species.type) + "|" + std::to_string(reinterpret_cast<uintptr_t>(model));
	auto it = m_assets.find(key);
	if (it != m_assets.end())
		return it->second.get();

	auto assets = std::make_unique<CritterAssets>();
	assets->model = model;

	std::vector<char> bytes;
	if (species.script != nullptr && readFile(species.script, bytes) && !bytes.empty()
		&& parseAnimScript(bytes.data(), bytes.size(), assets->script))
	{
		// The frog script names critter_frog_a as its skeleton; the shipped file is Act_34_Frog.anm.
		std::vector<std::string> skeletons;
		if (!assets->script.skeleton.empty())
			skeletons.push_back(assets->script.skeleton + ".anm");
		if (!assets->script.mesh.empty())
			skeletons.push_back(assets->script.mesh + ".anm");
		std::string scriptStem = species.script;
		scriptStem = scriptStem.substr(0, scriptStem.find_last_of('.'));
		skeletons.push_back(scriptStem + ".anm");
		if (std::string(species.type) == "FROG")
			skeletons.push_back("Act_34_Frog.anm");

		for (const std::string& name : skeletons)
		{
			bytes.clear();
			if (readFile(name, bytes) && !bytes.empty() && parseAnm(bytes.data(), bytes.size(), assets->anm)
				&& !assets->anm.nodes.empty())
				break;
			assets->anm = AnmData{};
		}

		const size_t nodes = assets->anm.nodes.size();
		if (nodes > 0 && nodes + 1 <= static_cast<size_t>(Mesh::kMaxSkinBones))
		{
			if (model != nullptr && model->bones.size() == nodes)
			{
				for (const Bone& bone : model->bones)
					assets->origins.push_back(bone.defaultPosition);
			}
			assets->boneParents.assign(nodes + 1, 0);
			for (size_t i = 0; i < nodes; i++)
				assets->boneParents[i + 1] = assets->anm.nodes[i].parent + 1;
			assets->animated = true;
		}
		else if (nodes > 0)
		{
			Debug::log(std::string("Critter ") + species.type + ": skeleton has too many nodes to skin (" + std::to_string(nodes) + ")");
		}

		assets->idleAnims = resolveAnims(assets->script, species.idleAnims);
		assets->moveAnims = resolveAnims(assets->script, species.moveAnims);
		assets->altAnims = resolveAnims(assets->script, species.altAnims);
		assets->landAnims = resolveAnims(assets->script, species.landAnims);
		if (assets->idleAnims.empty() && assets->moveAnims.empty() && !assets->script.anims.empty())
			assets->idleAnims.push_back(0);
	}

	CritterAssets* raw = assets.get();
	m_assets[key] = std::move(assets);
	return raw;
}

void CritterSystem::load(const std::vector<Ty1Instance>& instances, const std::vector<Model*>& rooms,
	const std::function<bool(const Mesh*)>& solid, const FileReader& readFile)
{
	clear();
	m_floor.build(rooms, solid);

	int critterTotal = 0;
	for (int index = 0; index < static_cast<int>(instances.size()); index++)
	{
		const Ty1Instance& instance = instances[static_cast<size_t>(index)];
		if (!instance.critter)
			continue;
		const CritterSpecies* species = findCritterSpecies(instance.typeName);
		if (species == nullptr)
			continue;
		const bool meshless = species->move == CritterMove::Sprite || species->sprite != nullptr;
		if (instance.model == nullptr && !meshless)
			continue;

		CritterAssets* assets = meshless ? nullptr : assetsFor(instance.model, *species, readFile);
		auto field = std::make_unique<CritterField>(instance, index, *species, assets, &m_floor);
		critterTotal += static_cast<int>(field->critters().size());
		Debug::log("Critter field " + instance.typeName + " #" + std::to_string(index)
			+ ": " + std::to_string(field->critters().size()) + " critters"
			+ (assets && assets->animated ? ", animated (" + std::to_string(assets->anm.nodes.size()) + " nodes)" : ", static"));
		m_fieldByInstance[index] = m_fields.size();
		m_fields.push_back(std::move(field));
	}
	if (!m_fields.empty())
		Debug::log("Critter fields: " + std::to_string(m_fields.size()) + ", critters: " + std::to_string(critterTotal));
}

int CritterSystem::update(float dt)
{
	if (m_paused || m_fields.empty())
		return 0;
	// A hitch (level load, a dragged window) must not replay a burst of ticks.
	// One step keeps the fields moving; the fractional remainder is alpha().
	m_accumulator += std::min(dt, kTickSeconds * 5.0f);
	if (m_accumulator < kTickSeconds)
		return 0;
	m_accumulator = std::fmod(m_accumulator, kTickSeconds);
	for (const std::unique_ptr<CritterField>& field : m_fields)
		field->update();
	return 1;
}

float CritterSystem::alpha() const
{
	return std::clamp(m_accumulator / kTickSeconds, 0.0f, 1.0f);
}

CritterField* CritterSystem::fieldForInstance(int instanceIndex)
{
	const auto it = m_fieldByInstance.find(instanceIndex);
	return it == m_fieldByInstance.end() ? nullptr : m_fields[it->second].get();
}

const CritterField* CritterSystem::fieldForInstance(int instanceIndex) const
{
	const auto it = m_fieldByInstance.find(instanceIndex);
	return it == m_fieldByInstance.end() ? nullptr : m_fields[it->second].get();
}
