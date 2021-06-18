#include <cage-core/entities.h>
#include <cage-core/geometry.h>
#include <cage-core/concurrent.h>
#include <cage-core/assetManager.h>
#include <cage-core/memoryBuffer.h>
#include <cage-core/image.h>
#include <cage-core/collider.h>
#include <cage-core/threadPool.h>
#include <cage-core/debug.h>
#include <cage-core/mesh.h>

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
	constexpr real tileLength = 30; // real world size of a tile (in 1 dimension)
	constexpr uint32 tileMeshResolution = 60; // number of vertices (in 1 dimension)
	constexpr real distanceToUnloadTile = 300;

	enum class TileStateEnum
	{
		Init,
		Generate,
		Generating,
		Upload,
		Entity,
		Ready,
	};

	struct TileBase
	{
		Holder<Collider> cpuCollider;
		Holder<Mesh> cpuMesh;
		Holder<Model> gpuMesh;
		Holder<Image> cpuAlbedo;
		Holder<Texture> gpuAlbedo;
		Holder<Image> cpuSpecial;
		Holder<Texture> gpuSpecial;
		Holder<RenderObject> renderObject;
		TilePos pos;
		Entity *entity = nullptr;
		uint32 meshName = 0;
		uint32 albedoName = 0;
		uint32 specialName = 0;
		uint32 objectName = 0;
		uint32 textureResolution = 0;

		real distanceToPlayer() const
		{
			return pos.distanceToPlayer(tileLength);
		}

		transform l2w() const
		{
			return transform(vec3(pos.x, pos.y, 0) * tileLength);
		}
	};

	struct Tile : public TileBase
	{
		std::atomic<TileStateEnum> status = TileStateEnum::Init;
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
				ass->remove(t.specialName);
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
				addTerrainCollider(t.objectName, t.cpuCollider.share());

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

	Holder<Texture> dispatchTexture(Holder<Image> &image)
	{
		Holder<Texture> t = newTexture();
		t->importImage(image.get());
		t->filters(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, 100);
		t->wraps(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
		t->generateMipmaps();
		image.clear();
		return t;
	}

	Holder<Model> dispatchMesh(Holder<Mesh> &poly)
	{
		Holder<Model> m = newModel();
		ModelHeader::MaterialData mat;
		m->importMesh(+poly, { (char*)&mat, (char*)(&mat + 1) });
		poly.clear();
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
				t.gpuSpecial = dispatchTexture(t.cpuSpecial);
				t.gpuMesh = dispatchMesh(t.cpuMesh);

				{ // set texture names for the mesh
					uint32 textures[MaxTexturesCountPerMaterial];
					detail::memset(textures, 0, sizeof(textures));
					textures[0] = t.albedoName;
					textures[1] = t.specialName;
					t.gpuMesh->setTextureNames(textures);
				}

				// transfer asset ownership
				ass->fabricate<AssetSchemeIndexTexture, Texture>(t.albedoName, std::move(t.gpuAlbedo), stringizer() + "albedo " + t.pos);
				ass->fabricate<AssetSchemeIndexTexture, Texture>(t.specialName, std::move(t.gpuSpecial), stringizer() + "special " + t.pos);
				ass->fabricate<AssetSchemeIndexModel, Model>(t.meshName, std::move(t.gpuMesh), stringizer() + "mesh " + t.pos);
				ass->fabricate<AssetSchemeIndexRenderObject, RenderObject>(t.objectName, std::move(t.renderObject), stringizer() + "object " + t.pos);

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

	std::vector<uint32> initializeMeshIndices()
	{
		constexpr uint32 r = tileMeshResolution;
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

	void generateMesh(Tile &t)
	{
		constexpr real pwoa = tileLength / (tileMeshResolution - 1);
		constexpr vec2 pwox = vec2(pwoa, 0);
		constexpr vec2 pwoy = vec2(0, pwoa);
		std::vector<vec3> positions, normals;
		positions.reserve(tileMeshResolution * tileMeshResolution);
		normals.reserve(tileMeshResolution * tileMeshResolution);
		transform l2w = t.l2w();
		for (uint32 y = 0; y < tileMeshResolution; y++)
		{
			for (uint32 x = 0; x < tileMeshResolution; x++)
			{
				vec2 pt = (vec2(x, y) - 2) * tileLength / (tileMeshResolution - 5);
				vec2 pw = vec2(l2w * vec3(pt, 0));
				real z = terrainOffset(pw);
				positions.push_back(vec3(pt, z));
				real tox = terrainOffset(pw + pwox) - z;
				real toy = terrainOffset(pw + pwoy) - z;
				normals.push_back(normalize(vec3(-tox, -toy, 0.1)));
			}
		}
		t.cpuMesh = newMesh();
		t.cpuMesh->positions(positions);
		t.cpuMesh->normals(normals);
		t.cpuMesh->indices(meshIndices());
		{
			MeshSimplifyConfig cfg;
			cfg.minEdgeLength = 0.25;
			cfg.maxEdgeLength = 3;
			cfg.approximateError = 0.1;
			meshSimplify(+t.cpuMesh, cfg);
		}
		{
			MeshUnwrapConfig cfg;
			cfg.texelsPerUnit = 3;
			t.textureResolution = meshUnwrap(+t.cpuMesh, cfg);
		}

		//auto msh = t.cpuMesh->copy();
		//msh->applyTransform(t.l2w());
		//msh->exportObjFile({}, stringizer() + "debug/" + t.pos + ".obj");
	}

	void generateCollider(Tile &t)
	{
		Holder<Mesh> p = t.cpuMesh->copy();
		meshApplyTransform(+p, t.l2w());
		t.cpuCollider = newCollider();
		t.cpuCollider->importMesh(+p);
		t.cpuCollider->rebuild();
	}

	void textureGenerator(Tile *t, uint32 x, uint32 y, const ivec3 &idx, const vec3 &weights)
	{
		vec3 p = t->cpuMesh->positionAt(idx, weights) * t->l2w();
		vec3 color; real roughness; real metallic;
		terrainMaterial(vec2(p), color, roughness, metallic, false);
		t->cpuAlbedo->set(x, y, color);
		t->cpuSpecial->set(x, y, vec2(roughness, metallic));
	}

	void generateTextures(Tile &t)
	{
		t.cpuAlbedo = newImage();
		t.cpuAlbedo->initialize(t.textureResolution, t.textureResolution, 3);
		t.cpuSpecial = newImage();
		t.cpuSpecial->initialize(t.textureResolution, t.textureResolution, 2);
		MeshGenerateTextureConfig cfg;
		cfg.generator.bind<Tile *, &textureGenerator>(&t);
		cfg.width = cfg.height = t.textureResolution;
		meshGenerateTexture(+t.cpuMesh, cfg);
		imageDilation(+t.cpuAlbedo, 2);
		imageDilation(+t.cpuSpecial, 2);
		t.cpuSpecial->colorConfig.gammaSpace = GammaSpaceEnum::Linear;

		//auto tex = t.cpuAlbedo->copy();
		//tex->verticalFlip();
		//tex->exportFile(stringizer() + "debug/" + t.pos + ".png");
	}

	void generateRenderObject(Tile &t)
	{
		t.renderObject = newRenderObject();
		real thresholds[1] = { 0 };
		uint32 meshIndices[2] = { 0, 1 };
		uint32 meshNames[1] = { t.meshName };
		t.renderObject->setLods(thresholds, meshIndices, meshNames);
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
			t->specialName = ass->generateUniqueName();
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
