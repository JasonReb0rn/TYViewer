#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "critters/critter_floor.h"
#include "loader/assets/anim_script.h"
#include "loader/assets/anm.h"

class Model;
struct Ty1Instance;

// TY1 critters. The game code (CritterField2, Critter2, BlitterCritter, and the
// per-species Desc / state classes) is only present as symbols in ty-decomp, so the
// state names below are the game's and the movement constants are guesses.

// Which movement code a species runs.
enum class CritterMove
{
	// Ibis, gecko, cow, sheep, soldier crab: Idle, then Walk to a point on the floor.
	Ground,
	// Frog, grasshopper, wallaby: Idle, then StartJump along an arc to a nearby floor point.
	Hopper,
	// Moth, butterfly: Fly between points in the field, land on a valid floor, Idle.
	FlySit,
	// Dragonfly: FindPoint, Moving, WaitingMove (hover).
	Hover,
	// Seagull, bird flock: always flying, flap while climbing, glide otherwise.
	Flock,
	// Kingfisher, kookaburra, lorikeet: Fly to a perch (a waypoint when the field has them), land, Idle.
	Perch,
	// Fish, cuttlefish, seahorse: Wait / Swim / Cruise inside the volume.
	Swim,
	// Turtle: Swimming at the field height inside its range.
	Turtle,
	// Water skimmer: darts across the water at the field height.
	Surface,
	// Water dragon, synker frog: stays where it was placed and plays its idle.
	Point,
	// Fly, firefly: BlitterCritter sprites. Drawn as small markers.
	Sprite,
};

enum class CritterState
{
	Idle,
	Walk,
	Run,
	StartJump,
	Fly,
	Land,
	FindPoint,
	Moving,
	WaitingMove,
	Swim,
	Cruise,
};

struct CritterSpecies
{
	const char* type;
	CritterMove move;
	// .bad anim script. Null when the species has no skeleton.
	const char* script;
	// Count when the field has no `count` (turtle, fly).
	int defaultCount;
	// Units per 30 Hz tick.
	float speed;
	float runSpeed;
	// Radians per tick.
	float turnRate;
	// Idle length in ticks.
	int idleMin;
	int idleMax;
	// Anim names per state, '|' separated. Missing names are skipped.
	const char* idleAnims;
	const char* moveAnims;
	// Run for ground critters, glide for flyers, the jump for hoppers.
	const char* altAnims;
	const char* landAnims;
	// Hop length and height (Hopper), or height above the floor (flyers).
	float hopDistance;
	float hopHeight;
	// Gecko: the body follows the surface normal.
	bool alignToFloor;
	// FlyDesc critters drawn as a camera-facing sprite strip instead of a model.
	const char* sprite = nullptr;
	// World units across the sprite.
	float spriteSize = 0.0f;
	// Frames in the strip, left to right.
	int spriteFrames = 0;
	// Dragonfly: 8 headings 45 degrees apart with wings still, then the same 8 with
	// wings blurred. Otherwise the frames are a flicker loop (firefly).
	bool spriteByHeading = false;
	bool spriteAdditive = false;
};

// Null for types that are not critters.
const CritterSpecies* findCritterSpecies(const std::string& typeName);

// Model, skeleton, and anim script shared by every critter of one model.
struct CritterAssets
{
	Model* model = nullptr;
	AnmData anm;
	AnimScriptData script;
	std::vector<glm::vec3> origins;
	// boneParents[i] is the parent matrix of model matrix i. Fed to Mesh::drawSkinned.
	std::vector<int> boneParents;
	bool animated = false;

	std::vector<int> idleAnims;
	std::vector<int> moveAnims;
	std::vector<int> altAnims;
	std::vector<int> landAnims;
};

struct Critter
{
	CritterState state = CritterState::Idle;
	int timer = 0;
	glm::vec3 position{ 0.0f };
	glm::vec3 forward{ 0.0f, 0.0f, -1.0f };
	glm::vec3 up{ 0.0f, 1.0f, 0.0f };
	glm::vec3 prevPosition{ 0.0f };
	glm::vec3 prevForward{ 0.0f, 0.0f, -1.0f };
	glm::vec3 prevUp{ 0.0f, 1.0f, 0.0f };
	glm::vec3 target{ 0.0f };
	glm::vec3 jumpFrom{ 0.0f };
	float speed = 0.0f;
	float bank = 0.0f;
	float prevBank = 0.0f;
	int jumpTicks = 0;
	int jumpLength = 1;

	AnimScriptPlayer anim;
	AnmPose pose;
};

