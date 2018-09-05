#ifndef cragsman_common_h_sdg456ds4hg6
#define cragsman_common_h_sdg456ds4hg6

#include <cage-core/core.h>
#include <cage-core/math.h>

using namespace cage;

void initializeMap(uint64 seed, uint32 level);
entityClass *findClinch(const vec3 &pos, real maxDist); // find clinch closest to the point
entityClass *findClinch(const line &ln); // find clinch that intersects the line
real terrainOffset(const vec2 &position);
vec3 terrainIntersection(const line &ln);
entityClass *newParticle(const vec3 &position, const vec3 &velocity, const vec3 &color, real mass, uint32 ttl);

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
