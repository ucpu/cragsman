#include <vector>
#include <unordered_map>
#include <algorithm>

#include "common.h"

#include <cage-core/log.h>
#include <cage-core/entities.h>

#include <cage-client/core.h>
#include <cage-client/engine.h>

componentClass *physicsComponent::component;
componentClass *springComponent::component;

springComponent::springComponent() : objects{0, 0}
{}

namespace
{
	class physicsSimulationClass
	{
	public:
		static const uint32 repeatSteps = 2; // increasing steps increases simulation precision
		const real deltaTime;

		std::vector<entityClass*> entsToDestroy;
		std::unordered_map<entityClass*, vec3> acceleration;

		physicsSimulationClass() : deltaTime(controlThread().timePerTick * 1e-6f / repeatSteps)
		{}

		static vec3 entPos(entityClass *e)
		{
			ENGINE_GET_COMPONENT(transform, t, e);
			return t.position;
		}

		static real entMass(entityClass *e)
		{
			if (e->hasComponent(physicsComponent::component))
			{
				GAME_GET_COMPONENT(physics, p, e);
				CAGE_ASSERT_RUNTIME(p.mass > 1e-7, p.mass);
				return p.mass;
			}
			return real::PositiveInfinity;
		}

		static vec3 entVel(entityClass *e)
		{
			if (e->hasComponent(physicsComponent::component))
			{
				GAME_GET_COMPONENT(physics, p, e);
				return p.velocity;
			}
			return {};
		}

		void addForce(entityClass *e, const vec3 &f)
		{
			CAGE_ASSERT_RUNTIME(f.valid(), f);
			acceleration[e] += f / entMass(e);
		}

		void springs()
		{
			real timeStep2 = deltaTime * deltaTime;
			for (entityClass *e : springComponent::component->getComponentEntities()->entities())
			{
				GAME_GET_COMPONENT(spring, s, e);
				CAGE_ASSERT_RUNTIME(s.restDistance >= 0, s.restDistance);
				CAGE_ASSERT_RUNTIME(s.stiffness > 0 && s.stiffness < 1, s.stiffness);
				CAGE_ASSERT_RUNTIME(s.damping > 0 && s.damping < 1, s.damping);
				entityClass *e1 = entities()->getEntity(s.objects[0]);
				entityClass *e2 = entities()->getEntity(s.objects[1]);
				vec3 p1 = entPos(e1);
				vec3 p2 = entPos(e2);
				real m1 = entMass(e1);
				real m2 = entMass(e2);
				CAGE_ASSERT_RUNTIME(m1.finite() || m2.finite());
				real m = (m1.finite() && m2.finite()) ? (m1 * m2) / (m1 + m2) : m1.finite() ? m1 : m2;
				CAGE_ASSERT_RUNTIME(m.valid() && m.finite());
				vec3 x = p2 - p1;
				if (x.squaredLength() > 1e-5)
					x -= x.normalize() * s.restDistance;
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
			for (entityClass *e : physicsComponent::component->getComponentEntities()->entities())
			{
				GAME_GET_COMPONENT(physics, p, e);
				addForce(e, p.mass * g);
			}
		}

		void collisions()
		{
			// todo
		}

		void applyAccelerations()
		{
			for (entityClass *e : physicsComponent::component->getComponentEntities()->entities())
			{
				CAGE_ASSERT_RUNTIME(acceleration[e].valid(), acceleration[e]);
				GAME_GET_COMPONENT(physics, p, e);
				CAGE_ASSERT_RUNTIME(p.velocity.valid(), p.velocity);
				ENGINE_GET_COMPONENT(transform, t, e);
				CAGE_ASSERT_RUNTIME(t.position.valid(), t.position);
				p.velocity += acceleration[e] * deltaTime;
				t.position += p.velocity * deltaTime;
			}
		}

		void destroyEntities()
		{
			std::sort(entsToDestroy.begin(), entsToDestroy.end());
			auto e = std::unique(entsToDestroy.begin(), entsToDestroy.end());
			entsToDestroy.erase(e, entsToDestroy.end());
			for (entityClass *e : entsToDestroy)
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
		physicsSimulationClass simulation;
		simulation.run();
		return false;
	}

	bool engineInitialize()
	{
		physicsComponent::component = entities()->defineComponent(physicsComponent(), true);
		springComponent::component = entities()->defineComponent(springComponent(), true);
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
