/*
 * Scene.cpp
 *
 * Copyright (c) 2014-2015 SEACAVE
 *
 * Author(s):
 *
 *      cDc <cdc.seacave@gmail.com>
 *
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Additional Terms:
 *
 *      You are required to preserve legal notices and author attributions in
 *      that material or in the Appropriate Legal Notices displayed by works
 *      containing it.
 */

#include "Common.h"
#include "Scene.h"

using namespace VIEWER;


// D E F I N E S ///////////////////////////////////////////////////

#define IMAGE_MAX_RESOLUTION 1024


// S T R U C T S ///////////////////////////////////////////////////

enum EVENT_TYPE {
	EVT_JOB = 0,
	EVT_CLOSE,
};

class EVTClose : public Event
{
public:
	EVTClose() : Event(EVT_CLOSE) {}
};
class EVTLoadImage : public Event
{
public:
	Scene* pScene;
	MVS::IIndex idx;
	unsigned nMaxResolution;
	bool Run(void*) {
		Image& image = pScene->images[idx];
		ASSERT(image.idx != NO_ID);
		MVS::Image& imageData = pScene->scene.images[image.idx];
		ASSERT(imageData.IsValid());
		if (imageData.image.empty() && !imageData.ReloadImage(nMaxResolution))
			return false;
		imageData.UpdateCamera(pScene->scene.platforms);
		image.AssignImage(imageData.image);
		imageData.ReleaseImage();
		glfwPostEmptyEvent();
		return true;
	}
	EVTLoadImage(Scene* _pScene, MVS::IIndex _idx, unsigned _nMaxResolution=0)
		: Event(EVT_JOB), pScene(_pScene), idx(_idx), nMaxResolution(_nMaxResolution) {}
};
class EVTComputeOctree : public Event
{
public:
	Scene* pScene;
	bool Run(void*) {
		MVS::Scene& scene = pScene->scene;
		if (!scene.mesh.IsEmpty()) {
			Scene::OctreeMesh octMesh(scene.mesh.vertices, [](Scene::OctreeMesh::IDX_TYPE size, Scene::OctreeMesh::Type /*radius*/) {
				return size > 256;
			});
			scene.mesh.ListIncidentFaces();
			pScene->octMesh.Swap(octMesh);
		} else
		if (!scene.pointcloud.IsEmpty()) {
			Scene::OctreePoints octPoints(scene.pointcloud.points, [](Scene::OctreePoints::IDX_TYPE size, Scene::OctreePoints::Type /*radius*/) {
				return size > 512;
			});
			pScene->octPoints.Swap(octPoints);
		}
		return true;
	}
	EVTComputeOctree(Scene* _pScene)
		: Event(EVT_JOB), pScene(_pScene) {}
};

void* Scene::ThreadWorker(void*) {
	while (true) {
		CAutoPtr<Event> evt(events.GetEvent());
		switch (evt->GetID()) {
		case EVT_JOB:
			evt->Run();
			break;
		case EVT_CLOSE:
			return NULL;
		default:
			ASSERT("Should not happen!" == NULL);
		}
	}
	return NULL;
}
/*----------------------------------------------------------------*/


// S T R U C T S ///////////////////////////////////////////////////

SEACAVE::EventQueue Scene::events;
SEACAVE::Thread Scene::thread;

Scene::Scene(ARCHIVE_TYPE _nArchiveType)
	:
	nArchiveType(_nArchiveType),
	listPointCloud(0)
{
}
Scene::~Scene()
{
	Release();
}

void Scene::Empty()
{
	ReleasePointCloud();
	ReleaseMesh();
	obbPoints.Release();
	if (window.IsValid()) {
		window.ReleaseClbk();
		window.Reset();
		window.SetName(_T("(empty)"));
	}
	textures.Release();
	images.Release();
	scene.Release();
	sceneName.clear();
	geometryName.clear();
}
void Scene::Release()
{
	if (window.IsValid())
		window.SetVisible(false);
	if (!thread.isRunning()) {
		events.AddEvent(new EVTClose());
		thread.join();
	}
	Empty();
	window.Release();
	glfwTerminate();
}
void Scene::ReleasePointCloud()
{
	if (listPointCloud) {
		glDeleteLists(listPointCloud, 1);
		listPointCloud = 0;
	}
}
void Scene::ReleaseMesh()
{
	if (!listMeshes.empty()) {
		for (GLuint listMesh: listMeshes)
			glDeleteLists(listMesh, 1);
		listMeshes.Release();
	}
}

