// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "scene.h"
#include "camera.h"
#include "mesh_manager.h"
#include "render_operation.h"

#include "base/macros.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"


namespace mmo
{
	void SceneQueuedRenderableVisitor::Visit(RenderablePass& rp)
	{
	}

	bool SceneQueuedRenderableVisitor::Visit(const Pass& p)
	{
		return true;
	}

	void SceneQueuedRenderableVisitor::Visit(Renderable& r)
	{
		targetScene->RenderSingleObject(r);
	}

	Scene::Scene()
	{
		m_renderQueue = std::make_unique<RenderQueue>();

		// Create default material
		m_defaultMaterial = std::make_shared<Material>("SceneDefault");
		m_defaultMaterial->SetType(MaterialType::Opaque);
		m_defaultMaterial->SetTwoSided(false);
		m_defaultMaterial->SetCastShadows(true);
		m_defaultMaterial->SetReceivesShadows(true);

		// Setup shaders (TODO)
	}

	void Scene::Clear()
	{
		m_rootNode->RemoveAllChildren();
		m_cameras.clear();
		m_camVisibleObjectsMap.clear();
		m_entities.clear();
		m_manualRenderObjects.clear();
	}

	Camera* Scene::CreateCamera(const String& name)
	{
		ASSERT(!name.empty());
		ASSERT(m_cameras.find(name) == m_cameras.end());

		auto camera = std::make_unique<Camera>(name);
		camera->SetScene(this);

		const auto insertedCamIt = 
			m_cameras.emplace(Cameras::value_type(name, std::move(camera)));

		auto* cam = insertedCamIt.first->second.get();
		m_camVisibleObjectsMap[cam] = VisibleObjectsBoundsInfo();

		return cam;
	}

	void Scene::DestroyCamera(const Camera& camera)
	{
		DestroyCamera(camera.GetName());
	}

	void Scene::DestroyCamera(const String& name)
	{
		if (const auto cameraIt = m_cameras.find(name); cameraIt != m_cameras.end())
		{
			m_cameras.erase(cameraIt);
		}
	}

	void Scene::DestroyEntity(const Entity& entity)
	{
		const auto entityIt = m_entities.find(entity.GetName());
		if (entityIt != m_entities.end())
		{
			m_entities.erase(entityIt);
		}
	}

	void Scene::DestroySceneNode(const SceneNode& sceneNode)
	{
		ASSERT(&sceneNode != m_rootNode);

		const auto nodeIt = m_sceneNodes.find(sceneNode.GetName());
		if (nodeIt != m_sceneNodes.end())
		{
			m_sceneNodes.erase(nodeIt);
		}
	}

	Light& Scene::CreateLight(const String& name, LightType type)
	{
		auto light = std::make_unique<Light>(name);
		light->SetType(type);

		auto [iterator, inserted] = m_lights.emplace(name, std::move(light));
		return *iterator->second.get();
	}

	Camera* Scene::GetCamera(const String& name)
	{
		const auto cameraIt = m_cameras.find(name);
		ASSERT(cameraIt != m_cameras.end());
		
		return cameraIt->second.get();
	}

	bool Scene::HasCamera(const String& name)
	{
		return m_cameras.find(name) != m_cameras.end();
	}

	void Scene::DestroyAllCameras()
	{
		m_cameras.clear();
	}

	void Scene::Render(Camera& camera)
	{
		auto& gx = GraphicsDevice::Get();

		m_renderableVisitor.targetScene = this;
		m_renderableVisitor.scissoring = false;

		UpdateSceneGraph();
		PrepareRenderQueue();

		const auto visibleObjectsIt = m_camVisibleObjectsMap.find(&camera);
		ASSERT(visibleObjectsIt != m_camVisibleObjectsMap.end());
		visibleObjectsIt->second.Reset();
		FindVisibleObjects(camera, visibleObjectsIt->second);

		// Clear current render target
		gx.SetFillMode(camera.GetFillMode());

		// Enable depth test & write & set comparison method to less
		gx.SetDepthEnabled(true);
		gx.SetDepthWriteEnabled(true);
		gx.SetDepthTestComparison(DepthTestMethod::Less);

		gx.SetTransformMatrix(World, Matrix4::Identity);
		gx.SetTransformMatrix(Projection, camera.GetProjectionMatrix());
		gx.SetTransformMatrix(View, camera.GetViewMatrix());

		RenderVisibleObjects();
	}

	void Scene::UpdateSceneGraph()
	{
		GetRootSceneNode().Update(true, false);
	}

