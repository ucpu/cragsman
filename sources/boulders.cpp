#include <vector>

#include "common.h"

#include <cage-core/log.h>
#include <cage-core/entities.h>
#include <cage-core/hashString.h>

#include <cage-engine/core.h>
#include <cage-engine/engine.h>

namespace
{
	struct boulderComponent
	{
		static entityComponent *component;
	};

	entityComponent *boulderComponent::component;

	bool engineUpdate()
	{
		if (!characterBody)
			return false;
		CAGE_COMPONENT_ENGINE(transform, pt, entities()->get(characterBody));
		if (randomChance() < 0.01)
		{ // spawn a boulder
			entity *e = entities()->createAnonymous();
			CAGE_COMPONENT_ENGINE(transform, t, e);
			t.scale = randomChance() + 1.5;
			t.position = pt.position + vec3(randomChance() * 300 - 150, 250, 0);
			t.position[2] = terrainOffset(vec2(t.position)) + t.scale;
			t.orientation = randomDirectionQuat();
			CAGE_COMPONENT_ENGINE(render, r, e);
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
		std::vector<entity*> entsToDestroy;
		for (entity *e : boulderComponent::component->entities())
		{ // rotate boulders
			CAGE_COMPONENT_ENGINE(transform, t, e);
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