bool Scene::Init(const cv::Size& size, LPCTSTR windowName, LPCTSTR fileName, LPCTSTR geometryFileName)
{
	ASSERT(scene.IsEmpty());

	// init window
	if (glfwInit() == GL_FALSE)
		return false;
	if (!window.Init(size, windowName))
		return false;
	if (gladLoadGL() == GL_FALSE)
		return false;
    VERBOSE("OpenGL: %s %s", glGetString(GL_RENDERER), glGetString(GL_VERSION));
	name = windowName;
	window.clbkOpenScene = DELEGATEBINDCLASS(Window::ClbkOpenScene, &Scene::Open, this);

	// init OpenGL
	glPolygonMode(GL_FRONT, GL_FILL);
	glEnable(GL_DEPTH_TEST);
	glClearColor(0.f, 0.5f, 0.9f, 1.f);

	static const float light0_ambient[] = {0.1f, 0.1f, 0.1f, 1.f};
	static const float light0_diffuse[] = {1.f, 1.f, 1.f, 1.f};
	static const float light0_position[] = {0.f, 0.f, 1000.f, 0.f};
	static const float light0_specular[] = {0.4f, 0.4f, 0.4f, 1.f};

	glLightfv(GL_LIGHT0, GL_AMBIENT, light0_ambient);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, light0_diffuse);
	glLightfv(GL_LIGHT0, GL_SPECULAR, light0_specular);
	glLightfv(GL_LIGHT0, GL_POSITION, light0_position);
	glLightModelf(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);

	glEnable(GL_LIGHT0);
	glDisable(GL_LIGHTING);

	// init working thread
	thread.start(ThreadWorker);

	// open scene or init empty scene
	window.SetCamera(Camera());
	if (fileName != NULL)
		Open(fileName, geometryFileName);
	window.SetVisible(true);
	return true;
}
bool Scene::Open(LPCTSTR fileName, LPCTSTR geometryFileName)
{
	ASSERT(fileName);
	DEBUG_EXTRA("Loading: '%s'", Util::getFileNameExt(fileName).c_str());
	Empty();
	sceneName = fileName;

	// load the scene
	WORKING_FOLDER = Util::getFilePath(fileName);
	INIT_WORKING_FOLDER;
	if (!scene.Load(fileName, true))
		return false;
	if (geometryFileName) {
		// try to load given mesh
		MVS::Mesh mesh;
		MVS::PointCloud pointcloud;
		if (mesh.Load(geometryFileName)) {
			scene.mesh.Swap(mesh);
			geometryName = geometryFileName;
			geometryMesh = true;
		} else
		// try to load as a point-cloud
		if (pointcloud.Load(geometryFileName)) {
			scene.pointcloud.Swap(pointcloud);
			geometryName = geometryFileName;
			geometryMesh = false;
		}
	}
	if (!scene.pointcloud.IsEmpty())
		scene.pointcloud.PrintStatistics(scene.images.data(), &scene.obb);

	#if 1
	// create octree structure used to accelerate selection functionality
	if (!scene.IsEmpty())
		events.AddEvent(new EVTComputeOctree(this));
	#endif

	// init scene
	AABB3d bounds(true);
	Point3d center(Point3d::INF);
	if (scene.IsBounded()) {
		bounds = AABB3d(scene.obb.GetAABB());
		center = bounds.GetCenter();
	} else {
		if (!scene.pointcloud.IsEmpty()) {
			bounds = scene.pointcloud.GetAABB(MINF(3u,scene.nCalibratedImages));
			if (bounds.IsEmpty())
				bounds = scene.pointcloud.GetAABB();
			center = scene.pointcloud.GetCenter();
		}
		if (!scene.mesh.IsEmpty()) {
			scene.mesh.ComputeNormalFaces();
			bounds.Insert(scene.mesh.GetAABB());
			center = scene.mesh.GetCenter();
		}
	}

	// init images
	AABB3d imageBounds(true);
	images.Reserve(scene.images.size());
	FOREACH(idxImage, scene.images) {
		const MVS::Image& imageData = scene.images[idxImage];
		if (!imageData.IsValid())
			continue;
		images.emplace_back(idxImage);
		imageBounds.InsertFull(imageData.camera.C);
	}
	if (imageBounds.IsEmpty())
		imageBounds.Enlarge(0.5);
	if (bounds.IsEmpty())
		bounds = imageBounds;

	// init and load texture
	if (scene.mesh.HasTexture()) {
		FOREACH(i, scene.mesh.texturesDiffuse) {
			Image& image = textures.emplace_back();
			ASSERT(image.idx == NO_ID);
			#if 0
			Image8U3& textureDiffuse = scene.mesh.texturesDiffuse[i];
			cv::flip(textureDiffuse, textureDiffuse, 0);
			image.SetImage(textureDiffuse);
			textureDiffuse.release();
			#else // preserve texture, used only to be able to export the mesh
			Image8U3 textureDiffuse;
			cv::flip(scene.mesh.texturesDiffuse[i], textureDiffuse, 0);
			image.SetImage(textureDiffuse);
			#endif
			image.GenerateMipmap();
		}
	}

	// compile bounding-box
	CompileBounds();

	// init camera
	window.SetCamera(Camera(bounds,
		center == Point3d::INF ? Point3d(bounds.GetCenter()) : center,
		images.size()<2?1.f:(float)imageBounds.EnlargePercent(REAL(1)/images.size()).GetSize().norm()));
	window.camera.maxCamID = images.size();
	window.SetName(String::FormatString((name + _T(": %s")).c_str(), Util::getFileName(fileName).c_str()));
	window.clbkSaveScene = DELEGATEBINDCLASS(Window::ClbkSaveScene, &Scene::Save, this);
	window.clbkExportScene = DELEGATEBINDCLASS(Window::ClbkExportScene, &Scene::Export, this);
	window.clbkCenterScene = DELEGATEBINDCLASS(Window::ClbkCenterScene, &Scene::Center, this);
	window.clbkCompilePointCloud = DELEGATEBINDCLASS(Window::ClbkCompilePointCloud, &Scene::CompilePointCloud, this);
	window.clbkCompileMesh = DELEGATEBINDCLASS(Window::ClbkCompileMesh, &Scene::CompileMesh, this);
	window.clbkTogleSceneBox = DELEGATEBINDCLASS(Window::ClbkTogleSceneBox, &Scene::TogleSceneBox, this);
	window.clbkCropToBounds = DELEGATEBINDCLASS(Window::ClbkCropToBounds, &Scene::CropToBounds, this);
	if (scene.IsBounded())
		window.clbkCompileBounds = DELEGATEBINDCLASS(Window::ClbkCompileBounds, &Scene::CompileBounds, this);
	if (!scene.IsEmpty())
		window.clbkRayScene = DELEGATEBINDCLASS(Window::ClbkRayScene, &Scene::CastRay, this);
	window.Reset(!scene.pointcloud.IsEmpty()&&!scene.mesh.IsEmpty()?Window::SPR_NONE:Window::SPR_ALL,
		MINF(2u,images.size()));
	return true;
}

