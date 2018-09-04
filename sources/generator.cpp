#include <array>
#include <atomic>
#include <vector>
#include <set>

#include "common.h"

#include <cage-core/log.h>
#include <cage-core/entities.h>
#include <cage-core/geometry.h>
#include <cage-core/concurrent.h>
#include <cage-core/assets.h>
#include <cage-core/utility/memoryBuffer.h>
#include <cage-core/utility/png.h>
#include <cage-core/utility/noise.h>
#include <cage-core/utility/color.h>

#include <cage-client/core.h>
#include <cage-client/engine.h>
#include <cage-client/graphics.h>
#include <cage-client/opengl.h>
#include <cage-client/assetStructs.h>
#include <cage-client/graphics/shaderConventions.h>

real terrainOffset(const vec2 &position)
{
	real lowf = noiseClouds(1, position * 0.008);
	real medf = noiseCell(2, position * 0.021, 3);
	vec2 off = vec2(noiseClouds(3, position * 0.1), noiseClouds(4, position * 0.1));
	real highf = noiseClouds(5, position * 0.1 + off);
	return lowf * 23 + medf * 7 + highf;
}

namespace
{
	const real tileLength = 20; // real world size of a tile (in 1 dimension)
	const uint32 tileMeshResolution = 30; // number of vertices (in 1 dimension)
	const uint32 tileTextureResolution = 128; // number of texels (in 1 dimension)

	eventListener<bool()> engineUpdateListener;
	eventListener<bool()> engineAssetsListener;
	eventListener<bool()> engineFinalizeListener;
	eventListener<bool()> engineDispatchListener;
	vec2 playerPosition;
	bool stopping;

	enum class tileStatusEnum
	{
		Init,
		Generate,
		Upload,
		Fabricate,
		Entity,
		Ready,
		Defabricate,
		Unload1,
		Unload2,
	};

	struct vertexStruct
	{
		vec3 position;
		vec3 normal;
		//vec3 tangent;
		//vec3 bitangent;
		vec2 uv;
	};

	struct tilePosStruct
	{
		sint32 x, y;
		tilePosStruct() : x(0), y(0)
		{}
		bool operator < (const tilePosStruct &other) const
		{
			if (y == other.y)
				return x < other.x;
			return y < other.y;
		}
		real distanceToPlayer() const
		{
			return distance(vec2(x, y) * tileLength, playerPosition);
		}
		operator string () const { return string() + x + " " + y; }
	};

	struct tileStruct
	{
		std::vector<vertexStruct> cpuMesh;
		holder<meshClass> gpuMesh;
		holder<textureClass> gpuAlbedo;
		holder<pngImageClass> cpuAlbedo;
		holder<textureClass> gpuMaterial;
		holder<pngImageClass> cpuMaterial;
		//holder<textureClass> gpuNormal;
		//holder<pngImageClass> cpuNormal;
		holder<objectClass> gpuObject;
		tilePosStruct pos;
		entityClass *entity;
		std::atomic<tileStatusEnum> status;
		uint32 meshName;
		uint32 albedoName;
		uint32 materialName;
		//uint32 normalName;
		uint32 objectName;
		tileStruct() : status(tileStatusEnum::Init), meshName(0), albedoName(0), materialName(0), /*normalName(0),*/ objectName(0)
		{}
		real distanceToPlayer() const
		{
			return pos.distanceToPlayer();
		}
	};

	std::array<tileStruct, 1024> tiles;

	/////////////////////////////////////////////////////////////////////////////
	// CONTROL
	/////////////////////////////////////////////////////////////////////////////

	void updatePlayerPosition()
	{
		if (!entities()->hasEntity(characterBody))
			return;
		ENGINE_GET_COMPONENT(transform, t, entities()->getEntity(characterBody));
		playerPosition = vec2(t.position);
	}

	std::set<tilePosStruct> findNeededTiles()
	{
		std::set<tilePosStruct> neededTiles;
		tilePosStruct pt;
		pt.x = numeric_cast<sint32>(playerPosition[0] / tileLength);
		pt.y = numeric_cast<sint32>(playerPosition[1] / tileLength);
		tilePosStruct r;
		for (r.y = pt.y - 10; r.y <= pt.y + 10; r.y++)
		{
			for (r.x = pt.x - 10; r.x <= pt.x + 10; r.x++)
			{
				if (r.distanceToPlayer() < 200)
					neededTiles.insert(r);
			}
		}
		return neededTiles;
	}

