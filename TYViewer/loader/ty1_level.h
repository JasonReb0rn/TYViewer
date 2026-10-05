#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

// What the level fields say this instance is. Prop is a placed mesh.
enum class Ty1Kind
{
	Prop,
	Critter,
	Water,
	Trigger,
	Sound,
	Patrol
};

// One `key = value` from the instance, in file order. Indented lines are children
// of the block they belong to (an event, a camera, a box, a sphere, or a path).
struct Ty1Field
{
	std::string key;
	std::string value;
	std::vector<Ty1Field> children;
};

// One row in the object inspector. `link` is another instance index, or -1.
struct Ty1InfoLine
{
	std::string text;
	int link = -1;
	bool dim = false;
	bool heading = false;
	int indent = 0;
};

// One placed object from a TY1 .lv2 `name` block. `model` is filled by the caller.
struct Ty1Instance
{
	std::string typeName;
	std::string modelFile;
	// Second mesh at the same transform. Caged bilbies use this for the cage.
	std::string extraModelFile;
	// Character label from `type = N,name`, such as grandma.
	std::string variantLabel;
	class Model* model = nullptr;
	class Model* extraModel = nullptr;
	glm::vec3 position{ 0.0f, 0.0f, 0.0f };
	// Pitch, yaw, roll in radians, as stored in the level.
	glm::vec3 rotation{ 0.0f, 0.0f, 0.0f };
	glm::vec3 scale{ 1.0f, 1.0f, 1.0f };
	bool visible = true;
	// Gem::Draw and Portal::Draw copy the camera rotation each frame. Level rot is not used.
	bool billboard = false;
	// Added to world Y before the rotation. The portal value is a guess:
	// the decompiled parent stores `position`, and Portal::LoadDone is not recovered.
	float drawLiftY = 0.0f;
	bool seatBottom = false;
	Ty1Kind kind = Ty1Kind::Prop;

	// World-space box across `model` and `extraModel`, computed once after a level's
	// props finish loading (Application::computeInstanceWorldBounds). Used to
	// frustum-cull this instance out of the per-frame draw batches. false when this
	// instance has no mesh (a pure trigger/patrol/sound has nothing to cull).
	bool hasAabb = false;
	glm::vec3 worldAabbMin{ 0.0f, 0.0f, 0.0f };
	glm::vec3 worldAabbMax{ 0.0f, 0.0f, 0.0f };

	// Flock roam box, or a water volume. Full size, centered on `position`,
	// turned by `rotation`. Not the mesh scale.
	bool critter = false;
	glm::vec3 roamSize{ 0.0f, 0.0f, 0.0f };

	// Nested `box = boxzone`. Full width, height, depth, centered on `boxPosition`.
	bool hasBox = false;
	glm::vec3 boxPosition{ 0.0f, 0.0f, 0.0f };
	float boxYaw = 0.0f;
	float boxPitch = 0.0f;
	glm::vec3 boxSize{ 0.0f, 0.0f, 0.0f };

	// Trigger sphere, sound radius, an enemy `range` / dive `radius`,
	// or the restart approach sphere. That radius is 500 and is not in the file.
	bool hasSphere = false;
	bool soundSphere = false;
	bool rangeSphere = false;
	glm::vec3 spherePosition{ 0.0f, 0.0f, 0.0f };
	float sphereRadius = 0.0f;

	// Patrol, enemy region, spawner, or camera path. PATH regions are closed.
	std::vector<glm::vec3> waypoints;
	bool closePath = false;
	float pathWidth = 0.0f;

	// `ID = number,label` from the file. -1 when the instance has no ID.
	// `blank` and `none` leave the label empty.
	int objectId = -1;
	std::string objectLabel;

	// Every field in the instance, including the ones the draw path already uses.
	std::vector<Ty1Field> fields;
};

// List label for this instance. A plain prop is "prop". A prop with a range sphere is "range".
const char* ty1KindName(const Ty1Instance& instance);

// First instance index for each nonzero ID in this level.
std::unordered_map<int, int> ty1IdIndex(const std::vector<Ty1Instance>& instances);

// Header, file fields, and the other instances whose events point here.
// `index` must address `instances`.
std::vector<Ty1InfoLine> describeTy1Instance(
	const std::vector<Ty1Instance>& instances,
	int index,
	const std::unordered_map<int, int>& idToIndex);

// Krome stores this as a row-vector matrix (scale, then pitch, yaw, roll, translation
// in the last row). The returned matrix is that transform for a column-vector shader.
// A billboard ignores level rot. `cameraView` is Camera::getViewMatrix (eye Z negated,
// before render flips Z). Null keeps the stored rotation.
glm::mat4 ty1InstanceMatrix(const Ty1Instance& instance, const glm::mat4* cameraView = nullptr);

// `globalModelText` is the archive file global.model. `mdlFiles` is every .mdl name.
// `levelFile` is the .lv2 name (`a1.lv2`, `a1ex.lv2`). It picks the opal and thunder
// egg mesh. Instances with no mesh keep an empty modelFile and still appear in the list.
std::vector<Ty1Instance> parseTy1Instances(
	const std::string& levelText,
	const std::string& globalModelText,
	const std::vector<std::string>& mdlFiles,
	const std::string& levelFile);
