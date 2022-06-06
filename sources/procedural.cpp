#include "common.h"

#include <cage-core/geometry.h>
#include <cage-core/noiseFunction.h>
#include <cage-core/color.h>
#include <cage-core/random.h>

#include <array>
#include <algorithm>

namespace
{
	const uint32 GlobalSeed = (uint32)detail::randomGenerator().next();

	uint32 newSeed()
	{
		static uint32 index = 35741890;
		index = hash(index);
		return GlobalSeed + index;
	}

	Holder<NoiseFunction> newClouds(uint32 octaves)
	{
		NoiseFunctionCreateConfig cfg;
		cfg.seed = newSeed();
		cfg.octaves = octaves;
		cfg.type = NoiseTypeEnum::Value;
		return newNoiseFunction(cfg);
	}

	Holder<NoiseFunction> newValue()
	{
		return newClouds(0);
	}

	Holder<NoiseFunction> newCell(NoiseOperationEnum operation = NoiseOperationEnum::Distance, NoiseDistanceEnum distance = NoiseDistanceEnum::Euclidean)
	{
		NoiseFunctionCreateConfig cfg;
		cfg.seed = newSeed();
		cfg.type = NoiseTypeEnum::Cellular;
		cfg.operation = operation;
		cfg.distance = distance;
		return newNoiseFunction(cfg);
	}

	template<class T>
	Real evaluateOrig(Holder<NoiseFunction> &noiseFunction, const T &position)
	{
		return noiseFunction->evaluate(position);
	}

	template<class T>
	Real evaluateClamp(Holder<NoiseFunction> &noiseFunction, const T &position)
	{
		return noiseFunction->evaluate(position) * 0.5 + 0.5;
	}

	Real rerange(Real v, Real ia, Real ib, Real oa, Real ob)
	{
		return (v - ia) / (ib - ia) * (ob - oa) + oa;
	}

	Real sharpEdge(Real v)
	{
		return rerange(clamp(v, 0.45, 0.55), 0.45, 0.55, 0, 1);
	}

	Vec3 pdnToRgb(Real h, Real s, Real v)
	{
		return colorHsvToRgb(Vec3(h / 360, s / 100, v / 100));
	}

	template<uint32 N, class T>
	T ninterpolate(const T v[N], Real f) 
	{
		CAGE_ASSERT(f >= 0 && f < 1);
		f *= (N - 1); // 0..(N-1)
		uint32 i = numeric_cast<uint32>(f);
		CAGE_ASSERT(i + 1 < N);
		return interpolate(v[i], v[i + 1], f - i);
	}

	Vec3 recolor(const Vec3 &color, Real deviation, const Vec3 &pos)
	{
		static Holder<NoiseFunction> value1 = newValue();
		static Holder<NoiseFunction> value2 = newValue();
		static Holder<NoiseFunction> value3 = newValue();

		Real h = evaluateClamp(value1, pos) * 0.5 + 0.25;
		Real s = evaluateClamp(value2, pos);
		Real v = evaluateClamp(value3, pos);
		Vec3 hsv = colorRgbToHsv(color) + (Vec3(h, s, v) - 0.5) * deviation;
		hsv[0] = (hsv[0] + 1) % 1;
		return colorHsvToRgb(clamp(hsv, 0, 1));
	}

	void darkRockGeneral(const Vec3 &pos, Vec3 &color, Real &roughness, Real &metallic, const Vec3 *colors, uint32 colorsCount)
	{
		static Holder<NoiseFunction> clouds1 = newClouds(3);
		static Holder<NoiseFunction> clouds2 = newClouds(3);
		static Holder<NoiseFunction> clouds3 = newClouds(3);
		static Holder<NoiseFunction> clouds4 = newClouds(3);
		static Holder<NoiseFunction> clouds5 = newClouds(3);

		Vec3 off = Vec3(evaluateClamp(clouds1, pos * 0.065), evaluateClamp(clouds2, pos * 0.104), evaluateClamp(clouds3, pos * 0.083));
		Real f = evaluateClamp(clouds4, pos * 0.0756 + off);
		switch (colorsCount)
		{
		case 3: color = ninterpolate<3>(colors, f); break;
		case 4: color = ninterpolate<4>(colors, f); break;
		default: CAGE_THROW_CRITICAL(NotImplemented, "unsupported colorsCount");
		}
		color = recolor(color, 0.1, pos * 2.1);
		roughness = evaluateClamp(clouds5, pos * 1.132) * 0.4 + 0.3;
		metallic = 0.02;
	}

