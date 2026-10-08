#include "shader.h"

#include <cstdlib>

#include <string>
#include <fstream>
#include <iostream>
#include <sstream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "util/stringext.h"

#include "util/parser.h"

#include "application.h"

namespace
{
	const Shader* g_boundShader = nullptr;
}

Shader::Shader(std::ifstream& stream, const std::unordered_map<std::string, int>& properties)
	: m_id(0),
	properties(properties)
{

	// TODO :
	// ONLY FOR DEBUGGING PURPOSES !!!
	// REMOVE BEFORE PUBLISHING !!!
	std::pair<std::string, std::string> source = Parser::parseShader(stream, properties);

	m_id = create(source.first, source.second);
}

Shader::Shader(const std::string& vertexSource, const std::string& fragmentSource)
	: m_id(0)
{
	m_id = create(vertexSource, fragmentSource);
}
Shader::~Shader()
{
	if (g_boundShader == this)
		g_boundShader = nullptr;
	glDeleteProgram(m_id);
}

unsigned int Shader::create(const std::string& vertexShader, const std::string& fragmentShader)
{
	unsigned int program = glCreateProgram();
	unsigned int vs = compile(GL_VERTEX_SHADER, vertexShader);
	unsigned int fs = compile(GL_FRAGMENT_SHADER, fragmentShader);

	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glValidateProgram(program);

	glDeleteShader(vs);
	glDeleteShader(fs);

	return program;
}
unsigned Shader::compile(unsigned int type, const std::string& source)
{
	unsigned int id = glCreateShader(type);
	const char* src = source.c_str();
	glShaderSource(id, 1, &src, nullptr);
	glCompileShader(id);

	int result;
	glGetShaderiv(id, GL_COMPILE_STATUS, &result);
	if (result == GL_FALSE)
	{
		int length;
		glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
		char* message = (char*)alloca(length * sizeof(char));
		glGetShaderInfoLog(id, length, &length, message);
		std::cout << "Failed to compile " << (type == GL_VERTEX_SHADER ? "vertex" : "fragment") << " shader!" << std::endl;
		std::cout << message << std::endl;
		glDeleteShader(id);
		return 0;
	}

	return id;
}


void Shader::bind() const
{
	if (g_boundShader == this)
		return;
	glUseProgram(m_id);
	g_boundShader = this;
}
void Shader::unbind() const
{
	glUseProgram(0);
	if (g_boundShader == this)
		g_boundShader = nullptr;
}

void Shader::invalidateBind()
{
	g_boundShader = nullptr;
}

bool Shader::cachedInt(int location, int value)
{
	if (location < 0)
		return true;
	if (static_cast<size_t>(location) >= m_uniformValues.size())
		m_uniformValues.resize(static_cast<size_t>(location) + 1);
	CachedUniform& slot = m_uniformValues[static_cast<size_t>(location)];
	if (slot.kind == 1 && slot.i == value)
		return true;
	slot.kind = 1;
	slot.i = value;
	return false;
}

bool Shader::cachedFloat(int location, float value)
{
	if (location < 0)
		return true;
	if (static_cast<size_t>(location) >= m_uniformValues.size())
		m_uniformValues.resize(static_cast<size_t>(location) + 1);
	CachedUniform& slot = m_uniformValues[static_cast<size_t>(location)];
	if (slot.kind == 2 && slot.f == value)
		return true;
	slot.kind = 2;
	slot.f = value;
	return false;
}

bool Shader::cachedVec4(int location, const glm::vec4& value)
{
	if (location < 0)
		return true;
	if (static_cast<size_t>(location) >= m_uniformValues.size())
		m_uniformValues.resize(static_cast<size_t>(location) + 1);
	CachedUniform& slot = m_uniformValues[static_cast<size_t>(location)];
	if (slot.kind == 3 && slot.v4 == value)
		return true;
	slot.kind = 3;
	slot.v4 = value;
	return false;
}

