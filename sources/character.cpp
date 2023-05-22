#include "common.h"

#include <cage-core/entities.h>
#include <cage-core/geometry.h>
#include <cage-core/hashString.h>
#include <cage-core/variableSmoothingBuffer.h>
#include <cage-core/camera.h>

#include <cage-engine/scene.h>
#include <cage-engine/window.h>
#include <cage-engine/inputs.h>
#include <cage-simple/engine.h>
#include <cage-simple/cameraRay.h>

#include <vector>
#include <algorithm>

uint32 cameraName;
uint32 characterBody;
Vec3 playerPosition;

namespace
{
	uint32 lightName;
	uint32 cursorName;

	const uint32 characterHandsCount = 3;
	uint32 characterHands[characterHandsCount];
	uint32 characterElbows[characterHandsCount];
	uint32 characterShoulders[characterHandsCount];
	uint32 characterHandJoints[characterHandsCount];
	uint32 currentHand;

	VariableSmoothingBuffer<Vec3> smoothBodyPosition;

	Entity *addSpring(uint32 a, uint32 b, Real restDistance, Real stiffness, Real damping)
	{
		Entity *spring = engineEntities()->createUnique();
		SpringComponent &s = spring->value<SpringComponent>();
		s.objects[0] = a;
		s.objects[1] = b;
		s.restDistance = restDistance;
		s.stiffness = stiffness;
		s.damping = damping;
		std::sort(s.objects, s.objects + 1);
		return spring;
	}

