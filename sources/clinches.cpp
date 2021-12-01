#include "common.h"
#include "baseTile.h"

#include <cage-core/entities.h>
#include <cage-core/geometry.h>
#include <cage-core/spatialStructure.h>
#include <cage-core/hashString.h>
#include <cage-core/random.h>

#include <cage-engine/scene.h>
#include <cage-simple/engine.h>

#include <vector>
#include <algorithm>

namespace
{
	const Real tileLength = 70; 

	Holder<SpatialStructure> spatialSearchData;
	Holder<SpatialQuery> spatialSearchQuery;

	struct Tile
	{
		TilePos pos;
		std::vector<Entity *> clinches;
		Real distanceToPlayer() const
		{
			return pos.distanceToPlayer(tileLength);
		}
	};

	std::vector<Tile> tiles;

	void generateClinches(Tile &t)
	{
		CAGE_ASSERT(t.clinches.empty());
		RandomGenerator rg(hash(t.pos.x), hash(t.pos.y));
		uint32 cnt = numeric_cast<uint32>(pow(Real::E(), max(t.pos.y - 2, 0) * -0.01) * 5) + 1;
		for (uint32 i = 0; i < cnt; i++)
		{
			Entity *e = engineEntities()->createUnique();
			t.clinches.push_back(e);
			TransformComponent &tr = e->value<TransformComponent>();
			Vec2 pos = (Vec2(t.pos.x, t.pos.y) + Vec2(rg.randomChance(), rg.randomChance()) - 0.5) * tileLength;
			tr.position = Vec3(pos, terrainOffset(pos) + CLINCH_TERRAIN_OFFSET);
			RenderComponent &r = e->value<RenderComponent>();
			r.object = HashString("cragsman/clinch/clinch.object");
		}
	}

	bool engineUpdate()
	{
		bool changes = false;
		{ // remove unneeded tiles
			tiles.erase(std::remove_if(tiles.begin(), tiles.end(), [&](const Tile &t) {
				bool r = t.distanceToPlayer() > 400;
				changes |= r;
				return r;
			}), tiles.end());
		}
		{ // check needed tiles
			std::set<TilePos> needed = findNeededTiles(tileLength, 300);
			for (Tile &t : tiles)
				needed.erase(t.pos);
			for (const TilePos &n : needed)
			{
				Tile t;
				t.pos = n;
				generateClinches(t);
				tiles.push_back(std::move(t));
				changes = true;
			}
		}
		{ // update spatial
			if (changes)
			{
				spatialSearchData->clear();
				for (Tile &t : tiles)
				{
					for (Entity *e : t.clinches)
					{
						TransformComponent &tr = e->value<TransformComponent>();
						spatialSearchData->update(e->name(), Sphere(tr.position, 1));
					}
				}
				spatialSearchData->rebuild();
			}
		}
		return false;
	}

	bool engineInitialize()
	{
		spatialSearchData = newSpatialStructure({});
		spatialSearchQuery = newSpatialQuery(spatialSearchData.share());
		return false;
	}

	class Callbacks
	{
		EventListener<bool()> engineUpdateListener;
		EventListener<bool()> engineInitListener;
	public:
		Callbacks()
		{
			engineUpdateListener.attach(controlThread().update);
			engineUpdateListener.bind<&engineUpdate>();
			engineInitListener.attach(controlThread().initialize);
			engineInitListener.bind<&engineInitialize>();
		}
	} callbacksInstance;
}

void findInitialClinches(uint32 &count, Entity **result)
{
	spatialSearchQuery->intersection(Aabb::Universe());
	auto res = spatialSearchQuery->result();
	if (res.size() < count)
	{
		count = 0;
		return;
	}
	std::sort(res.begin(), res.end(), [](uint32 a, uint32 b) {
		TransformComponent &ta = engineEntities()->get(a)->value<TransformComponent>();
		TransformComponent &tb = engineEntities()->get(b)->value<TransformComponent>();
		Real da = distance(ta.position, Vec3());
		Real db = distance(tb.position, Vec3());
		return da < db;
	});
	for (uint32 i = 0; i < count; i++)
		result[i] = engineEntities()->get(res[i]);
}

Entity *findClinch(const Vec3 &pos, Real maxDist)
{
	spatialSearchQuery->intersection(Sphere(pos, maxDist));
	auto res = spatialSearchQuery->result();
	if (!res.size())
		return nullptr;
	uint32 n = *std::min_element(res.begin(), res.end(), [pos](uint32 a, uint32 b) {
		TransformComponent &ta = engineEntities()->get(a)->value<TransformComponent>();
		TransformComponent &tb = engineEntities()->get(b)->value<TransformComponent>();
		Real da = distance(ta.position, pos);
		Real db = distance(tb.position, pos);
		return da < db;
	});
	return engineEntities()->get(n);
}
