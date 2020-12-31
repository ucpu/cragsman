#include "common.h"

#include <cage-core/geometry.h>
#include <cage-core/entities.h>
#include <cage-core/collisionStructure.h>
#include <cage-core/collider.h>

#include <cage-engine/core.h>
#include <cage-engine/engine.h>

#include <vector>
#include <unordered_map>
#include <algorithm>

EntityComponent *PhysicsComponent::component;
EntityComponent *SpringComponent::component;

SpringComponent::SpringComponent() : objects{0, 0}
{}

real sphereVolume(real radius)
{
	return 4 * real::Pi() * pow(radius, 3) / 3;
}

namespace
{
	Holder<CollisionStructure> collisionSearchData;
	Holder<CollisionQuery> collisionSearchQuery;

	class PhysicsSimulation
	{
	public:
		static const uint32 repeatSteps = 2; // increasing steps increases simulation precision
		const real deltaTime;

		std::vector<Entity*> entsToDestroy;
		std::unordered_map<Entity*, vec3> acceleration;

		PhysicsSimulation() : deltaTime(controlThread().updatePeriod() * 1e-6f / repeatSteps)
		{}

		static vec3 entPos(Entity *e)
		{
			CAGE_COMPONENT_ENGINE(Transform, t, e);
			return t.position;
		}

		static real entMass(Entity *e)
		{
			if (e->has(PhysicsComponent::component))
			{
				GAME_COMPONENT(Physics, p, e);
				CAGE_ASSERT(p.mass > 1e-7);
				return p.mass;
			}
			return real::Infinity();
		}

		static vec3 entVel(Entity *e)
		{
			if (e->has(PhysicsComponent::component))
			{
				GAME_COMPONENT(Physics, p, e);
				return p.velocity;
			}
			return {};
		}

		void addForce(Entity *e, const vec3 &f)
		{
			CAGE_ASSERT(f.valid());
			acceleration[e] += f / entMass(e);
		}

		void springs()
		{
			real timeStep2 = deltaTime * deltaTime;
			for (Entity *e : SpringComponent::component->entities())
			{
				GAME_COMPONENT(Spring, s, e);
				CAGE_ASSERT(s.restDistance >= 0);
				CAGE_ASSERT(s.stiffness > 0 && s.stiffness < 1);
				CAGE_ASSERT(s.damping > 0 && s.damping < 1);
				Entity *e1 = engineEntities()->get(s.objects[0]);
				Entity *e2 = engineEntities()->get(s.objects[1]);
				vec3 p1 = entPos(e1);
				vec3 p2 = entPos(e2);
				real m1 = entMass(e1);
				real m2 = entMass(e2);
				CAGE_ASSERT(m1.finite() || m2.finite());
				real m = (m1.finite() && m2.finite()) ? (m1 * m2) / (m1 + m2) : m1.finite() ? m1 : m2;
				CAGE_ASSERT(m.valid() && m.finite());
				vec3 x = p2 - p1;
				if (lengthSquared(x) > 1e-5)
					x -= normalize(x) * s.restDistance;
				else
					x -= randomDirection3() * s.restDistance;
				vec3 v = entVel(e2) - entVel(e1);
				vec3 f = x * (s.stiffness * m / timeStep2) + v * (s.damping * m / deltaTime);
				addForce(e1, f);
				addForce(e2, -f);
			}
		}

		void gravity()
		{
			vec3 g = vec3(0, -9.8, 0);
			for (Entity *e : PhysicsComponent::component->entities())
			{
				GAME_COMPONENT(Physics, p, e);
				addForce(e, p.mass * g);
			}
		}

		vec3 collisionResponse(const transform &t, const PhysicsComponent &p, const triangle &tr)
		{
			vec3 n = tr.normal();
			vec3 bounce = -2 * n * dot(n, p.velocity);

			vec3 tp = closestPoint(tr, t.position);
			real penetration = -(distance(t.position, tp) - p.collisionRadius);
			penetration = clamp(penetration, 0, 1);
			vec3 dir = normalize(t.position - tp);
			vec3 depenetration = dir * (pow(penetration + 1, 3) - 1);

			return (bounce * 0.9 + depenetration * 2) * p.mass;
		}

