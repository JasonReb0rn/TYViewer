#include "render_stats.h"

int RenderStats::drawCalls = 0;
int RenderStats::instancedBatches = 0;
long long RenderStats::trianglesDrawn = 0;
int RenderStats::instancesVisited = 0;
int RenderStats::instancesCulled = 0;
int RenderStats::partsVisited = 0;
int RenderStats::partsCulled = 0;
float RenderStats::lastSceneMs = 0.0f;
float RenderStats::lastReflectionMs = 0.0f;
int RenderStats::reflectionDrawCalls = 0;
int RenderStats::reflectionPlanes = 0;
float RenderStats::lastSimMs = 0.0f;
int RenderStats::simTicks = 0;

void RenderStats::beginFrame()
{
	drawCalls = 0;
	instancedBatches = 0;
	trianglesDrawn = 0;
	instancesVisited = 0;
	instancesCulled = 0;
	partsVisited = 0;
	partsCulled = 0;
	reflectionDrawCalls = 0;
	reflectionPlanes = 0;
}