	void basePaper(const Vec3 &pos, Vec3 &color, Real &roughness, Real &metallic)
	{
		static Holder<NoiseFunction> clouds1 = newClouds(5);
		static Holder<NoiseFunction> clouds2 = newClouds(5);
		static Holder<NoiseFunction> clouds3 = newClouds(5);
		static Holder<NoiseFunction> clouds4 = newClouds(3);
		static Holder<NoiseFunction> clouds5 = newClouds(3);
		static Holder<NoiseFunction> cell1 = newCell(NoiseOperationEnum::Distance2, NoiseDistanceEnum::Euclidean);
		static Holder<NoiseFunction> cell2 = newCell(NoiseOperationEnum::Distance2, NoiseDistanceEnum::Euclidean);
		static Holder<NoiseFunction> cell3 = newCell(NoiseOperationEnum::Distance2, NoiseDistanceEnum::Euclidean);

		Vec3 off = Vec3(evaluateClamp(cell1, pos * 0.063), evaluateClamp(cell2, pos * 0.063), evaluateClamp(cell3, pos * 0.063));
		if (evaluateClamp(clouds4, pos * 0.097 + off * 2.2) < 0.6)
		{ // rock 1
			color = colorHsvToRgb(Vec3(
				evaluateClamp(clouds1, pos * 0.134) * 0.01 + 0.08,
				evaluateClamp(clouds2, pos * 0.344) * 0.2 + 0.2,
				evaluateClamp(clouds3, pos * 0.100) * 0.4 + 0.55
			));
			roughness = evaluateClamp(clouds5, pos * 0.848) * 0.5 + 0.3;
			metallic = 0.02;
		}
		else
		{ // rock 2
			color = colorHsvToRgb(Vec3(
				evaluateClamp(clouds1, pos * 0.321) * 0.02 + 0.094,
				evaluateClamp(clouds2, pos * 0.258) * 0.3 + 0.08,
				evaluateClamp(clouds3, pos * 0.369) * 0.2 + 0.59
			));
			roughness = 0.5;
			metallic = 0.049;
		}
	}

	void baseSphinx(const Vec3 &pos, Vec3 &color, Real &roughness, Real &metallic)
	{
		// https://www.canstockphoto.com/egyptian-sphinx-palette-26815891.html

		static const Vec3 colors[4] = {
			pdnToRgb(31, 34, 96),
			pdnToRgb(31, 56, 93),
			pdnToRgb(26, 68, 80),
			pdnToRgb(21, 69, 55)
		};

		static Holder<NoiseFunction> clouds1 = newClouds(4);
		static Holder<NoiseFunction> clouds2 = newClouds(3);

		Real off = evaluateClamp(clouds1, pos * 0.0041);
		Real y = (pos[1] * 0.012 + 1000) % 4;
		Real c = (y + off * 2 - 1 + 4) % 4;
		uint32 i = numeric_cast<uint32>(c);
		Real f = sharpEdge(c - i);
		if (i < 3)
			color = interpolate(colors[i], colors[i + 1], f);
		else
			color = interpolate(colors[3], colors[0], f);
		color = recolor(color, 0.1, pos * 1.1);
		roughness = evaluateClamp(clouds2, pos * 0.941) * 0.3 + 0.4;
		metallic = 0.02;
	}

	void baseWhite(const Vec3 &pos, Vec3 &color, Real &roughness, Real &metallic)
	{
		// https://www.pinterest.com/pin/432908582921844576/

		static const Vec3 colors[3] = {
			pdnToRgb(19, 1, 96),
			pdnToRgb(14, 3, 88),
			pdnToRgb(217, 9, 74)
		};

		static Holder<NoiseFunction> clouds1 = newClouds(3);
		static Holder<NoiseFunction> clouds2 = newClouds(3);
		static Holder<NoiseFunction> clouds3 = newClouds(3);
		static Holder<NoiseFunction> clouds4 = newClouds(3);
		static Holder<NoiseFunction> value1 = newValue();

		Vec3 off = Vec3(evaluateClamp(clouds1, pos * 0.1), evaluateClamp(clouds2, pos * 0.1), evaluateClamp(clouds3, pos * 0.1));
		Real n = evaluateClamp(value1, pos * 0.1 + off);
		color = ninterpolate<3>(colors, n);
		color = recolor(color, 0.2, pos * 0.72);
		color = recolor(color, 0.13, pos * 1.3);
		roughness = pow(evaluateClamp(clouds4, pos * 1.441), 0.5) * 0.7 + 0.01;
		metallic = 0.05;
	}

