
#include "common.h"

#include <cage-core/log.h>
#include <cage-core/entities.h>
#include <cage-core/utility/hashString.h>

#include <cage-client/core.h>
#include <cage-client/engine.h>

namespace
{
	struct boulderRotationComponent
	{
		static componentClass *component;
	};

	componentClass *boulderRotationComponent::component;

	bool engineUpdate()
	{
		if (random() < 0.01)
		{ // spawn a boulder
			ENGINE_GET_COMPONENT(transform, pt, entities()->getEntity(characterBody));
			entityClass *e = entities()->newAnonymousEntity();
			ENGINE_GET_COMPONENT(transform, t, e);
			t.scale = random() + 1.5;
			t.position = pt.position + vec3(random() * 400 - 200, 300, 0);
			t.position[2] = terrainOffset(vec2(t.position)) + t.scale;
			t.orientation = randomDirectionQuat();
			ENGINE_GET_COMPONENT(render, r, e);
			r.object = hashString("cragsman/boulder/boulder.object");
			{
				real dummy;
				terrainMaterial(vec2(t.position), r.color, dummy, dummy);
			}
			GAME_GET_COMPONENT(physics, p, e);
			p.collisionRadius = t.scale;
			real volume = 4 * real::Pi * pow(p.collisionRadius, 3) / 3;
			p.mass = volume * 0.5;
			GAME_GET_COMPONENT(timeout, to, e);
			to.ttl = 1000;
			GAME_GET_COMPONENT(boulderRotation, br, e);
		}
		for (entityClass *e : boulderRotationComponent::component->getComponentEntities()->entities())
		{ // rotate boulders
			ENGINE_GET_COMPONENT(transform, t, e);
			GAME_GET_COMPONENT(physics, p, e);
			real amount = p.velocity.length();
			quat rot = quat(degs(amount), degs(), degs());
			t.orientation = rot * t.orientation;
		}
		return false;
	}

	bool engineInitialize()
	{
		boulderRotationComponent::component = entities()->defineComponent(boulderRotationComponent(), true);
		return false;
	}

	class callbacksInitClass
	{
	public:
		eventListener<bool()> engineInitListener;
		eventListener<bool()> engineUpdateListener;

		callbacksInitClass()
		{
			engineInitListener.attach(controlThread().initialize);
			engineInitListener.bind<&engineInitialize>();
			engineUpdateListener.attach(controlThread().update);
			engineUpdateListener.bind<&engineUpdate>();
		}
	} callbacksInitInstance;
}