// export the scene
bool Scene::Save(LPCTSTR _fileName, bool bRescaleImages)
{
	if (!IsOpen())
		return false;
	REAL imageScale = 0;
	if (bRescaleImages) {
		window.SetVisible(false);
		std::cout << "Enter image resolution scale: ";
		String strScale;
		std::cin >> strScale;
		window.SetVisible(true);
		imageScale = strScale.From<REAL>(0);
	}
	const String fileName(_fileName != NULL ? String(_fileName) : Util::insertBeforeFileExt(sceneName, _T("_new")));
	MVS::Mesh mesh;
	if (!scene.mesh.IsEmpty() && !geometryName.empty() && geometryMesh)
		mesh.Swap(scene.mesh);
	MVS::PointCloud pointcloud;
	if (!scene.pointcloud.IsEmpty() && !geometryName.empty() && !geometryMesh)
		pointcloud.Swap(scene.pointcloud);
	if (imageScale > 0 && imageScale < 1) {
		// scale and save images
		const String folderName(Util::getFilePath(MAKE_PATH_FULL(WORKING_FOLDER_FULL, fileName)) + String::FormatString("images%d" PATH_SEPARATOR_STR, ROUND2INT(imageScale*100)));
		if (!scene.ScaleImages(0, imageScale, folderName)) {
			DEBUG("error: can not scale scene images to '%s'", folderName.c_str());
			return false;
		}
	}
	if (!scene.Save(fileName, nArchiveType)) {
		DEBUG("error: can not save scene to '%s'", fileName.c_str());
		return false;
	}
	if (!mesh.IsEmpty())
		scene.mesh.Swap(mesh);
	if (!pointcloud.IsEmpty())
		scene.pointcloud.Swap(pointcloud);
	sceneName = fileName;
	return true;
}

// export the scene
bool Scene::Export(LPCTSTR _fileName, LPCTSTR exportType) const
{
	if (!IsOpen())
		return false;
	ASSERT(!sceneName.IsEmpty());
	String lastFileName;
	const String fileName(_fileName != NULL ? String(_fileName) : sceneName);
	const String baseFileName(Util::getFileFullName(fileName));
	const bool bPoints(scene.pointcloud.Save(lastFileName=(baseFileName+_T("_pointcloud.ply")), nArchiveType==ARCHIVE_MVS));
	const bool bMesh(scene.mesh.Save(lastFileName=(baseFileName+_T("_mesh")+(exportType?exportType:(Util::getFileExt(fileName)==_T(".obj")?_T(".obj"):_T(".ply")))), cList<String>(), true));
	#if TD_VERBOSE != TD_VERBOSE_OFF
	if (VERBOSITY_LEVEL > 2 && (bPoints || bMesh))
		scene.ExportCamerasMLP(Util::getFileFullName(lastFileName)+_T(".mlp"), lastFileName);
	#endif
	AABB3f aabb(true);
	if (scene.IsBounded()) {
		std::ofstream fs(baseFileName+_T("_roi.txt"));
		if (fs)
			fs << scene.obb;
		aabb = scene.obb.GetAABB();
	} else
	if (!scene.pointcloud.IsEmpty()) {
		aabb = scene.pointcloud.GetAABB();
	} else
	if (!scene.mesh.IsEmpty()) {
		aabb = scene.mesh.GetAABB();
	}
	if (!aabb.IsEmpty()) {
		std::ofstream fs(baseFileName+_T("_roi_box.txt"));
		if (fs)
			fs << aabb;
	}
	return bPoints || bMesh;
}

