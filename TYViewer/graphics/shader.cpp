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
void Shader::setUniform4f(const std::string& name, glm::vec4 v)
{
	glUniform4f(getUniformLocation(name), v.x, v.y, v.z, v.w);
}
void Shader::setUniformMat4(const std::string& name, glm::mat4 mat)
{
	glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, &mat[0][0]);
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

		uniform mat4 VPMatrix;
		uniform mat4 modelMatrix;

		out vec4 v_colour;
		out vec2 v_texcoord;

		void main()
		{
			gl_Position = VPMatrix * modelMatrix * position;
			v_colour = colour;
			v_texcoord = texcoord;
		}
	)";

	// Basic fragment shader with texture support
	const std::string fragmentShader = R"(
		#version 330 core
		in vec4 v_colour;
		in vec2 v_texcoord;

		uniform sampler2D diffuseTexture;
		uniform vec4 tintColour;

		out vec4 color;

		void main()
		{
			vec4 texColor = texture(diffuseTexture, v_texcoord);
			color = texColor * v_colour * tintColour;
		}
	)";

	return new Shader(vertexShader, fragmentShader);
}
