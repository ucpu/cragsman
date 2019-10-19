#include <vector>
#include <array>
#include <set>
#include <atomic>

#include "common.h"
#include "baseTile.h"

#include <cage-core/log.h>
#include <cage-core/entities.h>
#include <cage-core/geometry.h>
#include <cage-core/concurrent.h>
#include <cage-core/assetManager.h>
#include <cage-core/memoryBuffer.h>
#include <cage-core/image.h>
#include <cage-core/collisionMesh.h>

#include <cage-engine/core.h>
#include <cage-engine/engine.h>
#include <cage-engine/graphics.h>
#include <cage-engine/opengl.h>
#include <cage-engine/assetStructs.h>
#include <cage-engine/graphics/shaderConventions.h>

std::set<tilePosStruct> findNeededTiles(real tileLength, real range)
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
			if (r.distanceToPlayer(tileLength) < range)
				neededTiles.insert(r);
		}
	}
	return neededTiles;
}

namespace
{
	const real tileLength = 30; // real world size of a tile (in 1 dimension)
	const uint32 tileMeshResolution = 40; // number of vertices (in 1 dimension)
	const uint32 tileTextureResolution = 128; // number of texels (in 1 dimension)
	const real distanceToUnloadTile = 300;

	enum class tileStatusEnum
	{
		Init,
		Generate,
		Generating,
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

	struct tileStruct
	{
		holder<collisionMesh> cpuCollider;
		std::vector<vertexStruct> cpuMesh;
		holder<renderMesh> gpuMesh;
		holder<renderTexture> gpuAlbedo;
		holder<image> cpuAlbedo;
		holder<renderTexture> gpuMaterial;
		holder<image> cpuMaterial;
		//holder<renderTexture> gpuNormal;
		//holder<image> cpuNormal;
		holder<renderObject> gpuObject;
		tilePosStruct pos;
		entity *entity_;
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
			return pos.distanceToPlayer(tileLength);
		}
	};

	std::array<tileStruct, 256> tiles;
	bool stopping;

	/////////////////////////////////////////////////////////////////////////////
	// CONTROL
	/////////////////////////////////////////////////////////////////////////////

