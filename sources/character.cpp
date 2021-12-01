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

#include <vector>
#include <algorithm>

uint32 cameraName;
uint32 characterBody;
Vec3 playerPosition;

namespace
{
	InputListener<InputClassEnum::MousePress, InputMouse, bool> mousePressListener;

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

	Vec3 screenToWorld(const Vec2i &point)
	{
		Vec2i res = engineWindow()->resolution();
		Vec2 p = Vec2(point[0], point[1]);
		p /= Vec2(res[0], res[1]);
		p = p * 2 - 1;
		Real px = p[0], py = -p[1];
		TransformComponent &ts = engineEntities()->get(cameraName)->value<TransformComponent>();
		CameraComponent &cs = engineEntities()->get(cameraName)->value<CameraComponent>();
		Mat4 view = Mat4(inverse(ts));
		Mat4 proj = perspectiveProjection(cs.camera.perspectiveFov, Real(res[0]) / Real(res[1]), cs.near, cs.far);
		Mat4 inv = inverse(proj * view);
		Vec4 pn = inv * Vec4(px, py, -1, 1);
		Vec4 pf = inv * Vec4(px, py, 1, 1);
		Vec3 near = Vec3(pn) / pn[3];
		Vec3 far = Vec3(pf) / pf[3];
		return terrainIntersection(makeSegment(near, far));
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
			Entity *cam;
			cameraName = (cam = engineEntities()->createUnique())->name();
			TransformComponent &t = cam->value<TransformComponent>();
			CameraComponent &c = cam->value<CameraComponent>();
			c.ambientColor = Vec3(1);
			c.ambientIntensity = 0.03;
			c.near = 10;
			c.far = 500;
			c.effects = CameraEffectsFlags::Default;
		}

		{ // light
			Entity *lig;
			lightName = (lig = engineEntities()->createUnique())->name();
			TransformComponent &t = lig->value<TransformComponent>();
			LightComponent &l = lig->value<LightComponent>();
			ShadowmapComponent &s = lig->value<ShadowmapComponent>();
			l.lightType = LightTypeEnum::Directional;
			l.color = Vec3(1);
			l.intensity = 3;
			s.resolution = 4096;
			s.worldSize = Vec3(150, 150, 200);
		}

		{ // cursor
			Entity *cur;
			cursorName = (cur = engineEntities()->createUnique())->name();
			TransformComponent &t = cur->value<TransformComponent>();
		}

		{ // body
			Entity *body;
			characterBody = (body = engineEntities()->createUnique())->name();
			TransformComponent &t = body->value<TransformComponent>();
			RenderComponent &r = body->value<RenderComponent>();
			r.object = HashString("cragsman/character/body.object");
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
					TransformComponent &t = shoulder->value<TransformComponent>();
					RenderComponent &r = shoulder->value<RenderComponent>();
					r.object = HashString("cragsman/character/shoulder.object");
					PhysicsComponent &p = shoulder->value<PhysicsComponent>();
					p.collisionRadius = 2.3067 / 2;
					p.mass = sphereVolume(p.collisionRadius);
				}
				{ // elbow
					TransformComponent &t = elbow->value<TransformComponent>();
					RenderComponent &r = elbow->value<RenderComponent>();
					r.object = HashString("cragsman/character/elbow.object");
					PhysicsComponent &p = elbow->value<PhysicsComponent>();
					p.collisionRadius = 2.56723 / 2;
					p.mass = sphereVolume(p.collisionRadius);
				}
				{ // hand
					TransformComponent &t = hand->value<TransformComponent>();
					Rads angle = Real(i) / characterHandsCount * Rads::Full();
					Vec2 pos = Vec2(cos(angle), sin(angle)) * 20;
					t.position = Vec3(pos, terrainOffset(pos));
					RenderComponent &r = hand->value<RenderComponent>();
					r.object = HashString("cragsman/character/hand.object");
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
					SpringVisualComponent &sv = e->value<SpringVisualComponent>();
					sv.color = colorDeviation(colorIndex(i), 0.1);
				}
				{
					Entity *e = addSpring(characterElbows[i], characterHands[i], 10, 0.05, 0.1);
					SpringVisualComponent &sv = e->value<SpringVisualComponent>();
					sv.color = colorDeviation(colorIndex(i), 0.1);
				}
			}
			currentHand = 0;
		}

		return true;
	}

	void engineUpdate()
	{
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
	}

	void engineInitialize()
	{
		mousePressListener.attach(engineWindow()->events);
		mousePressListener.bind<&mousePress>();
	}

	class Callbacks
	{
		EventListener<void()> engineInitListener;
		EventListener<void()> engineUpdateListener;
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
