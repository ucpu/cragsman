#include <cage-core/entities.h>
#include <cage-core/geometry.h>
#include <cage-core/concurrent.h>
#include <cage-core/assetManager.h>
#include <cage-core/memoryBuffer.h>
#include <cage-core/image.h>
#include <cage-core/collisionMesh.h>
#include <cage-core/threadPool.h>
#include <cage-core/debug.h>

#include <cage-engine/engine.h>
#include <cage-engine/graphics.h>
#include <cage-engine/opengl.h>
#include <cage-engine/assetStructs.h>
#include <cage-engine/shaderConventions.h>

#include "common.h"
#include "baseTile.h"

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
		Entity,
		Ready,
	};

	struct Vertex
	{
		vec3 position;
		vec3 normal;
		vec2 uv;
	};

	struct TileBase
	{
		Holder<CollisionMesh> cpuCollider;
		std::vector<Vertex> cpuMesh;
		Holder<Mesh> gpuMesh;
		Holder<Texture> gpuAlbedo;
		Holder<Image> cpuAlbedo;
		Holder<Texture> gpuMaterial;
		Holder<Image> cpuMaterial;
		Holder<RenderObject> renderObject;
		TilePos pos;
		Entity *entity = nullptr;
		uint32 meshName = 0;
		uint32 albedoName = 0;
		uint32 materialName = 0;
		uint32 objectName = 0;

		real distanceToPlayer() const
		{
			return pos.distanceToPlayer(tileLength);
		}
	};

	struct Tile : public TileBase
	{
		std::atomic<TileStateEnum> status{ TileStateEnum::Init };
	};

	std::vector<Holder<Thread>> generatorThreads;
	std::array<Tile, 256> tiles;
	std::atomic<bool> stopping;

	/////////////////////////////////////////////////////////////////////////////
	// CONTROL
	/////////////////////////////////////////////////////////////////////////////

	void engineUpdate()
	{
		AssetManager *ass = engineAssets();
		std::set<TilePos> neededTiles = stopping ? std::set<TilePos>() : findNeededTiles(tileLength, 200);
		for (Tile &t : tiles)
		{
			// mark unneeded tiles
			if (t.status != TileStateEnum::Init)
				neededTiles.erase(t.pos);

			// remove tiles
			if (t.status == TileStateEnum::Ready && (t.distanceToPlayer() > distanceToUnloadTile || stopping))
			{
				ass->remove(t.meshName);
				ass->remove(t.albedoName);
				ass->remove(t.materialName);
				ass->remove(t.objectName);
				removeTerrainCollider(t.objectName);
				t.entity->destroy();
				(TileBase&)t = TileBase();
				t.status = TileStateEnum::Init;
			}

			// create entity
			else if (t.status == TileStateEnum::Entity)
			{
				// register the collider
				addTerrainCollider(t.objectName, t.cpuCollider.get());

				{ // create the entity
					t.entity = engineEntities()->createAnonymous();
					CAGE_COMPONENT_ENGINE(Transform, tr, t.entity);
					tr.position = vec3(t.pos.x, t.pos.y, 0) * tileLength;
					CAGE_COMPONENT_ENGINE(Render, r, t.entity);
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
	}

	void engineFinalize()
	{
		stopping = true;
		generatorThreads.clear();
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
	
	void engineDispatch()
	{
		AssetManager *ass = engineAssets();
		CAGE_CHECK_GL_ERROR_DEBUG();
		for (Tile &t : tiles)
		{
			if (t.status == TileStateEnum::Upload)
			{
				t.gpuAlbedo = dispatchTexture(t.cpuAlbedo);
				t.gpuMaterial = dispatchTexture(t.cpuMaterial);
				t.gpuMesh = dispatchMesh(t.cpuMesh);

				{ // set texture names for the mesh
					uint32 textures[MaxTexturesCountPerMaterial];
					detail::memset(textures, 0, sizeof(textures));
					textures[0] = t.albedoName;
					textures[1] = t.materialName;
					t.gpuMesh->setTextureNames(textures);
				}

				// transfer asset ownership
				ass->fabricate<AssetSchemeIndexTexture, Texture>(t.albedoName, templates::move(t.gpuAlbedo), stringizer() + "albedo " + t.pos);
				ass->fabricate<AssetSchemeIndexTexture, Texture>(t.materialName, templates::move(t.gpuMaterial), stringizer() + "material " + t.pos);
				ass->fabricate<AssetSchemeIndexMesh, Mesh>(t.meshName, templates::move(t.gpuMesh), stringizer() + "mesh " + t.pos);
				ass->fabricate<AssetSchemeIndexRenderObject, RenderObject>(t.objectName, templates::move(t.renderObject), stringizer() + "object " + t.pos);

				t.status = TileStateEnum::Entity;
				break;
			}
		}
		CAGE_CHECK_GL_ERROR_DEBUG();
	}

	/////////////////////////////////////////////////////////////////////////////
	// GENERATOR
	/////////////////////////////////////////////////////////////////////////////

	Tile *generatorChooseTile()
	{
		static Holder<Mutex> mut = newMutex();
		ScopeLock<Mutex> lock(mut);
		Tile *result = nullptr;
		real rd = real::Nan();
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
			if (result && d > rd)
				continue;
			result = &t;
			rd = d;
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

	void generateRenderObject(Tile &t)
	{
		t.renderObject = newRenderObject();
		float thresholds[1] = { 0 };
		uint32 meshIndices[2] = { 0, 1 };
		uint32 meshNames[1] = { t.meshName };
		t.renderObject->setLods(1, 1, thresholds, meshIndices, meshNames);
	}

	void generatorEntry()
	{
		AssetManager *ass = engineAssets();
		while (!stopping)
		{
			Tile *t = generatorChooseTile();
			if (!t)
			{
				threadSleep(10000);
				continue;
			}

			// assets names
			t->albedoName = ass->generateUniqueName();
			t->materialName = ass->generateUniqueName();
			t->meshName = ass->generateUniqueName();
			t->objectName = ass->generateUniqueName();

			generateMesh(*t);
			generateCollider(*t);
			generateTextures(*t);
			generateRenderObject(*t);

			t->status = TileStateEnum::Upload;
		}
	}

	/////////////////////////////////////////////////////////////////////////////
	// INITIALIZE
	/////////////////////////////////////////////////////////////////////////////

	void engineInitialize()
	{
		uint32 cpuCount = max(processorsCount(), 2u) - 1;
		for (uint32 i = 0; i < cpuCount; i++)
			generatorThreads.push_back(newThread(Delegate<void()>().bind<&generatorEntry>(), stringizer() + "generator " + i));
	}

	class Callbacks
	{
		EventListener<void()> engineUpdateListener;
		EventListener<void()> engineInitializeListener;
		EventListener<void()> engineFinalizeListener;
		EventListener<void()> engineUnloadListener;
		EventListener<void()> engineDispatchListener;
	public:
		Callbacks()
		{
			engineUpdateListener.attach(controlThread().update);
			engineUpdateListener.bind<&engineUpdate>();
			engineInitializeListener.attach(controlThread().initialize);
			engineInitializeListener.bind<&engineInitialize>();
			engineFinalizeListener.attach(controlThread().finalize);
			engineFinalizeListener.bind<&engineFinalize>();
			engineUnloadListener.attach(controlThread().unload);
			engineUnloadListener.bind<&engineUpdate>();
			engineDispatchListener.attach(graphicsDispatchThread().dispatch);
			engineDispatchListener.bind<&engineDispatch>();
		}
	} callbacksInstance;
}
