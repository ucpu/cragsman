#include <vector>
#include <algorithm>

#include "common.h"

#include <cage-core/log.h>
#include <cage-core/entities.h>
#include <cage-core/geometry.h>
#include <cage-core/utility/hashString.h>
#include <cage-core/utility/variableSmoothingBuffer.h>

#include <cage-client/core.h>
#include <cage-client/engine.h>
#include <cage-client/window.h>
#include <cage-client/gui.h>

uint32 cameraName;
uint32 characterBody;

namespace
{
	windowEventListeners windowListeners;
	eventListener<bool()> engineInitListener;
	eventListener<bool()> engineUpdateListener;

	uint32 lightName;
	uint32 cursorName;

	const uint32 characterHandsCount = 3;
	uint32 characterHands[characterHandsCount];
	uint32 characterElbows[characterHandsCount];
	uint32 characterShoulders[characterHandsCount];
	uint32 characterHandJoints[characterHandsCount];
	uint32 currentHand;

	variableSmoothingBufferStruct<vec3> smoothBodyPosition;

	entityClass *addSpring(uint32 a, uint32 b, real restDistance, real stiffness, real damping)
	{
		entityClass *spring = entities()->newUniqueEntity();
		GAME_GET_COMPONENT(spring, s, spring);
		s.objects[0] = a;
		s.objects[1] = b;
		s.restDistance = restDistance;
		s.stiffness = stiffness;
		s.damping = damping;
		std::sort(s.objects, s.objects + 1);
		return spring;
	}

	vec3 colorIndex(uint32 i)
	{
		switch (i)
		{
		case 0: return vec3(1, 1, 0);
		case 1: return vec3(0, 1, 1);
		case 2: return vec3(1, 0, 1);
		default: CAGE_THROW_CRITICAL(exception, "invalid color index");
		}
	}

	void jointHandClinch(uint32 handIndex, uint32 clinchName)
	{
		entityClass *e = addSpring(characterHands[handIndex], clinchName, 0, 0.3, 0.3);
		characterHandJoints[handIndex] = e->getName();
	}

	void removeSprings(uint32 n)
	{
		auto sns = springComponent::component->getComponentEntities()->entities();
		std::vector<entityClass *> ses(sns.begin(), sns.end());
		for (entityClass *e : ses)
		{
			GAME_GET_COMPONENT(spring, s, e);
			if (s.objects[0] == n || s.objects[1] == n)
				e->destroy();
		}
	}

	vec3 screenToWorld(const pointStruct &point)
	{
		pointStruct res = window()->resolution();
		vec2 p = vec2(point.x, point.y);
		p /= vec2(res.x, res.y);
		p = p * 2 - 1;
		real px = p[0], py = -p[1];
		ENGINE_GET_COMPONENT(transform, ts, entities()->getEntity(cameraName));
		ENGINE_GET_COMPONENT(camera, cs, entities()->getEntity(cameraName));
		mat4 view = mat4(ts.inverse());
		mat4 proj = perspectiveProjection(cs.perspectiveFov, real(res.x) / real(res.y), cs.near, cs.far);
		mat4 inv = (proj * view).inverse();
		vec4 pn = inv * vec4(px, py, -1, 1);
		vec4 pf = inv * vec4(px, py, 1, 1);
		vec3 near = vec3(pn) / pn[3];
		vec3 far = vec3(pf) / pf[3];
		return terrainIntersection(makeSegment(near, far));
	}

	void initializeCharacter()
	{
		{ // camera
			entityClass *cam;
			cameraName = (cam = entities()->newUniqueEntity())->getName();
			ENGINE_GET_COMPONENT(transform, t, cam);
			ENGINE_GET_COMPONENT(camera, c, cam);
			c.ambientLight = vec3(1, 1, 1) * 0.25;
			c.near = 10;
			c.far = 250;
		}

		{ // light
			entityClass *lig;
			lightName = (lig = entities()->newUniqueEntity())->getName();
			ENGINE_GET_COMPONENT(transform, t, lig);
			ENGINE_GET_COMPONENT(light, l, lig);
			ENGINE_GET_COMPONENT(shadowmap, s, lig);
			l.lightType = lightTypeEnum::Directional;
			t.orientation = quat(degs(-50), degs(60), degs());
			s.resolution = 4096;
			s.worldRadius = vec3(150, 150, 200);
		}

		{ // cursor
			entityClass *cur;
			cursorName = (cur = entities()->newUniqueEntity())->getName();
			ENGINE_GET_COMPONENT(transform, t, cur);
		}

		{ // body
			entityClass *body;
			characterBody = (body = entities()->newUniqueEntity())->getName();
			ENGINE_GET_COMPONENT(transform, t, body);
			ENGINE_GET_COMPONENT(render, r, body);
			r.object = hashString("cragsman/character/body.object");
			GAME_GET_COMPONENT(physics, p, body);
			p.collisionRadius = 3;
			p.mass = sphereVolume(p.collisionRadius);
		}

		{ // hands
			for (uint32 i = 0; i < characterHandsCount; i++)
			{
				entityClass *hand = nullptr, *elbow = nullptr, *shoulder = nullptr;
				characterShoulders[i] = (shoulder = entities()->newUniqueEntity())->getName();
				characterElbows[i] = (elbow = entities()->newUniqueEntity())->getName();
				characterHands[i] = (hand = entities()->newUniqueEntity())->getName();
				{ // shoulder
					ENGINE_GET_COMPONENT(transform, t, shoulder);
					ENGINE_GET_COMPONENT(render, r, shoulder);
					r.object = hashString("cragsman/character/shoulder.object");
					GAME_GET_COMPONENT(physics, p, shoulder);
					p.collisionRadius = 2.3067 / 2;
					p.mass = sphereVolume(p.collisionRadius);
				}
				{ // elbow
					ENGINE_GET_COMPONENT(transform, t, elbow);
					ENGINE_GET_COMPONENT(render, r, elbow);
					r.object = hashString("cragsman/character/elbow.object");
					GAME_GET_COMPONENT(physics, p, elbow);
					p.collisionRadius = 2.56723 / 2;
					p.mass = sphereVolume(p.collisionRadius);
				}
				{ // hand
					ENGINE_GET_COMPONENT(transform, t, hand);
					rads angle = real(i) / characterHandsCount * rads::Full;
					vec2 pos = vec2(cos(angle), sin(angle)) * 20;
					t.position = vec3(pos, terrainOffset(pos));
					ENGINE_GET_COMPONENT(render, r, hand);
					r.object = hashString("cragsman/character/hand.object");
					GAME_GET_COMPONENT(physics, p, hand);
					p.collisionRadius = 1.1;
					p.mass = sphereVolume(p.collisionRadius);
					if (i == 0)
						jointHandClinch(i, cursorName);
					else
					{
						entityClass *c = findClinch(t.position, 100);
						jointHandClinch(i, c->getName());
					}
				}
				addSpring(characterBody, characterShoulders[i], 4, 0.05, 0.1);
				{
					entityClass *e = addSpring(characterShoulders[i], characterElbows[i], 7, 0.05, 0.1);
					GAME_GET_COMPONENT(springVisual, sv, e);
					sv.color = colorDeviation(colorIndex(i), 0.1);
				}
				{
					entityClass *e = addSpring(characterElbows[i], characterHands[i], 10, 0.05, 0.1);
					GAME_GET_COMPONENT(springVisual, sv, e);
					sv.color = colorDeviation(colorIndex(i), 0.1);
				}
			}
		}
		currentHand = 0;
	}

