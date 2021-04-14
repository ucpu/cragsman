#ifndef cragsman_common_h_sdg456ds4hg6
#define cragsman_common_h_sdg456ds4hg6

#include <cage-core/core.h>
#include <cage-core/math.h>

using namespace cage;

void findInitialClinches(uint32 &count, Entity **result);
Entity *findClinch(const vec3 &pos, real maxDist);
real terrainOffset(const vec2 &position);
void terrainMaterial(const vec2 &pos, vec3 &color, real &roughness, real &metallic, bool rockOnly);
vec3 terrainIntersection(const Line &ln);
void addTerrainCollider(uint32 name, Collider *c);
void removeTerrainCollider(uint32 name);
real sphereVolume(real radius);
vec3 colorDeviation(const vec3 &color, real deviation);
quat sunLightOrientation(const vec2 &playerPosition);

struct PhysicsComponent
{
	static EntityComponent *component;
	vec3 velocity;
	real mass;
	real collisionRadius;
};

struct SpringComponent
{
	static EntityComponent *component;
	uint32 objects[2];
	real restDistance;
	real stiffness;
	real damping;
	SpringComponent();
};

struct SpringVisualComponent
{
	static EntityComponent *component;
	vec3 color;
	SpringVisualComponent();
};

struct TimeoutComponent
{
	static EntityComponent *component;
	uint32 ttl;
	TimeoutComponent();
};

#define GAME_COMPONENT(T,C,E) ::T##Component &C = (E)->value<::T##Component>(::T##Component::component);

extern uint32 cameraName;
extern uint32 characterBody;
extern vec3 playerPosition;

#define CLINCH_TERRAIN_OFFSET 1.2f

#endif
