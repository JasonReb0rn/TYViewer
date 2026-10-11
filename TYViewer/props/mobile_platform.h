#pragma once

#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct Ty1Instance;

// Playback for AIRPLATFORM and PONTOON. Both inherit the PC game's MobilePlatform
// (cubic Hermite in arc length, Activate / Deactivate, bMoveOne). DROPPONTOON hangs
// at hangPos until Fall, drops to the path, then uses that same travel. The authored
// Ty1Instance is left unchanged so a level save still writes the placed pose.
class MobilePlatformSystem
{
public:
	void clear();
	// One sim per air platform, pontoon, or drop pontoon. Widens world AABBs so a
	// traveling deck, and anything standing on it, stays inside the frustum.
	void load(std::vector<Ty1Instance>& instances);

	// One 30 Hz step when the accumulator crosses a tick. 0 when paused or idle.
	int update(float dt);
	void setPaused(bool paused);
	bool paused() const { return m_paused; }
	bool empty() const { return m_platforms.empty(); }
	// Fraction of the way from the previous tick to the current one.
	float alpha() const;

	// Checkbox state for a mobile platform. On restarts the trip; off returns to the placed pose.
	// A drop pontoon starts at hangPos. On releases the hang; off returns there.
	bool platformPreview(int instanceIndex) const;
	void setPlatformPreview(int instanceIndex, bool enabled);
	// Checkbox state for a button or script. On delivers its events; off restores each target.
	bool triggerPreview(int instanceIndex) const;
	void setTriggerPreview(int instanceIndex, bool enabled, const std::vector<Ty1Instance>& instances);

	// Level-space position and pitch/yaw/roll. False when this instance is not moving.
	bool drawPose(int instanceIndex, float alpha, glm::vec3& position, glm::vec3& rotation) const;
	// fract(simSeconds) * 20π. The same angle on every fan, moving or not.
	float propellerAngle() const;

private:
	struct Node
	{
		glm::vec3 position{ 0.0f };
		glm::vec3 tangent{ 0.0f };
		float length = 0.0f;
	};

	struct Path
	{
		std::vector<Node> nodes;
		float totalLength = 0.0f;
		void sample(float t, glm::vec3& position, glm::vec3& derivative) const;
	};

	struct Platform
	{
		int instance = -1;
		int objectId = -1;
		bool active = false;
		bool authoredActive = false;
		bool previewEnabled = false;
		bool moveOne = false;
		bool finished = false;
		bool circular = false;
		float speed = 0.0f;
		float turnSpeed = 0.0f;
		float pitch = 0.0f;
		float roll = 0.0f;
		float durationTicks = 0.0f;
		float elapsedTicks = 0.0f;
		float startElapsed = 0.0f;
		glm::vec3 scale{ 1.0f };
		Path path;
		glm::vec3 prevPosition{ 0.0f };
		glm::vec3 position{ 0.0f };
		float prevYaw = 0.0f;
		float yaw = 0.0f;
		float initialYaw = 0.0f;
		// DROPPONTOON. Hangs at hangPos until Fall, then drops onto the path.
		bool drop = false;
		bool hanging = false;
		bool falling = false;
		float fallTicks = 0.0f;
		float fallDuration = 0.0f;
		glm::vec3 hangPos{ 0.0f };
		glm::vec3 fallTarget{ 0.0f };
	};

	struct Rider
	{
		int instance = -1;
		int platform = -1;
		glm::vec3 local{ 0.0f };
		float yawOffset = 0.0f;
		float pitch = 0.0f;
		float roll = 0.0f;
		float drawLiftY = 0.0f;
	};

	void step(Platform& platform);
	void place(Platform& platform, float elapsedTicks, bool applyTurn);
	void rewind(Platform& platform, bool active);
	void beginFall(Platform& platform);
	void message(int objectId, const std::string& name);
	glm::mat4 deckMatrix(const Platform& platform, const glm::vec3& position, float yaw) const;
	int platformIndexForId(int objectId) const;

	static void addPoint(std::vector<Node>& nodes, int& count, const glm::vec3& point);
	static void closeLoop(std::vector<Node>& nodes, int count);
	static float segmentLength(const Node& a, const Node& b);
	static Path buildPath(const std::vector<glm::vec3>& points, bool circular);
	static void hermite(const Node& a, const Node& b, float u, glm::vec3& position, glm::vec3& derivative);

	std::vector<Platform> m_platforms;
	std::vector<Rider> m_riders;
	std::vector<int> m_platformOf;
	std::vector<int> m_riderOf;
	std::vector<char> m_triggerOn;
	float m_accumulator = 0.0f;
	int m_tick = 0;
	bool m_paused = false;
};