void Shader::setUniform1i(const std::string& name, int v)
{
	const int location = getUniformLocation(name);
	if (!cachedInt(location, v))
		glUniform1i(location, v);
}
void Shader::setUniform1f(const std::string& name, float v)
{
	const int location = getUniformLocation(name);
	if (!cachedFloat(location, v))
		glUniform1f(location, v);
}
void Shader::setUniform2f(const std::string& name, glm::vec2 v)
{
	glUniform2f(getUniformLocation(name), v.x, v.y);
}
void Shader::setUniform3f(const std::string& name, glm::vec3 v)
{
	glUniform3f(getUniformLocation(name), v.x, v.y, v.z);
}
void Shader::setUniform4f(const std::string& name, glm::vec4 v)
{
	const int location = getUniformLocation(name);
	if (!cachedVec4(location, v))
		glUniform4f(location, v.x, v.y, v.z, v.w);
}
void Shader::setUniformMat4(const std::string& name, glm::mat4 mat)
{
	glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, &mat[0][0]);
}
void Shader::setUniformMat3(const std::string& name, glm::mat3 mat)
{
	glUniformMatrix3fv(getUniformLocation(name), 1, GL_FALSE, &mat[0][0]);
}
void Shader::setUniformMat4Array(const std::string& name, const glm::mat4* mats, int count)
{
	if (count > 0)
		glUniformMatrix4fv(getUniformLocation(name), count, GL_FALSE, &mats[0][0][0]);
}
void Shader::setUniform1iArray(const std::string& name, const int* values, int count)
{
	if (count > 0)
		glUniform1iv(getUniformLocation(name), count, values);
}

void Shader::setUniform1i(const char* name, int v)
{
	const int location = getUniformLocation(name);
	if (!cachedInt(location, v))
		glUniform1i(location, v);
}
void Shader::setUniform1f(const char* name, float v)
{
	const int location = getUniformLocation(name);
	if (!cachedFloat(location, v))
		glUniform1f(location, v);
}
void Shader::setUniform2f(const char* name, glm::vec2 v)
{
	glUniform2f(getUniformLocation(name), v.x, v.y);
}
void Shader::setUniform3f(const char* name, glm::vec3 v)
{
	glUniform3f(getUniformLocation(name), v.x, v.y, v.z);
}
void Shader::setUniform4f(const char* name, glm::vec4 v)
{
	const int location = getUniformLocation(name);
	if (!cachedVec4(location, v))
		glUniform4f(location, v.x, v.y, v.z, v.w);
}
void Shader::setUniformMat4(const char* name, glm::mat4 mat)
{
	glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, &mat[0][0]);
}
void Shader::setUniformMat3(const char* name, glm::mat3 mat)
{
	glUniformMatrix3fv(getUniformLocation(name), 1, GL_FALSE, &mat[0][0]);
}
void Shader::setUniformMat4Array(const char* name, const glm::mat4* mats, int count)
{
	if (count > 0)
		glUniformMatrix4fv(getUniformLocation(name), count, GL_FALSE, &mats[0][0][0]);
}
void Shader::setUniform1iArray(const char* name, const int* values, int count)
{
	if (count > 0)
		glUniform1iv(getUniformLocation(name), count, values);
}

int Shader::uniformLocation(const char* name)
{
	return getUniformLocation(name);
}

void Shader::setUniform1i(int location, int v)
{
	if (!cachedInt(location, v))
		glUniform1i(location, v);
}

void Shader::setUniform1f(int location, float v)
{
	if (!cachedFloat(location, v))
		glUniform1f(location, v);
}

void Shader::setUniformMat4(int location, const glm::mat4& mat)
{
	if (location < 0)
		return;
	glUniformMatrix4fv(location, 1, GL_FALSE, &mat[0][0]);
}

void Shader::setUniformMat3(int location, const glm::mat3& mat)
{
	if (location < 0)
		return;
	glUniformMatrix3fv(location, 1, GL_FALSE, &mat[0][0]);
}

int Shader::getUniformLocation(const std::string& name)
{
	if (m_uniformLocationCache.find(name) != m_uniformLocationCache.end())
		return m_uniformLocationCache[name];

	int location = glGetUniformLocation(m_id, name.c_str());
	if (location == -1)
		std::cout << "Warning: uniform " << name << " doesnt't exist!" << std::endl;
	m_uniformLocationCache[name] = location;
	return location;
}

