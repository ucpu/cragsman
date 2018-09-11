
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

	{ // low-frequency noise
		real scl = noiseClouds(seed++, pos * 0.004);
		vec4 c = noiseCell(seed++, pos * (scl + 0.5) * 0.0007, noiseDistanceEnum::Chebychev);
		result += (c[0] - c[1] - c[2] + c[3]) * 20;
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

namespace
{
	vec3 pdnToRgb(real h, real s, real v)
	{
		return convertHsvToRgb(vec3(h / 360, s / 100, v / 100));
	}

	template<uint32 N, class T>
	T ninterpolate(const T v[3], real f) // f is 0..1
	{
		f *= N; // 0..N
		uint32 i = numeric_cast<uint32>(f);
		return interpolate(v[i], v[i + 1], f - i);
	}

	vec3 recolor(const vec3 &color, real deviation, uint32 &seed, const vec2 &pos)
	{
		real h = noiseClouds(seed++, pos * 0.5, 1) * 0.5 + 0.25;
		real s = noiseClouds(seed++, pos * 0.5, 1);
		real v = noiseClouds(seed++, pos * 0.5, 1);
		vec3 hsv = convertRgbToHsv(color) + (vec3(h, s, v) - 0.5) * deviation;
		hsv[0] = (hsv[0] + 1) % 1;
		return convertHsvToRgb(clamp(hsv, vec3(), vec3(1, 1, 1)));
	}

	void darkRockGeneral(uint32 &seed, const vec2 &pos, vec3 &color, real &roughness, real &metallic, const vec3 *colors, uint32 colorsCount)
	{
		vec2 off = vec2(noiseClouds(seed++, pos * 0.065), noiseClouds(seed++, pos * 0.1));
		real f = noiseClouds(seed++, pos * 0.0756 + off);

		switch (colorsCount)
		{
		case 3: color = ninterpolate<3>(colors, f); break;
		case 4: color = ninterpolate<4>(colors, f); break;
		default: CAGE_THROW_CRITICAL(notImplementedException, "unsupported colorsCount");
		}

		color = recolor(color, 0.1, seed, pos);

		roughness = 0.8;
		metallic = 0.02;
	}

	void basePaper(uint32 &seed, const vec2 &pos, vec3 &color, real &roughness, real &metallic)
	{
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

	void baseSphinx(uint32 &seed, const vec2 &pos, vec3 &color, real &roughness, real &metallic)
	{
		// https://www.canstockphoto.com/egyptian-sphinx-palette-26815891.html

		static const vec3 colors[4] = {
			pdnToRgb(31, 34, 96),
			pdnToRgb(31, 56, 93),
			pdnToRgb(26, 68, 80),
			pdnToRgb(21, 69, 55)
		};

		real off = noiseClouds(seed++, pos * 0.0041, 4);
		real y = (pos[1] * 0.012 + 1000) % 4;
		real c = (y + off * 2 - 1 + 4) % 4;
		uint32 i = numeric_cast<uint32>(c);
		real f = sharpEdge(c - i);
		if (i < 3)
			color = interpolate(colors[i], colors[i + 1], f);
		else
			color = interpolate(colors[3], colors[0], f);

		color = recolor(color, 0.1, seed, pos);

		roughness = 0.8;
		metallic = 0.002;
	}

	void baseWhite(uint32 &seed, const vec2 &pos, vec3 &color, real &roughness, real &metallic)
	{
		// https://www.pinterest.com/pin/432908582921844576/

		static const vec3 colors[3] = {
			pdnToRgb(19, 1, 96),
			pdnToRgb(14, 3, 88),
			pdnToRgb(217, 9, 74)
		};

		vec2 off = vec2(noiseClouds(seed++, pos * 0.1), noiseClouds(seed++, pos * 0.1));
		real n = noiseValue(seed++, pos * 0.1 + off);
		color = ninterpolate<3>(colors, n);

		color = recolor(color, 0.1, seed, pos);

		roughness = 0.3;
		metallic = 0.05;
	}

	void baseDarkRock1(uint32 &seed, const vec2 &pos, vec3 &color, real &roughness, real &metallic)
	{
		// https://www.goodfreephotos.com/united-states/colorado/other-colorado/rock-cliff-in-the-fog-in-colorado.jpg.php

		vec2 off = vec2(noiseClouds(seed++, pos * 0.043), noiseClouds(seed++, pos * 0.043));
		vec4 ff = noiseCell(seed++, pos * 0.0147 + off * 0.23);
		real f = ff[1] - ff[0];
		real m = noiseClouds(seed++, pos * 0.018);
		if (f < 0.017 && m < 0.35)
		{ // the vein
			static const vec3 vein[2] = {
				pdnToRgb(18, 18, 60),
				pdnToRgb(21, 22, 49)
			};

			color = interpolate(vein[0], vein[1], noiseValue(seed++, pos));
			roughness = 0.7;
			metallic = 0.98;

			seed += 6;
		}
		else
		{ // the rocks
			static const vec3 colors[3] = {
				pdnToRgb(240, 1, 45),
				pdnToRgb(230, 5, 41),
				pdnToRgb(220, 25, 27)
			};

			darkRockGeneral(seed, pos, color, roughness, metallic, colors, sizeof(colors) / sizeof(colors[0]));

			seed += 1;
		}
	}

	void baseDarkRock2(uint32 &seed, const vec2 &pos, vec3 &color, real &roughness, real &metallic)
	{
		 // https://www.schemecolor.com/rocky-cliff-color-scheme.php

		static const vec3 colors[4] = {
			pdnToRgb(240, 1, 45),
			pdnToRgb(230, 6, 35),
			pdnToRgb(240, 11, 28),
			pdnToRgb(232, 27, 21)
		};

		darkRockGeneral(seed, pos, color, roughness, metallic, colors, sizeof(colors)/sizeof(colors[0]));
	}
}

void terrainMaterial(const vec2 &pos, vec3 &color, real &roughness, real &metallic, bool rockOnly)
{
	uint32 seed = globalSeed;

	switch (globalSeed % 5)
	{
	case 0: basePaper(seed, pos, color, roughness, metallic); break;
	case 1: baseSphinx(seed, pos, color, roughness, metallic); break;
	case 2: baseWhite(seed, pos, color, roughness, metallic); break;
	case 3: baseDarkRock1(seed, pos, color, roughness, metallic); break;
	case 4: baseDarkRock2(seed, pos, color, roughness, metallic); break;
	default: CAGE_THROW_CRITICAL(notImplementedException, "unknown terrain base color enum");
	}

	{ // small cracks
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

	if (rockOnly)
		return;

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

	// positive -> up facing
	// negative -> down facing
	real vn = (terrainOffset(pos + vec2(0, -0.1)) - terrainOffset(pos)) / 0.1;

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

quat sunLightOrientation(const vec2 &playerPosition)
{
	return quat(degs(-50), degs(sin(degs(playerPosition[0] * 0.2 + 40)) * 70), degs());
}