	Vec3 colorIndex(uint32 i)
	{
		switch (i)
		{
		case 0: return Vec3(1, 1, 0);
		case 1: return Vec3(0, 1, 1);
		case 2: return Vec3(1, 0, 1);
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
		auto sns = engineEntities()->component<SpringComponent>()->entities();
		std::vector<Entity *> ses(sns.begin(), sns.end());
		for (Entity *e : ses)
		{
			SpringComponent &s = e->value<SpringComponent>();
			if (s.objects[0] == n || s.objects[1] == n)
				e->destroy();
		}
	}

	Vec3 screenToWorld(Vec2 p)
	{
		return terrainIntersection(cameraRay(engineEntities()->get(cameraName), p));
	}

	bool mousePress(InputMouse in)
	{
		if (in.buttons == MouseButtonsFlags::Left && in.mods == ModifiersFlags::None)
		{
			TransformComponent &ht = engineEntities()->get(characterHands[currentHand])->value<TransformComponent>();
			Entity *clinch = findClinch(ht.position, 3);
			if (clinch)
			{
				uint32 clinchName = clinch->name();
				for (uint32 i = 0; i < characterHandsCount; i++)
				{
					SpringComponent &s = engineEntities()->get(characterHandJoints[i])->value<SpringComponent>();
					if (s.objects[1] == clinchName)
						return true; // do not allow multiple hands on single clinch
				}
				{ // attach current hand to the clinch
					SpringComponent &s = engineEntities()->get(characterHandJoints[currentHand])->value<SpringComponent>();
					s.objects[1] = clinchName;
				}
				currentHand = (currentHand + 1) % characterHandsCount;
				{ // free another hand
					SpringComponent &s = engineEntities()->get(characterHandJoints[currentHand])->value<SpringComponent>();
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
			Entity *cam = engineEntities()->createUnique();
			cameraName = cam->name();
			cam->value<TransformComponent>();
			CameraComponent &c = cam->value<CameraComponent>();
			c.ambientColor = Vec3(1);
			c.ambientIntensity = 0.03;
			c.near = 10;
			c.far = 500;
		}

		{ // light
			Entity *lig = engineEntities()->createUnique();
			lightName = lig->name();
			lig->value<TransformComponent>();
			LightComponent &l = lig->value<LightComponent>();
			l.lightType = LightTypeEnum::Directional;
			l.color = Vec3(1);
			l.intensity = 3;
			ShadowmapComponent &s = lig->value<ShadowmapComponent>();
			s.resolution = 4096;
			s.worldSize = Vec3(150, 150, 200);
		}

		{ // cursor
			Entity *cur = engineEntities()->createUnique();
			cursorName = cur->name();
			cur->value<TransformComponent>();
		}

		{ // body
			Entity *body = engineEntities()->createUnique();
			characterBody = body->name();
			body->value<TransformComponent>();
			body->value<RenderComponent>().object = HashString("cragsman/character/body.object");
			PhysicsComponent &p = body->value<PhysicsComponent>();
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
					shoulder->value<TransformComponent>();
					shoulder->value<RenderComponent>().object = HashString("cragsman/character/shoulder.object");
					PhysicsComponent &p = shoulder->value<PhysicsComponent>();
					p.collisionRadius = 2.3067 / 2;
					p.mass = sphereVolume(p.collisionRadius);
				}
				{ // elbow
					elbow->value<TransformComponent>();
					elbow->value<RenderComponent>().object = HashString("cragsman/character/elbow.object");
					PhysicsComponent &p = elbow->value<PhysicsComponent>();
					p.collisionRadius = 2.56723 / 2;
					p.mass = sphereVolume(p.collisionRadius);
				}
				{ // hand
					Rads angle = Real(i) / characterHandsCount * Rads::Full();
					Vec2 pos = Vec2(cos(angle), sin(angle)) * 20;
					hand->value<TransformComponent>().position = Vec3(pos, terrainOffset(pos));
					hand->value<RenderComponent>().object = HashString("cragsman/character/hand.object");
					PhysicsComponent &p = hand->value<PhysicsComponent>();
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
					e->value<SpringVisualComponent>().color = colorDeviation(colorIndex(i), 0.1);
				}
				{
					Entity *e = addSpring(characterElbows[i], characterHands[i], 10, 0.05, 0.1);
					e->value<SpringVisualComponent>().color = colorDeviation(colorIndex(i), 0.1);
				}
			}
			currentHand = 0;
		}

		return true;
	}

	const auto engineUpdateListener = controlThread().update.listen([]() {
		if (!characterBody)
		{
			if (!initializeTheGame())
				return;
		}

		{ // player position
			if (characterBody && engineEntities()->has(characterBody))
			{
				TransformComponent &t = engineEntities()->get(characterBody)->value<TransformComponent>();
				playerPosition = t.position;
			}
			else
				playerPosition = Vec3::Nan();
		}

		{ // cursor
			TransformComponent &bt = engineEntities()->get(characterBody)->value<TransformComponent>();
			Vec3 target = screenToWorld(engineWindow()->mousePosition());
			if (target.valid())
			{
				static const Real maxBodyCursorDistance = 30;
				if (distance(bt.position, target) > maxBodyCursorDistance)
					target = normalize(target - bt.position) * maxBodyCursorDistance + bt.position;
				TransformComponent &ct = engineEntities()->get(cursorName)->value<TransformComponent>();
				target[2] = terrainOffset(Vec2(target)) + ClinchTerrainOffset;
				ct.position = target;
			}
		}

		{ // camera
			TransformComponent &bt = engineEntities()->get(characterBody)->value<TransformComponent>();
			TransformComponent &ct = engineEntities()->get(cameraName)->value<TransformComponent>();
			smoothBodyPosition.add(bt.position);
			Vec3 sbp = smoothBodyPosition.smooth();
			ct.position = sbp + Vec3(0, 0, 100);
			Quat rot = Quat(sbp - ct.position, Vec3(0, 1, 0));
			ct.orientation = interpolate(ct.orientation, rot, 0.1);
		}

		{ // light
			TransformComponent &bt = engineEntities()->get(characterBody)->value<TransformComponent>();
			TransformComponent &lt = engineEntities()->get(lightName)->value<TransformComponent>();
			lt.position = bt.position;
			lt.orientation = sunLightOrientation(Vec2(playerPosition));
		}

		{ // hands orientations
			for (uint32 i = 0; i < characterHandsCount; i++)
			{
				TransformComponent &ht = engineEntities()->get(characterHands[i])->value<TransformComponent>();
				TransformComponent &et = engineEntities()->get(characterElbows[i])->value<TransformComponent>();
				ht.orientation = Quat(ht.position - et.position, Vec3(0, 0, 1), false);
			}
		}
	});

	EventListener<bool(const GenericInput &)> mousePressListener;
	const auto engineInitListener = controlThread().initialize.listen([]() {
		mousePressListener.attach(engineWindow()->events);
		mousePressListener.bind(inputListener<InputClassEnum::MousePress, InputMouse>(&mousePress));
	});
}
