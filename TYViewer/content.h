#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <cmath>

#include <unordered_map>
#include <vector>

#include "SOIL2/SOIL2.h"

#include "loader/archive.h"

#include "loader/assets/wfn.h"

#include "loader/assets/mdl2.h"
#include "loader/assets/mdg.h"

#include "graphics/texture.h"
#include "graphics/shader.h"
#include "util/font.h"
#include "model.h"

#include "util/bitconverter.h"
#include "util/stringext.h"

#include "debug.h"

#include <glm/mat3x3.hpp>
#include <glm/vec4.hpp>


#define MDL2_SUBMESH_SIZE 80

#define MDL2_SEGMENT_VERTEX_LIST_COUNT_OFFSET	12
#define MDL2_SEGMENT_VERTEX_LIST_OFFSET 52

class Content
{
public:
	// If specified texture cannot be loaded, return this.
	// This is your job to assign!
	//    Wait, who am I talking to..? myself..?
	//    I even assigned it, why is old me so aggresive..?
	Texture* defaultTexture = NULL;

	void initialize();
	bool loadRKV(const std::string& path, int archiveIndex = 0);
	
	// Get list of all .mdl files from a specific archive
	std::vector<std::string> getModelList(int archiveIndex);
	// TY1: .lv2 files. TY2: *.lv3.bni chunks (not other .bni files).
	std::vector<std::string> getLevelList(int archiveIndex);
	
	// Set active archive for loading
	void setActiveArchive(int archiveIndex);
	int getActiveArchive() const { return activeArchiveIndex; }
	// Read raw bytes from the currently-active archive (useful for exporters/tools).
	bool getActiveFileData(const std::string& name, std::vector<char>& data) const;
	// True when the active archive contains a file with this name (case-insensitive).
	bool hasActiveFile(const std::string& name) const
	{
		if (activeArchiveIndex < 0 || activeArchiveIndex > 1)
			return false;
		Archive* archive = archives[activeArchiveIndex];
		if (archive == NULL)
			return false;
		File file;
		return archive->getFile(name, file) && file.size != 0;
	}

	template<typename T>
	T* load(const std::string& name)
	{}

	template<>
	Texture* load<Texture>(const std::string& name);

	template<>
	Shader* load<Shader>(const std::string& name);

	template<>
	Model* load<Model>(const std::string& name);

	template<>
	Font* load<Font>(const std::string& name);

	// global.mad texture-matrix animation. The modes are mutually exclusive in the game.
	enum class Ty1UvAnim
	{
		None = 0,
		Scroll,
		Animate,
		Rotate,
		SinRotate,
		EnvScroll
	};

	// TY1 global.mad entry. Unknown names stay opaque with no texture alias.
	struct Ty1MaterialDraw
	{
		std::string textureAlias;
		MeshBlend blend = MeshBlend::Opaque;
		// global.mad "invisible 1": the game does not draw this material.
		bool invisible = false;
		// global.mad "effect = grass,...". The surface may still be drawn.
		bool grassEffect = false;
		// The number after "grass,": section index in grass_types.ini. -1 when absent.
		int grassIndex = -1;
		// global.mad "masked 1" / "aref". Cutout cards (tree walls) discard below this.
		bool masked = false;
		float alphaRef = 0.01f;
		// scroll / animate / rotate / sinrotate / envscroll parameters, in file order.
		Ty1UvAnim uvAnim = Ty1UvAnim::None;
		float uvParam[6] = {};
		// ManuallyScrollTextures. Rates are UV per second of a 60 Hz tick.
		// The animation clock runs at half real time, so these play at 30 Hz.
		// lockU replaces U every tick (SetMatrixX) before the scroll is added.
		bool manualScroll = false;
		float manualDu = 0.0f;
		float manualDv = 0.0f;
		bool lockU = false;
		float lockedU = 0.0f;
		// global.mad clampUV / address. Absent lines repeat, which is what waterfalls use.
		bool clampU = false;
		bool clampV = false;
		// indirectwater. x,y tile the ripple map across the mesh UV. z,w scale the warp.
		// The two flags are the game's camera-locked and world-space paths. Shipped materials leave both off.
		bool indirectWater = false;
		bool waterCameraUv = false;
		bool waterWorldUv = false;
		float waterX = 25.0f;
		float waterY = 50.0f;
		float waterZ = 0.005f;
		float waterW = 0.005f;
	};
	Ty1MaterialDraw lookupTy1Material(const std::string& materialName);

	// One grass_types.ini section, as MKGrass_InitTypes stores it. Lengths are
	// already in world units (metres * 100). Section order is the effect index.
	struct Ty1GrassType
	{
		std::string name;
		// clumpSize * density, capped at 32: blades per triangle.
		int bladesPerTriangle = 0;
		float minHeight = 0.0f;
		float maxHeight = 0.0f;
		float width = 0.0f;
		float upVector = 0.0f;
		int numTextures = 0;
		float maxVisibleRadius = 0.0f;
		std::string material;
	};
	const std::vector<Ty1GrassType>& ty1GrassTypes();
	// Playback clock for ty1UvMatrix. Yaw and pitch are radians.
	void setTy1AnimClock(float timeSeconds, float yawRadians, float pitchRadians);
	float ty1AnimTime() const { return ty1AnimTimeSeconds; }
	float ty1AnimYaw() const { return ty1AnimYawRadians; }
	float ty1AnimPitch() const { return ty1AnimPitchRadians; }
	// Game texture matrix, conjugated through the TY1 mesh V flip, as a mat3.
	glm::mat3 ty1UvMatrix(const std::string& materialName, float timeSeconds, float yawRadians, float pitchRadians) const;
	// Per-axis wrap from clampUV / address. Materials without those lines repeat.
	void ty1UvWrap(const std::string& materialName, bool& clampU, bool& clampV) const;
	// indirectwater scales as (x, y, z, w). False when the material is not water.
	bool ty1IndirectWater(const std::string& materialName, glm::vec4& scale) const;