	void Scene::RenderVisibleObjects()
	{
		for (auto& queue = GetRenderQueue(); auto& [groupId, group] : queue)
		{
			RenderQueueGroupObjects(*group);
		}
	}

	void Scene::InitRenderQueue()
	{
		m_renderQueue = std::make_unique<RenderQueue>();

		// TODO: Maybe initialize some properties for special render queues
	}

	void Scene::PrepareRenderQueue()
	{
		auto& renderQueue = GetRenderQueue();
		renderQueue.Clear();
	}

	void Scene::FindVisibleObjects(Camera& camera, VisibleObjectsBoundsInfo& visibleObjectBounds)
	{
		GetRootSceneNode().FindVisibleObjects(camera, GetRenderQueue(), visibleObjectBounds, true);
	}

	void Scene::RenderObjects(const QueuedRenderableCollection& objects)
	{
		objects.AcceptVisitor(m_renderableVisitor);
	}
	
	void Scene::RenderQueueGroupObjects(RenderQueueGroup& group)
	{
		for(const auto& [priority, priorityGroup] : group)
		{
			RenderObjects(priorityGroup->GetSolids());
		}
	}

	void Scene::NotifyLightsDirty()
	{
		++m_lightsDirtyCounter;
	}

	void Scene::FindLightsAffectingCamera(const Camera& camera)
	{
		m_testLightInfos.clear();
		m_testLightInfos.reserve(m_lights.size());

		for (const auto& [name, light] : m_lights)
		{
			if (!light->IsVisible())
			{
				continue;
			}

			LightInfo lightInfo;
			lightInfo.light = light.get();
			lightInfo.type = light->GetType();
			lightInfo.lightMask = 0;	// TODO
			lightInfo.castsShadow = false;	// TODO

			// Directional lights don't have a position and thus are always visible
			if (lightInfo.type == LightType::Directional)
			{
				lightInfo.position = Vector3::Zero;
				lightInfo.range = 0.0f;
			}
			else
			{
				// Do a visibility check (culling) for each non directional light
				lightInfo.range = light->GetAttenuationRange();
				lightInfo.position = light->GetDerivedPosition();

				const Sphere sphere{lightInfo.position, lightInfo.range};
				if (!camera.IsVisible(sphere))
				{
					continue;
				}
			}
			
			m_testLightInfos.emplace_back(std::move(lightInfo));
		}

		if (m_cachedLightInfos != m_testLightInfos)
		{
			m_cachedLightInfos = m_testLightInfos;
			NotifyLightsDirty();
		}
	}

	void Scene::RenderSingleObject(Renderable& renderable)
	{
		RenderOperation op { };
		renderable.PrepareRenderOperation(op);

		op.vertexBuffer->Set();
		if (op.useIndexes)
		{
			ASSERT(op.indexBuffer);
			op.indexBuffer->Set();
		}

		auto& gx = GraphicsDevice::Get();

		// Grab material with fallback to default material of the scene
		auto material = renderable.GetMaterial();
		if (!material)
		{
			material = m_defaultMaterial;
		}
		
		gx.SetTopologyType(op.topology);
		gx.SetVertexFormat(op.vertexFormat);

		// Bind textures to the render stage
		material->Apply(gx);

		gx.SetFaceCullMode(material->IsTwoSided() ? FaceCullMode::None : FaceCullMode::Front);	// ???
		gx.SetBlendMode(material->IsTranslucent() ? BlendMode::Alpha : BlendMode::Opaque);

		// TODO: Set light-dependent settings
		if (material->IsLit())
		{
		}
		else
		{
			
		}
		
		gx.SetTransformMatrix(World, renderable.GetWorldTransform());

		if (op.useIndexes)
		{
			gx.DrawIndexed(op.startIndex, op.endIndex);
		}
		else
		{
			gx.Draw(op.endIndex == 0 ? op.vertexBuffer->GetVertexCount() - op.startIndex : op.endIndex - op.startIndex, op.startIndex);
		}
	}

	ManualRenderObject* Scene::CreateManualRenderObject(const String& name)
	{
		ASSERT(m_manualRenderObjects.find(name) == m_manualRenderObjects.end());

		// TODO: No longer use singleton graphics device
		auto [iterator, created] = 
			m_manualRenderObjects.emplace(
				name, 
				std::make_unique<ManualRenderObject>(GraphicsDevice::Get()));
		
		return iterator->second.get();
	}

	SceneNode& Scene::GetRootSceneNode() 
	{
		if (!m_rootNode)
		{
			m_rootNode = &CreateSceneNode("__root__");
			m_rootNode->NotifyRootNode();
		}

		return *m_rootNode;
	}