void Scene::CompilePointCloud()
{
	if (scene.pointcloud.IsEmpty())
		return;
	ReleasePointCloud();
	listPointCloud = glGenLists(1);
	glNewList(listPointCloud, GL_COMPILE);
	ASSERT((window.sparseType&(Window::SPR_POINTS|Window::SPR_LINES)) != 0);
	// compile point-cloud
	if ((window.sparseType&Window::SPR_POINTS) != 0) {
		ASSERT_ARE_SAME_TYPE(float, MVS::PointCloud::Point::Type);
		glBegin(GL_POINTS);
		glColor3f(1.f, 1.f, 1.f);
		MVS::DepthData depthData;
		MVS::DepthMap& depthMap = depthData.depthMap;
		MVS::ConfidenceMap confMap;
		if (window.colorSource == Window::COLORSOURCE::COL_DEPTH || window.colorSource == Window::COLORSOURCE::COL_COMPOSITE || window.colorSource == Window::COLORSOURCE::COL_NORMAL) {
			if (!depthData.Load(sceneName, window.colorSource == Window::COLORSOURCE::COL_NORMAL ? 3 : 1)) {
				DEBUG("warning: can not load depth-map");
				window.colorSource = Window::COLORSOURCE::COL_IMAGE;
			} else {
				window.colorSource == Window::COLORSOURCE::COL_NORMAL ?
					MVS::EstimateConfidenceFromNormal(depthData, confMap, 1) :
					MVS::EstimateConfidenceFromDepth(depthData, confMap, 1, 3);
			}
		}
		int j, k, cmpt(0);
		unsigned numPoints(0);
		FOREACH(i, scene.pointcloud.points) {
			if (!scene.pointcloud.pointViews.empty() &&
				scene.pointcloud.pointViews[i].size() < window.minViews)
				continue;
<<<<<<< HEAD
			if (!scene.pointcloud.colors.empty()) {
=======
			if (!scene.pointcloud.colors.empty() && window.colorSource == Window::COLORSOURCE::COL_IMAGE) {
>>>>>>> 8089fd75d6a5ece2abe99a72cadf1314134d4efd
				const MVS::PointCloud::Color& c = scene.pointcloud.colors[i];
				glColor3ub(c.r, c.g, c.b);
			}
			if (window.colorSource == Window::COLORSOURCE::COL_DEPTH || window.colorSource == Window::COLORSOURCE::COL_COMPOSITE || window.colorSource == Window::COLORSOURCE::COL_NORMAL) {
				do {
					j = cmpt/depthMap.cols;
					k = cmpt%depthMap.cols;
					cmpt++;
				} while (depthMap(j, k) <= 0);
				const float confidence = window.colorSource == Window::COLORSOURCE::COL_COMPOSITE ?
					0.3f*confMap(j, k) + 0.7f*scene.pointcloud.pointWeights[i][0] :
					confMap(j, k);
				if (confidence < window.colorThreshold)
					continue;
				const Pixel8U c = Pixel8U::gray2color(confidence);
				glColor3ub(c.r, c.g, c.b);
			}
			if (window.colorSource == Window::COLORSOURCE::COL_CONFIDENCE && !scene.pointcloud.pointWeights.empty()) {
				const float confidence = scene.pointcloud.pointWeights[i][0];
				if (confidence < window.colorThreshold)
					continue;
				const Pixel8U c = Pixel8U::gray2color(confidence);
				glColor3ub(c.r, c.g, c.b);
			}
			const MVS::PointCloud::Point& X = scene.pointcloud.points[i];
			glVertex3fv(X.ptr());
			++numPoints;
		}
		glEnd();
		DEBUG("Point-cloud %.2f%%%% with %s color source and %.2f confidence threshold compiled",
			100.f*(float)numPoints/scene.pointcloud.GetSize(),
			window.colorSource == Window::COLORSOURCE::COL_DEPTH ? "depth" :
			window.colorSource == Window::COLORSOURCE::COL_CONFIDENCE ? "confidence" :
			window.colorSource == Window::COLORSOURCE::COL_COMPOSITE ? "composite" :
			window.colorSource == Window::COLORSOURCE::COL_NORMAL ? "normal" :
			"image", window.colorThreshold);
	}
	glEndList();
}

void Scene::CompileMesh()
{
	if (scene.mesh.IsEmpty())
		return;
	ReleaseMesh();
	if (scene.mesh.faceNormals.empty())
		scene.mesh.ComputeNormalFaces();
	// translate, normalize and flip Y axis of the texture coordinates
	MVS::Mesh::TexCoordArr normFaceTexcoords;
	if (scene.mesh.HasTexture() && window.bRenderTexture)
		scene.mesh.FaceTexcoordsNormalize(normFaceTexcoords, true);
	MVS::Mesh::TexIndex texIdx(0);
	do {
		GLuint& listMesh = listMeshes.emplace_back(glGenLists(1));
		listMesh = glGenLists(1);
		glNewList(listMesh, GL_COMPILE);
		// compile mesh
		ASSERT_ARE_SAME_TYPE(float, MVS::Mesh::Vertex::Type);
		ASSERT_ARE_SAME_TYPE(float, MVS::Mesh::Normal::Type);
		ASSERT_ARE_SAME_TYPE(float, MVS::Mesh::TexCoord::Type);
		glColor3f(1.f, 1.f, 1.f);
		glBegin(GL_TRIANGLES);
		FOREACH(idxFace, scene.mesh.faces) {
			if (!scene.mesh.faceTexindices.empty() && scene.mesh.faceTexindices[idxFace] != texIdx)
				continue;
			const MVS::Mesh::Face& face = scene.mesh.faces[idxFace];
			const MVS::Mesh::Normal& n = scene.mesh.faceNormals[idxFace];
			glNormal3fv(n.ptr());
			for (int j = 0; j < 3; ++j) {
				if (!normFaceTexcoords.empty()) {
					const MVS::Mesh::TexCoord& t = normFaceTexcoords[idxFace*3 + j];
					glTexCoord2fv(t.ptr());
				}
				const MVS::Mesh::Vertex& p = scene.mesh.vertices[face[j]];
				glVertex3fv(p.ptr());
			}
		}
		glEnd();
		glEndList();
	} while (++texIdx < scene.mesh.texturesDiffuse.size());
<<<<<<< HEAD
=======
	DEBUG("%s compiled", scene.mesh.HasTexture() ? "Textured mesh" : "Mesh");
>>>>>>> 8089fd75d6a5ece2abe99a72cadf1314134d4efd
}

void Scene::CompileBounds()
{
	obbPoints.Release();
	if (!scene.IsBounded()) {
		window.bRenderBounds = false;
		return;
	}
	window.bRenderBounds = !window.bRenderBounds;
	if (window.bRenderBounds) {
		static const uint8_t indices[12*2] = {
			0,2, 2,3, 3,1, 1,0,
			0,6, 2,4, 3,5, 1,7,
			6,4, 4,5, 5,7, 7,6
		};
		OBB3f::POINT corners[OBB3f::numCorners];
		scene.obb.GetCorners(corners);
		for (int i=0; i<12; ++i) {
			obbPoints.emplace_back(corners[indices[i*2+0]]);
			obbPoints.emplace_back(corners[indices[i*2+1]]);
		}
	}
}

void Scene::CropToBounds()
{
	if (!IsOpen())
		return;
	if (!scene.IsBounded())
		return;
	scene.pointcloud.RemovePointsOutside(scene.obb);
	scene.mesh.RemoveFacesOutside(scene.obb);
	Center();
}

void Scene::Draw()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glPointSize(window.pointSize);

	// render point-cloud
	if (listPointCloud) {
		glDisable(GL_TEXTURE_2D);
		glCallList(listPointCloud);
	}
	// render mesh
	if (!listMeshes.empty()) {
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		if (!scene.mesh.faceTexcoords.empty() && window.bRenderTexture) {
			glEnable(GL_TEXTURE_2D);
			FOREACH(i, listMeshes) {
				textures[i].Bind();
				glCallList(listMeshes[i]);
			}
			glDisable(GL_TEXTURE_2D);
		} else {
			glEnable(GL_LIGHTING);
			for (GLuint listMesh: listMeshes)
				glCallList(listMesh);
			glDisable(GL_LIGHTING);
		}
	}
	// render cameras
	if (window.bRenderCameras) {
		glDisable(GL_CULL_FACE);
		const Point3* ptrPrevC(NULL);
		FOREACH(idx, images) {
			Image& image = images[idx];
			const MVS::Image& imageData = scene.images[image.idx];
			const MVS::Camera& camera = imageData.camera;
			// cache image corner coordinates
			const double scaleFocal(window.camera.scaleF);
			const Point2d pp(camera.GetPrincipalPoint());
			const double focal(camera.GetFocalLength()/scaleFocal);
			const double cx(-pp.x/focal);
			const double cy(-pp.y/focal);
			const double px((double)imageData.width/focal+cx);
			const double py((double)imageData.height/focal+cy);
			const Point3d ic1(cx, cy, scaleFocal);
			const Point3d ic2(cx, py, scaleFocal);
			const Point3d ic3(px, py, scaleFocal);
			const Point3d ic4(px, cy, scaleFocal);
			// change coordinates system to the camera space
			glPushMatrix();
			glMultMatrixd((GLdouble*)TransL2W((const Matrix3x3::EMat)camera.R, -(const Point3::EVec)camera.C).data());
			// draw image thumbnail
			const bool bSelectedImage(idx == window.camera.currentCamID);
			if (bSelectedImage) {
				if (image.IsValid()) {
					// render image
					glEnable(GL_TEXTURE_2D);
					image.Bind();
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					glEnable(GL_BLEND);
					glDisable(GL_DEPTH_TEST);
					glColor4f(1,1,1,window.cameraBlend);
					glBegin(GL_QUADS);
					glTexCoord2d(0,0); glVertex3dv(ic1.ptr());
					glTexCoord2d(0,1); glVertex3dv(ic2.ptr());
					glTexCoord2d(1,1); glVertex3dv(ic3.ptr());
					glTexCoord2d(1,0); glVertex3dv(ic4.ptr());
					glEnd();
					glDisable(GL_TEXTURE_2D);
					glDisable(GL_BLEND);
					glEnable(GL_DEPTH_TEST);
				} else {
					// start and wait to load the image
					if (image.IsImageEmpty()) {
						// start loading
						image.SetImageLoading();
						events.AddEvent(new EVTLoadImage(this, idx, IMAGE_MAX_RESOLUTION));
					} else {
						// check if the image is available and set it
						image.TransferImage();
					}
				}
			}
			glDisable(GL_TEXTURE_2D);
			// draw camera frame
			const bool bSelectedCamera(window.selectionType == Window::SEL_CAMERA && window.selectionIdx == idx);
			glLineWidth(bSelectedCamera ? 3.f : 2.f);
			glColor3f(bSelectedImage || bSelectedCamera ? 0.f : 1.f, 1.f, 0.f);
			glBegin(GL_LINES);
			glVertex3d(0,0,0); glVertex3dv(ic1.ptr());
			glVertex3d(0,0,0); glVertex3dv(ic2.ptr());
			glVertex3d(0,0,0); glVertex3dv(ic3.ptr());
			glVertex3d(0,0,0); glVertex3dv(ic4.ptr());
			glVertex3dv(ic1.ptr()); glVertex3dv(ic2.ptr());
			glVertex3dv(ic2.ptr()); glVertex3dv(ic3.ptr());
			glVertex3dv(ic3.ptr()); glVertex3dv(ic4.ptr());
			glVertex3dv(ic4.ptr()); glVertex3dv(ic1.ptr());
			glEnd();
			// draw camera position and image center
			glPointSize(window.pointSize+3.f);
			glBegin(GL_POINTS);
			glColor3f(1,0,0); glVertex3f(0,0,0); // camera position
			glColor3f(0,1,0); glVertex3f(0,0,(float)scaleFocal); // image center
			glColor3f(0,0,1); glVertex3d((0.5*imageData.width-pp.x)/focal, cy, scaleFocal); // image up
			glEnd();
			// restore coordinate system
			glPopMatrix();
			// render image visibility info
			if (window.bRenderImageVisibility && idx != NO_ID && idx==window.camera.currentCamID) {
				if (scene.pointcloud.IsValid()) {
					const Image& image = images[idx];
					glPointSize(window.pointSize*1.1f);
					glDisable(GL_DEPTH_TEST);
					glBegin(GL_POINTS);
					glColor3f(1.f,0.f,0.f);
					FOREACH(i, scene.pointcloud.points) {
						ASSERT(!scene.pointcloud.pointViews[i].empty());
						if (scene.pointcloud.pointViews[i].size() < window.minViews)
							continue;
						if (scene.pointcloud.pointViews[i].FindFirst(image.idx) == MVS::PointCloud::ViewArr::NO_INDEX)
							continue;
						glVertex3fv(scene.pointcloud.points[i].ptr());
					}
					glEnd();
					glEnable(GL_DEPTH_TEST);
					glPointSize(window.pointSize);
				}
			}
			// render camera trajectory
			if (window.bRenderCameraTrajectory && ptrPrevC) {
				glLineWidth(1.f);
				glBegin(GL_LINES);
				glColor3f(1.f,0.5f,0.f);
				glVertex3dv(ptrPrevC->ptr());
				glVertex3dv(camera.C.ptr());
				glEnd();
			}
			ptrPrevC = &camera.C;
		}
	}
	// render selection
	if (window.selectionType != Window::SEL_NA) {
		glPointSize(window.pointSize+4);
		glDisable(GL_DEPTH_TEST);
		glBegin(GL_POINTS);
		glColor3f(1,0,0); glVertex3fv(window.selectionPoints[0].ptr());
		if (window.selectionType == Window::SEL_TRIANGLE) {
		glColor3f(0,1,0); glVertex3fv(window.selectionPoints[1].ptr());
		glColor3f(0,0,1); glVertex3fv(window.selectionPoints[2].ptr());
		}
		glEnd();
		if (window.bRenderViews && window.selectionType == Window::SEL_POINT) {
			if (!scene.pointcloud.pointViews.empty()) {
				glLineWidth(1.f);
				glBegin(GL_LINES);
				const MVS::PointCloud::ViewArr& views = scene.pointcloud.pointViews[(MVS::PointCloud::Index)window.selectionIdx];
				ASSERT(!views.empty());
				for (MVS::PointCloud::View idxImage: views) {
					const MVS::Image& imageData = scene.images[idxImage];
					glVertex3dv(imageData.camera.C.ptr());
					glVertex3fv(window.selectionPoints[0].ptr());
				}
				glEnd();
			}
		}
		glEnable(GL_DEPTH_TEST);
		glPointSize(window.pointSize);
	}
	// render oriented-bounding-box
	if (!obbPoints.empty()) {
		glDepthMask(GL_FALSE);
		glLineWidth(2.f);
		glBegin(GL_LINES);
		glColor3f(0.5f,0.1f,0.8f);
		for (IDX i=0; i<obbPoints.size(); i+=2) {
			glVertex3fv(obbPoints[i+0].ptr());
			glVertex3fv(obbPoints[i+1].ptr());
		}
		glEnd();
		glDepthMask(GL_TRUE);
	}
	// draw coordinate axes
	{
		constexpr int axisWindowSize(200);
		constexpr float axisLength(1.5f);
		GLfloat matrix[16];
		glGetFloatv(GL_MODELVIEW_MATRIX, matrix);
		glPushMatrix();
		glPushAttrib(GL_VIEWPORT_BIT);
		// draw at bottom-right corner and scale down
		glViewport(window.size.width - axisWindowSize, 0, axisWindowSize, axisWindowSize);
		glLoadIdentity();
		glTranslatef(0.f, 0.f, -3.f);
		matrix[12] = matrix[13] = matrix[14] = 0.f;
		glMultMatrixf(matrix);
		glLineWidth(4.f);
		// X axis (Red)
		glBegin(GL_LINES);
		glColor3f(1.f, 0.f, 0.f);
		glVertex3f(0.f, 0.f, 0.f);
		glVertex3f(axisLength, 0.f, 0.f);
		// Y axis (Green)
		glColor3f(0.f, 1.f, 0.f);
		glVertex3f(0.f, 0.f, 0.f);
		glVertex3f(0.f, axisLength, 0.f);
		// Z axis (Blue)
		glColor3f(0.f, 0.f, 1.f);
		glVertex3f(0.f, 0.f, 0.f);
		glVertex3f(0.f, 0.f, axisLength);
		glEnd();
		// draw small spheres at axis ends for better visibility
		glPointSize(10.f);
		glBegin(GL_POINTS);
		glColor3f(1.f, 0.f, 0.f);
		glVertex3f(axisLength, 0.f, 0.f);
		glColor3f(0.f, 1.f, 0.f);
		glVertex3f(0.f, axisLength, 0.f);
		glColor3f(0.f, 0.f, 1.f);
		glVertex3f(0.f, 0.f, axisLength);
		glEnd();
		glPopAttrib();
		glPopMatrix();
	}
	glfwSwapBuffers(window.GetWindow());
}