	bool engineUpdate()
	{
		std::set<tilePosStruct> neededTiles = stopping ? std::set<tilePosStruct>() : findNeededTiles(tileLength, 200);
		for (tileStruct &t : tiles)
		{
			// mark unneeded tiles
			if (t.status != tileStatusEnum::Init)
				neededTiles.erase(t.pos);
			// remove tiles
			if (t.status == tileStatusEnum::Ready && (t.distanceToPlayer() > distanceToUnloadTile || stopping))
			{
				removeTerrainCollider(t.objectName);
				t.cpuCollider.clear();
				t.entity_->destroy();
				t.entity_ = nullptr;
				t.status = tileStatusEnum::Defabricate;
			}
			// create entity
			else if (t.status == tileStatusEnum::Entity)
			{
				addTerrainCollider(t.objectName, t.cpuCollider.get());
				{ // set texture names for the mesh
					uint32 textures[MaxTexturesCountPerMaterial];
					detail::memset(textures, 0, sizeof(textures));
					textures[0] = t.albedoName;
					textures[1] = t.materialName;
					//textures[2] = t.normalName;
					t.gpuMesh->setTextureNames(textures);
				}
				{ // set object properties
					float thresholds[1] = { 0 };
					uint32 meshIndices[2] = { 0, 1 };
					uint32 meshNames[1] = { t.meshName };
					t.gpuObject->setLods(1, 1, thresholds, meshIndices, meshNames);
				}
				{ // create the entity
					t.entity_ = entities()->createAnonymous();
					CAGE_COMPONENT_ENGINE(transform, tr, t.entity_);
					tr.position = vec3(t.pos.x, t.pos.y, 0) * tileLength;
					CAGE_COMPONENT_ENGINE(render, r, t.entity_);
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

		if (!neededTiles.empty())
		{
			CAGE_LOG(severityEnum::Warning, "cragsman", "not enough terrain tile slots");
			detail::debugBreakpoint();
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
		if (stopping)
			engineUpdate();
		for (tileStruct &t : tiles)
		{
			if (t.status == tileStatusEnum::Fabricate)
			{
				t.albedoName = assets()->generateUniqueName();
				t.materialName = assets()->generateUniqueName();
				//t.normalName = assets()->generateUniqueName();
				t.meshName = assets()->generateUniqueName();
				t.objectName = assets()->generateUniqueName();
				assets()->fabricate(assetSchemeIndexRenderTexture, t.albedoName, string() + "albedo " + t.pos);
				assets()->fabricate(assetSchemeIndexRenderTexture, t.materialName, string() + "material " + t.pos);
				//assets()->fabricate(assetSchemeIndexRenderTexture, t.normalName, string() + "normal " + t.pos);
				assets()->fabricate(assetSchemeIndexMesh, t.meshName, string() + "mesh " + t.pos);
				assets()->fabricate(assetSchemeIndexRenderObject, t.objectName, string() + "object " + t.pos);
				assets()->set<assetSchemeIndexRenderTexture, renderTexture>(t.albedoName, t.gpuAlbedo.get());
				assets()->set<assetSchemeIndexRenderTexture, renderTexture>(t.materialName, t.gpuMaterial.get());
				//assets()->set<assetSchemeIndexRenderTexture, renderTexture>(t.normalName, t.gpuNormal.get());
				assets()->set<assetSchemeIndexMesh, renderMesh>(t.meshName, t.gpuMesh.get());
				assets()->set<assetSchemeIndexRenderObject, renderObject>(t.objectName, t.gpuObject.get());
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

	holder<renderTexture> dispatchTexture(holder<image> &image)
	{
		holder<renderTexture> t = newRenderTexture();
		switch (image->channels())
		{
		case 2:
			t->image2d(image->width(), image->height(), GL_RG8, GL_RG, GL_UNSIGNED_BYTE, image->bufferData());
			break;
		case 3:
			t->image2d(image->width(), image->height(), GL_SRGB8, GL_RGB, GL_UNSIGNED_BYTE, image->bufferData());
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

	holder<renderMesh> dispatchMesh(std::vector<vertexStruct> &vertices)
	{
		holder<renderMesh> m = newRenderMesh();
		renderMeshHeader::materialData material;
		const std::vector<uint32> &indices = meshIndices();
		m->setBuffers(numeric_cast<uint32>(vertices.size()), sizeof(vertexStruct), vertices.data(), numeric_cast<uint32>(indices.size()), indices.data(), sizeof(material), &material);
		m->setPrimitiveType(GL_TRIANGLES);
		m->setAttribute(CAGE_SHADER_ATTRIB_IN_POSITION, 3, GL_FLOAT, sizeof(vertexStruct), 0);
		m->setAttribute(CAGE_SHADER_ATTRIB_IN_NORMAL, 3, GL_FLOAT, sizeof(vertexStruct), 12);
		m->setAttribute(CAGE_SHADER_ATTRIB_IN_UV, 2, GL_FLOAT, sizeof(vertexStruct), 24);
		real l = tileLength * 0.5;
		m->setBoundingBox(aabb(vec3(-l, -l, -real::Infinity()), vec3(l, l, real::Infinity())));
		std::vector<vertexStruct>().swap(vertices);
		return m;
	}

	holder<renderObject> dispatchObject()
	{
		holder<renderObject> o = newRenderObject();
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
		static holder<syncMutex> mut = newSyncMutex();
		scopeLock<syncMutex> lock(mut);
		tileStruct *result = nullptr;
		for (tileStruct &t : tiles)
		{
			if (t.status != tileStatusEnum::Generate)
				continue;
			real d = t.distanceToPlayer();
			if (d > distanceToUnloadTile)
			{
				t.status = tileStatusEnum::Init;
				continue;
			}
			if (result && t.distanceToPlayer() > result->distanceToPlayer())
				continue;
			result = &t;
		}
		if (result)
			result->status = tileStatusEnum::Generating;
		return result;
	}

	void generateMesh(tileStruct &t)
	{
		static const real pwoa = tileLength / (tileMeshResolution - 1);
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
				v.normal = normalize(vec3(-tox, -toy, 0.1));
				t.cpuMesh.push_back(v);
			}
		}
	}

	void generateCollider(tileStruct &t)
	{
		transform m = transform(vec3(t.pos.x, t.pos.y, 0) * tileLength);
		t.cpuCollider = newCollisionMesh();
		const std::vector<uint32> &ids = meshIndices();
		uint32 cnt = numeric_cast<uint32>(ids.size() / 3);
		for (uint32 i = 0; i < cnt; i++)
		{
			triangle tr(
				t.cpuMesh[ids[i * 3 + 0]].position,
				t.cpuMesh[ids[i * 3 + 1]].position,
				t.cpuMesh[ids[i * 3 + 2]].position
			);
			t.cpuCollider->addTriangle(tr * m);
		}
		t.cpuCollider->rebuild();
	}

	void initializeTexture(holder<image> &img, uint32 components)
	{
		img = newImage();
		img->empty(tileTextureResolution, tileTextureResolution, components);
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
				terrainMaterial(pw, color, roughness, metallic, false);
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
			generateCollider(*t);
			generateTextures(*t);
			t->status = tileStatusEnum::Upload;
		}
	}

	/////////////////////////////////////////////////////////////////////////////
	// INITIALIZE
	/////////////////////////////////////////////////////////////////////////////

	std::vector<holder<threadHandle>> generatorThreads;

	class callbacksInitClass
	{
		eventListener<bool()> engineUpdateListener;
		eventListener<bool()> engineAssetsListener;
		eventListener<bool()> engineFinalizeListener;
		eventListener<bool()> engineDispatchListener;
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

			uint32 cpuCount = max(processorsCount(), 2u) - 1;
			for (uint32 i = 0; i < cpuCount; i++)
				generatorThreads.push_back(newThread(delegate<void()>().bind<&generatorEntry>(), string() + "generator " + i));
		}
	} callbacksInitInstance;
}
