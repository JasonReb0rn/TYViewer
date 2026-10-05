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
	glUseProgram(m_id);
}
void Shader::unbind() const
{
	glUseProgram(0);
}

void Shader::setUniform1i(const std::string& name, int v)
{
	glUniform1i(getUniformLocation(name), v);
}
void Shader::setUniform1f(const std::string& name, float v)
{
	glUniform1f(getUniformLocation(name), v);
}
void Shader::setUniform2f(const std::string& name, glm::vec2 v)
{
	glUniform2f(getUniformLocation(name), v.x, v.y);
}
void Shader::setUniform4f(const std::string& name, glm::vec4 v)
{
	glUniform4f(getUniformLocation(name), v.x, v.y, v.z, v.w);
}
void Shader::setUniformMat4(const std::string& name, glm::mat4 mat)
{
	glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, &mat[0][0]);
}
void Shader::setUniformMat3(const std::string& name, glm::mat3 mat)
{
	glUniformMatrix3fv(getUniformLocation(name), 1, GL_FALSE, &mat[0][0]);
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

		out vec4 v_colour;
		out vec2 v_texcoord;

		void main()
		{
			mat4 world = useInstancing != 0
				? mat4(instanceMatrix0, instanceMatrix1, instanceMatrix2, instanceMatrix3)
				: modelMatrix;
			gl_Position = VPMatrix * world * position;
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