void Scene::Loop()
{
	while (!glfwWindowShouldClose(window.GetWindow())) {
		window.UpdateView(images, scene.images);
		Draw();
		glfwWaitEvents();
	}
}


void Scene::Center()
{
	if (!IsOpen())
		return;
	scene.Center();
	CompilePointCloud();
	CompileMesh();
	if (scene.IsBounded()) {
		window.bRenderBounds = false;
		CompileBounds();
	}
	events.AddEvent(new EVTComputeOctree(this));
}

void Scene::TogleSceneBox()
{
	if (!IsOpen())
		return;
	const auto EnlargeAABB = [](AABB3f aabb) {
		return aabb.Enlarge(aabb.GetSize().maxCoeff()*0.03f);
	};
	if (scene.IsBounded())
		scene.obb = OBB3f(true);
	else if (!scene.mesh.IsEmpty())
		scene.obb.Set(EnlargeAABB(scene.mesh.GetAABB()));
	else if (!scene.pointcloud.IsEmpty())
		scene.obb.Set(EnlargeAABB(scene.pointcloud.GetAABB(window.minViews)));
	CompileBounds();
}


void Scene::CastRay(const Ray3& ray, int action)
{
	if (!IsOctreeValid())
		return;
	const double timeClick(0.2);
	const double timeDblClick(0.3);
	const double now(glfwGetTime());

	switch (action) {
	case GLFW_PRESS: {
		// remember when the click action started
		window.selectionTimeClick = now;
		break; }
	case GLFW_RELEASE: {
		if (now-window.selectionTimeClick > timeClick) {
			// this is a long click, ignore it
			break;
		}
		if (window.selectionType != Window::SEL_NA && now-window.selectionTime < timeDblClick) {
			// this is a double click, center scene at the selected element
			if (window.selectionType == Window::SEL_CAMERA)
				window.camera.currentCamID = window.selectionIdx;
			window.CenterCamera(window.selectionPoints[3]);
			window.selectionTime = now;
			break;
		}
		window.selectionType = Window::SEL_NA;
		REAL minDist = REAL(FLT_MAX);
		IDX newSelectionIdx = NO_IDX;
		Point3f newSelectionPoints[4];
		if (!octMesh.IsEmpty()) {
			// find ray intersection with the mesh
			const MVS::IntersectRayMesh intRay(octMesh, ray, scene.mesh);
			if (intRay.pick.IsValid()) {
				window.selectionType = Window::SEL_TRIANGLE;
				minDist = intRay.pick.dist;
				newSelectionIdx = intRay.pick.idx;
				const MVS::Mesh::Face& face = scene.mesh.faces[(MVS::Mesh::FIndex)newSelectionIdx];
				newSelectionPoints[0] = scene.mesh.vertices[face[0]];
				newSelectionPoints[1] = scene.mesh.vertices[face[1]];
				newSelectionPoints[2] = scene.mesh.vertices[face[2]];
				newSelectionPoints[3] = ray.GetPoint(minDist).cast<float>();
			}
		}
		if (!octPoints.IsEmpty()) {
			// find ray intersection with the points
			const MVS::IntersectRayPoints intRay(octPoints, ray, scene.pointcloud, window.minViews);
			if (intRay.pick.IsValid() && intRay.pick.dist < minDist) {
				window.selectionType = Window::SEL_POINT;
				minDist = intRay.pick.dist;
				newSelectionIdx = intRay.pick.idx;
				newSelectionPoints[0] = newSelectionPoints[3] = scene.pointcloud.points[newSelectionIdx];
			}
		}
		// check for camera intersection
		const TCone<REAL, 3> cone(ray, D2R(REAL(0.5)));
		const TConeIntersect<REAL, 3> coneIntersect(cone);
		FOREACH(idx, images) {
			const Image& image = images[idx];
			const MVS::Image& imageData = scene.images[image.idx];
			ASSERT(imageData.IsValid());
			REAL dist;
			if (coneIntersect.Classify(imageData.camera.C, dist) == VISIBLE && dist < minDist) {
				window.selectionType = Window::SEL_CAMERA;
				minDist = dist;
				newSelectionIdx = idx;
				newSelectionPoints[0] = newSelectionPoints[3] = imageData.camera.C;
			}
		}
		// check if we have a new selection
		if (window.selectionType != Window::SEL_NA) {
			window.selectionIdx = newSelectionIdx;
			window.selectionPoints[0] = newSelectionPoints[0];
			window.selectionPoints[1] = newSelectionPoints[1];
			window.selectionPoints[2] = newSelectionPoints[2];
			window.selectionPoints[3] = newSelectionPoints[3];
			window.selectionTime = now;
			switch (window.selectionType) {
			case Window::SEL_TRIANGLE: {
				DEBUG("Face selected:\n\tindex: %u\n\tvertex 1: %u (%g, %g, %g)\n\tvertex 2: %u (%g, %g, %g)\n\tvertex 3: %u (%g, %g, %g)",
					newSelectionIdx,
					scene.mesh.faces[newSelectionIdx][0], newSelectionPoints[0].x, newSelectionPoints[0].y, newSelectionPoints[0].z,
					scene.mesh.faces[newSelectionIdx][1], newSelectionPoints[1].x, newSelectionPoints[1].y, newSelectionPoints[1].z,
					scene.mesh.faces[newSelectionIdx][2], newSelectionPoints[2].x, newSelectionPoints[2].y, newSelectionPoints[2].z
				);
				break; }
			case Window::SEL_POINT: {
				DEBUG("Point selected:\n\tindex: %u (%g, %g, %g)%s",
					newSelectionIdx,
					newSelectionPoints[0].x, newSelectionPoints[0].y, newSelectionPoints[0].z,
					[&]() {
						if (scene.pointcloud.pointViews.empty())
							return String();
						const MVS::PointCloud::ViewArr& views = scene.pointcloud.pointViews[newSelectionIdx];
						ASSERT(!views.empty());
						String strViews(String::FormatString("\n\tviews: %u", views.size()));
						FOREACH(v, views) {
							const MVS::PointCloud::View idxImage = views[v];
							const MVS::Image& imageData = scene.images[idxImage];
							const Point2 x(imageData.camera.TransformPointW2I(Cast<REAL>(window.selectionPoints[0])));
							const float conf = scene.pointcloud.pointWeights.empty() ? 0.f : scene.pointcloud.pointWeights[newSelectionIdx][v];
							strViews += String::FormatString("\n\t\t%s (%.2f %.2f pixel, %.2f conf)", Util::getFileNameExt(imageData.name).c_str(), x.x, x.y, conf);
						}
						return strViews;
					}().c_str()
				);
				break; }
			case Window::SEL_CAMERA: {
				window.camera.prevCamID = window.camera.currentCamID = NO_ID;
				const Image& image = images[newSelectionIdx];
				const MVS::Image& imageData = scene.images[image.idx];
				const MVS::Camera& camera = imageData.camera;
				Point3 eulerAngles;
				camera.R.GetRotationAnglesZYX(eulerAngles.x, eulerAngles.y, eulerAngles.z);
				DEBUG("Camera selected:\n\tindex: %u (ID: %u)\n\tname: %s (mask %s)\n\timage size: %ux%u"
					"\n\tintrinsics: fx %.2f, fy %.2f, cx %.2f, cy %.2f"
					"\n\tposition: %g, %g, %g\n\trotation (deg): %.2f, %.2f, %.2f"
					"\n\taverage depth: %.2g\n\tneighbors: %u",
					image.idx, imageData.ID, Util::getFileNameExt(imageData.name).c_str(),
					imageData.maskName.empty() ? "none" : Util::getFileNameExt(imageData.maskName).c_str(),
					imageData.width, imageData.height,
					camera.K(0, 0), camera.K(1, 1), camera.K(0, 2), camera.K(1, 2),
					camera.C.x, camera.C.y, camera.C.z,
					R2D(eulerAngles.x), R2D(eulerAngles.y), R2D(eulerAngles.z),
					imageData.avgDepth, imageData.neighbors.size()
				);
				break; }
			}
		}
		break; }
	}
}
/*----------------------------------------------------------------*/
