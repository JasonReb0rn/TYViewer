#pragma once

#include <string>
#include <vector>

class AnmPose;

// TY1 animation script (.bad text). MKAnimScript in the decomp (common/MKAnimScript.cpp).
struct AnimScriptRange
{
	int start = 0;
	int end = 0;
	int speed = 1;
};

enum class AnimCycle
{
	Stop,
	Loop,
	Rebound,
};

struct AnimScriptAnim
{
	std::string name;
	std::vector<AnimScriptRange> ranges;
	AnimCycle cycle = AnimCycle::Stop;
};

struct AnimScriptData
{
	std::string mesh;
	// Skeleton name with the extension stripped; the runtime loads <skeleton>.anm.
	std::string skeleton;
	std::vector<AnimScriptAnim> anims;

	// Case-insensitive. -1 when missing.
	int find(const std::string& name) const;
};

bool parseAnimScript(const char* data, size_t size, AnimScriptData& out);

// MKAnimScript playback state for one critter.
class AnimScriptPlayer
{
public:
	void init(const AnimScriptData* data);
	bool valid() const { return m_data != nullptr && !m_data->anims.empty(); }

	// MKAnimScript::SetAnim / TweenAnim. Unknown indices are ignored.
	void setAnim(int anim);
	void tweenAnim(int anim, int steps);
	// Starts `anim` unless it is already playing (or being tweened to).
	void play(int anim, int tweenSteps);

	// MKAnimScript::Animate. One call per game tick.
	void animate(float advance);
	// MKAnimScript::Apply.
	void apply(AnmPose& pose);

	int current() const { return m_anim; }
	// Stop-cycle anims have frozen on their last frame.
	bool finished() const { return m_step == 0.0f && m_nextAnim < 0; }
	int loops() const { return m_loops; }
	float frame() const { return m_frame; }

private:
	const AnimScriptData* m_data = nullptr;
	int m_anim = -1;
	int m_range = 0;
	float m_frame = 0.0f;
	float m_step = 0.0f;
	int m_loops = 0;

	int m_nextAnim = -1;
	float m_nextFrame = 0.0f;
	int m_tweenCount = 0;
};
