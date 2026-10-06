#pragma once

#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

// TY1 skeleton animation (.anm). AnimationData in the decomp (common/Animation.h).
// The PC file is little endian; every pointer is an offset from the start of the file.
struct AnmKey
{
	int frame = 0;
	glm::vec3 position{ 0.0f };
	// Quaternion x, y, z, w. Keys are not always unit length; the game uses them as stored.
	glm::vec4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
	glm::vec3 scale{ 1.0f };
};

struct AnmNode
{
	std::string name;
	// Index into AnmData::nodes, or -1 when the node hangs off the model root.
	int parent = -1;
	glm::vec3 origin{ 0.0f };
	std::vector<AnmKey> keys;
};

struct AnmData
{
	int frameCount = 0;
	std::vector<AnmNode> nodes;
};

// False when the magic is not ANIM or a table points outside the file.
bool parseAnm(const char* data, size_t size, AnmData& out);

// Animation (decomp common/Animation.cpp). Holds one tween state per node and the
// model matrices: matrices()[0] is the model root, matrices()[n + 1] is node n.
// Matrices are model space, so the caller multiplies the instance world matrix in.
class AnmPose
{
public:
	// `origins` replaces the .anm node origins, as Model::SetAnimation does with the
	// model's own anim-node table. Pass an empty list to keep the .anm origins.
	void init(const AnmData* data, const std::vector<glm::vec3>& origins);
	bool valid() const { return m_data != nullptr && !m_frames.empty(); }

	// Animation::Tween. A weight below 1 blends from the current pose.
	// A repeat of the last built frame and weight leaves the matrices alone.
	void tween(float frame, float weight);
	// Animation::CalculateMatrices. No work when tween() did not change the target.
	void calculateMatrices();

	const std::vector<glm::mat4>& matrices() const { return m_matrices; }
	int nodeCount() const { return static_cast<int>(m_frames.size()); }

private:
	struct Frame
	{
		glm::vec3 position{ 0.0f };
		glm::vec4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
		glm::vec3 scale{ 1.0f };
		glm::vec3 origin{ 0.0f };
		float targetFrame = 0.0f;
		float targetWeight = 0.0f;
		bool frameCalc = false;
		bool matrixCalc = false;
	};

	void calculateFrame(int index);
	void calculateNodeMatrix(int index);

	const AnmData* m_data = nullptr;
	std::vector<Frame> m_frames;
	std::vector<glm::mat4> m_matrices;
	bool m_hasTarget = false;
	bool m_matricesReady = false;
	float m_targetFrame = 0.0f;
	float m_targetWeight = 0.0f;
};
