
#include "common.h"

#include <cage-core/log.h>
#include <cage-core/geometry.h>
#include <cage-core/noise.h>
#include <cage-core/color.h>
#include <cage-core/random.h>

namespace
{
	const uint32 globalSeed = (uint32)currentRandomGenerator().next();

	holder<noiseClass> newClouds(uint32 seed, uint32 octaves = 3)
	{
		noiseCreateConfig cfg;
		cfg.seed = seed;
		cfg.octaves = octaves;
		cfg.type = noiseTypeEnum::Value;
		return newNoise(cfg);
	}

	holder<noiseClass> newValue(uint32 seed)
	{
		return newClouds(seed + 745, 0);
	}

	holder<noiseClass> newCell(uint32 seed, noiseOperationEnum operation = noiseOperationEnum::Distance, noiseDistanceEnum distance = noiseDistanceEnum::Euclidean, uint32 index0 = 0, uint32 index1 = 1)
	{
		noiseCreateConfig cfg;
		cfg.seed = seed;
		cfg.type = noiseTypeEnum::Cellular;
		cfg.operation = operation;
		cfg.distance = distance;
		cfg.index0 = index0;
		cfg.index1 = index1;
		return newNoise(cfg);
	}

	template<class T>
	real evaluateCell(holder<noiseClass> &noise, const T &position)
	{
		return noise->evaluate(position);
	}

	template<class T>
	real evaluateClamp(holder<noiseClass> &noise, const T &position)
	{
		return noise->evaluate(position) * 0.5 + 0.5;
	}

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
			return sin(rads::Stright() * (v - 0.8) / 0.2 + rads::Right());
		return v / 0.8;
	}
}

real terrainOffset(const vec2 &pos)
{
	real result;

	{ // slope
		result -= pos[1] * 0.2;
	}

	{ // horizontal slabs
		static holder<noiseClass> clouds1 = newClouds(globalSeed + 4);
		static holder<noiseClass> clouds2 = newClouds(globalSeed + 5);
		real off = evaluateClamp(clouds1, pos * 0.0065);
		real mask = evaluateClamp(clouds2, pos * 0.00715);
		result += slab(pos[1] * 0.027 + off * 2.5) * sharpEdge(mask * 2 - 0.7) * 5;
	}

	{ // extra saliences
		static holder<noiseClass> cell1 = newCell(globalSeed + 7);
		static holder<noiseClass> clouds1 = newClouds(globalSeed + 8);
		real a = evaluateCell(cell1, pos * 0.0241);
		real b = evaluateCell(clouds1, pos * 0.041);
		result += sharpEdge(a + b - 0.4);
	}

	{ // medium-frequency waves
		static holder<noiseClass> clouds1 = newClouds(globalSeed + 10);
		static holder<noiseClass> clouds2 = newClouds(globalSeed + 11);
		static holder<noiseClass> clouds3 = newClouds(globalSeed + 12);
		real scl = evaluateClamp(clouds1, pos * 0.00921);
		real rot = evaluateClamp(clouds2, pos * 0.00398);
		vec2 off = vec2(rot + 0.5, 1.5 - rot);
		real mask = evaluateClamp(clouds3, pos * 0.00654);
		static holder<noiseClass> cell1 = newCell(globalSeed + 15, noiseOperationEnum::Subtract);
		real a = evaluateCell(cell1, (pos * 0.01 + off) * (scl + 0.5));
		result += pow((min(a + 0.95, 1) - 0.95) * 20, 3) * sharpEdge(mask - 0.1) * 0.5;
	}

	{ // high-frequency x-aligned cracks
		static holder<noiseClass> clouds1 = newClouds(globalSeed + 15);
		static holder<noiseClass> clouds2 = newClouds(globalSeed + 16);
		real a = pow(evaluateClamp(clouds1, pos * vec2(0.036, 0.13)), 0.2);
		real b = pow(evaluateClamp(clouds2, pos * vec2(0.047, 0.029)), 0.1);
		result += min(a, b) * 3;
	}

	{ // medium-frequency y-aligned cracks
		static holder<noiseClass> clouds1 = newClouds(globalSeed + 17);
		static holder<noiseClass> clouds2 = newClouds(globalSeed + 18);
		real a = pow(evaluateClamp(clouds1, pos * vec2(0.11, 0.027) * 0.5), 0.2);
		real b = pow(evaluateClamp(clouds2, pos * vec2(0.033, 0.051) * 0.5), 0.1);
		result += min(a, b) * 3;
	}

	CAGE_ASSERT_RUNTIME(result.valid());
	return result;
}

