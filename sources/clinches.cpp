#include <set>
#include <vector>
#include <algorithm>

#include "common.h"
#include "baseTile.h"

#include <cage-core/log.h>
#include <cage-core/entities.h>
#include <cage-core/geometry.h>
#include <cage-core/spatial.h>
#include <cage-core/hashString.h>
#include <cage-core/random.h>

#include <cage-client/core.h>
#include <cage-client/engine.h>

namespace cage
{
	namespace detail
	{
		CAGE_API uint32 hash(uint32);
	}
}

namespace
{
	const real tileLength = 70; // real world size of a tile (in 1 dimension)

	holder<spatialData> spatialSearchData;
	holder<spatialQuery> spatialSearchQuery;

	struct tileStruct
	{
		tilePosStruct pos;
		std::vector<entity *> clinches;
		real distanceToPlayer() const
		{
			return pos.distanceToPlayer(tileLength);
		}
	};

	std::vector<tileStruct> tiles;

	void generateClinches(tileStruct &t)
	{
		CAGE_ASSERT_RUNTIME(t.clinches.empty());
		randomGenerator rg(detail::hash(t.pos.x), detail::hash(t.pos.y));
		uint32 cnt = numeric_cast<uint32>(pow(real::E(), max(t.pos.y - 2, 0) * -0.01) * 5) + 1;
		for (uint32 i = 0; i < cnt; i++)
		{
			entity *e = entities()->createUnique();
			t.clinches.push_back(e);
			CAGE_COMPONENT_ENGINE(transform, tr, e);
			vec2 pos = (vec2(t.pos.x, t.pos.y) + vec2(rg.randomChance(), rg.randomChance()) - 0.5) * tileLength;
			tr.position = vec3(pos, terrainOffset(pos) + CLINCH_TERRAIN_OFFSET);
			CAGE_COMPONENT_ENGINE(render, r, e);
			r.object = hashString("cragsman/clinch/clinch.object");
		}
	}

	bool engineUpdate()
	{
		bool changes = false;
		{ // remove unneeded tiles
			tiles.erase(std::remove_if(tiles.begin(), tiles.end(), [&](const tileStruct &t) {
				bool r = t.distanceToPlayer() > 400;
				changes |= r;
				return r;
			}), tiles.end());
		}
		{ // check needed tiles
			std::set<tilePosStruct> needed = findNeededTiles(tileLength, 300);
			for (tileStruct &t : tiles)
				needed.erase(t.pos);
			for (const tilePosStruct &n : needed)
			{
				tileStruct t;
				t.pos = n;
				generateClinches(t);
				tiles.push_back(templates::move(t));
				changes = true;
			}
		}
		{ // update spatial
			if (changes)
			{
				spatialSearchData->clear();
				for (tileStruct &t : tiles)
				{
					for (entity *e : t.clinches)
					{
						CAGE_COMPONENT_ENGINE(transform, tr, e);
						spatialSearchData->update(e->name(), sphere(tr.position, 1));
					}
				}
				spatialSearchData->rebuild();
			}
		}
		return false;
	}

	bool engineInitialize()
	{
		spatialSearchData = newSpatialData(spatialDataCreateConfig());
		spatialSearchQuery = newSpatialQuery(spatialSearchData.get());
		return false;
	}

	class callbacksInitClass
	{
		eventListener<bool()> engineUpdateListener;
		eventListener<bool()> engineInitListener;
	public:
		callbacksInitClass()
		{
			engineUpdateListener.attach(controlThread().update);
			engineUpdateListener.bind<&engineUpdate>();
			engineInitListener.attach(controlThread().initialize);
			engineInitListener.bind<&engineInitialize>();
		}
	} callbacksInitInstance;
}

void findInitialClinches(uint32 &count, entity **result)
{
	spatialSearchQuery->intersection(aabb::Universe());
	if (spatialSearchQuery->resultCount() < count)
	{
		count = 0;
		return;
	}
	std::vector<uint32> vec(spatialSearchQuery->result().begin(), spatialSearchQuery->result().end());
	std::sort(vec.begin(), vec.end(), [](uint32 a, uint32 b) {
		CAGE_COMPONENT_ENGINE(transform, ta, entities()->get(a));
		CAGE_COMPONENT_ENGINE(transform, tb, entities()->get(b));
		real da = distance(ta.position, vec3());
		real db = distance(tb.position, vec3());
		return da < db;
	});
	for (uint32 i = 0; i < count; i++)
		result[i] = entities()->get(vec[i]);
}

entity *findClinch(const vec3 &pos, real maxDist)
{
	spatialSearchQuery->intersection(sphere(pos, maxDist));
	if (!spatialSearchQuery->resultCount())
		return nullptr;
	std::vector<uint32> vec(spatialSearchQuery->result().begin(), spatialSearchQuery->result().end());
	uint32 n = *std::min_element(vec.begin(), vec.end(), [pos](uint32 a, uint32 b) {
		CAGE_COMPONENT_ENGINE(transform, ta, entities()->get(a));
		CAGE_COMPONENT_ENGINE(transform, tb, entities()->get(b));
		real da = distance(ta.position, pos);
		real db = distance(tb.position, pos);
		return da < db;
	});
	return entities()->get(n);
}
