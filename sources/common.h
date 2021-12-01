#ifndef cragsman_common_h_sdg456ds4hg6
#define cragsman_common_h_sdg456ds4hg6

#include <cage-core/core.h>
#include <cage-core/math.h>

using namespace cage;

void findInitialClinches(uint32 &count, Entity **result);
Entity *findClinch(const Vec3 &pos, Real maxDist);
Real terrainOffset(const Vec2 &position);
void terrainMaterial(const Vec2 &pos, Vec3 &color, Real &roughness, Real &metallic, bool rockOnly);
Vec3 terrainIntersection(const Line &ln);
void addTerrainCollider(uint32 name, Holder<Collider> c);
void removeTerrainCollider(uint32 name);
Real sphereVolume(Real radius);
Vec3 colorDeviation(const Vec3 &color, Real deviation);
Quat sunLightOrientation(const Vec2 &playerPosition);

struct PhysicsComponent
{
	Vec3 velocity;
	Real mass;
	Real collisionRadius;
};

struct SpringComponent
{
	uint32 objects[2];
	Real restDistance;
	Real stiffness;
	Real damping;
	SpringComponent();
};

struct SpringVisualComponent
{
	Vec3 color;
	SpringVisualComponent();
};

struct TimeoutComponent
{
	uint32 ttl;
	TimeoutComponent();
};

extern uint32 cameraName;
extern uint32 characterBody;
extern Vec3 playerPosition;

constexpr float ClinchTerrainOffset = 1.2f;

#endif
