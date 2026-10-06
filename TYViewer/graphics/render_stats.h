#pragma once

// Per-frame counters for the TY1 scene pass. The app is single-threaded, so these
// are plain statics, reset once per frame in Application::render and read back
// afterward for the on-screen readout and for A/B measurement while optimizing.
struct RenderStats
{
	// glDrawElements + glDrawElementsInstanced calls this frame.
	static int drawCalls;
	// Of drawCalls, how many were glDrawElementsInstanced (one call drawing many props).
	static int instancedBatches;
	static long long trianglesDrawn;
	// Prop instances that passed visibility and the frustum test this frame.
	static int instancesVisited;
	// Prop instances skipped by the frustum test (visible flag on, but off-screen).
	static int instancesCulled;
	// Level part (room mesh) draws that passed the frustum test this frame.
	static int partsVisited;
	// Enabled level parts skipped by the frustum test (off-screen). Disabled parts
	// (hidden collision, user toggles) are not counted either way.
	static int partsCulled;
	// Wall time for the scene draw section (room meshes + prop batches), in milliseconds.
	static float lastSceneMs;
	// CritterSystem::update, which runs before the scene timer. 0 or 1 tick per frame.
	static float lastSimMs;
	static int simTicks;

	static void beginFrame();
};
