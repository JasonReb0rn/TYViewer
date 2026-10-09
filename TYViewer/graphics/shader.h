#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

struct ShaderProgramSource
{
	std::string vertexSource;
	std::string fragmentSource;
};

class Shader
{
public:
	Shader(std::ifstream& stream, const std::unordered_map<std::string, int>& properties);
	Shader(const std::string& vertexSource, const std::string& fragmentSource);
	~Shader();

	void bind() const;
	void unbind() const;
	// The GUI and the vertex overlay call glUseProgram directly. The next bind
	// must not assume this program is still current.
	static void invalidateBind();

	void setUniform1i(const std::string& name, int v);
	void setUniform1f(const std::string& name, float v);
	void setUniform2f(const std::string& name, glm::vec2 v);
	void setUniform3f(const std::string& name, glm::vec3 v);
	void setUniform4f(const std::string& name, glm::vec4 v);
	void setUniformMat4(const std::string& name, glm::mat4 mat);
	void setUniformMat3(const std::string& name, glm::mat3 mat);
	// `name` is the array uniform; uploads `count` elements from element 0.
	void setUniformMat4Array(const std::string& name, const glm::mat4* mats, int count);
	void setUniform1iArray(const std::string& name, const int* values, int count);

	// String literals keep a stable address, so these skip the std::string allocation
	// and the heap-backed name lookup. Call sites that pass a literal pick them.
	void setUniform1i(const char* name, int v);
	void setUniform1f(const char* name, float v);
	void setUniform2f(const char* name, glm::vec2 v);
	void setUniform3f(const char* name, glm::vec3 v);
	void setUniform4f(const char* name, glm::vec4 v);
	void setUniformMat4(const char* name, glm::mat4 mat);
	void setUniformMat3(const char* name, glm::mat3 mat);
	void setUniformMat4Array(const char* name, const glm::mat4* mats, int count);
	void setUniform1iArray(const char* name, const int* values, int count);

	// Resolve once, then upload from the int. The reflection pass does this so a
	// repeated tint or useInstancing does not hash the uniform name.
	int uniformLocation(const char* name);
	void setUniform1i(int location, int v);
	void setUniform1f(int location, float v);
	void setUniformMat4(int location, const glm::mat4& mat);
	void setUniformMat3(int location, const glm::mat3& mat);

	// Level and prop draws. No water displacement and no reflection varyings.
	static Shader* createDefault();
	// PC water surfaces only. Wave displacement, noise wobble, reflection sample.
	static Shader* createWater();
	// Reflections off. Wave displacement plus the GameCube indirect ripple.
	static Shader* createSimpleWater();
	// Reflection pass. cutout discards low alpha (tree cards, blended sheets).
	// clip writes a clip distance for meshes that cross the plane when the oblique
	// near plane is not in use. Opaque reflections use neither, so early-Z stays on.
	static Shader* createReflection(bool cutout, bool clip);

private:
	unsigned int m_id;
	std::unordered_map<std::string, int> m_uniformLocationCache;

	std::unordered_map<std::string, int> properties;

	unsigned int create(const std::string& vertexShader, const std::string& fragmentShader);
	unsigned int compile(unsigned int type, const std::string& source);

	int getUniformLocation(const std::string& name);
	int getUniformLocation(const char* name);

	// Last uploaded scalar or vec4 per location. A repeat of the same value skips the GL call.
	struct CachedUniform
	{
		unsigned char kind = 0;
		int i = 0;
		float f = 0.0f;
		glm::vec4 v4{ 0.0f };
	};
	bool cachedInt(int location, int value);
	bool cachedFloat(int location, float value);
	bool cachedVec4(int location, const glm::vec4& value);

	std::unordered_map<const char*, int> m_uniformLocationLiterals;
	std::vector<CachedUniform> m_uniformValues;
};