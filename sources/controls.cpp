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
	uint32 currentHand;

	variableSmoothingBufferStruct<vec3> smoothBodyPosition;

	void addSpring(uint32 a, uint32 b, real restDistance, real stiffness, real damping)
	{
		entityClass *spring = entities()->newUniqueEntity();
		GAME_GET_COMPONENT(spring, s, spring);
		s.objects[0] = a;
		s.objects[1] = b;
		s.restDistance = restDistance;
		s.stiffness = stiffness;
		s.damping = damping;
		std::sort(s.objects, s.objects + 1);
	}

	void addSpringBodyElbow(uint32 a, uint32 b)
	{
		addSpring(a, b, 10, 0.05, 0.1);
	}

	void addSpringElbowHand(uint32 a, uint32 b)
	{
		addSpring(a, b, 10, 0.05, 0.1);
	}

	void addJoint(uint32 a, uint32 b)
	{
		addSpring(a, b, 0, 0.3, 0.3);
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
		return terrainIntersection(line(near, far - near, 0, 1).normalize());
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
			c.far = 150;
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
			s.worldRadius = vec3(150, 150, 150);
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
			p.mass = 50;
			p.collisionRadius = 3;
		}

		{ // hands
			for (uint32 i = 0; i < characterHandsCount; i++)
			{
				entityClass *hand, *elbow;
				characterHands[i] = (hand = entities()->newUniqueEntity())->getName();
				characterElbows[i] = (elbow = entities()->newUniqueEntity())->getName();
				{ // hand
					ENGINE_GET_COMPONENT(transform, t, hand);
					rads angle = real(i) / characterHandsCount * rads::Full;
					vec2 pos = vec2(cos(angle), sin(angle)) * 20;
					t.position = vec3(pos, terrainOffset(pos));
					ENGINE_GET_COMPONENT(render, r, hand);
					r.object = hashString("cragsman/character/hand.object");
					GAME_GET_COMPONENT(physics, p, hand);
					p.mass = 10;
					p.collisionRadius = 1;
					if (i == 0)
						addJoint(characterHands[i], cursorName);
					else
					{
						entityClass *c = findClinch(t.position, 100);
						addJoint(characterHands[i], c->getName());
					}
				}
				{ // elbow
					ENGINE_GET_COMPONENT(transform, t, elbow);
					ENGINE_GET_COMPONENT(render, r, elbow);
					r.object = hashString("cragsman/character/elbow.object");
					GAME_GET_COMPONENT(physics, p, elbow);
					p.mass = 10;
					p.collisionRadius = 1;
				}
				addSpringBodyElbow(characterBody, characterElbows[i]);
				addSpringElbowHand(characterElbows[i], characterHands[i]);
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
				removeSprings(characterHands[currentHand]);
				addSpringElbowHand(characterHands[currentHand], characterElbows[currentHand]);
				addJoint(characterHands[currentHand], clinch->getName());

				currentHand = (currentHand + 1) % characterHandsCount;

				removeSprings(characterHands[currentHand]);
				addSpringElbowHand(characterHands[currentHand], characterElbows[currentHand]);
				addJoint(characterHands[currentHand], cursorName);
			}
			return true;
		}
		return false;
	}

	void restartGame()
	{
		CAGE_LOG(severityEnum::Info, "controls", "restarting the game");

		entities()->getAllEntities()->destroyAllEntities();
		initializeMap(0, 0);
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
			static const real maxBodyCursorDistance = 25;
			if (distance(bt.position, target) > maxBodyCursorDistance)
				target = (target - bt.position).normalize() * maxBodyCursorDistance + bt.position;
			ENGINE_GET_COMPONENT(transform, ct, entities()->getEntity(cursorName));
			target[2] = terrainOffset(vec2(target)) + CLINCH_TERRAIN_OFFSET;
			ct.position = target;
		}

		{ // camera
			ENGINE_GET_COMPONENT(transform, bt, entities()->getEntity(characterBody));
			ENGINE_GET_COMPONENT(transform, ct, entities()->getEntity(cameraName));
			smoothBodyPosition.add(bt.position);
			vec3 sbp = smoothBodyPosition.smooth();
			ct.position = sbp + vec3(0, -30, 100);
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
