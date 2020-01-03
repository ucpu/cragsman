#include "common.h"

#include <cage-core/entities.h>
#include <cage-core/hashString.h>
#include <cage-core/color.h>

#include <cage-engine/core.h>
#include <cage-engine/engine.h>

#include <vector>
#include <unordered_map>
#include <algorithm>

EntityComponent *SpringVisualComponent::component;
EntityComponent *TimeoutComponent::component;

SpringVisualComponent::SpringVisualComponent() : color(1, 1, 1)
{}

TimeoutComponent::TimeoutComponent() : ttl(0)
{}

Entity *newParticle(const vec3 &position, const vec3 &velocity, const vec3 &color, real mass, uint32 ttl)
{
	Entity *e = engineEntities()->createAnonymous();
	CAGE_COMPONENT_ENGINE(Transform, t, e);
	CAGE_COMPONENT_ENGINE(Render, r, e);
	GAME_COMPONENT(Physics, p, e);
	GAME_COMPONENT(Timeout, to, e);
	t.position = position;
	t.orientation = randomDirectionQuat();
	r.color = color;
	r.object = HashString("cragsman/particle/particle.object");
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
	vec3 entMov(Entity *e)
	{
		if (e->has(PhysicsComponent::component))
		{
			GAME_COMPONENT(Physics, p, e);
			return p.velocity / p.mass;
		}
		return {};
	}

	bool engineUpdate()
	{
		{ // timeout entities
			std::vector<Entity *> etd;
			for (Entity *e : TimeoutComponent::component->entities())
			{
				GAME_COMPONENT(Timeout, t, e);
				if (t.ttl-- == 0)
					etd.push_back(e);
			}
			for (Entity *e : etd)
				e->destroy();
		}

		{ // spring visuals
			for (Entity *e : SpringVisualComponent::component->entities())
			{
				CAGE_ASSERT(e->has(SpringComponent::component));
				GAME_COMPONENT(Spring, s, e);
				GAME_COMPONENT(SpringVisual, v, e);
				Entity *e0 = engineEntities()->get(s.objects[0]);
				Entity *e1 = engineEntities()->get(s.objects[1]);
				CAGE_COMPONENT_ENGINE(Transform, t0, e0);
				CAGE_COMPONENT_ENGINE(Transform, t1, e1);
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
					Entity *pe = newParticle(
						interpolate(t0.position, t1.position, portion) + randomDirection3() * deviation * 1.5,
						interpolate(v0, v1, portion) + randomDirection3() * deviation * 5,
						color, 0.05, 5);
					if (randomChance() < 0.2)
					{
						CAGE_COMPONENT_ENGINE(Light, pl, pe);
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
		SpringVisualComponent::component = engineEntities()->defineComponent(SpringVisualComponent(), true);
		TimeoutComponent::component = engineEntities()->defineComponent(TimeoutComponent(), true);
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
