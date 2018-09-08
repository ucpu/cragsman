
#include "common.h"

#include <cage-core/log.h>
#include <cage-core/geometry.h>
#include <cage-core/utility/noise.h>
#include <cage-core/utility/color.h>
#include <cage-core/utility/random.h>

namespace
{
	const uint32 globalSeed = (uint32)currentRandomGenerator().next();

	real rerange(real v, real ia, real ib, real oa, real ob)
	{
		return (v - ia) / (ib - ia) * (ob - oa) + oa;
	}

	real sharpEdge(real v)
	{
		return rerange(clamp(v, 0.45, 0.55), 0.45, 0.55, 0, 1);
	}

	real slab(real v)
	{
		v = v % 1;
		if (v > 0.8)
			return sin(rads::Stright * (v - 0.8) / 0.2 + rads::Right);
		return v / 0.8;
	}
}

real terrainOffset(const vec2 &pos)
{
	uint32 seed = globalSeed;
	real result;

	{ // slope
		result -= pos[1] * 0.2;
	}

	{ // horizontal slabs
		real off = noiseClouds(seed++, pos * 0.0065);
		real mask = noiseClouds(seed++, pos * 0.00715);
		result += slab(pos[1] * 0.027 + off * 2.5) * sharpEdge(mask * 2 - 0.7) * 5;
	}

	{ // extra saliences
		real a = noiseCell(seed++, pos * 0.05)[0];
		result += sharpEdge(a);
	}

	{ // medium-frequency waves
		real scl = noiseClouds(seed++, pos * 0.00921);
		real rot = noiseClouds(seed++, pos * 0.00398);
		vec2 off = vec2(rot + 0.5, 1.5 - rot);
		real mask = noiseClouds(seed++, pos * 0.00654);
		vec4 a = noiseCell(seed++, (pos * 0.01 + off) * (scl + 0.5));
		result += pow((min(a[1] - a[0] + 0.95, 1) - 0.95) * 20, 3) * sharpEdge(mask - 0.1) * 0.5;
	}

	{ // high-frequency x-aligned cracks
		real a = pow(noiseClouds(seed++, pos * vec2(0.036, 0.13)), 0.2);
		real b = pow(noiseClouds(seed++, pos * vec2(0.047, 0.029)), 0.1);
		result += min(a, b) * 2;
	}

	return result;
}

void terrainMaterial(const vec2 &pos, vec3 &color, real &roughness, real &metallic)
{
	// positive -> up facing
	// negative -> down facing
	real vn = (terrainOffset(pos + vec2(0, -0.1)) - terrainOffset(pos)) / 0.1;

	uint32 seed = globalSeed;

	{ // base color
		vec2 off = vec2(noiseCell(seed++, pos * 0.063)[1], noiseCell(seed++, pos * 0.063)[1]);
		if (noiseClouds(seed++, pos * 0.097 + off * 0.2) < 0.73)
		{ // rock 1
			color = convertHsvToRgb(vec3(
				noiseClouds(seed++, pos * 0.134, 5) * 0.01 + 0.08,
				noiseClouds(seed++, pos * 0.344, 5) * 0.2 + 0.2,
				noiseClouds(seed++, pos * 0.100, 5) * 0.4 + 0.55
			));
			roughness = 0.8;
			metallic = 0.002;
		}
		else
		{ // rock 2
			color = convertHsvToRgb(vec3(
				noiseClouds(seed++, pos * 0.321, 5) * 0.02 + 0.094,
				noiseClouds(seed++, pos * 0.258, 5) * 0.3 + 0.08,
				noiseClouds(seed++, pos * 0.369, 5) * 0.2 + 0.59
			));
			roughness = 0.6;
			metallic = 0.049;
		}
	}

	{ // cracks
		vec4 ff = noiseCell(seed++, pos * 0.187);
		real f = ff[1] - ff[0];
		real m = noiseClouds(seed++, pos * 0.43);
		if (f < 0.02 && m < 0.5)
		{
			color *= 0.6;
			roughness *= 1.2;
		}
	}

	// white glistering spots
	if (noiseCell(seed++, pos * 0.234)[0] > 0.52)
	{
		real c = noiseClouds(seed++, pos * 3, 2) * 0.7 + 0.5;
		color = vec3(c, c, c);
		roughness = 0.05;
		metallic = 0.97;
	}

	{ // large cracks
		vec2 off = vec2(noiseCell(seed++, pos * 0.1)[1], noiseCell(seed++, pos * 0.1)[1]);
		vec4 ff = noiseCell(seed++, pos * 0.034 + off * 0.23);
		real f = ff[1] - ff[0];
		real m = noiseClouds(seed++, pos * 0.023);
		if (f < 0.015 && m < 0.4)
		{
			color *= 0.3;
			roughness *= 1.5;
		}
	}

	{ // large grass (on up facing surfaces)
		real thr = noiseClouds(seed++, pos * 0.015);
		if (vn > thr + 0.2)
		{
			real mask = noiseClouds(seed++, pos * 1.723);
			real m = sharpEdge(mask);
			vec3 grass = convertHsvToRgb(vec3(
				noiseValue(seed++, pos) * 0.2 + 0.18,
				noiseValue(seed++, pos) * 0.2 + 0.5,
				noiseValue(seed++, pos) * 0.2 + 0.5
			));
			color = interpolate(color, grass, m);
			roughness = interpolate(roughness, 0.3, m);
			metallic = interpolate(metallic, 0, m);
		}
		else
			seed += 4;
	}
}