int Shader::getUniformLocation(const char* name)
{
	const auto found = m_uniformLocationLiterals.find(name);
	if (found != m_uniformLocationLiterals.end())
		return found->second;

	int location = glGetUniformLocation(m_id, name);
	if (location == -1)
		std::cout << "Warning: uniform " << name << " doesnt't exist!" << std::endl;
	m_uniformLocationLiterals.emplace(name, location);
	return location;
}

Shader* Shader::createDefault()
{
	// Basic vertex shader
	const std::string vertexShader = R"(
		#version 330 core
		layout(location = 0) in vec4 position;
		layout(location = 1) in vec4 normal;
		layout(location = 2) in vec4 colour;
		layout(location = 3) in vec2 texcoord;
		layout(location = 4) in vec3 skin;
		// One placed-object world matrix, one column per attribute. Only read when
		// useInstancing is set; Mesh::drawInstanced is the only caller that sets it.
		layout(location = 5) in vec4 instanceMatrix0;
		layout(location = 6) in vec4 instanceMatrix1;
		layout(location = 7) in vec4 instanceMatrix2;
		layout(location = 8) in vec4 instanceMatrix3;

		uniform mat4 VPMatrix;
		uniform mat4 modelMatrix;
		uniform mat3 uvMatrix;
		uniform vec2 clipOffset;
		uniform int useInstancing;
		// TY1 model matrices: bones[0] is the model root, bones[n + 1] is anim node n.
		// boneParents[i] is the matrix of node i's parent.
		uniform int useSkinning;
		uniform mat4 bones[64];
		uniform int boneParents[64];

		out vec4 v_colour;
		out vec2 v_texcoord;

		void main()
		{
			mat4 world = useInstancing != 0
				? mat4(instanceMatrix0, instanceMatrix1, instanceMatrix2, instanceMatrix3)
				: modelMatrix;
			vec4 local = position;
			if (useSkinning != 0)
			{
				// Model_Draw (ModelGC.cpp): w * M[m1] * v + (1 - w) * M[m2] * v.
				// Guess: the PC files leave m2 at 0 on blended vertices, so the
				// second matrix is taken to be the first matrix's parent.
				int m1 = clamp(int(skin.y + 0.5), 0, 63);
				int m2 = clamp(int(skin.z + 0.5), 0, 63);
				float w = skin.x;
				if (w < 1.0 && m2 == 0)
					m2 = boneParents[m1];
				vec4 p = vec4(position.xyz, 1.0);
				local = w * (bones[m1] * p) + (1.0 - w) * (bones[m2] * p);
				local.w = 1.0;
			}
			vec4 worldPos = world * local;
			gl_Position = VPMatrix * worldPos;
			gl_Position.xy += clipOffset * gl_Position.w;
			v_colour = colour;
			v_texcoord = (uvMatrix * vec3(texcoord, 1.0)).xy;
		}
	)";

	// Basic fragment shader with texture support
	const std::string fragmentShader = R"(
		#version 330 core
		in vec4 v_colour;
		in vec2 v_texcoord;

		uniform sampler2D diffuseTexture;
		uniform sampler2D waterRipple;
		uniform vec4 waterScale;
		uniform vec4 tintColour;
		uniform float alphaRef;
		uniform int solidColour;
		uniform int water;

		out vec4 color;

		// GameCube indirect matrix entries are signed 11-bit, value * 1024.
		// 1.0 becomes 1024, which sign-extends to -1024.
		int quantS11(float value)
		{
			int q = int(value * 1024.0) & 2047;
			if ((q & 1024) != 0)
				q -= 2048;
			return q;
		}

		void main()
		{
			vec2 sampleUv = v_texcoord;
			if (water != 0)
			{
				// Mesh V is stored flipped. The ripple map is looked up in game UV.
				vec2 gameUv = vec2(v_texcoord.x, 1.0 - v_texcoord.y);
				vec3 abg = texture(waterRipple, gameUv * waterScale.xy).abg;
				ivec3 ind = ivec3(round(abg * 255.0)) - 128;
				int ma = quantS11(waterScale.z);
				int md = quantS11(waterScale.w);
				int bias = quantS11(1.0);
				// scale_exp 1: (dot >> 3) << 1, in 1/128-texel units.
				int rawS = ((ma * ind.x + bias * ind.z) >> 3) << 1;
				int rawT = ((md * ind.y + bias * ind.z) >> 3) << 1;
				vec2 deltaGame = vec2(rawS, rawT) / (vec2(textureSize(diffuseTexture, 0)) * 128.0);
				sampleUv += vec2(deltaGame.x, -deltaGame.y);
			}

			vec4 texColor = texture(diffuseTexture, sampleUv);
			vec4 shaded = water != 0
				? vec4(texColor.rgb, texColor.a * v_colour.a) * tintColour
				: texColor * v_colour * tintColour;
			if (solidColour != 0)
			{
				if (texColor.a < alphaRef)
					discard;
				color = tintColour;
			}
			else
			{
				if (shaded.a < alphaRef)
					discard;
				color = shaded;
			}
		}
	)";

	return new Shader(vertexShader, fragmentShader);
}