	bool mousePress(mouseButtonsFlags b, modifiersFlags m, const pointStruct &p)
	{
		if (b == mouseButtonsFlags::Left && m == modifiersFlags::None)
		{
			ENGINE_GET_COMPONENT(transform, ht, entities()->getEntity(characterHands[currentHand]));
			entityClass *clinch = findClinch(ht.position, 3);
			if (clinch)
			{
				uint32 clinchName = clinch->getName();
				for (uint32 i = 0; i < characterHandsCount; i++)
				{
					GAME_GET_COMPONENT(spring, s, entities()->getEntity(characterHandJoints[i]));
					if (s.objects[1] == clinchName)
						return true; // do not allow multiple hands on single clinch
				}
				{ // attach current hand to the clinch
					GAME_GET_COMPONENT(spring, s, entities()->getEntity(characterHandJoints[currentHand]));
					s.objects[1] = clinchName;
				}
				currentHand = (currentHand + 1) % characterHandsCount;
				{ // free another hand
					GAME_GET_COMPONENT(spring, s, entities()->getEntity(characterHandJoints[currentHand]));
					s.objects[1] = cursorName;
				}
			}
			return true;
		}
		return false;
	}

	void restartGame()
	{
		CAGE_LOG(severityEnum::Info, "controls", "restarting the game");

		entities()->getAllEntities()->destroyAllEntities();
		initializeClinches();
		initializeCharacter();
	}

	bool engineInitialize()
	{
		windowListeners.attachAll(window());
		windowListeners.mousePress.bind<&mousePress>();
		return false;
	}

	bool engineUpdate()
	{
		{ // cursor
			ENGINE_GET_COMPONENT(transform, bt, entities()->getEntity(characterBody));
			vec3 target = screenToWorld(window()->mousePosition());
			if (target.valid())
			{
				static const real maxBodyCursorDistance = 30;
				if (distance(bt.position, target) > maxBodyCursorDistance)
					target = (target - bt.position).normalize() * maxBodyCursorDistance + bt.position;
				ENGINE_GET_COMPONENT(transform, ct, entities()->getEntity(cursorName));
				target[2] = terrainOffset(vec2(target)) + CLINCH_TERRAIN_OFFSET;
				ct.position = target;
			}
		}

		{ // camera
			ENGINE_GET_COMPONENT(transform, bt, entities()->getEntity(characterBody));
			ENGINE_GET_COMPONENT(transform, ct, entities()->getEntity(cameraName));
			smoothBodyPosition.add(bt.position);
			vec3 sbp = smoothBodyPosition.smooth();
			ct.position = sbp + vec3(0, 0, 100);
			quat rot = quat(sbp - ct.position, vec3(0, 1, 0));
			ct.orientation = interpolate(ct.orientation, rot, 0.1);
		}

		{ // light
			ENGINE_GET_COMPONENT(transform, bt, entities()->getEntity(characterBody));
			ENGINE_GET_COMPONENT(transform, lt, entities()->getEntity(lightName));
			lt.position = bt.position;
		}

		{ // hands orientations
			for (uint32 i = 0; i < characterHandsCount; i++)
			{
				ENGINE_GET_COMPONENT(transform, ht, entities()->getEntity(characterHands[i]));
				ENGINE_GET_COMPONENT(transform, et, entities()->getEntity(characterElbows[i]));
				ht.orientation = quat(ht.position - et.position, vec3(0, 0, 1), false);
			}
		}
		return false;
	}

	bool firstUpdate()
	{
		restartGame();
		engineUpdateListener.bind<&engineUpdate>();
		return engineUpdate();
	}

	class callbacksInitClass
	{
	public:
		callbacksInitClass()
		{
			engineUpdateListener.attach(controlThread().update);
			engineUpdateListener.bind<&firstUpdate>();
			engineInitListener.attach(controlThread().initialize);
			engineInitListener.bind<&engineInitialize>();
		}
	} callbacksInitInstance;
}
