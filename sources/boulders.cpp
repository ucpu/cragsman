#include <vector>

#include "common.h"

#include <cage-core/log.h>
#include <cage-core/entities.h>
#include <cage-core/hashString.h>

#include <cage-client/core.h>
#include <cage-client/engine.h>

namespace
{
	struct boulderComponent
	{
		static componentClass *component;
	};

	componentClass *boulderComponent::component;

	bool engineUpdate()
	{
		if (!characterBody)
			return false;
		ENGINE_GET_COMPONENT(transform, pt, entities()->getEntity(characterBody));
		if (randomChance() < 0.01)
		{ // spawn a boulder
			entityClass *e = entities()->newAnonymousEntity();
			ENGINE_GET_COMPONENT(transform, t, e);
			t.scale = randomChance() + 1.5;
			t.position = pt.position + vec3(randomChance() * 300 - 150, 250, 0);
			t.position[2] = terrainOffset(vec2(t.position)) + t.scale;
			t.orientation = randomDirectionQuat();
			ENGINE_GET_COMPONENT(render, r, e);
			r.object = hashString("cragsman/boulder/boulder.object");
			{
				real dummy;
				terrainMaterial(vec2(t.position), r.color, dummy, dummy, true);
			}
			GAME_GET_COMPONENT(physics, p, e);
			p.collisionRadius = t.scale;
			p.mass = sphereVolume(p.collisionRadius) * 0.5;
			GAME_GET_COMPONENT(boulder, br, e);
		}
		std::vector<entityClass*> entsToDestroy;
		for (entityClass *e : boulderComponent::component->getComponentEntities()->entities())
		{ // rotate boulders
			ENGINE_GET_COMPONENT(transform, t, e);
			if (t.position[1] < pt.position[1] - 150)
				entsToDestroy.push_back(e);
			else
			{
				GAME_GET_COMPONENT(physics, p, e);
				vec3 r = 1.5 * p.velocity / p.collisionRadius;
				quat rot = quat(degs(r[2] - r[1]), degs(), degs(-r[0]));
				t.orientation = rot * t.orientation;
			}
		}
		for (auto e : entsToDestroy)
			e->destroy();
		return false;
	}

	bool engineInitialize()
	{
		boulderComponent::component = entities()->defineComponent(boulderComponent(), true);
		return false;
	}

	class callbacksInitClass
	{
		eventListener<bool()> engineInitListener;
		eventListener<bool()> engineUpdateListener;
	public:
		callbacksInitClass()
		{
			engineInitListener.attach(controlThread().initialize);
			engineInitListener.bind<&engineInitialize>();
			engineUpdateListener.attach(controlThread().update);
			engineUpdateListener.bind<&engineUpdate>();
		}
	} callbacksInitInstance;
}