Shader* Shader::createWater()
{
	// PC water.shader. Only water surfaces bind this. 1a/2a are (dirX, dirZ, frequency, phase).
	// 1b/2b.x is negated height. The reflection lookup uses the flat vertex, before the waves.
	const std::string vertexShader = R"(
		#version 330 core
		layout(location = 0) in vec4 position;
		layout(location = 1) in vec4 normal;
		layout(location = 2) in vec4 colour;
		layout(location = 3) in vec2 texcoord;
		layout(location = 4) in vec3 skin;
		layout(location = 5) in vec4 instanceMatrix0;
		layout(location = 6) in vec4 instanceMatrix1;
		layout(location = 7) in vec4 instanceMatrix2;
		layout(location = 8) in vec4 instanceMatrix3;

		uniform mat4 VPMatrix;
		uniform mat4 modelMatrix;
		uniform mat3 uvMatrix;
		uniform int useInstancing;
		uniform vec4 waterWaveCoeffs1a;
		uniform vec4 waterWaveCoeffs1b;
		uniform vec4 waterWaveCoeffs2a;
		uniform vec4 waterWaveCoeffs2b;

		out vec4 v_colour;
		out vec2 v_texcoord;
		out vec3 v_worldPos;
		out vec4 v_screenPos;
		out float v_viewW;

		void main()
		{
			mat4 world = useInstancing != 0
				? mat4(instanceMatrix0, instanceMatrix1, instanceMatrix2, instanceMatrix3)
				: modelMatrix;
			vec4 flatPos = world * position;
			vec4 worldPos = flatPos;
			// Wave 2 reuses wave 1's direction. The PC shader does this too.
			float dir2d = flatPos.x * waterWaveCoeffs1a.x + flatPos.z * waterWaveCoeffs1a.y;
			float height1 = sin(dir2d * waterWaveCoeffs1a.z - waterWaveCoeffs1a.w) * waterWaveCoeffs1b.x;
			float height2 = sin(dir2d * waterWaveCoeffs2a.z - waterWaveCoeffs2a.w) * waterWaveCoeffs2b.x;
			worldPos.y += height1 + height2;
			gl_Position = VPMatrix * worldPos;
			v_viewW = gl_Position.w;
			v_worldPos = worldPos.xyz;
			v_screenPos = VPMatrix * flatPos;
			v_colour = colour;
			v_texcoord = (uvMatrix * vec3(texcoord, 1.0)).xy;
		}
	)";

	const std::string fragmentShader = R"(
		#version 330 core
		in vec4 v_colour;
		in vec2 v_texcoord;
		in vec3 v_worldPos;
		in vec4 v_screenPos;
		in float v_viewW;

		uniform sampler2D diffuseTexture;
		uniform sampler2D noiseTexture;
		uniform sampler2D reflectTexture;
		uniform vec4 tintColour;
		// water.shader: 1 is (distanceScale, 0, 0, time). 2 is (uScale, vScale, noiseScale, reflectWobble).
		uniform vec4 waterWobbleCoeffs1;
		uniform vec4 waterWobbleCoeffs2;
		// (1/width, 1/height, reflectMix, reflectAdd). Steady crossfade, so mix and add are unscaled.
		uniform vec4 waterReflectCoeff;
		uniform float alphaRef;
		uniform int reflectEnabled;

		out vec4 color;

		void main()
		{
			float distScale = clamp(waterWobbleCoeffs1.x / v_viewW - 1.0, 0.3, 1.0);
			float animTime = waterWobbleCoeffs1.w;
			vec2 noiseUV1 = mod((v_worldPos.xz + vec2(5.3, 3.7) * animTime) * waterWobbleCoeffs2.z, 1.0);
			vec2 noiseUV2 = mod((v_worldPos.zx + vec2(-9.4, -4.2) * animTime) * waterWobbleCoeffs2.z, 1.0);
			vec4 noise = texture(noiseTexture, noiseUV1) + texture(noiseTexture, noiseUV2) - 1.0;
			vec2 uvOffset = noise.xy * distScale;
			// Mesh V is stored flipped, so the wobble's V is negated. Screen space is not.
			vec2 sampleUv = v_texcoord + vec2(uvOffset.x * waterWobbleCoeffs2.x, -uvOffset.y * waterWobbleCoeffs2.y);
			vec4 shaded = texture(diffuseTexture, sampleUv) * v_colour * tintColour;
			if (reflectEnabled != 0 && v_screenPos.w != 0.0)
			{
				vec2 screenUV = (v_screenPos.xy / v_screenPos.w) * 0.5 + 0.5;
				vec4 reflected = texture(reflectTexture, screenUV + uvOffset * waterWobbleCoeffs2.w);
				reflected.w = 1.0;
				shaded = mix(shaded, reflected, waterReflectCoeff.z);
				shaded.xyz += reflected.xyz * waterReflectCoeff.w;
			}
			if (shaded.a < alphaRef)
				discard;
			color = shaded;
		}
	)";

	return new Shader(vertexShader, fragmentShader);
}