		void collisions()
		{
			for (Entity *e : PhysicsComponent::component->entities())
			{
				GAME_COMPONENT(Physics, p, e);
				if (!p.collisionRadius.valid())
					continue;
				CAGE_COMPONENT_ENGINE(Transform, t, e);
				if (collisionSearchQuery->query(sphere(t.position, p.collisionRadius)))
				{
					const Collider *c = nullptr;
					transform dummy;
					collisionSearchQuery->collider(c, dummy);
					CAGE_ASSERT(dummy == transform());
					for (auto cp : collisionSearchQuery->collisionPairs())
					{
						const triangle &tr = c->triangles()[cp.b];
						addForce(e, collisionResponse(t, p, tr));
					}
				}
				{ // ensure that the object is in front of the wall
					// it is intended to correct objects that has fallen behind the wall before the wall was generated
					// but it is not physical
					real to = terrainOffset(vec2(t.position));
					if (t.position[2] < to - p.collisionRadius * 0.5)
						t.position[2] = to + p.collisionRadius;
				}
			}
		}

		void applyAccelerations()
		{
			for (Entity *e : PhysicsComponent::component->entities())
			{
				CAGE_ASSERT(acceleration[e].valid());
				GAME_COMPONENT(Physics, p, e);
				CAGE_ASSERT(p.velocity.valid());
				CAGE_COMPONENT_ENGINE(Transform, t, e);
				CAGE_ASSERT(t.position.valid());
				p.velocity *= 0.995; // damping
				p.velocity += acceleration[e] * deltaTime;
				t.position += p.velocity * deltaTime;
			}
		}

		void destroyEntities()
		{
			std::sort(entsToDestroy.begin(), entsToDestroy.end());
			auto e = std::unique(entsToDestroy.begin(), entsToDestroy.end());
			entsToDestroy.erase(e, entsToDestroy.end());
			for (Entity *e : entsToDestroy)
				e->destroy();
		}

		void run()
		{
			for (uint32 step = 0; step < repeatSteps; step++)
			{
				springs();
				gravity();
				collisions();
				applyAccelerations();
				destroyEntities();
			}
		}
	};

	bool engineUpdate()
	{
		PhysicsSimulation simulation;
		simulation.run();
		return false;
	}

	bool engineInitialize()
	{
		PhysicsComponent::component = engineEntities()->defineComponent(PhysicsComponent(), true);
		SpringComponent::component = engineEntities()->defineComponent(SpringComponent(), true);
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
			{
				collisionSearchData = newCollisionStructure({});
				collisionSearchQuery = newCollisionQuery(collisionSearchData.get());
			}
		}
	} callbacksInstance;
}

void addTerrainCollider(uint32 name, Collider *c)
{
	collisionSearchData->update(name, c, transform());
	collisionSearchData->rebuild();
}

void removeTerrainCollider(uint32 name)
{
	collisionSearchData->remove(name);
	collisionSearchData->rebuild();
}

vec3 terrainIntersection(const line &ln)
{
	CAGE_ASSERT(ln.normalized());
	if (!collisionSearchQuery->query(ln))
	{
		// use old, less accurate method
		real dst = ln.a()[2] / dot(ln.direction, vec3(0, 0, -1));
		vec3 base = ln.a() + ln.direction * dst;
		CAGE_ASSERT(abs(base[2]) < 1e-5);
		return vec3(vec2(base), terrainOffset(vec2(base)));
	}
	const Collider *c = nullptr;
	transform dummy;
	collisionSearchQuery->collider(c, dummy);
	const triangle &t = c->triangles()[collisionSearchQuery->collisionPairs()[0].b];
	return intersection(ln, t);
}