class CritterField
{
public:
	CritterField(const Ty1Instance& instance, int instanceIndex, const CritterSpecies& species,
		CritterAssets* assets, const CritterFloor* floor);

	// One game tick (30 Hz).
	void update();

	// CritterField2::GeneratePointInField. `onFloor` drops the point onto the floor
	// under it and fails when there is none.
	bool generatePointInField(glm::vec3& out, bool onFloor);
	// CritterField2::ClipPointToField.
	glm::vec3 clipPointToField(const glm::vec3& point) const;
	// CritterField2::GetFloor. Casts along the field's down axis from `from`.
	bool getFloor(const glm::vec3& from, float above, float below, glm::vec3& outPoint, glm::vec3& outNormal) const;

	// World matrix between the last two ticks; `alpha` is 0 at the previous tick.
	glm::mat4 critterMatrix(const Critter& critter, float alpha) const;
	glm::vec3 critterPosition(const Critter& critter, float alpha) const;

	int instanceIndex() const { return m_instanceIndex; }
	const CritterSpecies& species() const { return m_species; }
	CritterAssets* assets() const { return m_assets; }
	const std::vector<Critter>& critters() const { return m_critters; }
	// Ticks since the level loaded.
	int ticks() const { return m_ticks; }

private:
	float random01();
	float randomRange(float lo, float hi) { return lo + (hi - lo) * random01(); }
	int randomTicks(int lo, int hi);
	int pickAnim(const std::vector<int>& anims);
	void playAnim(Critter& critter, const std::vector<int>& anims, bool restart = false);
	glm::vec3 fieldUp() const { return m_axes[1]; }
	glm::vec3 toLocal(const glm::vec3& world) const;
	glm::vec3 toWorld(const glm::vec3& local) const;

	void spawn(int count);
	void turnToward(Critter& critter, const glm::vec3& direction, float rate) const;
	void stepToward(Critter& critter, float speed, bool keepUpright);
	bool snapToFloor(Critter& critter);
	void keepAboveFloor(Critter& critter, float clearance);

	void updateGround(Critter& critter);
	void updateHopper(Critter& critter);
	void updateFlySit(Critter& critter);
	void updateHover(Critter& critter);
	void updateFlock(Critter& critter);
	void updatePerch(Critter& critter);
	void updateSwim(Critter& critter);
	void updateTurtle(Critter& critter);
	void updateSurface(Critter& critter);
	void updateSprite(Critter& critter);

	const CritterSpecies& m_species;
	CritterAssets* m_assets;
	const CritterFloor* m_floor;
	int m_instanceIndex;
	uint32_t m_seed;

	glm::vec3 m_center{ 0.0f };
	// Columns are the field's local x, y, z axes in world space.
	glm::mat3 m_axes{ 1.0f };
	glm::vec3 m_half{ 0.0f };
	// `range` fields are a circle around the center rather than a box.
	bool m_round = false;
	std::vector<glm::vec3> m_waypoints;
	// ShoalFish_Cruise steers the whole shoal toward one point.
	glm::vec3 m_shoalTarget{ 0.0f };
	int m_shoalTimer = 0;
	int m_ticks = 0;

	std::vector<Critter> m_critters;
};

// Every critter field in the open level, plus the shared assets and the floor.
class CritterSystem
{
public:
	using FileReader = std::function<bool(const std::string& name, std::vector<char>& data)>;

	void clear();
	// Rooms are the level's room models; `solid` picks the meshes critters stand on.
	void load(const std::vector<Ty1Instance>& instances, const std::vector<Model*>& rooms,
		const std::function<bool(const class Mesh*)>& solid, const FileReader& readFile);
	// Real seconds. Steps the fields at 30 Hz.
	void update(float dt);

	bool paused() const { return m_paused; }
	void setPaused(bool paused) { m_paused = paused; }
	bool empty() const { return m_fields.empty(); }

	// Fraction of the way from the previous tick to the current one.
	float alpha() const;
	const std::vector<std::unique_ptr<CritterField>>& fields() const { return m_fields; }
	// The field spawned by a level instance, or null.
	const CritterField* fieldForInstance(int instanceIndex) const;

private:
	CritterAssets* assetsFor(Model* model, const CritterSpecies& species, const FileReader& readFile);

	CritterFloor m_floor;
	std::unordered_map<std::string, std::unique_ptr<CritterAssets>> m_assets;
	std::vector<std::unique_ptr<CritterField>> m_fields;
	std::unordered_map<int, size_t> m_fieldByInstance;
	float m_accumulator = 0.0f;
	bool m_paused = false;
};