Shader* Shader::createReflection(bool cutout, bool clip)
{
	// waterReflect.shader. Instanced props and room meshes. The oblique near plane clips
	// the reflection, so the common program does not write gl_ClipDistance and does not
	// discard: both of those disable early-Z for every fragment in the program.
	std::string vertexShader = R"(
		#version 330 core
		layout(location = 0) in vec4 position;
		layout(location = 1) in vec4 normal;
		layout(location = 2) in vec4 colour;
		layout(location = 3) in vec2 texcoord;
		layout(location = 4) in vec3 skin;
		layout(location = 5) in vec4 instanceMatrix0;
		layout(location = 6) in vec4 instanceMatrix1;
		layout(location = 7) in vec4 instanceMatrix2;
		layout(location = 8) in vec4 instanceMatrix3;

		uniform mat4 VPMatrix;
		uniform mat4 modelMatrix;
		uniform mat3 uvMatrix;
		uniform int useInstancing;
)";
	if (clip)
		vertexShader += "\t\tuniform float clipPlaneY;\n";
	vertexShader += R"(
		out vec4 v_colour;
		out vec2 v_texcoord;

		void main()
		{
			mat4 world = useInstancing != 0
				? mat4(instanceMatrix0, instanceMatrix1, instanceMatrix2, instanceMatrix3)
				: modelMatrix;
			vec4 flatPos = world * position;
			gl_Position = VPMatrix * flatPos;
)";
	if (clip)
		vertexShader += "\t\t\tgl_ClipDistance[0] = flatPos.y - clipPlaneY;\n";
	vertexShader += R"(
			v_colour = colour;
			v_texcoord = (uvMatrix * vec3(texcoord, 1.0)).xy;
		}
	)";

	std::string fragmentShader = R"(
		#version 330 core
		in vec4 v_colour;
		in vec2 v_texcoord;

		uniform sampler2D diffuseTexture;
		uniform vec4 tintColour;
)";
	if (cutout)
		fragmentShader += "\t\tuniform float alphaRef;\n";
	fragmentShader += R"(
		out vec4 color;

		void main()
		{
			vec4 shaded = texture(diffuseTexture, v_texcoord) * v_colour * tintColour;
)";
	if (cutout)
		fragmentShader += "\t\t\tif (shaded.a < alphaRef)\n\t\t\t\tdiscard;\n";
	fragmentShader += R"(
			color = shaded;
		}
	)";

	return new Shader(vertexShader, fragmentShader);
}