	SceneNode& Scene::CreateSceneNode()
	{
		auto sceneNode = std::make_unique<SceneNode>(*this);
		SceneNode* rawNode = sceneNode.get();

		ASSERT(m_sceneNodes.find(rawNode->GetName()) == m_sceneNodes.end());
		m_sceneNodes[sceneNode->GetName()] = std::move(sceneNode);

		return *rawNode;
	}

	SceneNode& Scene::CreateSceneNode(const String& name)
	{
		ASSERT(m_sceneNodes.find(name) == m_sceneNodes.end());
		
		auto sceneNode = std::make_unique<SceneNode>(*this, name);
		m_sceneNodes[name] = std::move(sceneNode);
		return *m_sceneNodes[name];
	}

	Entity* Scene::CreateEntity(const String& entityName, const String& meshName)
	{
		const auto mesh = MeshManager::Get().Load(meshName);
		if (!mesh)
		{
			ELOG("Failed to load mesh " << meshName);
			return nullptr;
		}

		return CreateEntity(entityName, mesh);
	}

	Entity* Scene::CreateEntity(const String& entityName, const MeshPtr& mesh)
	{
		ASSERT(m_entities.find(entityName) == m_entities.end());
		ASSERT(mesh);

		auto [entityIt, created] = m_entities.emplace(entityName, std::make_unique<Entity>(entityName, mesh));
		
		return entityIt->second.get();
	}

	RenderQueue& Scene::GetRenderQueue()
	{
		if (!m_renderQueue)
		{
			InitRenderQueue();
		}

		ASSERT(m_renderQueue);
		return *m_renderQueue;
	}

	std::vector<Entity*> Scene::GetAllEntities() const
	{
		// TODO: this is bad performance-wise but should serve for now
		std::vector<Entity*> result;
		result.reserve(m_entities.size());

		for (const auto& pair : m_entities)
		{
			result.push_back(pair.second.get());
		}

		return result;
	}

	std::unique_ptr<AABBSceneQuery> Scene::CreateAABBQuery(const AABB& box)
	{
		auto query = std::make_unique<AABBSceneQuery>(*this);
		query->SetBox(box);

		return std::move(query);
	}

	std::unique_ptr<SphereSceneQuery> Scene::CreateSphereQuery(const Sphere& sphere)
	{
		auto query = std::make_unique<SphereSceneQuery>(*this);
		query->SetSphere(sphere);

		return std::move(query);
	}

	std::unique_ptr<RaySceneQuery> Scene::CreateRayQuery(const Ray& ray)
	{
		auto query = std::make_unique<RaySceneQuery>(*this);
		query->SetRay(ray);

		return std::move(query);
	}

	RaySceneQuery::RaySceneQuery(Scene& scene)
		: SceneQuery(scene)
	{
	}

	const RaySceneQueryResult& RaySceneQuery::Execute()
	{
		Execute(*this);

		return m_result;
	}

	void RaySceneQuery::Execute(RaySceneQueryListener& listener)
	{
		// TODO: Instead of iterating over ALL objects in the scene, be smarter (for example octree)

		for (const auto& entity : m_scene.GetAllEntities())
		{
			if (!entity->IsInScene()) continue;

			const auto hitResult = m_ray.intersectsAABB(entity->GetBoundingBox());
			if (!hitResult.first)
			{
				continue;
			}

			if (!listener.QueryResult(*entity, hitResult.second))
			{
				return;
			}
		}
	}

	bool RaySceneQuery::QueryResult(MovableObject& obj, float distance)
	{

		return false;
	}

	AABBSceneQuery::AABBSceneQuery(Scene& scene)
		: RegionSceneQuery(scene)
	{
	}

	void AABBSceneQuery::Execute(SceneQueryListener& listener)
	{
		// TODO
	}

	RegionSceneQuery::RegionSceneQuery(Scene& scene)
		: SceneQuery(scene)
	{
	}

	const SceneQueryResult& RegionSceneQuery::Execute()
	{
		Execute(*this);

		return m_lastResult;
	}

	bool RegionSceneQuery::QueryResult(MovableObject& first)
	{
		m_lastResult.push_back(&first);
		return true;
	}

	SceneQuery::SceneQuery(Scene& scene)
		: m_scene(scene)
	{
	}

	SphereSceneQuery::SphereSceneQuery(Scene& scene)
		: RegionSceneQuery(scene)
	{
	}

	void SphereSceneQuery::Execute(SceneQueryListener& listener)
	{
		// TODO
	}
}
