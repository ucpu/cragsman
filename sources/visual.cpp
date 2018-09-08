#include <vector>
#include <unordered_map>
#include <algorithm>

#include "common.h"

#include <cage-core/log.h>
#include <cage-core/entities.h>
#include <cage-core/utility/hashString.h>
#include <cage-core/utility/color.h>

#include <cage-client/core.h>
#include <cage-client/engine.h>

componentClass *springVisualComponent::component;
componentClass *timeoutComponent::component;

springVisualComponent::springVisualComponent() : color(1, 1, 1)
{}

timeoutComponent::timeoutComponent() : ttl(0)
{}

entityClass *newParticle(const vec3 &position, const vec3 &velocity, const vec3 &color, real mass, uint32 ttl)
{
	entityClass *e = entities()->newAnonymousEntity();
	ENGINE_GET_COMPONENT(transform, t, e);
	ENGINE_GET_COMPONENT(render, r, e);
	GAME_GET_COMPONENT(physics, p, e);
	GAME_GET_COMPONENT(timeout, to, e);
	t.position = position;
	t.orientation = randomDirectionQuat();
	r.color = color;
	r.object = hashString("cragsman/particle/particle.object");
	p.mass = mass;
	p.velocity = velocity;
	p.collisionRadius = real::Nan;
	to.ttl = ttl;
	return e;
}

namespace
{
	vec3 entMov(entityClass *e)
	{
		if (e->hasComponent(physicsComponent::component))
		{
			GAME_GET_COMPONENT(physics, p, e);
			return p.velocity / p.mass;
		}
		return {};
	}

	vec3 colorDeviation(const vec3 &color, real deviation)
	{
		return convertHsvToRgb(clamp(convertRgbToHsv(color) + (vec3(random(), random(), random()) - 0.5) * deviation, vec3(), vec3(1,1,1)));
	}

	bool engineUpdate()
	{
		{ // timeout entities
			std::vector<entityClass *> etd;
			for (entityClass *e : timeoutComponent::component->getComponentEntities()->entities())
			{
				GAME_GET_COMPONENT(timeout, t, e);
				if (t.ttl-- == 0)
					etd.push_back(e);
			}
			for (entityClass *e : etd)
				e->destroy();
		}

		{ // spring visuals
			for (entityClass *e : springVisualComponent::component->getComponentEntities()->entities())
			{
				CAGE_ASSERT_RUNTIME(e->hasComponent(springComponent::component));
				GAME_GET_COMPONENT(spring, s, e);
				GAME_GET_COMPONENT(springVisual, v, e);
				entityClass *e0 = entities()->getEntity(s.objects[0]);
				entityClass *e1 = entities()->getEntity(s.objects[1]);
				ENGINE_GET_COMPONENT(transform, t0, e0);
				ENGINE_GET_COMPONENT(transform, t1, e1);
				vec3 v0 = entMov(e0);
				vec3 v1 = entMov(e1);
#ifdef CAGE_DEBUG
				static const real particlesPerUnit = 0.1;
#else
				static const real particlesPerUnit = 2;
#endif // CAGE_DEBUG

				uint32 cnt = numeric_cast<uint32>(t0.position.distance(t1.position) * particlesPerUnit) + 3;
				for (uint32 i = 1; i < cnt - 1; i++)
				{
					real deviation = sin(rads::Stright * real(i) / cnt);
					real portion = (random() + i) / cnt;
					newParticle(
						interpolate(t0.position, t1.position, portion) + randomDirection3() * deviation * 1.5,
						interpolate(v0, v1, portion) + randomDirection3() * deviation * 5,
						colorDeviation(v.color, 0.1),
						0.05, 5);
				}
			}
		}

		return false;
	}

	bool engineInitialize()
	{
		springVisualComponent::component = entities()->defineComponent(springVisualComponent(), true);
		timeoutComponent::component = entities()->defineComponent(timeoutComponent(), true);
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
