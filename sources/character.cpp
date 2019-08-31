#include <vector>
#include <algorithm>

#include "common.h"

#include <cage-core/log.h>
#include <cage-core/entities.h>
#include <cage-core/geometry.h>
#include <cage-core/hashString.h>
#include <cage-core/variableSmoothingBuffer.h>
#include <cage-core/camera.h>

#include <cage-client/core.h>
#include <cage-client/engine.h>
#include <cage-client/window.h>
#include <cage-client/gui.h>

uint32 cameraName;
uint32 characterBody;
vec3 playerPosition;

namespace
{
	windowEventListeners windowListeners;

	uint32 lightName;
	uint32 cursorName;

	const uint32 characterHandsCount = 3;
	uint32 characterHands[characterHandsCount];
	uint32 characterElbows[characterHandsCount];
	uint32 characterShoulders[characterHandsCount];
	uint32 characterHandJoints[characterHandsCount];
	uint32 currentHand;

	variableSmoothingBuffer<vec3> smoothBodyPosition;

	entity *addSpring(uint32 a, uint32 b, real restDistance, real stiffness, real damping)
	{
		entity *spring = entities()->createUnique();
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
		entity *e = addSpring(characterHands[handIndex], clinchName, 0, 0.3, 0.3);
		characterHandJoints[handIndex] = e->name();
	}

	void removeSprings(uint32 n)
	{
		auto sns = springComponent::component->entities();
		std::vector<entity *> ses(sns.begin(), sns.end());
		for (entity *e : ses)
		{
			GAME_GET_COMPONENT(spring, s, e);
			if (s.objects[0] == n || s.objects[1] == n)
				e->destroy();
		}
	}

	vec3 screenToWorld(const ivec2 &point)
	{
		ivec2 res = window()->resolution();
		vec2 p = vec2(point.x, point.y);
		p /= vec2(res.x, res.y);
		p = p * 2 - 1;
		real px = p[0], py = -p[1];
		CAGE_COMPONENT_ENGINE(transform, ts, entities()->get(cameraName));
		CAGE_COMPONENT_ENGINE(camera, cs, entities()->get(cameraName));
		mat4 view = mat4(inverse(ts));
		mat4 proj = perspectiveProjection(cs.camera.perspectiveFov, real(res.x) / real(res.y), cs.near, cs.far);
		mat4 inv = inverse(proj * view);
		vec4 pn = inv * vec4(px, py, -1, 1);
		vec4 pf = inv * vec4(px, py, 1, 1);
		vec3 near = vec3(pn) / pn[3];
		vec3 far = vec3(pf) / pf[3];
		return terrainIntersection(makeSegment(near, far));
	}

	bool mousePress(mouseButtonsFlags b, modifiersFlags m, const ivec2 &p)
	{
		if (b == mouseButtonsFlags::Left && m == modifiersFlags::None)
		{
			CAGE_COMPONENT_ENGINE(transform, ht, entities()->get(characterHands[currentHand]));
			entity *clinch = findClinch(ht.position, 3);
			if (clinch)
			{
				uint32 clinchName = clinch->name();
				for (uint32 i = 0; i < characterHandsCount; i++)
				{
					GAME_GET_COMPONENT(spring, s, entities()->get(characterHandJoints[i]));
					if (s.objects[1] == clinchName)
						return true; // do not allow multiple hands on single clinch
				}
				{ // attach current hand to the clinch
					GAME_GET_COMPONENT(spring, s, entities()->get(characterHandJoints[currentHand]));
					s.objects[1] = clinchName;
				}
				currentHand = (currentHand + 1) % characterHandsCount;
				{ // free another hand
					GAME_GET_COMPONENT(spring, s, entities()->get(characterHandJoints[currentHand]));
					s.objects[1] = cursorName;
				}
			}
			return true;
		}
		return false;
	}