	void baseDarkRock1(const Vec3 &pos, Vec3 &color, Real &roughness, Real &metallic)
	{
		// https://www.goodfreephotos.com/united-states/colorado/other-colorado/rock-cliff-in-the-fog-in-colorado.jpg.php

		static Holder<NoiseFunction> clouds1 = newClouds(3);
		static Holder<NoiseFunction> clouds2 = newClouds(3);
		static Holder<NoiseFunction> clouds3 = newClouds(3);
		static Holder<NoiseFunction> clouds4 = newClouds(3);
		static Holder<NoiseFunction> clouds5 = newClouds(3);
		static Holder<NoiseFunction> cell1 = newCell(NoiseOperationEnum::Subtract);
		static Holder<NoiseFunction> value1 = newValue();

		Vec3 off = Vec3(evaluateClamp(clouds1, pos * 0.043), evaluateClamp(clouds2, pos * 0.043), evaluateClamp(clouds3, pos * 0.043));
		Real f = evaluateClamp(cell1, pos * 0.0147 + off * 0.23);
		Real m = evaluateClamp(clouds4, pos * 0.018);
		if (f < 0.017 && m < 0.35)
		{ // the vein
			static const Vec3 vein[2] = {
				pdnToRgb(18, 18, 60),
				pdnToRgb(21, 22, 49)
			};

			color = interpolate(vein[0], vein[1], evaluateClamp(value1, pos));
			roughness = evaluateClamp(clouds5, pos * 0.718) * 0.3 + 0.3;
			metallic = 0.6;
		}
		else
		{ // the rocks
			static const Vec3 colors[3] = {
				pdnToRgb(240, 1, 45),
				pdnToRgb(230, 5, 41),
				pdnToRgb(220, 25, 27)
			};

			darkRockGeneral(pos, color, roughness, metallic, colors, sizeof(colors) / sizeof(colors[0]));
		}
	}

	void baseDarkRock2(const Vec3 &pos, Vec3 &color, Real &roughness, Real &metallic)
	{
		// https://www.schemecolor.com/rocky-cliff-color-scheme.php

		static const Vec3 colors[4] = {
			pdnToRgb(240, 1, 45),
			pdnToRgb(230, 6, 35),
			pdnToRgb(240, 11, 28),
			pdnToRgb(232, 27, 21)
		};

		darkRockGeneral(pos, color, roughness, metallic, colors, sizeof(colors) / sizeof(colors[0]));
	}

	void basesSwitch(uint32 baseIndex, const Vec3 &pos, Vec3 &color, Real &roughness, Real &metallic)
	{
		switch (baseIndex)
		{
		case 0: basePaper(pos, color, roughness, metallic); break;
		case 1: baseSphinx(pos, color, roughness, metallic); break;
		case 2: baseWhite(pos, color, roughness, metallic); break;
		case 3: baseDarkRock1(pos, color, roughness, metallic); break;
		case 4: baseDarkRock2(pos, color, roughness, metallic); break;
		default: CAGE_THROW_CRITICAL(NotImplemented, "unknown terrain base color enum");
		}
	}

	std::array<Real, 5> basesWeights(const Vec3 &pos)
	{
		static Holder<NoiseFunction> clouds1 = newClouds(3);
		static Holder<NoiseFunction> clouds2 = newClouds(3);
		static Holder<NoiseFunction> clouds3 = newClouds(3);
		static Holder<NoiseFunction> clouds4 = newClouds(3);
		static Holder<NoiseFunction> clouds5 = newClouds(3);

		const Vec3 p = pos * 0.01;
		std::array<Real, 5> result;
		result[0] = clouds1->evaluate(p);
		result[1] = clouds2->evaluate(p);
		result[2] = clouds3->evaluate(p);
		result[3] = clouds4->evaluate(p);
		result[4] = clouds5->evaluate(p);
		return result;
	}

	struct WeightIndex
	{
		Real weight;
		uint32 index = m;
	};

