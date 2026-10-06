#include "camera.h"

#include <glad/glad.h>

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

Camera::Camera(const glm::vec3& position, const glm::vec3& rotation,
	float fieldOfView, float aspectRatio, float clipNear, float clipFar) :
	position(position),
	rotation(rotation),
	fieldOfView(fieldOfView),
	aspectRatio(aspectRatio),
	clipPlaneNear(clipNear), clipPlaneFar(clipFar),
	worldUp({ 0.0f, 1.0f, 0.0f }),

	projection(glm::mat4(1.0f)), view(glm::mat4(1.0f))
{
	updateProjectionMatrix();
	updateViewMatrix();
}

void Camera::setPosition(const glm::vec3& p)
{
	position = p;
	updateViewMatrix();
}
const glm::vec3& Camera::getPosition() const
{
	return position;
}

void Camera::setRotation(const glm::vec3& r)
{
	rotation = r;
	updateViewMatrix();
}
const glm::vec3& Camera::getRotation() const
{
	return rotation;
}

void Camera::setFieldOfView(const float& fov)
{
	fieldOfView = fov;
	updateProjectionMatrix();
}
const float& Camera::getFieldOfView() const
{
	return fieldOfView;
}

void Camera::setAspectRatio(const float& ar)
{
	aspectRatio = ar;
	updateProjectionMatrix();
}
const float& Camera::getAspectRatio() const
{
	return aspectRatio;
}

void Camera::setClipPlaneNear(const float& n)
{
	clipPlaneNear = n;
	updateProjectionMatrix();
}
const float& Camera::getClipPlaneNear() const
{
	return clipPlaneNear;
}

void Camera::setClipPlaneFar(const float& f)
{
	clipPlaneFar = f;
	updateProjectionMatrix();
}
const float& Camera::getClipPlaneFar() const
{
	return clipPlaneFar;
}


const glm::mat4& Camera::getProjectionMatrix() const
{
	return projection;
}

const glm::mat4& Camera::getViewMatrix() const
{
	return view;
}


void Camera::localRotate(const glm::vec3& r)
{
	rotation += r;

	if (rotation.y > 89.0f)
		rotation.y = 89.0f;
	if (rotation.y < -89.0f)
		rotation.y = -89.0f;

	updateViewMatrix();
}

void Camera::localTranslate(const glm::vec3& t)
{
	position += -localRight * t.x + localUp * t.y + localForward * t.z;
	updateViewMatrix();
}


void Camera::updateProjectionMatrix()
{
	const float fov = glm::radians(fieldOfView);
	const float n = clipPlaneNear;
	const float f = clipPlaneFar;
	// Reversed depth into the float world target: the near plane is the greater
	// value and the depth test keeps it (GL_GEQUAL, clear 0). Passing far then
	// near to glm::perspective only reverses the [-1, 1] clip range. This GPU
	// still quantizes that range to 24 bits while converting it to window depth,
	// so a 1-unit gap at a few thousand units ties and level parts fight. With
	// glClipControl the clip range is already [0, 1] (near = 1, far = 0) and the
	// float buffer keeps the separation.
	if (glad_glClipControl != nullptr && f > n && aspectRatio > 0.0f)
	{
		const float t = tanf(fov * 0.5f);
		projection = glm::mat4(0.0f);
		projection[0][0] = 1.0f / (aspectRatio * t);
		projection[1][1] = 1.0f / t;
		projection[2][2] = n / (f - n);
		projection[2][3] = -1.0f;
		projection[3][2] = (f * n) / (f - n);
		return;
	}

	projection = glm::perspective(fov, aspectRatio, f, n);
}

void Camera::updateViewMatrix()
{
	localForward.x = std::cos(glm::radians(rotation.x)) * std::cos(glm::radians(rotation.y));
	localForward.y = std::sin(glm::radians(rotation.y));
	localForward.z = std::sin(glm::radians(rotation.x)) * std::cos(glm::radians(rotation.y));
	localForward = glm::normalize(localForward);

	localRight = glm::normalize(glm::cross(localForward, worldUp));
	localUp = glm::normalize(glm::cross(localRight, localForward));

	view = glm::lookAt(position, position + localForward, localUp);
}
