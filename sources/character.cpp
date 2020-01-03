#include "common.h"

#include <cage-core/entities.h>
#include <cage-core/geometry.h>
#include <cage-core/hashString.h>
#include <cage-core/variableSmoothingBuffer.h>
#include <cage-core/camera.h>

#include <cage-engine/core.h>
#include <cage-engine/engine.h>
#include <cage-engine/window.h>
#include <cage-engine/gui.h>

#include <vector>
#include <algorithm>

uint32 cameraName;
uint32 characterBody;
vec3 playerPosition;

namespace
{
	WindowEventListeners windowListeners;

	uint32 lightName;
	uint32 cursorName;

	const uint32 characterHandsCount = 3;
	uint32 characterHands[characterHandsCount];
	uint32 characterElbows[characterHandsCount];
	uint32 characterShoulders[characterHandsCount];
	uint32 characterHandJoints[characterHandsCount];
	uint32 currentHand;

	VariableSmoothingBuffer<vec3> smoothBodyPosition;

	Entity *addSpring(uint32 a, uint32 b, real restDistance, real stiffness, real damping)
	{
		Entity *spring = engineEntities()->createUnique();
		GAME_COMPONENT(Spring, s, spring);
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
		default: CAGE_THROW_CRITICAL(Exception, "invalid color index");
		}
	}

	void jointHandClinch(uint32 handIndex, uint32 clinchName)
	{
		Entity *e = addSpring(characterHands[handIndex], clinchName, 0, 0.3, 0.3);
		characterHandJoints[handIndex] = e->name();
	}

	void removeSprings(uint32 n)
	{
		auto sns = SpringComponent::component->entities();
		std::vector<Entity *> ses(sns.begin(), sns.end());
		for (Entity *e : ses)
		{
			GAME_COMPONENT(Spring, s, e);
			if (s.objects[0] == n || s.objects[1] == n)
				e->destroy();
		}
	}

	vec3 screenToWorld(const ivec2 &point)
	{
		ivec2 res = engineWindow()->resolution();
		vec2 p = vec2(point.x, point.y);
		p /= vec2(res.x, res.y);
		p = p * 2 - 1;
		real px = p[0], py = -p[1];
		CAGE_COMPONENT_ENGINE(Transform, ts, engineEntities()->get(cameraName));
		CAGE_COMPONENT_ENGINE(Camera, cs, engineEntities()->get(cameraName));
		mat4 view = mat4(inverse(ts));
		mat4 proj = perspectiveProjection(cs.camera.perspectiveFov, real(res.x) / real(res.y), cs.near, cs.far);
		mat4 inv = inverse(proj * view);
		vec4 pn = inv * vec4(px, py, -1, 1);
		vec4 pf = inv * vec4(px, py, 1, 1);
		vec3 near = vec3(pn) / pn[3];
		vec3 far = vec3(pf) / pf[3];
		return terrainIntersection(makeSegment(near, far));
	}

	bool mousePress(MouseButtonsFlags b, ModifiersFlags m, const ivec2 &p)
	{
		if (b == MouseButtonsFlags::Left && m == ModifiersFlags::None)
		{
			CAGE_COMPONENT_ENGINE(Transform, ht, engineEntities()->get(characterHands[currentHand]));
			Entity *clinch = findClinch(ht.position, 3);
			if (clinch)
			{
				uint32 clinchName = clinch->name();
				for (uint32 i = 0; i < characterHandsCount; i++)
				{
					GAME_COMPONENT(Spring, s, engineEntities()->get(characterHandJoints[i]));
					if (s.objects[1] == clinchName)
						return true; // do not allow multiple hands on single clinch
				}
				{ // attach current hand to the clinch
					GAME_COMPONENT(Spring, s, engineEntities()->get(characterHandJoints[currentHand]));
					s.objects[1] = clinchName;
				}
				currentHand = (currentHand + 1) % characterHandsCount;
				{ // free another hand
					GAME_COMPONENT(Spring, s, engineEntities()->get(characterHandJoints[currentHand]));
					s.objects[1] = cursorName;
				}
			}
			return true;
		}
		return false;
	}

	bool initializeTheGame()
	{
		std::vector<Entity*> clinches;
		{
			clinches.resize(characterHandsCount);
			uint32 cnt = characterHandsCount;
			findInitialClinches(cnt, clinches.data());
			if (cnt != characterHandsCount)
				return false;
		}

		{ // camera
			Entity *cam;
			cameraName = (cam = engineEntities()->createUnique())->name();
			CAGE_COMPONENT_ENGINE(Transform, t, cam);
			CAGE_COMPONENT_ENGINE(Camera, c, cam);
			c.ambientLight = vec3(0.03);
			c.near = 10;
			c.far = 500;
			c.effects = CameraEffectsFlags::CombinedPass;
			//c.effects = CameraEffectsFlags::GammaCorrection;
		}

		{ // light
			Entity *lig;
			lightName = (lig = engineEntities()->createUnique())->name();
			CAGE_COMPONENT_ENGINE(Transform, t, lig);
			CAGE_COMPONENT_ENGINE(Light, l, lig);
			CAGE_COMPONENT_ENGINE(Shadowmap, s, lig);
			l.lightType = LightTypeEnum::Directional;
			l.color = vec3(3);
			s.resolution = 4096;
			s.worldSize = vec3(150, 150, 200);
		}

		{ // cursor
			Entity *cur;
			cursorName = (cur = engineEntities()->createUnique())->name();
			CAGE_COMPONENT_ENGINE(Transform, t, cur);
		}

		{ // body
			Entity *body;
			characterBody = (body = engineEntities()->createUnique())->name();
			CAGE_COMPONENT_ENGINE(Transform, t, body);
			CAGE_COMPONENT_ENGINE(Render, r, body);
			r.object = HashString("cragsman/character/body.object");
			GAME_COMPONENT(Physics, p, body);
			p.collisionRadius = 3;
			p.mass = sphereVolume(p.collisionRadius);
		}

		{ // hands
			for (uint32 i = 0; i < characterHandsCount; i++)
			{
				Entity *hand = nullptr, *elbow = nullptr, *shoulder = nullptr;
				characterShoulders[i] = (shoulder = engineEntities()->createUnique())->name();
				characterElbows[i] = (elbow = engineEntities()->createUnique())->name();
				characterHands[i] = (hand = engineEntities()->createUnique())->name();
				{ // shoulder
					CAGE_COMPONENT_ENGINE(Transform, t, shoulder);
					CAGE_COMPONENT_ENGINE(Render, r, shoulder);
					r.object = HashString("cragsman/character/shoulder.object");
					GAME_COMPONENT(Physics, p, shoulder);
					p.collisionRadius = 2.3067 / 2;
					p.mass = sphereVolume(p.collisionRadius);
				}
				{ // elbow
					CAGE_COMPONENT_ENGINE(Transform, t, elbow);
					CAGE_COMPONENT_ENGINE(Render, r, elbow);
					r.object = HashString("cragsman/character/elbow.object");
					GAME_COMPONENT(Physics, p, elbow);
					p.collisionRadius = 2.56723 / 2;
					p.mass = sphereVolume(p.collisionRadius);
				}
				{ // hand
					CAGE_COMPONENT_ENGINE(Transform, t, hand);
					rads angle = real(i) / characterHandsCount * rads::Full();
					vec2 pos = vec2(cos(angle), sin(angle)) * 20;
					t.position = vec3(pos, terrainOffset(pos));
					CAGE_COMPONENT_ENGINE(Render, r, hand);
					r.object = HashString("cragsman/character/hand.object");
					GAME_COMPONENT(Physics, p, hand);
					p.collisionRadius = 1.1;
					p.mass = sphereVolume(p.collisionRadius);
					if (i == 0)
						jointHandClinch(i, cursorName);
					else
						jointHandClinch(i, clinches[i]->name());
				}
				addSpring(characterBody, characterShoulders[i], 4, 0.05, 0.1);
				{
					Entity *e = addSpring(characterShoulders[i], characterElbows[i], 7, 0.05, 0.1);
					GAME_COMPONENT(SpringVisual, sv, e);
					sv.color = colorDeviation(colorIndex(i), 0.1);
				}
				{
					Entity *e = addSpring(characterElbows[i], characterHands[i], 10, 0.05, 0.1);
					GAME_COMPONENT(SpringVisual, sv, e);
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
			if (characterBody && engineEntities()->has(characterBody))
			{
				CAGE_COMPONENT_ENGINE(Transform, t, engineEntities()->get(characterBody));
				playerPosition = t.position;
			}
			else
				playerPosition = vec3::Nan();
		}

		{ // cursor
			CAGE_COMPONENT_ENGINE(Transform, bt, engineEntities()->get(characterBody));
			vec3 target = screenToWorld(engineWindow()->mousePosition());
			if (target.valid())
			{
				static const real maxBodyCursorDistance = 30;
				if (distance(bt.position, target) > maxBodyCursorDistance)
					target = normalize(target - bt.position) * maxBodyCursorDistance + bt.position;
				CAGE_COMPONENT_ENGINE(Transform, ct, engineEntities()->get(cursorName));
				target[2] = terrainOffset(vec2(target)) + CLINCH_TERRAIN_OFFSET;
				ct.position = target;
			}
		}

		{ // camera
			CAGE_COMPONENT_ENGINE(Transform, bt, engineEntities()->get(characterBody));
			CAGE_COMPONENT_ENGINE(Transform, ct, engineEntities()->get(cameraName));
			smoothBodyPosition.add(bt.position);
			vec3 sbp = smoothBodyPosition.smooth();
			ct.position = sbp + vec3(0, 0, 100);
			quat rot = quat(sbp - ct.position, vec3(0, 1, 0));
			ct.orientation = interpolate(ct.orientation, rot, 0.1);
		}

		{ // light
			CAGE_COMPONENT_ENGINE(Transform, bt, engineEntities()->get(characterBody));
			CAGE_COMPONENT_ENGINE(Transform, lt, engineEntities()->get(lightName));
			lt.position = bt.position;
			lt.orientation = sunLightOrientation(vec2(playerPosition));
		}

		{ // hands orientations
			for (uint32 i = 0; i < characterHandsCount; i++)
			{
				CAGE_COMPONENT_ENGINE(Transform, ht, engineEntities()->get(characterHands[i]));
				CAGE_COMPONENT_ENGINE(Transform, et, engineEntities()->get(characterElbows[i]));
				ht.orientation = quat(ht.position - et.position, vec3(0, 0, 1), false);
			}
		}

		return false;
	}

	bool engineInitialize()
	{
		windowListeners.attachAll(engineWindow());
		windowListeners.mousePress.bind<&mousePress>();
		return false;
	}

	class Callbacks
	{
		EventListener<bool()> engineInitListener;
		EventListener<bool()> engineUpdateListener;
	public:
		Callbacks()
		{
			engineUpdateListener.attach(controlThread().update);
			engineUpdateListener.bind<&engineUpdate>();
			engineInitListener.attach(controlThread().initialize);
			engineInitListener.bind<&engineInitialize>();
		}
	} callbacksInstance;
}