namespace
{
	vec3 pdnToRgb(real h, real s, real v)
	{
		return convertHsvToRgb(vec3(h / 360, s / 100, v / 100));
	}

	template<uint32 N, class T>
	T ninterpolate(const T v[N], real f) // f is 0..1
	{
		CAGE_ASSERT_RUNTIME(f >= 0 && f < 1, f, N);
		f *= (N - 1); // 0..(N-1)
		uint32 i = numeric_cast<uint32>(f);
		CAGE_ASSERT_RUNTIME(i + 1 < N, f, i, N);
		return interpolate(v[i], v[i + 1], f - i);
	}

	vec3 recolor(const vec3 &color, real deviation, const vec2 &pos)
	{
		static holder<noiseClass> value1 = newValue(globalSeed + 123);
		static holder<noiseClass> value2 = newValue(globalSeed + 124);
		static holder<noiseClass> value3 = newValue(globalSeed + 125);
		real h = evaluateClamp(value1, pos) * 0.5 + 0.25;
		real s = evaluateClamp(value2, pos);
		real v = evaluateClamp(value3, pos);
		vec3 hsv = convertRgbToHsv(color) + (vec3(h, s, v) - 0.5) * deviation;
		hsv[0] = (hsv[0] + 1) % 1;
		return convertHsvToRgb(clamp(hsv, vec3(), vec3(1, 1, 1)));
	}

	void darkRockGeneral(const vec2 &pos, vec3 &color, real &roughness, real &metallic, const vec3 *colors, uint32 colorsCount)
	{
		static holder<noiseClass> clouds1 = newClouds(globalSeed + 103);
		static holder<noiseClass> clouds2 = newClouds(globalSeed + 112);
		static holder<noiseClass> clouds3 = newClouds(globalSeed + 121);
		vec2 off = vec2(evaluateClamp(clouds1, pos * 0.065), evaluateClamp(clouds2, pos * 0.1));
		real f = evaluateClamp(clouds3, pos * 0.0756 + off);

		switch (colorsCount)
		{
		case 3: color = ninterpolate<3>(colors, f); break;
		case 4: color = ninterpolate<4>(colors, f); break;
		default: CAGE_THROW_CRITICAL(notImplementedException, "unsupported colorsCount");
		}

		color = recolor(color, 0.1, pos * 2.1);

		static holder<noiseClass> clouds4 = newClouds(globalSeed + 148);
		roughness = evaluateClamp(clouds4, pos * 1.132) * 0.4 + 0.3;
		metallic = 0.02;
	}

	void basePaper(const vec2 &pos, vec3 &color, real &roughness, real &metallic)
	{
		static holder<noiseClass> clouds1 = newClouds(globalSeed + 143, 5);
		static holder<noiseClass> clouds2 = newClouds(globalSeed + 142, 5);
		static holder<noiseClass> clouds3 = newClouds(globalSeed + 141, 5);
		static holder<noiseClass> clouds4 = newClouds(globalSeed + 140);
		static holder<noiseClass> clouds5 = newClouds(globalSeed + 139);
		static holder<noiseClass> cell1 = newCell(globalSeed + 151, noiseOperationEnum::Distance, noiseDistanceEnum::Euclidean, 1);
		static holder<noiseClass> cell2 = newCell(globalSeed + 152, noiseOperationEnum::Distance, noiseDistanceEnum::Euclidean, 1);
		vec2 off = vec2(evaluateCell(cell1, pos * 0.063), evaluateCell(cell2, pos * 0.063));
		if (evaluateClamp(clouds4, pos * 0.097 + off * 2.2) < 0.6)
		{ // rock 1
			color = convertHsvToRgb(vec3(
				evaluateClamp(clouds1, pos * 0.134) * 0.01 + 0.08,
				evaluateClamp(clouds2, pos * 0.344) * 0.2 + 0.2,
				evaluateClamp(clouds3, pos * 0.100) * 0.4 + 0.55
			));
			roughness = evaluateClamp(clouds5, pos * 0.848) * 0.5 + 0.3;
			metallic = 0.02;
		}
		else
		{ // rock 2
			color = convertHsvToRgb(vec3(
				evaluateClamp(clouds1, pos * 0.321) * 0.02 + 0.094,
				evaluateClamp(clouds2, pos * 0.258) * 0.3 + 0.08,
				evaluateClamp(clouds3, pos * 0.369) * 0.2 + 0.59
			));
			roughness = 0.5;
			metallic = 0.049;
		}
	}

