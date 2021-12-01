#include "common.h"

#include <cage-core/geometry.h>
#include <cage-core/entities.h>
#include <cage-core/collisionStructure.h>
#include <cage-core/collider.h>

#include <cage-engine/scene.h>
#include <cage-simple/engine.h>

#include <vector>
#include <unordered_map>
#include <algorithm>

SpringComponent::SpringComponent() : objects{0, 0}
{}

Real sphereVolume(Real radius)
{
	return 4 * Real::Pi() * pow(radius, 3) / 3;
}

namespace
{
	Holder<CollisionStructure> collisionSearchData;
	Holder<CollisionQuery> collisionSearchQuery;

	class PhysicsSimulation
	{
	public:
		static constexpr uint32 repeatSteps = 2; // increasing steps increases simulation precision
		const Real deltaTime;

		std::vector<Entity*> entsToDestroy;
		std::unordered_map<Entity*, Vec3> acceleration;

		PhysicsSimulation() : deltaTime(controlThread().updatePeriod() * 1e-6f / repeatSteps)
		{}

		static Vec3 entPos(Entity *e)
		{
			TransformComponent &t = e->value<TransformComponent>();
			return t.position;
		}

		static Real entMass(Entity *e)
		{
			if (e->has<PhysicsComponent>())
			{
				PhysicsComponent &p = e->value<PhysicsComponent>();
				CAGE_ASSERT(p.mass > 1e-7);
				return p.mass;
			}
			return Real::Infinity();
		}

		static Vec3 entVel(Entity *e)
		{
			if (e->has<PhysicsComponent>())
			{
				PhysicsComponent &p = e->value<PhysicsComponent>();
				return p.velocity;
			}
			return {};
		}

		void addForce(Entity *e, const Vec3 &f)
		{
			CAGE_ASSERT(f.valid());
			acceleration[e] += f / entMass(e);
		}

		void springs()
		{
			Real timeStep2 = deltaTime * deltaTime;
			for (Entity *e : engineEntities()->component<SpringComponent>()->entities())
			{
				SpringComponent &s = e->value<SpringComponent>();
				CAGE_ASSERT(s.restDistance >= 0);
				CAGE_ASSERT(s.stiffness > 0 && s.stiffness < 1);
				CAGE_ASSERT(s.damping > 0 && s.damping < 1);
				Entity *e1 = engineEntities()->get(s.objects[0]);
				Entity *e2 = engineEntities()->get(s.objects[1]);
				Vec3 p1 = entPos(e1);
				Vec3 p2 = entPos(e2);
				Real m1 = entMass(e1);
				Real m2 = entMass(e2);
				CAGE_ASSERT(m1.finite() || m2.finite());
				Real m = (m1.finite() && m2.finite()) ? (m1 * m2) / (m1 + m2) : m1.finite() ? m1 : m2;
				CAGE_ASSERT(m.valid() && m.finite());
				Vec3 x = p2 - p1;
				if (lengthSquared(x) > 1e-5)
					x -= normalize(x) * s.restDistance;
				else
					x -= randomDirection3() * s.restDistance;
				Vec3 v = entVel(e2) - entVel(e1);
				Vec3 f = x * (s.stiffness * m / timeStep2) + v * (s.damping * m / deltaTime);
				addForce(e1, f);
				addForce(e2, -f);
			}
		}

		void gravity()
		{
			Vec3 g = Vec3(0, -9.8, 0);
			for (Entity *e : engineEntities()->component<PhysicsComponent>()->entities())
			{
				PhysicsComponent &p = e->value<PhysicsComponent>();
				addForce(e, p.mass * g);
			}
		}

		Vec3 collisionResponse(const Transform &t, const PhysicsComponent &p, const Triangle &tr)
		{
			Vec3 n = tr.normal();
			Vec3 bounce = -2 * n * dot(n, p.velocity);

			Vec3 tp = closestPoint(tr, t.position);
			Real penetration = -(distance(t.position, tp) - p.collisionRadius);
			penetration = clamp(penetration, 0, 1);
			Vec3 dir = normalize(t.position - tp);
			Vec3 depenetration = dir * (pow(penetration + 1, 3) - 1);

			return (bounce * 0.9 + depenetration * 2) * p.mass;
		}

		void collisions()
		{
			for (Entity *e : engineEntities()->component<PhysicsComponent>()->entities())
			{
				PhysicsComponent &p = e->value<PhysicsComponent>();
				if (!p.collisionRadius.valid())
					continue;
				TransformComponent &t = e->value<TransformComponent>();
				if (collisionSearchQuery->query(Sphere(t.position, p.collisionRadius)))
				{
					Holder<const Collider> c;
					Transform dummy;
					collisionSearchQuery->collider(c, dummy);
					CAGE_ASSERT(dummy == Transform());
					for (auto cp : collisionSearchQuery->collisionPairs())
					{
						const Triangle &tr = c->triangles()[cp.b];
						addForce(e, collisionResponse(t, p, tr));
					}
				}
				{ // ensure that the object is in front of the wall
					// it is intended to correct objects that has fallen behind the wall before the wall was generated
					// but it is not physical
					Real to = terrainOffset(Vec2(t.position));
					if (t.position[2] < to - p.collisionRadius * 0.5)
						t.position[2] = to + p.collisionRadius;
				}
			}
		}

		void applyAccelerations()
		{
			for (Entity *e : engineEntities()->component<PhysicsComponent>()->entities())
			{
				CAGE_ASSERT(acceleration[e].valid());
				PhysicsComponent &p = e->value<PhysicsComponent>();
				CAGE_ASSERT(p.velocity.valid());
				TransformComponent &t = e->value<TransformComponent>();
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

	void engineUpdate()
	{
		PhysicsSimulation simulation;
		simulation.run();
	}

	void engineInitialize()
	{
		engineEntities()->defineComponent(PhysicsComponent());
		engineEntities()->defineComponent(SpringComponent());
	}

	class Callbacks
	{
		EventListener<void()> engineInitListener;
		EventListener<void()> engineUpdateListener;
	public:
		Callbacks()
		{
			engineInitListener.attach(controlThread().initialize);
			engineInitListener.bind<&engineInitialize>();
			engineUpdateListener.attach(controlThread().update);
			engineUpdateListener.bind<&engineUpdate>();
			{
				collisionSearchData = newCollisionStructure({});
				collisionSearchQuery = newCollisionQuery(collisionSearchData.share());
			}
		}
	} callbacksInstance;
}

void addTerrainCollider(uint32 name, Holder<Collider> c)
{
	collisionSearchData->update(name, std::move(c), Transform());
	collisionSearchData->rebuild();
}

void removeTerrainCollider(uint32 name)
{
	collisionSearchData->remove(name);
	collisionSearchData->rebuild();
}

Vec3 terrainIntersection(const Line &ln)
{
	CAGE_ASSERT(ln.normalized());
	if (!collisionSearchQuery->query(ln))
	{
		// use old, less accurate method
		Real dst = ln.a()[2] / dot(ln.direction, Vec3(0, 0, -1));
		Vec3 base = ln.a() + ln.direction * dst;
		CAGE_ASSERT(abs(base[2]) < 1e-5);
		return Vec3(Vec2(base), terrainOffset(Vec2(base)));
	}
	Holder<const Collider> c;
	Transform dummy;
	collisionSearchQuery->collider(c, dummy);
	const Triangle &t = c->triangles()[collisionSearchQuery->collisionPairs()[0].b];
	return intersection(ln, t);
}