	// One hash lookup shared by the three calls above, given a name a caller has
	// already lowercased and cached (Mesh does, by material). Null when the
	// material has no global.mad entry; every *For overload treats that as defaults.
	const Ty1MaterialDraw* findTy1MaterialLower(const std::string& lowerMaterialName) const;
	glm::mat3 ty1UvMatrixFor(const Ty1MaterialDraw* draw, float timeSeconds, float yawRadians, float pitchRadians) const;
	void ty1UvWrapFor(const Ty1MaterialDraw* draw, bool& clampU, bool& clampV) const;
	bool ty1IndirectWaterFor(const Ty1MaterialDraw* draw, glm::vec4& scale) const;
	// Shared 16x16 ripple map. Null until the first animation update.
	Texture* ty1WaterRipple() const { return waterRipple; }
	void updateTy1WaterRipple();

	// One water_types.ini section. Every section starts as a copy of [default].
	// Phases are not in the file: wave 0 starts at 0, wave 1 at 13.3.
	struct Ty1WaterType
	{
		std::string name;
		float wave0AnimSpeed = 2.0f;
		float wave0DirX = 0.0f;
		float wave0DirZ = 1.0f;
		float wave0Height = 0.2f;
		float wave0Freq = 0.013f;
		float wave0Phase = 0.0f;
		float wave1AnimSpeed = 3.0f;
		float wave1DirX = 1.0f;
		float wave1DirZ = 0.0f;
		float wave1Height = 0.3f;
		float wave1Freq = 0.02f;
		float wave1Phase = 13.3f;
		glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
		float wobbleUVScale = 0.06f;
		float noiseScale = 0.001f;
		// waterWobbleCoeffs1.x. Stub is 3000.
		float distanceScale = 3000.0f;
		// water.shader REFL path. waterReflectCoeff.z / .w. Stub defaults are 0.2.
		float reflectMix = 0.2f;
		float reflectAdd = 0.2f;
		// waterWobbleCoeffs2.w. Stub is 0.05. The ini key reflectScale is not read.
		float reflectWobble = 0.05f;
		// envMapAnimSpeed is stored doubled. A later animSpeed line replaces it.
		float animSpeed = 8.0f;
		// Integrated animSpeed. The fragment wobble reads this; the vertex wave does not.
		float time = 0.0f;
	};
	// Reads water_types.ini from the TY1 archive. Safe to call more than once.
	void loadTy1WaterTypes();
	// False for an unknown name, and for the [default] section itself.
	bool ty1IsWaterTypeName(const std::string& name) const;
	// Null when the type is unknown. Valid until the next archive load.
	const Ty1WaterType* ty1WaterTypeFor(const std::string& typeName) const;
	// noise.dds. Null when the archive has no such file.
	Texture* ty1WaterNoise() const { return waterNoise; }
	// Planes filled by the reflection pass. A water chunk samples the one whose height matches.
	struct Ty1ReflectionPlane
	{
		float height = 0.0f;
		unsigned texture = 0;
	};
	void setTy1ReflectionPlanes(const Ty1ReflectionPlane* planes, int count);
	// -1 when this surface has no reflection this frame.
	int ty1ReflectionPlaneFor(float surfaceY) const;
	unsigned ty1ReflectionTexture(int index) const;
	// Clip for the reflection pass. Meshes read it every draw so it cannot leak.
	void setTy1WaterClip(bool enabled, float planeY);
	bool ty1WaterClipEnabled() const { return ty1WaterClipOn; }
	float ty1WaterClipY() const { return ty1WaterClipPlaneY; }
	// Real seconds, not the halved animation clock. No-op until the ini is loaded.
	// Paused with the critters by the caller.
	void updateTy1WaterWaves(float dtSeconds);

private:
	void createDefaultTexture();
	void loadTy1Materials();

	Archive* archives[2]; // 0 = TY1, 1 = TY2
	int activeArchiveIndex;

	std::unordered_map<std::string, Texture*> textures;
	std::unordered_map<std::string, Shader*> shaders;
	std::unordered_map<std::string, Model*> models;
	std::unordered_map<std::string, Font*> fonts;

	bool ty1MaterialsReady = false;
	std::unordered_map<std::string, Ty1MaterialDraw> ty1Materials;
	bool ty1GrassTypesReady = false;
	std::vector<Ty1GrassType> ty1GrassTypeList;
	bool ty1WaterTypesReady = false;
	std::unordered_map<std::string, Ty1WaterType> ty1WaterTypes;
	float ty1AnimTimeSeconds = 0.0f;
	float ty1AnimYawRadians = 0.0f;
	float ty1AnimPitchRadians = 0.0f;

	Texture* waterRipple = nullptr;
	Texture* waterNoise = nullptr;
	Ty1ReflectionPlane ty1ReflectionPlanes[2] = {};
	int ty1ReflectionPlaneCount = 0;
	bool ty1WaterClipOn = false;
	float ty1WaterClipPlaneY = 0.0f;
	bool waterPhasesReady = false;
	// Fixed phase of each ripple texel, from the game's RandomFR hash. Angle is added at upload.
	float waterPhaseA[256] = {};
	float waterPhaseB[256] = {};
};

#include "content.inl"