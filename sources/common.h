#ifndef cragsman_common_h_sdg456ds4hg6
#define cragsman_common_h_sdg456ds4hg6

#include <cage-core/core.h>
#include <cage-core/math.h>

using namespace cage;

void findInitialClinches(uint32 &count, entity **result);
entity *findClinch(const vec3 &pos, real maxDist);
real terrainOffset(const vec2 &position);
void terrainMaterial(const vec2 &pos, vec3 &color, real &roughness, real &metallic, bool rockOnly);
vec3 terrainIntersection(const line &ln);
void addTerrainCollider(uint32 name, collisionMesh *c);
void removeTerrainCollider(uint32 name);
real sphereVolume(real radius);
vec3 colorDeviation(const vec3 &color, real deviation);
quat sunLightOrientation(const vec2 &playerPosition);

struct physicsComponent
{
	static entityComponent *component;
	vec3 velocity;
	real mass;
	real collisionRadius;
};

struct springComponent
{
	static entityComponent *component;
	uint32 objects[2];
	real restDistance;
	real stiffness;
	real damping;
	springComponent();
};

struct springVisualComponent
{
	static entityComponent *component;
	vec3 color;
	springVisualComponent();
};

struct timeoutComponent
{
	static entityComponent *component;
	uint32 ttl;
	timeoutComponent();
};

#define GAME_GET_COMPONENT(T,C,E) ::CAGE_JOIN(T, Component) &C = (E)->value<::CAGE_JOIN(T, Component)>(::CAGE_JOIN(T, Component)::component);

extern uint32 cameraName;
extern uint32 characterBody;
extern vec3 playerPosition;

#define CLINCH_TERRAIN_OFFSET 1.2f

#endif
