#pragma once

#include <string>

class Texture
{
public:
	Texture(unsigned int id, int width = 0, int height = 0);
	~Texture();

	// RGBA8, linear, repeating. The water ripple map is updated in place each frame.
	static Texture* createRGBA(int width, int height, const unsigned char* pixels);
	void updateRGBA(const unsigned char* pixels) const;

	void bind(unsigned int slot = 0) const;
	void unbind() const;
private:
	unsigned int id;
	int width;
	int height;
};