	bool initializeTheGame()
	{
		std::vector<entity*> clinches;
		{
			clinches.resize(characterHandsCount);
			uint32 cnt = characterHandsCount;
			findInitialClinches(cnt, clinches.data());
			if (cnt != characterHandsCount)
				return false;
		}

		{ // camera
			entity *cam;
			cameraName = (cam = entities()->createUnique())->name();
			CAGE_COMPONENT_ENGINE(transform, t, cam);
			CAGE_COMPONENT_ENGINE(camera, c, cam);
			c.ambientLight = vec3(0.03);
			c.near = 10;
			c.far = 500;
			c.effects = cameraEffectsFlags::CombinedPass;
			//c.effects = cameraEffectsFlags::GammaCorrection;
		}

		{ // light
			entity *lig;
			lightName = (lig = entities()->createUnique())->name();
			CAGE_COMPONENT_ENGINE(transform, t, lig);
			CAGE_COMPONENT_ENGINE(light, l, lig);
			CAGE_COMPONENT_ENGINE(shadowmap, s, lig);
			l.lightType = lightTypeEnum::Directional;
			l.color = vec3(3);
			s.resolution = 4096;
			s.worldSize = vec3(150, 150, 200);
		}

		{ // cursor
			entity *cur;
			cursorName = (cur = entities()->createUnique())->name();
			CAGE_COMPONENT_ENGINE(transform, t, cur);
		}

		{ // body
			entity *body;
			characterBody = (body = entities()->createUnique())->name();
			CAGE_COMPONENT_ENGINE(transform, t, body);
			CAGE_COMPONENT_ENGINE(render, r, body);
			r.object = hashString("cragsman/character/body.object");
			GAME_GET_COMPONENT(physics, p, body);
			p.collisionRadius = 3;
			p.mass = sphereVolume(p.collisionRadius);
		}

		{ // hands
			for (uint32 i = 0; i < characterHandsCount; i++)
			{
				entity *hand = nullptr, *elbow = nullptr, *shoulder = nullptr;
				characterShoulders[i] = (shoulder = entities()->createUnique())->name();
				characterElbows[i] = (elbow = entities()->createUnique())->name();
				characterHands[i] = (hand = entities()->createUnique())->name();
				{ // shoulder
					CAGE_COMPONENT_ENGINE(transform, t, shoulder);
					CAGE_COMPONENT_ENGINE(render, r, shoulder);
					r.object = hashString("cragsman/character/shoulder.object");
					GAME_GET_COMPONENT(physics, p, shoulder);
					p.collisionRadius = 2.3067 / 2;
					p.mass = sphereVolume(p.collisionRadius);
				}
				{ // elbow
					CAGE_COMPONENT_ENGINE(transform, t, elbow);
					CAGE_COMPONENT_ENGINE(render, r, elbow);
					r.object = hashString("cragsman/character/elbow.object");
					GAME_GET_COMPONENT(physics, p, elbow);
					p.collisionRadius = 2.56723 / 2;
					p.mass = sphereVolume(p.collisionRadius);
				}
				{ // hand
					CAGE_COMPONENT_ENGINE(transform, t, hand);
					rads angle = real(i) / characterHandsCount * rads::Full();
					vec2 pos = vec2(cos(angle), sin(angle)) * 20;
					t.position = vec3(pos, terrainOffset(pos));
					CAGE_COMPONENT_ENGINE(render, r, hand);
					r.object = hashString("cragsman/character/hand.object");
					GAME_GET_COMPONENT(physics, p, hand);
					p.collisionRadius = 1.1;
					p.mass = sphereVolume(p.collisionRadius);
					if (i == 0)
						jointHandClinch(i, cursorName);
					else
						jointHandClinch(i, clinches[i]->name());
				}
				addSpring(characterBody, characterShoulders[i], 4, 0.05, 0.1);
				{
					entity *e = addSpring(characterShoulders[i], characterElbows[i], 7, 0.05, 0.1);
					GAME_GET_COMPONENT(springVisual, sv, e);
					sv.color = colorDeviation(colorIndex(i), 0.1);
				}
				{
					entity *e = addSpring(characterElbows[i], characterHands[i], 10, 0.05, 0.1);
					GAME_GET_COMPONENT(springVisual, sv, e);
					sv.color = colorDeviation(colorIndex(i), 0.1);
				}
			}
			currentHand = 0;
		}

		return true;
	}

	bool engineUpdate()
	{
		if (!characterBody)
		{
			if (!initializeTheGame())
				return false;
		}

		{ // player position
			if (characterBody && entities()->has(characterBody))
			{
				CAGE_COMPONENT_ENGINE(transform, t, entities()->get(characterBody));
				playerPosition = t.position;
			}
			else
				playerPosition = vec3::Nan();
		}

		{ // cursor
			CAGE_COMPONENT_ENGINE(transform, bt, entities()->get(characterBody));
			vec3 target = screenToWorld(window()->mousePosition());
			if (target.valid())
			{
				static const real maxBodyCursorDistance = 30;
				if (distance(bt.position, target) > maxBodyCursorDistance)
					target = normalize(target - bt.position) * maxBodyCursorDistance + bt.position;
				CAGE_COMPONENT_ENGINE(transform, ct, entities()->get(cursorName));
				target[2] = terrainOffset(vec2(target)) + CLINCH_TERRAIN_OFFSET;
				ct.position = target;
			}
		}

		{ // camera
			CAGE_COMPONENT_ENGINE(transform, bt, entities()->get(characterBody));
			CAGE_COMPONENT_ENGINE(transform, ct, entities()->get(cameraName));
			smoothBodyPosition.add(bt.position);
			vec3 sbp = smoothBodyPosition.smooth();
			ct.position = sbp + vec3(0, 0, 100);
			quat rot = quat(sbp - ct.position, vec3(0, 1, 0));
			ct.orientation = interpolate(ct.orientation, rot, 0.1);
		}

		{ // light
			CAGE_COMPONENT_ENGINE(transform, bt, entities()->get(characterBody));
			CAGE_COMPONENT_ENGINE(transform, lt, entities()->get(lightName));
			lt.position = bt.position;
			lt.orientation = sunLightOrientation(vec2(playerPosition));
		}

		{ // hands orientations
			for (uint32 i = 0; i < characterHandsCount; i++)
			{
				CAGE_COMPONENT_ENGINE(transform, ht, entities()->get(characterHands[i]));
				CAGE_COMPONENT_ENGINE(transform, et, entities()->get(characterElbows[i]));
				ht.orientation = quat(ht.position - et.position, vec3(0, 0, 1), false);
			}
		}

		return false;
	}

	bool engineInitialize()
	{
		windowListeners.attachAll(window());
		windowListeners.mousePress.bind<&mousePress>();
		return false;
	}

	class callbacksInitClass
	{
		eventListener<bool()> engineInitListener;
		eventListener<bool()> engineUpdateListener;
	public:
		callbacksInitClass()
		{
			engineUpdateListener.attach(controlThread().update);
			engineUpdateListener.bind<&engineUpdate>();
			engineInitListener.attach(controlThread().initialize);
			engineInitListener.bind<&engineInitialize>();
		}
	} callbacksInitInstance;
}