	bool engineUpdate()
	{
		updatePlayerPosition();
		std::set<tilePosStruct> neededTiles = findNeededTiles();
		for (tileStruct &t : tiles)
		{
			// mark unneeded tiles
			if (t.status != tileStatusEnum::Init)
				neededTiles.erase(t.pos);
			// remove tiles
			if (t.status == tileStatusEnum::Ready && t.distanceToPlayer() > 300)
			{
				t.entity->destroy();
				t.entity = nullptr;
				t.status = tileStatusEnum::Defabricate;
			}
			// create entity
			else if (t.status == tileStatusEnum::Entity)
			{
				{ // set texture names for the mesh
					uint32 textures[MaxTexturesCountPerMaterial];
					detail::memset(textures, 0, sizeof(textures));
					textures[0] = t.albedoName;
					textures[1] = t.materialName;
					//textures[2] = t.normalName;
					t.gpuMesh->setTextures(textures);
				}
				{ // set object properties
					t.gpuObject->setLodLevels(1);
					t.gpuObject->setLodMeshes(0, 1);
					t.gpuObject->setMeshName(0, 0, t.meshName);
				}
				{ // create the entity
					t.entity = entities()->newAnonymousEntity();
					ENGINE_GET_COMPONENT(transform, tr, t.entity);
					tr.position = vec3(t.pos.x, t.pos.y, 0) * tileLength;
					ENGINE_GET_COMPONENT(render, r, t.entity);
					r.object = t.objectName;
				}
				t.status = tileStatusEnum::Ready;
			}
		}
		// generate new needed tiles
		for (tileStruct &t : tiles)
		{
			if (neededTiles.empty())
				break;
			if (t.status == tileStatusEnum::Init)
			{
				t.pos = *neededTiles.begin();
				neededTiles.erase(neededTiles.begin());
				t.status = tileStatusEnum::Generate;
			}
		}
		return false;
	}

	bool engineFinalize()
	{
		stopping = true;
		return false;
	}

	/////////////////////////////////////////////////////////////////////////////
	// ASSETS
	/////////////////////////////////////////////////////////////////////////////

	bool engineAssets()
	{
		for (tileStruct &t : tiles)
		{
			if (t.status == tileStatusEnum::Fabricate)
			{
				t.albedoName = assets()->generateUniqueName();
				t.materialName = assets()->generateUniqueName();
				//t.normalName = assets()->generateUniqueName();
				t.meshName = assets()->generateUniqueName();
				t.objectName = assets()->generateUniqueName();
				assets()->fabricate(assetSchemeIndexTexture, t.albedoName, string() + "albedo " + t.pos);
				assets()->fabricate(assetSchemeIndexTexture, t.materialName, string() + "material " + t.pos);
				//assets()->fabricate(assetSchemeIndexTexture, t.normalName, string() + "normal " + t.pos);
				assets()->fabricate(assetSchemeIndexMesh, t.meshName, string() + "mesh " + t.pos);
				assets()->fabricate(assetSchemeIndexObject, t.objectName, string() + "object " + t.pos);
				assets()->set<assetSchemeIndexTexture, textureClass>(t.albedoName, t.gpuAlbedo.get());
				assets()->set<assetSchemeIndexTexture, textureClass>(t.materialName, t.gpuMaterial.get());
				//assets()->set<assetSchemeIndexTexture, textureClass>(t.normalName, t.gpuNormal.get());
				assets()->set<assetSchemeIndexMesh, meshClass>(t.meshName, t.gpuMesh.get());
				assets()->set<assetSchemeIndexObject, objectClass>(t.objectName, t.gpuObject.get());
				t.status = tileStatusEnum::Entity;
				break;
			}
			else if (t.status == tileStatusEnum::Defabricate)
			{
				assets()->remove(t.albedoName);
				assets()->remove(t.materialName);
				//assets()->remove(t.normalName);
				assets()->remove(t.meshName);
				assets()->remove(t.objectName);
				t.albedoName = 0;
				t.materialName = 0;
				//t.normalName = 0;
				t.meshName = 0;
				t.objectName = 0;
				t.status = tileStatusEnum::Unload1;
				break;
			}
		}
		return false;
	}

	/////////////////////////////////////////////////////////////////////////////
	// DISPATCH
	/////////////////////////////////////////////////////////////////////////////

