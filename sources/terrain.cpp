#include <vector>
#include <algorithm>

#include "common.h"

#include <cage-core/log.h>
#include <cage-core/entities.h>
#include <cage-core/geometry.h>
#include <cage-core/utility/spatial.h>
#include <cage-core/utility/hashString.h>

#include <cage-client/core.h>
#include <cage-client/engine.h>

namespace
{
	holder<spatialDataClass> spatialData;
	holder<spatialQueryClass> spatialQuery;
}

void initializeMap(uint64 seed, uint32 level)
{
	spatialData = newSpatialData(spatialDataCreateConfig());
	for (uint32 i = 0; i < 100; i++)
	{
		entityClass *e = entities()->newUniqueEntity();
		ENGINE_GET_COMPONENT(transform, t, e);
		vec2 pos = vec2(random() - 0.5, random() - 0.5) * 300;
		t.position = vec3(pos, terrainOffset(pos) + CLINCH_TERRAIN_OFFSET);
		ENGINE_GET_COMPONENT(render, r, e);
		r.object = hashString("cragsman/clinch/clinch.object");
		spatialData->update(e->getName(), sphere(t.position, 1));
	}
	spatialData->rebuild();
	spatialQuery = newSpatialQuery(spatialData.get());
}

entityClass *findClinch(const vec3 &pos, real maxDist)
{
	spatialQuery->intersection(sphere(pos, maxDist));
	if (!spatialQuery->resultCount())
		return nullptr;
	std::vector<uint32> vec(spatialQuery->result().begin(), spatialQuery->result().end());
	uint32 n = *std::min_element(vec.begin(), vec.end(), [pos](uint32 a, uint32 b) {
		ENGINE_GET_COMPONENT(transform, ta, entities()->getEntity(a));
		ENGINE_GET_COMPONENT(transform, tb, entities()->getEntity(b));
		real da = distance(ta.position, pos);
		real db = distance(tb.position, pos);
		return da < db;
	});
	return entities()->getEntity(n);
}

entityClass *findClinch(const line &ln)
{
	CAGE_ASSERT_RUNTIME(ln.normalized());
	spatialQuery->intersection(ln);
	if (spatialQuery->resultCount())
		return entities()->getEntity(spatialQuery->resultArray()[0]);
	return nullptr;
}

vec3 terrainIntersection(const line &ln)
{
	CAGE_ASSERT_RUNTIME(ln.normalized());
	real dst = ln.a()[2] / dot(ln.direction, vec3(0, 0, -1));
	vec3 base = ln.a() + ln.direction * dst;
	CAGE_ASSERT_RUNTIME(abs(base[2]) < 1e-5, base);
	return vec3(vec2(base), terrainOffset(vec2(base)));
}