	void baseSphinx(const vec2 &pos, vec3 &color, real &roughness, real &metallic)
	{
		// https://www.canstockphoto.com/egyptian-sphinx-palette-26815891.html

		static const vec3 colors[4] = {
			pdnToRgb(31, 34, 96),
			pdnToRgb(31, 56, 93),
			pdnToRgb(26, 68, 80),
			pdnToRgb(21, 69, 55)
		};

		static holder<noiseClass> clouds1 = newClouds(globalSeed + 153, 4);
		real off = evaluateClamp(clouds1, pos * 0.0041);
		real y = (pos[1] * 0.012 + 1000) % 4;
		real c = (y + off * 2 - 1 + 4) % 4;
		uint32 i = numeric_cast<uint32>(c);
		real f = sharpEdge(c - i);
		if (i < 3)
			color = interpolate(colors[i], colors[i + 1], f);
		else
			color = interpolate(colors[3], colors[0], f);

		color = recolor(color, 0.1, pos * 1.1);

		static holder<noiseClass> clouds2 = newClouds(globalSeed + 154);
		roughness = evaluateClamp(clouds2, pos * 0.941) * 0.3 + 0.4;
		metallic = 0.02;
	}

	void baseWhite(const vec2 &pos, vec3 &color, real &roughness, real &metallic)
	{
		// https://www.pinterest.com/pin/432908582921844576/

		static const vec3 colors[3] = {
			pdnToRgb(19, 1, 96),
			pdnToRgb(14, 3, 88),
			pdnToRgb(217, 9, 74)
		};

		static holder<noiseClass> clouds1 = newClouds(globalSeed + 253);
		static holder<noiseClass> clouds2 = newClouds(globalSeed + 254);
		static holder<noiseClass> value1 = newValue(globalSeed + 255);
		vec2 off = vec2(evaluateClamp(clouds1, pos * 0.1), evaluateClamp(clouds2, pos * 0.1));
		real n = evaluateClamp(value1, pos * 0.1 + off);
		color = ninterpolate<3>(colors, n);

		color = recolor(color, 0.2, pos * 0.72);
		color = recolor(color, 0.13, pos * 1.3);

		static holder<noiseClass> clouds3 = newClouds(globalSeed + 256);
		roughness = pow(evaluateClamp(clouds3, pos * 1.441), 0.5) * 0.7 + 0.01;
		metallic = 0.05;
	}

	void baseDarkRock1(const vec2 &pos, vec3 &color, real &roughness, real &metallic)
	{
		// https://www.goodfreephotos.com/united-states/colorado/other-colorado/rock-cliff-in-the-fog-in-colorado.jpg.php

		static holder<noiseClass> clouds1 = newClouds(globalSeed + 323);
		static holder<noiseClass> clouds2 = newClouds(globalSeed + 324);
		static holder<noiseClass> clouds3 = newClouds(globalSeed + 325);
		vec2 off = vec2(evaluateClamp(clouds1, pos * 0.043), evaluateClamp(clouds2, pos * 0.043));
		static holder<noiseClass> cell1 = newCell(globalSeed + 326, noiseOperationEnum::Subtract);
		real f = evaluateCell(cell1, pos * 0.0147 + off * 0.23);
		real m = evaluateClamp(clouds3, pos * 0.018);
		if (f < 0.017 && m < 0.35)
		{ // the vein
			static const vec3 vein[2] = {
				pdnToRgb(18, 18, 60),
				pdnToRgb(21, 22, 49)
			};

			static holder<noiseClass> value1 = newValue(globalSeed + 741);
			color = interpolate(vein[0], vein[1], evaluateClamp(value1, pos));
			static holder<noiseClass> clouds2 = newClouds(globalSeed + 154);
			roughness = evaluateClamp(clouds2, pos * 0.718) * 0.3 + 0.3;
			metallic = 0.6;
		}
		else
		{ // the rocks
			static const vec3 colors[3] = {
				pdnToRgb(240, 1, 45),
				pdnToRgb(230, 5, 41),
				pdnToRgb(220, 25, 27)
			};

			darkRockGeneral(pos, color, roughness, metallic, colors, sizeof(colors) / sizeof(colors[0]));
		}
	}