	Real slab(Real v)
	{
		v = v % 1;
		if (v > 0.8)
			return sin(Rads::Full() * 0.5 * (v - 0.8) / 0.2 + Rads::Full() * 0.25);
		return v / 0.8;
	}
}

Real terrainOffset(const Vec2 &pos)
{
	static Holder<NoiseFunction> clouds1 = newClouds(3);
	static Holder<NoiseFunction> clouds2 = newClouds(3);
	static Holder<NoiseFunction> clouds3 = newClouds(3);
	static Holder<NoiseFunction> clouds4 = newClouds(3);
	static Holder<NoiseFunction> clouds5 = newClouds(3);
	static Holder<NoiseFunction> clouds6 = newClouds(3);
	static Holder<NoiseFunction> clouds7 = newClouds(3);
	static Holder<NoiseFunction> clouds8 = newClouds(3);
	static Holder<NoiseFunction> clouds9 = newClouds(3);
	static Holder<NoiseFunction> clouds10 = newClouds(3);
	static Holder<NoiseFunction> cell1 = newCell();
	static Holder<NoiseFunction> cell2 = newCell(NoiseOperationEnum::Subtract);

	Real result;

	{ // slope
		result -= pos[1] * 0.2;
	}

	{ // horizontal slabs
		Real off = evaluateClamp(clouds1, pos * 0.0065);
		Real mask = evaluateClamp(clouds2, pos * 0.00715);
		result += slab(pos[1] * 0.027 + off * 2.5) * sharpEdge(mask * 2 - 0.7) * 5;
	}

	{ // extra saliences
		Real a = evaluateClamp(cell1, pos * 0.0241);
		Real b = evaluateOrig(clouds3, pos * 0.041);
		result += sharpEdge(a + b - 0.4);
	}

	{ // medium-frequency waves
		Real scl = evaluateClamp(clouds4, pos * 0.00921);
		Real rot = evaluateClamp(clouds5, pos * 0.00398);
		Vec2 off = Vec2(rot + 0.5, 1.5 - rot);
		Real mask = evaluateClamp(clouds6, pos * 0.00654);
		Real a = evaluateClamp(cell2, (pos * 0.01 + off) * (scl + 0.5));
		result += pow((min(a + 0.95, 1) - 0.95) * 20, 3) * sharpEdge(mask - 0.1) * 0.5;
	}

	{ // high-frequency x-aligned cracks
		Real a = pow(evaluateClamp(clouds7, pos * Vec2(0.036, 0.13)), 0.2);
		Real b = pow(evaluateClamp(clouds8, pos * Vec2(0.047, 0.029)), 0.1);
		result += min(a, b) * 3;
	}

	{ // medium-frequency y-aligned cracks
		Real a = pow(evaluateClamp(clouds9, pos * Vec2(0.11, 0.027) * 0.5), 0.2);
		Real b = pow(evaluateClamp(clouds10, pos * Vec2(0.033, 0.051) * 0.5), 0.1);
		result += min(a, b) * 3;
	}

	CAGE_ASSERT(result.valid());
	return result;
}