	holder<textureClass> dispatchTexture(holder<pngImageClass> &image)
	{
		holder<textureClass> t = newTexture(window());
		switch (image->channels())
		{
		case 2:
			t->image2d(image->width(), image->height(), GL_RG8, GL_RG, GL_UNSIGNED_BYTE, image->bufferData());
			break;
		case 3:
			t->image2d(image->width(), image->height(), GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, image->bufferData());
			break;
		}
		t->filters(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, 100);
		t->wraps(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
		t->generateMipmaps();
		image.clear();
		return t;
	}

	std::vector<uint32> initializeMeshIndices()
	{
		uint32 r = tileMeshResolution;
		std::vector<uint32> v;
		v.reserve((r - 1) * (r - 1) * 2 * 3);
		for (uint32 y = 1; y < r; y++)
		{
			for (uint32 x = 1; x < r; x++)
			{
				uint32 a = y * r + x;
				uint32 b = a - r;
				uint32 c = b - 1;
				uint32 d = a - 1;
				v.push_back(d); v.push_back(c); v.push_back(b);
				v.push_back(d); v.push_back(b); v.push_back(a);
			}
		}
		return v;
	}

	const std::vector<uint32> &meshIndices()
	{
		static const std::vector<uint32> indices = initializeMeshIndices();
		return indices;
	}

	holder<meshClass> dispatchMesh(std::vector<vertexStruct> &vertices)
	{
		holder<meshClass> m = newMesh(window());
		meshHeaderStruct::materialDataStruct material;
		material.albedoMult = material.specialMult = vec4(1, 1, 1, 1);
		const std::vector<uint32> &indices = meshIndices();
		m->setBuffers(numeric_cast<uint32>(vertices.size()), sizeof(vertexStruct), vertices.data(), numeric_cast<uint32>(indices.size()), indices.data(), sizeof(material), &material);
		m->setPrimitiveType(GL_TRIANGLES);
		m->setAttribute(CAGE_SHADER_ATTRIB_IN_POSITION, 3, GL_FLOAT, sizeof(vertexStruct), 0);
		m->setAttribute(CAGE_SHADER_ATTRIB_IN_NORMAL, 3, GL_FLOAT, sizeof(vertexStruct), 12);
		m->setAttribute(CAGE_SHADER_ATTRIB_IN_UV, 2, GL_FLOAT, sizeof(vertexStruct), 24);
		real l = tileLength * 0.5;
		m->setBoundingBox(aabb(vec3(-l, -l, real::NegativeInfinity), vec3(l, l, real::PositiveInfinity)));
		m->setFlags(meshFlags::DepthTest | meshFlags::DepthWrite | meshFlags::Lighting | meshFlags::Normals | meshFlags::ShadowCast | meshFlags::Uvs);
		std::vector<vertexStruct>().swap(vertices);
		return m;
	}

	holder<objectClass> dispatchObject()
	{
		holder<objectClass> o = newObject();
		return o;
	}

	bool engineDispatch()
	{
		CAGE_CHECK_GL_ERROR_DEBUG();
		for (tileStruct &t : tiles)
		{
			if (t.status == tileStatusEnum::Unload1)
			{
				t.status = tileStatusEnum::Unload2;
			}
			else if (t.status == tileStatusEnum::Unload2)
			{
				t.gpuAlbedo.clear();
				t.gpuMaterial.clear();
				t.gpuMesh.clear();
				//t.gpuNormal.clear();
				t.gpuObject.clear();
				t.status = tileStatusEnum::Init;
			}
		}
		for (tileStruct &t : tiles)
		{
			if (t.status == tileStatusEnum::Upload)
			{
				t.gpuAlbedo = dispatchTexture(t.cpuAlbedo);
				t.gpuMaterial = dispatchTexture(t.cpuMaterial);
				//t.gpuNormal = dispatchTexture(t.cpuNormal);
				t.gpuMesh = dispatchMesh(t.cpuMesh);
				t.gpuObject = dispatchObject();
				t.status = tileStatusEnum::Fabricate;
				break;
			}
		}
		CAGE_CHECK_GL_ERROR_DEBUG();
		return false;
	}

	/////////////////////////////////////////////////////////////////////////////
	// GENERATOR
	/////////////////////////////////////////////////////////////////////////////

	tileStruct *generatorChooseTile()
	{
		tileStruct *result = nullptr;
		for (tileStruct &t : tiles)
		{
			if (t.status != tileStatusEnum::Generate)
				continue;
			if (result && t.distanceToPlayer() > result->distanceToPlayer())
				continue;
			result = &t;
		}
		return result;
	}

	void generateMesh(tileStruct &t)
	{
		static const real pwoa = tileLength / (tileMeshResolution - 1) * 0.1;
		static const vec2 pwox = vec2(pwoa, 0);
		static const vec2 pwoy = vec2(0, pwoa);
		t.cpuMesh.reserve(tileMeshResolution * tileMeshResolution);
		for (uint32 y = 0; y < tileMeshResolution; y++)
		{
			for (uint32 x = 0; x < tileMeshResolution; x++)
			{
				vertexStruct v;
				v.uv[0] = real(x) / (tileMeshResolution - 1);
				v.uv[1] = real(y) / (tileMeshResolution - 1);
				vec2 pt = (v.uv - 0.5) * tileLength;
				vec2 pw = vec2(t.pos.x, t.pos.y) * tileLength + pt;
				v.position = vec3(pt, terrainOffset(pw));
				real tox = terrainOffset(pw + pwox) - v.position[2];
				real toy = terrainOffset(pw + pwoy) - v.position[2];
				v.normal = vec3(-tox, -toy, 0.1).normalize();
				t.cpuMesh.push_back(v);
			}
		}
	}

	void initializeTexture(holder<pngImageClass> &img, uint32 components)
	{
		img = newPngImage();
		img->empty(tileTextureResolution, tileTextureResolution, components);
	}

	void material(const vec2 &pos, vec3 &color, real &roughness, real &metallic)
	{
		// base color
		{
			vec2 off = vec2(noiseCell(41, pos * 0.063, 2), noiseCell(43, pos * 0.063, 2));
			if (noiseClouds(42, pos * 0.097 + off * 0.2) < 0.6)
			{ // rock 1
				color = convertHsvToRgb(vec3(
					noiseClouds(65, pos * 0.134, 5) * 0.01 + 0.08,
					noiseClouds(66, pos * 0.344, 5) * 0.2 + 0.2,
					noiseClouds(67, pos * 0.100, 5) * 0.4 + 0.55
				));
				roughness = 0.8;
				metallic = 0.002;
			}
			else
			{ // rock 2
				color = convertHsvToRgb(vec3(
					noiseClouds(55, pos * 0.321, 5) * 0.02 + 0.094,
					noiseClouds(56, pos * 0.258, 5) * 0.3 + 0.08,
					noiseClouds(57, pos * 0.369, 5) * 0.2 + 0.59
				));
				roughness = 0.6;
				metallic = 0.049;
			}
		}

		// small cracks
		{
			real f1 = noiseCell(27, pos * 0.187, 1);
			real f2 = noiseCell(27, pos * 0.187, 2);
			real f = f2 - f1;
			real m = noiseClouds(28, pos * 0.43);
			if (f < 0.01 && m < 0.5)
			{
				color *= 0.6;
				roughness *= 1.2;
			}
		}

		// white glistering spots
		if (noiseCell(20, pos * 0.234, 1) > 0.52)
		{
			real c = noiseClouds(23, pos * 3, 2) * 0.7 + 0.5;
			color = vec3(c, c, c);
			roughness = 0.05;
			metallic = 0.97;
		}

		// large cracks
		{
			vec2 off = vec2(noiseCell(24, pos * 0.1, 2), noiseCell(23, pos * 0.1, 2));
			real f1 = noiseCell(21, pos * 0.034 + off * 0.23, 1);
			real f2 = noiseCell(21, pos * 0.034 + off * 0.23, 2);
			real f = f2 - f1;
			real m = noiseClouds(25, pos * 0.023);
			if (f < 0.015 && m < 0.4)
			{
				color *= 0.3;
				roughness *= 1.5;
			}
		}
	}

	void generateTextures(tileStruct &t)
	{
		initializeTexture(t.cpuAlbedo, 3);
		initializeTexture(t.cpuMaterial, 2);
		for (uint32 y = 0; y < tileTextureResolution; y++)
		{
			for (uint32 x = 0; x < tileTextureResolution; x++)
			{
				vec2 pw = (vec2(t.pos.x, t.pos.y) + (vec2(x, y) + 0.5) / tileTextureResolution - 0.5) * tileLength;
				vec3 color; real roughness; real metallic;
				material(pw, color, roughness, metallic);
				for (uint32 i = 0; i < 3; i++)
					t.cpuAlbedo->value(x, y, i, color[i].value);
				t.cpuMaterial->value(x, y, 0, roughness.value);
				t.cpuMaterial->value(x, y, 1, metallic.value);
			}
		}
	}

	void generatorEntry()
	{
		while (!stopping)
		{
			tileStruct *t = generatorChooseTile();
			if (!t)
			{
				threadSleep(10000);
				continue;
			}
			generateMesh(*t);
			generateTextures(*t);
			t->status = tileStatusEnum::Upload;
		}
	}

	/////////////////////////////////////////////////////////////////////////////
	// INITIALIZE
	/////////////////////////////////////////////////////////////////////////////

	class callbacksInitClass
	{
	public:
		callbacksInitClass()
		{
			engineUpdateListener.attach(controlThread().update);
			engineUpdateListener.bind<&engineUpdate>();
			engineAssetsListener.attach(controlThread().assets);
			engineAssetsListener.bind<&engineAssets>();
			engineFinalizeListener.attach(controlThread().finalize);
			engineFinalizeListener.bind<&engineFinalize>();
			engineDispatchListener.attach(graphicsDispatchThread().render);
			engineDispatchListener.bind<&engineDispatch>();
		}
	} callbacksInitInstance;

	holder<threadClass> generatorThread = newThread(delegate<void()>().bind<&generatorEntry>(), "generator");
}