	void baseDarkRock2(const vec2 &pos, vec3 &color, real &roughness, real &metallic)
	{
		// https://www.schemecolor.com/rocky-cliff-color-scheme.php

		static const vec3 colors[4] = {
			pdnToRgb(240, 1, 45),
			pdnToRgb(230, 6, 35),
			pdnToRgb(240, 11, 28),
			pdnToRgb(232, 27, 21)
		};

		darkRockGeneral(pos, color, roughness, metallic, colors, sizeof(colors) / sizeof(colors[0]));
	}
}

void terrainMaterial(const vec2 &pos, vec3 &color, real &roughness, real &metallic, bool rockOnly)
{
	switch (globalSeed % 5)
	{
	case 0: basePaper(pos, color, roughness, metallic); break;
	case 1: baseSphinx(pos, color, roughness, metallic); break;
	case 2: baseWhite(pos, color, roughness, metallic); break;
	case 3: baseDarkRock1(pos, color, roughness, metallic); break;
	case 4: baseDarkRock2(pos, color, roughness, metallic); break;
	default: CAGE_THROW_CRITICAL(notImplementedException, "unknown terrain base color enum");
	}

	{ // small cracks
		static holder<noiseClass> cell1 = newCell(globalSeed + 6974, noiseOperationEnum::Subtract);
		real f = evaluateCell(cell1, pos * 0.187);
		static holder<noiseClass> clouds1 = newClouds(globalSeed + 555);
		real m = evaluateClamp(clouds1, pos * 0.43);
		if (f < 0.02 && m < 0.5)
		{
			color *= 0.6;
			roughness *= 1.2;
		}
	}

	{ // white glistering spots
		static holder<noiseClass> cell1 = newCell(globalSeed + 6975);
		if (evaluateCell(cell1, pos * 0.084) > 0.95)
		{
			static holder<noiseClass> clouds2 = newClouds(globalSeed + 554, 2);
			real c = evaluateClamp(clouds2, pos * 3) * 0.2 + 0.8;
			color = vec3(c);
			roughness = 0.2;
			metallic = 0.4;
		}
	}

	if (rockOnly)
		return;

	{ // large cracks
		static holder<noiseClass> cell1 = newCell(globalSeed + 6976, noiseOperationEnum::Distance, noiseDistanceEnum::Euclidean, 1);
		static holder<noiseClass> cell2 = newCell(globalSeed + 6977, noiseOperationEnum::Distance, noiseDistanceEnum::Euclidean, 1);
		static holder<noiseClass> cell3 = newCell(globalSeed + 6978, noiseOperationEnum::Subtract);
		vec2 off = vec2(evaluateCell(cell1, pos * 0.1), evaluateCell(cell2, pos * 0.1));
		real f = evaluateCell(cell3, pos * 0.034 + off * 0.23);
		static holder<noiseClass> clouds3 = newClouds(globalSeed + 556);
		real m = evaluateClamp(clouds3, pos * 0.023);
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
		static holder<noiseClass> clouds4 = newClouds(globalSeed + 557);
		real thr = evaluateClamp(clouds4, pos * 0.015);
		if (vn > thr + 0.2)
		{
			static holder<noiseClass> clouds5 = newClouds(globalSeed + 558);
			real mask = evaluateClamp(clouds5, pos * 2.423);
			real m = sharpEdge(mask);
			static holder<noiseClass> value1 = newValue(globalSeed + 823);
			static holder<noiseClass> value2 = newValue(globalSeed + 824);
			static holder<noiseClass> value3 = newValue(globalSeed + 825);
			vec3 grass = convertHsvToRgb(vec3(
				evaluateClamp(value1, pos) * 0.3 + 0.13,
				evaluateClamp(value2, pos) * 0.2 + 0.5,
				evaluateClamp(value3, pos) * 0.2 + 0.5
			));
			static holder<noiseClass> clouds6 = newClouds(globalSeed + 558);
			real r = evaluateClamp(clouds6, pos * 1.23) * 0.4 + 0.2;
			color = interpolate(color, grass, m);
			roughness = interpolate(roughness, r, m);
			metallic = interpolate(metallic, 0.01, m);
		}
	}
}

quat sunLightOrientation(const vec2 &playerPosition)
{
	return quat(degs(-50), degs(sin(degs(playerPosition[0] * 0.2 + 40)) * 70), degs());
}
