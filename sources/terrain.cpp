#include "common.h"
#include "baseTile.h"

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

#include <vector>
#include <array>
#include <set>
#include <atomic>

std::set<TilePos> findNeededTiles(real tileLength, real range)
{
	std::set<TilePos> neededTiles;
	TilePos pt;
	pt.x = numeric_cast<sint32>(playerPosition[0] / tileLength);
	pt.y = numeric_cast<sint32>(playerPosition[1] / tileLength);
	TilePos r;
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

	enum class TileStateEnum
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

	struct Vertex
	{
		vec3 position;
		vec3 normal;
		//vec3 tangent;
		//vec3 bitangent;
		vec2 uv;
	};

	struct Tile
	{
		Holder<CollisionMesh> cpuCollider;
		std::vector<Vertex> cpuMesh;
		Holder<Mesh> gpuMesh;
		Holder<Texture> gpuAlbedo;
		Holder<Image> cpuAlbedo;
		Holder<Texture> gpuMaterial;
		Holder<Image> cpuMaterial;
		//Holder<Texture> gpuNormal;
		//Holder<Image> cpuNormal;
		Holder<RenderObject> gpuObject;
		TilePos pos;
		Entity *entity_;
		std::atomic<TileStateEnum> status;
		uint32 meshName;
		uint32 albedoName;
		uint32 materialName;
		//uint32 normalName;
		uint32 objectName;
		Tile() : status(TileStateEnum::Init), meshName(0), albedoName(0), materialName(0), /*normalName(0),*/ objectName(0)
		{}
		real distanceToPlayer() const
		{
			return pos.distanceToPlayer(tileLength);
		}
	};

	std::array<Tile, 256> tiles;
	bool stopping;

	/////////////////////////////////////////////////////////////////////////////
	// CONTROL
	/////////////////////////////////////////////////////////////////////////////

	bool engineUpdate()
	{
		std::set<TilePos> neededTiles = stopping ? std::set<TilePos>() : findNeededTiles(tileLength, 200);
		for (Tile &t : tiles)
		{
			// mark unneeded tiles
			if (t.status != TileStateEnum::Init)
				neededTiles.erase(t.pos);
			// remove tiles
			if (t.status == TileStateEnum::Ready && (t.distanceToPlayer() > distanceToUnloadTile || stopping))
			{
				removeTerrainCollider(t.objectName);
				t.cpuCollider.clear();
				t.entity_->destroy();
				t.entity_ = nullptr;
				t.status = TileStateEnum::Defabricate;
			}
			// create entity
			else if (t.status == TileStateEnum::Entity)
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
					t.entity_ = engineEntities()->createAnonymous();
					CAGE_COMPONENT_ENGINE(Transform, tr, t.entity_);
					tr.position = vec3(t.pos.x, t.pos.y, 0) * tileLength;
					CAGE_COMPONENT_ENGINE(Render, r, t.entity_);
					r.object = t.objectName;
				}
				t.status = TileStateEnum::Ready;
			}
		}
		// generate new needed tiles
		for (Tile &t : tiles)
		{
			if (neededTiles.empty())
				break;
			if (t.status == TileStateEnum::Init)
			{
				t.pos = *neededTiles.begin();
				neededTiles.erase(neededTiles.begin());
				t.status = TileStateEnum::Generate;
			}
		}

		if (!neededTiles.empty())
		{
			CAGE_LOG(SeverityEnum::Warning, "cragsman", "not enough terrain tile slots");
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

	bool engineTerrainAssets()
	{
		if (stopping)
			engineUpdate();
		for (Tile &t : tiles)
		{
			if (t.status == TileStateEnum::Fabricate)
			{
				t.albedoName = engineAssets()->generateUniqueName();
				t.materialName = engineAssets()->generateUniqueName();
				//t.normalName = engineAssets()->generateUniqueName();
				t.meshName = engineAssets()->generateUniqueName();
				t.objectName = engineAssets()->generateUniqueName();
				engineAssets()->fabricate(assetSchemeIndexTexture, t.albedoName, stringizer() + "albedo " + t.pos);
				engineAssets()->fabricate(assetSchemeIndexTexture, t.materialName, stringizer() + "material " + t.pos);
				//engineAssets()->fabricate(assetSchemeIndexTexture, t.normalName, stringizer() + "normal " + t.pos);
				engineAssets()->fabricate(assetSchemeIndexMesh, t.meshName, stringizer() + "mesh " + t.pos);
				engineAssets()->fabricate(assetSchemeIndexRenderObject, t.objectName, stringizer() + "object " + t.pos);
				engineAssets()->set<assetSchemeIndexTexture, Texture>(t.albedoName, t.gpuAlbedo.get());
				engineAssets()->set<assetSchemeIndexTexture, Texture>(t.materialName, t.gpuMaterial.get());
				//engineAssets()->set<assetSchemeIndexTexture, Texture>(t.normalName, t.gpuNormal.get());
				engineAssets()->set<assetSchemeIndexMesh, Mesh>(t.meshName, t.gpuMesh.get());
				engineAssets()->set<assetSchemeIndexRenderObject, RenderObject>(t.objectName, t.gpuObject.get());
				t.status = TileStateEnum::Entity;
			}
			else if (t.status == TileStateEnum::Defabricate)
			{
				engineAssets()->remove(t.albedoName);
				engineAssets()->remove(t.materialName);
				//engineAssets()->remove(t.normalName);
				engineAssets()->remove(t.meshName);
				engineAssets()->remove(t.objectName);
				t.albedoName = 0;
				t.materialName = 0;
				//t.normalName = 0;
				t.meshName = 0;
				t.objectName = 0;
				t.status = TileStateEnum::Unload1;
			}
		}
		return false;
	}

	/////////////////////////////////////////////////////////////////////////////
	// DISPATCH
	/////////////////////////////////////////////////////////////////////////////

	Holder<Texture> dispatchTexture(Holder<Image> &Image)
	{
		Holder<Texture> t = newTexture();
		switch (Image->channels())
		{
		case 2:
			t->image2d(Image->width(), Image->height(), GL_RG8, GL_RG, GL_UNSIGNED_BYTE, Image->bufferData());
			break;
		case 3:
			t->image2d(Image->width(), Image->height(), GL_SRGB8, GL_RGB, GL_UNSIGNED_BYTE, Image->bufferData());
			break;
		}
		t->filters(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, 100);
		t->wraps(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
		t->generateMipmaps();
		Image.clear();
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

	Holder<Mesh> dispatchMesh(std::vector<Vertex> &vertices)
	{
		Holder<Mesh> m = newMesh();
		MeshHeader::MaterialData material;
		const std::vector<uint32> &indices = meshIndices();
		m->setBuffers(numeric_cast<uint32>(vertices.size()), sizeof(Vertex), vertices.data(), numeric_cast<uint32>(indices.size()), indices.data(), sizeof(material), &material);
		m->setPrimitiveType(GL_TRIANGLES);
		m->setAttribute(CAGE_SHADER_ATTRIB_IN_POSITION, 3, GL_FLOAT, sizeof(Vertex), 0);
		m->setAttribute(CAGE_SHADER_ATTRIB_IN_NORMAL, 3, GL_FLOAT, sizeof(Vertex), 12);
		m->setAttribute(CAGE_SHADER_ATTRIB_IN_UV, 2, GL_FLOAT, sizeof(Vertex), 24);
		real l = tileLength * 0.5;
		m->setBoundingBox(aabb(vec3(-l, -l, -real::Infinity()), vec3(l, l, real::Infinity())));
		std::vector<Vertex>().swap(vertices);
		return m;
	}

	Holder<RenderObject> dispatchObject()
	{
		Holder<RenderObject> o = newRenderObject();
		return o;
	}

	bool engineDispatch()
	{
		CAGE_CHECK_GL_ERROR_DEBUG();
		for (Tile &t : tiles)
		{
			if (t.status == TileStateEnum::Unload1)
			{
				t.status = TileStateEnum::Unload2;
			}
			else if (t.status == TileStateEnum::Unload2)
			{
				t.gpuAlbedo.clear();
				t.gpuMaterial.clear();
				t.gpuMesh.clear();
				//t.gpuNormal.clear();
				t.gpuObject.clear();
				t.status = TileStateEnum::Init;
			}
		}
		for (Tile &t : tiles)
		{
			if (t.status == TileStateEnum::Upload)
			{
				t.gpuAlbedo = dispatchTexture(t.cpuAlbedo);
				t.gpuMaterial = dispatchTexture(t.cpuMaterial);
				//t.gpuNormal = dispatchTexture(t.cpuNormal);
				t.gpuMesh = dispatchMesh(t.cpuMesh);
				t.gpuObject = dispatchObject();
				t.status = TileStateEnum::Fabricate;
				break;
			}
		}
		CAGE_CHECK_GL_ERROR_DEBUG();
		return false;
	}

	/////////////////////////////////////////////////////////////////////////////
	// GENERATOR
	/////////////////////////////////////////////////////////////////////////////

	Tile *generatorChooseTile()
	{
		static Holder<Mutex> mut = newMutex();
		ScopeLock<Mutex> lock(mut);
		Tile *result = nullptr;
		for (Tile &t : tiles)
		{
			if (t.status != TileStateEnum::Generate)
				continue;
			real d = t.distanceToPlayer();
			if (d > distanceToUnloadTile)
			{
				t.status = TileStateEnum::Init;
				continue;
			}
			if (result && t.distanceToPlayer() > result->distanceToPlayer())
				continue;
			result = &t;
		}
		if (result)
			result->status = TileStateEnum::Generating;
		return result;
	}

	void generateMesh(Tile &t)
	{
		static const real pwoa = tileLength / (tileMeshResolution - 1);
		static const vec2 pwox = vec2(pwoa, 0);
		static const vec2 pwoy = vec2(0, pwoa);
		t.cpuMesh.reserve(tileMeshResolution * tileMeshResolution);
		for (uint32 y = 0; y < tileMeshResolution; y++)
		{
			for (uint32 x = 0; x < tileMeshResolution; x++)
			{
				Vertex v;
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

	void generateCollider(Tile &t)
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

	void initializeTexture(Holder<Image> &img, uint32 components)
	{
		img = newImage();
		img->empty(tileTextureResolution, tileTextureResolution, components);
	}

	void generateTextures(Tile &t)
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
			Tile *t = generatorChooseTile();
			if (!t)
			{
				threadSleep(10000);
				continue;
			}
			generateMesh(*t);
			generateCollider(*t);
			generateTextures(*t);
			t->status = TileStateEnum::Upload;
		}
	}

	/////////////////////////////////////////////////////////////////////////////
	// INITIALIZE
	/////////////////////////////////////////////////////////////////////////////

	std::vector<Holder<Thread>> generatorThreads;

	class Callbacks
	{
		EventListener<bool()> engineUpdateListener;
		EventListener<bool()> engineAssetsListener;
		EventListener<bool()> engineFinalizeListener;
		EventListener<bool()> engineDispatchListener;
	public:
		Callbacks()
		{
			engineUpdateListener.attach(controlThread().update);
			engineUpdateListener.bind<&engineUpdate>();
			engineAssetsListener.attach(controlThread().assets);
			engineAssetsListener.bind<&engineTerrainAssets>();
			engineFinalizeListener.attach(controlThread().finalize);
			engineFinalizeListener.bind<&engineFinalize>();
			engineDispatchListener.attach(graphicsDispatchThread().render);
			engineDispatchListener.bind<&engineDispatch>();

			uint32 cpuCount = max(processorsCount(), 2u) - 1;
			for (uint32 i = 0; i < cpuCount; i++)
				generatorThreads.push_back(newThread(Delegate<void()>().bind<&generatorEntry>(), stringizer() + "generator " + i));
		}
	} callbacksInstance;
}
