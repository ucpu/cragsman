#include "common.h"

#include <cage-core/entities.h>
#include <cage-core/hashString.h>
#include <cage-core/color.h>

#include <cage-engine/scene.h>
#include <cage-simple/engine.h>

#include <vector>
#include <unordered_map>
#include <algorithm>

SpringVisualComponent::SpringVisualComponent() : color(1, 1, 1)
{}

TimeoutComponent::TimeoutComponent() : ttl(0)
{}

Entity *newParticle(const Vec3 &position, const Vec3 &velocity, const Vec3 &color, Real mass, uint32 ttl)
{
	Entity *e = engineEntities()->createAnonymous();
	TransformComponent &t = e->value<TransformComponent>();
	RenderComponent &r = e->value<RenderComponent>();
	PhysicsComponent &p = e->value<PhysicsComponent>();
	TimeoutComponent &to = e->value<TimeoutComponent>();
	t.position = position;
	t.orientation = randomDirectionQuat();
	r.color = color;
	r.object = HashString("cragsman/particle/particle.object");
	p.mass = mass;
	p.velocity = velocity;
	p.collisionRadius = Real::Nan();
	to.ttl = ttl;
	return e;
}

Vec3 colorDeviation(const Vec3 &color, Real deviation)
{
	Vec3 hsv = colorRgbToHsv(color) + (Vec3(randomChance(), randomChance(), randomChance()) - 0.5) * deviation;
	hsv[0] = (hsv[0] + 1) % 1;
	return colorHsvToRgb(clamp(hsv, 0, 1));
}

namespace
{
	Vec3 entMov(Entity *e)
	{
		if (e->has<PhysicsComponent>())
		{
			PhysicsComponent &p = e->value<PhysicsComponent>();
			return p.velocity / p.mass;
		}
		return {};
	}

	const auto engineUpdateListener = controlThread().update.listen([]() {
		{ // timeout entities
			std::vector<Entity *> etd;
			for (Entity *e : engineEntities()->component<TimeoutComponent>()->entities())
			{
				TimeoutComponent &t = e->value<TimeoutComponent>();
				if (t.ttl-- == 0)
					etd.push_back(e);
			}
			for (Entity *e : etd)
				e->destroy();
		}

		{ // spring visuals
			for (Entity *e : engineEntities()->component<SpringVisualComponent>()->entities())
			{
				CAGE_ASSERT(e->has<SpringComponent>());
				SpringComponent &s = e->value<SpringComponent>();
				SpringVisualComponent &v = e->value<SpringVisualComponent>();
				Entity *e0 = engineEntities()->get(s.objects[0]);
				Entity *e1 = engineEntities()->get(s.objects[1]);
				TransformComponent &t0 = e0->value<TransformComponent>();
				TransformComponent &t1 = e1->value<TransformComponent>();
				Vec3 v0 = entMov(e0);
				Vec3 v1 = entMov(e1);

#ifdef CAGE_DEBUG
				uint32 cnt = 3;
#else
				uint32 cnt = numeric_cast<uint32>(distance(t0.position, t1.position) * 1.5) + 3;
				cnt = min(cnt, 20u);
#endif // CAGE_DEBUG
				for (uint32 i = 1; i < cnt - 1; i++)
				{
					Real deviation = sin(Rads::Full() * 0.5 * Real(i) / cnt);
					Real portion = (randomChance() + i) / cnt;
					Vec3 color = colorDeviation(v.color, 0.1);
					Entity *pe = newParticle(
						interpolate(t0.position, t1.position, portion) + randomDirection3() * deviation * 1.5,
						interpolate(v0, v1, portion) + randomDirection3() * deviation * 5,
						color, 0.05, 5);
					if (randomChance() < 0.2)
					{
						LightComponent &pl = pe->value<LightComponent>();
						pl.color = color;
						pl.intensity = 1.5;
						pl.attenuation = Vec3(0, 0, 0.15);
					}
				}
			}
		}
	});

	const auto engineInitListener = controlThread().initialize.listen([]() {
		engineEntities()->defineComponent(SpringVisualComponent());
		engineEntities()->defineComponent(TimeoutComponent());
	});
}
