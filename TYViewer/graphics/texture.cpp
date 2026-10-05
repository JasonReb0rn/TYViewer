#include "texture.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define TEXTURE_DEFAULT_ID 1

Texture* Texture::createRGBA(int width, int height, const unsigned char* pixels)
{
	unsigned int id = 0;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glBindTexture(GL_TEXTURE_2D, 0);
	return new Texture(id, width, height);
}

void Texture::updateRGBA(const unsigned char* pixels) const
{
	glBindTexture(GL_TEXTURE_2D, id);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glBindTexture(GL_TEXTURE_2D, 0);
}

Texture::Texture(unsigned int id, int width, int height)
	: id(id), width(width), height(height)
{
	glBindTexture(GL_TEXTURE_2D, id);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glBindTexture(GL_TEXTURE_2D, 0);
}

Texture::~Texture()
{
	glDeleteTextures(1, &id);
}

void Texture::bind(unsigned int slot) const
{
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(GL_TEXTURE_2D, id);
}
void Texture::unbind() const
{
	glBindTexture(GL_TEXTURE_2D, 0);
}