void terrainMaterial(const Vec2 &pos2, Vec3 &color, Real &roughness, Real &metallic, bool rockOnly)
{
	static Holder<NoiseFunction> clouds1 = newClouds(3);
	static Holder<NoiseFunction> clouds2 = newClouds(2);
	static Holder<NoiseFunction> clouds3 = newClouds(3);
	static Holder<NoiseFunction> clouds4 = newClouds(3);
	static Holder<NoiseFunction> clouds5 = newClouds(3);
	static Holder<NoiseFunction> clouds6 = newClouds(3);
	static Holder<NoiseFunction> cell1 = newCell(NoiseOperationEnum::Subtract);
	static Holder<NoiseFunction> cell2 = newCell();
	static Holder<NoiseFunction> cell3 = newCell(NoiseOperationEnum::Distance2, NoiseDistanceEnum::Euclidean);
	static Holder<NoiseFunction> cell4 = newCell(NoiseOperationEnum::Distance2, NoiseDistanceEnum::Euclidean);
	static Holder<NoiseFunction> cell5 = newCell(NoiseOperationEnum::Distance2, NoiseDistanceEnum::Euclidean);
	static Holder<NoiseFunction> cell6 = newCell(NoiseOperationEnum::Subtract);
	static Holder<NoiseFunction> value1 = newValue();
	static Holder<NoiseFunction> value2 = newValue();
	static Holder<NoiseFunction> value3 = newValue();

	Vec3 pos = Vec3(pos2, terrainOffset(pos2));

	{ // base
		std::array<Real, 5> weights5 = basesWeights(pos);
		std::array<WeightIndex, 5> indices5;
		for (uint32 i = 0; i < 5; i++)
		{
			indices5[i].index = i;
			indices5[i].weight = weights5[i] + 1;
		}
		std::sort(std::begin(indices5), std::end(indices5), [](const WeightIndex &a, const WeightIndex &b) {
			return a.weight > b.weight;
		});
		{ // normalize
			Real l;
			for (uint32 i = 0; i < 5; i++)
				l += sqr(indices5[i].weight);
			l = 1 / sqrt(l);
			for (uint32 i = 0; i < 5; i++)
				indices5[i].weight *= l;
		}
		Vec2 w2 = normalize(Vec2(indices5[0].weight, indices5[1].weight));
		CAGE_ASSERT(w2[0] >= w2[1]);
		Real d = w2[0] - w2[1];
		Real f = clamp(rerange(d, 0, 0.1, 0.5, 0), 0, 0.5);
		Vec3 c[2]; Real r[2]; Real m[2];
		for (uint32 i = 0; i < 2; i++)
			basesSwitch(indices5[i].index, pos, c[i], r[i], m[i]);
		color = interpolate(c[0], c[1], f);
		roughness = interpolate(r[0], r[1], f);
		metallic = interpolate(m[0], m[1], f);
	}

	{ // small cracks
		Real f = evaluateClamp(cell1, pos * 0.187);
		Real m = evaluateClamp(clouds1, pos * 0.43);
		if (f < 0.02 && m < 0.5)
		{
			color *= 0.6;
			roughness *= 1.2;
		}
	}

	{ // white glistering spots
		if (evaluateClamp(cell2, pos * 0.084) > 0.95)
		{
			Real c = evaluateClamp(clouds2, pos * 3) * 0.2 + 0.8;
			color = Vec3(c);
			roughness = 0.2;
			metallic = 0.4;
		}
	}

	if (rockOnly)
		return;

	{ // large cracks
		Vec3 off = Vec3(evaluateClamp(cell3, pos * 0.1), evaluateClamp(cell4, pos * 0.1), evaluateClamp(cell5, pos * 0.1));
		Real f = evaluateClamp(cell6, pos * 0.034 + off * 0.23);
		Real m = evaluateClamp(clouds3, pos * 0.023);
		if (f < 0.015 && m < 0.4)
		{
			color *= 0.3;
			roughness *= 1.5;
		}
	}

	// positive -> up facing
	// negative -> down facing
	Real vn = (terrainOffset(pos2 + Vec2(0, -0.1)) - terrainOffset(pos2)) / 0.1;

	{ // large grass (on up facing surfaces)
		Real thr = evaluateClamp(clouds4, pos * 0.015);
		if (vn > thr + 0.2)
		{
			Real mask = evaluateClamp(clouds5, pos * 2.423);
			Real m = sharpEdge(mask);
			Vec3 grass = colorHsvToRgb(Vec3(
				evaluateClamp(value1, pos) * 0.3 + 0.13,
				evaluateClamp(value2, pos) * 0.2 + 0.5,
				evaluateClamp(value3, pos) * 0.2 + 0.5
			));
			Real r = evaluateClamp(clouds6, pos * 1.23) * 0.4 + 0.2;
			color = interpolate(color, grass, m);
			roughness = interpolate(roughness, r, m);
			metallic = interpolate(metallic, 0.01, m);
		}
	}
}

Quat sunLightOrientation(const Vec2 &playerPosition)
{
	return Quat(Degs(-50), Degs(sin(Degs(playerPosition[0] * 0.2 + 40)) * 70), Degs());
}

namespace
{
	class Initializer
	{
	public:
		Initializer()
		{
			// ensure consistent order of initialization of all the static noise functions
			Vec2 p2;
			Vec3 p3, c;
			Real r, m;
			for (uint32 i = 0; i < 5; i++)
				basesSwitch(i, p3, c, r, m);
			terrainOffset(p2);
			terrainMaterial(p2, c, r, m, false);
		}
	} initializer;
}
