#include <vector>
#include <unordered_map>
#include <algorithm>

#include "common.h"

#include <cage-core/entities.h>
#include <cage-core/hashString.h>
#include <cage-core/color.h>

#include <cage-engine/core.h>
#include <cage-engine/engine.h>

entityComponent *springVisualComponent::component;
entityComponent *timeoutComponent::component;

springVisualComponent::springVisualComponent() : color(1, 1, 1)
{}

timeoutComponent::timeoutComponent() : ttl(0)
{}

entity *newParticle(const vec3 &position, const vec3 &velocity, const vec3 &color, real mass, uint32 ttl)
{
	entity *e = entities()->createAnonymous();
	CAGE_COMPONENT_ENGINE(transform, t, e);
	CAGE_COMPONENT_ENGINE(render, r, e);
	GAME_GET_COMPONENT(physics, p, e);
	GAME_GET_COMPONENT(timeout, to, e);
	t.position = position;
	t.orientation = randomDirectionQuat();
	r.color = color;
	r.object = hashString("cragsman/particle/particle.object");
	p.mass = mass;
	p.velocity = velocity;
	p.collisionRadius = real::Nan();
	to.ttl = ttl;
	return e;
}

vec3 colorDeviation(const vec3 &color, real deviation)
{
	vec3 hsv = colorRgbToHsv(color) + (vec3(randomChance(), randomChance(), randomChance()) - 0.5) * deviation;
	hsv[0] = (hsv[0] + 1) % 1;
	return colorHsvToRgb(clamp(hsv, vec3(), vec3(1)));
}

namespace
{
	vec3 entMov(entity *e)
	{
		if (e->has(physicsComponent::component))
		{
			GAME_GET_COMPONENT(physics, p, e);
			return p.velocity / p.mass;
		}
		return {};
	}

	bool engineUpdate()
	{
		{ // timeout entities
			std::vector<entity *> etd;
			for (entity *e : timeoutComponent::component->entities())
			{
				GAME_GET_COMPONENT(timeout, t, e);
				if (t.ttl-- == 0)
					etd.push_back(e);
			}
			for (entity *e : etd)
				e->destroy();
		}

		{ // spring visuals
			for (entity *e : springVisualComponent::component->entities())
			{
				CAGE_ASSERT(e->has(springComponent::component));
				GAME_GET_COMPONENT(spring, s, e);
				GAME_GET_COMPONENT(springVisual, v, e);
				entity *e0 = entities()->get(s.objects[0]);
				entity *e1 = entities()->get(s.objects[1]);
				CAGE_COMPONENT_ENGINE(transform, t0, e0);
				CAGE_COMPONENT_ENGINE(transform, t1, e1);
				vec3 v0 = entMov(e0);
				vec3 v1 = entMov(e1);

#ifdef CAGE_DEBUG
				uint32 cnt = 3;
#else
				uint32 cnt = numeric_cast<uint32>(distance(t0.position, t1.position) * 1.5) + 3;
				cnt = min(cnt, 20u);
#endif // CAGE_DEBUG
				for (uint32 i = 1; i < cnt - 1; i++)
				{
					real deviation = sin(rads::Full() * 0.5 * real(i) / cnt);
					real portion = (randomChance() + i) / cnt;
					vec3 color = colorDeviation(v.color, 0.1);
					entity *pe = newParticle(
						interpolate(t0.position, t1.position, portion) + randomDirection3() * deviation * 1.5,
						interpolate(v0, v1, portion) + randomDirection3() * deviation * 5,
						color, 0.05, 5);
					if (randomChance() < 0.2)
					{
						CAGE_COMPONENT_ENGINE(light, pl, pe);
						pl.color = color * 1.5;
						pl.attenuation = vec3(0, 0, 0.15);
					}
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
