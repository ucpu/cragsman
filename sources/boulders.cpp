#include "common.h"

#include <cage-core/entities.h>
#include <cage-core/hashString.h>

#include <cage-engine/scene.h>
#include <cage-simple/engine.h>

#include <vector>

namespace
{
	struct BoulderComponent
	{};

	const auto engineUpdateListener = controlThread().update.listen([]() {
		if (!characterBody)
			return;
		TransformComponent &pt = engineEntities()->get(characterBody)->value<TransformComponent>();
		if (randomChance() < 0.01)
		{ // spawn a boulder
			Entity *e = engineEntities()->createAnonymous();
			TransformComponent &t = e->value<TransformComponent>();
			t.scale = randomChance() + 1.5;
			t.position = pt.position + Vec3(randomChance() * 300 - 150, 250, 0);
			t.position[2] = terrainOffset(Vec2(t.position)) + t.scale;
			t.orientation = randomDirectionQuat();
			RenderComponent &r = e->value<RenderComponent>();
			r.object = HashString("cragsman/boulder/boulder.object");
			{
				Real dummy;
				terrainMaterial(Vec2(t.position), r.color, dummy, dummy, true);
			}
			PhysicsComponent &p = e->value<PhysicsComponent>();
			p.collisionRadius = t.scale;
			p.mass = sphereVolume(p.collisionRadius) * 0.5;
			e->value<BoulderComponent>();
		}
		std::vector<Entity *> entsToDestroy;
		for (Entity *e : engineEntities()->component<BoulderComponent>()->entities())
		{ // rotate boulders
			TransformComponent &t = e->value<TransformComponent>();
			if (t.position[1] < pt.position[1] - 150)
				entsToDestroy.push_back(e);
			else
			{
				PhysicsComponent &p = e->value<PhysicsComponent>();
				Vec3 r = 1.5 * p.velocity / p.collisionRadius;
				Quat rot = Quat(Degs(r[2] - r[1]), Degs(), Degs(-r[0]));
				t.orientation = rot * t.orientation;
			}
		}
		for (auto e : entsToDestroy)
			e->destroy();
	});

	const auto engineInitListener = controlThread().initialize.listen([]() {
		engineEntities()->defineComponent(BoulderComponent());
	});
}
