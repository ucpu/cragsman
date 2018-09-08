#ifndef cragsman_common_h_sdg456ds4hg6
#define cragsman_common_h_sdg456ds4hg6

#include <cage-core/core.h>
#include <cage-core/math.h>

using namespace cage;

void initializeClinches();
entityClass *findClinch(const vec3 &pos, real maxDist); // find clinch closest to the point
entityClass *findClinch(const line &ln); // find clinch that intersects the line
real terrainOffset(const vec2 &position);
void terrainMaterial(const vec2 &pos, vec3 &color, real &roughness, real &metallic, bool rockOnly);
vec3 terrainIntersection(const line &ln);
void addTerrainCollider(uint32 name, colliderClass *c);
void removeTerrainCollider(uint32 name);
real sphereVolume(real radius);
vec3 colorDeviation(const vec3 &color, real deviation);

struct physicsComponent
{
	static componentClass *component;
	vec3 velocity;
	real mass;
	real collisionRadius;
};

struct springComponent
{
	static componentClass *component;
	uint32 objects[2];
	real restDistance;
	real stiffness;
	real damping;
	springComponent();
};

struct springVisualComponent
{
	static componentClass *component;
	vec3 color;
	springVisualComponent();
};

struct timeoutComponent
{
	static componentClass *component;
	uint32 ttl;
	timeoutComponent();
};

#define GAME_GET_COMPONENT(T,C,E) ::CAGE_JOIN(T, Component) &C = (E)->value<::CAGE_JOIN(T, Component)>(::CAGE_JOIN(T, Component)::component);

extern uint32 cameraName;
extern uint32 characterBody;

#define CLINCH_TERRAIN_OFFSET 1.2f

#endif
