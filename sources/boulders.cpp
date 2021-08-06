#include "common.h"

#include <cage-core/entities.h>
#include <cage-core/hashString.h>

#include <cage-engine/core.h>
#include <cage-engine/engine.h>

#include <vector>

namespace
{
	struct BoulderComponent
	{
		static EntityComponent *component;
	};

	EntityComponent *BoulderComponent::component;

	bool engineUpdate()
	{
		if (!characterBody)
			return false;
		TransformComponent &pt = engineEntities()->get(characterBody)->value<TransformComponent>();
		if (randomChance() < 0.01)
		{ // spawn a boulder
			Entity *e = engineEntities()->createAnonymous();
			TransformComponent &t = e->value<TransformComponent>();
			t.scale = randomChance() + 1.5;
			t.position = pt.position + vec3(randomChance() * 300 - 150, 250, 0);
			t.position[2] = terrainOffset(vec2(t.position)) + t.scale;
			t.orientation = randomDirectionQuat();
			RenderComponent &r = e->value<RenderComponent>();
			r.object = HashString("cragsman/boulder/boulder.object");
			{
				real dummy;
				terrainMaterial(vec2(t.position), r.color, dummy, dummy, true);
			}
			GAME_COMPONENT(Physics, p, e);
			p.collisionRadius = t.scale;
			p.mass = sphereVolume(p.collisionRadius) * 0.5;
			GAME_COMPONENT(Boulder, br, e);
		}
		std::vector<Entity*> entsToDestroy;
		for (Entity *e : BoulderComponent::component->entities())
		{ // rotate boulders
			TransformComponent &t = e->value<TransformComponent>();
			if (t.position[1] < pt.position[1] - 150)
				entsToDestroy.push_back(e);
			else
			{
				GAME_COMPONENT(Physics, p, e);
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
		BoulderComponent::component = engineEntities()->defineComponent(BoulderComponent());
		return false;
	}

	class Callbacks
	{
		EventListener<bool()> engineInitListener;
		EventListener<bool()> engineUpdateListener;
	public:
		Callbacks()
		{
			engineInitListener.attach(controlThread().initialize);
			engineInitListener.bind<&engineInitialize>();
			engineUpdateListener.attach(controlThread().update);
			engineUpdateListener.bind<&engineUpdate>();
		}
	} callbacksInstance;
}
