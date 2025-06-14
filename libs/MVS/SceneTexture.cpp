/*
* SceneTexture.cpp
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
#include "RectsBinPack.h"
// connected components
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/connected_components.hpp>

using namespace MVS;


// D E F I N E S ///////////////////////////////////////////////////

// uncomment to enable multi-threading based on OpenMP
#ifdef _USE_OPENMP
#define TEXOPT_USE_OPENMP
#endif

// uncomment to use SparseLU for solving the linear systems
// (should be faster, but not working on old Eigen)
#if !defined(EIGEN_DEFAULT_TO_ROW_MAJOR) || EIGEN_WORLD_VERSION>3 || (EIGEN_WORLD_VERSION==3 && EIGEN_MAJOR_VERSION>2)
#define TEXOPT_SOLVER_SPARSELU
#endif

// method used to try to detect outlier face views
// (should enable more consistent textures, but it is not working)
#define TEXOPT_FACEOUTLIER_NA 0
#define TEXOPT_FACEOUTLIER_MEDIAN 1
#define TEXOPT_FACEOUTLIER_GAUSS_DAMPING 2
#define TEXOPT_FACEOUTLIER_GAUSS_CLAMPING 3
#define TEXOPT_FACEOUTLIER TEXOPT_FACEOUTLIER_GAUSS_CLAMPING

// method used to find optimal view per face
#define TEXOPT_INFERENCE_LBP 1
#define TEXOPT_INFERENCE TEXOPT_INFERENCE_LBP

// inference algorithm
#if TEXOPT_INFERENCE == TEXOPT_INFERENCE_LBP
#include "../Math/LBP.h"
namespace MVS {
constexpr LBPInference::EnergyType LBPMaxEnergy(1);
// Potts model as smoothness function
LBPInference::EnergyType STCALL SmoothnessPotts(LBPInference::NodeID, LBPInference::NodeID, LBPInference::LabelID l1, LBPInference::LabelID l2) {
	return l1 == l2 && l1 != 0 && l2 != 0 ? LBPInference::EnergyType(0) : LBPMaxEnergy;
}
}
#endif


// S T R U C T S ///////////////////////////////////////////////////

typedef Mesh::Vertex Vertex;
typedef Mesh::VIndex VIndex;
typedef Mesh::Face Face;
typedef Mesh::FIndex FIndex;
typedef Mesh::TexCoord TexCoord;
typedef Mesh::TexIndex TexIndex;

typedef int MatIdx;
typedef Eigen::Triplet<float,MatIdx> MatEntry;
typedef Eigen::SparseMatrix<float,Eigen::ColMajor,MatIdx> SparseMat;

enum Mask {
	empty = 0,
	border = 128,
	interior = 255
};

struct MeshTexture {
	// used to render the surface to a view camera
	typedef TImage<cuint32_t> FaceMap;
	struct RasterMesh : TRasterMesh<RasterMesh> {
		typedef TRasterMesh<RasterMesh> Base;
		FaceMap& faceMap;
		FIndex idxFace;
		Image8U mask;
		bool validFace;

		RasterMesh(const Mesh::VertexArr& _vertices, const Camera& _camera, DepthMap& _depthMap, FaceMap& _faceMap)
			: Base(_vertices, _camera, _depthMap), faceMap(_faceMap) {}
		void Clear() {
			Base::Clear();
			faceMap.memset((uint8_t)NO_ID);
		}
		void Raster(const ImageRef& pt, const Triangle& t, const Point3f& bary) {
			const Point3f pbary(PerspectiveCorrectBarycentricCoordinates(t, bary));
			const Depth z(ComputeDepth(t, pbary));
			ASSERT(z > Depth(0));
			Depth& depth = depthMap(pt);
			if (depth == 0 || depth > z) {
				depth = z;
				faceMap(pt) = validFace && (validFace = (mask(pt) != 0)) ? idxFace : NO_ID;
			}
		}
	};

	// used to represent a pixel color
	typedef Point3f Color;
	typedef CLISTDEF0(Color) Colors;

	// used to store info about a face (view, quality)
	struct FaceData {
		IIndex idxView;// the view seeing this face
		float quality; // how well the face is seen by this view
		#if TEXOPT_FACEOUTLIER != TEXOPT_FACEOUTLIER_NA
		Color color; // additionally store mean color (used to remove outliers)
		#endif
	};
	typedef cList<FaceData,const FaceData&,0,8,uint32_t> FaceDataArr; // store information about one face seen from several views
	typedef cList<FaceDataArr,const FaceDataArr&,2,1024,FIndex> FaceDataViewArr; // store data for all the faces of the mesh

	typedef cList<Mesh::FaceIdxArr, const Mesh::FaceIdxArr&,2,1024, FIndex> VirtualFaceIdxsArr; // store face indices for each virtual face

	// used to assign a view to a face
	typedef uint32_t Label;
	typedef cList<Label,Label,0,1024,FIndex> LabelArr;

	// represents a texture patch
	struct TexturePatch {
		Label label; // view index
		Mesh::FaceIdxArr faces; // indices of the faces contained by the patch
		RectsBinPack::Rect rect; // the bounding box in the view containing the patch
	};
	typedef cList<TexturePatch,const TexturePatch&,1,1024,FIndex> TexturePatchArr;

	// used to optimize texture patches
	struct SeamVertex {
		struct Patch {
			struct Edge {
				uint32_t idxSeamVertex; // the other vertex of this edge
				FIndex idxFace; // the face containing this edge in this patch

				inline Edge() {}
				inline Edge(uint32_t _idxSeamVertex) : idxSeamVertex(_idxSeamVertex) {}
				inline bool operator == (uint32_t _idxSeamVertex) const {
					return (idxSeamVertex == _idxSeamVertex);
				}
			};
			typedef cList<Edge,const Edge&,0,4,uint32_t> Edges;

			uint32_t idxPatch; // the patch containing this vertex
			Point2f proj; // the projection of this vertex in this patch
			Edges edges; // the edges starting from this vertex, contained in this patch (exactly two for manifold meshes)

			inline Patch() {}
			inline Patch(uint32_t _idxPatch) : idxPatch(_idxPatch) {}
			inline bool operator == (uint32_t _idxPatch) const {
				return (idxPatch == _idxPatch);
			}
		};
		typedef cList<Patch,const Patch&,1,4,uint32_t> Patches;

		VIndex idxVertex; // the index of this vertex
		Patches patches; // the patches meeting at this vertex (two or more)

		inline SeamVertex() {}
		inline SeamVertex(uint32_t _idxVertex) : idxVertex(_idxVertex) {}
		inline bool operator == (uint32_t _idxVertex) const {
			return (idxVertex == _idxVertex);
		}
		Patch& GetPatch(uint32_t idxPatch) {
			const uint32_t idx(patches.Find(idxPatch));
			if (idx == NO_ID)
				return patches.emplace_back(idxPatch);
			return patches[idx];
		}
		inline void SortByPatchIndex(IndexArr& indices) const {
			indices.resize(patches.size());
			std::iota(indices.Begin(), indices.End(), 0);
			std::sort(indices.Begin(), indices.End(), [&](IndexArr::Type i0, IndexArr::Type i1) -> bool {
				return patches[i0].idxPatch < patches[i1].idxPatch;
			});
		}
	};
	typedef cList<SeamVertex,const SeamVertex&,1,256,uint32_t> SeamVertices;

	// used to iterate vertex labels
	struct PatchIndex {
		bool bIndex;
		union {
			uint32_t idxPatch;
			uint32_t idxSeamVertex;
		};
	};
	typedef CLISTDEF0(PatchIndex) PatchIndices;
	struct VertexPatchIterator {
		uint32_t idx;
		uint32_t idxPatch;
		const SeamVertex::Patches* pPatches;
		inline VertexPatchIterator(const PatchIndex& patchIndex, const SeamVertices& seamVertices) : idx(NO_ID) {
			if (patchIndex.bIndex) {
				pPatches = &seamVertices[patchIndex.idxSeamVertex].patches;
			} else {
				idxPatch = patchIndex.idxPatch;
				pPatches = NULL;
			}
		}
		inline operator uint32_t () const {
			return idxPatch;
		}
		inline bool Next() {
			if (pPatches == NULL)
				return (idx++ == NO_ID);
			if (++idx >= pPatches->size())
				return false;
			idxPatch = (*pPatches)[idx].idxPatch;
			return true;
		}
	};

	// used to sample seam edges
	typedef TAccumulator<Color> AccumColor;
	typedef Sampler::Linear<float> Sampler;
	struct SampleImage {
		AccumColor accumColor;
		const Image8U3& image;
		const Sampler sampler;

		inline SampleImage(const Image8U3& _image) : image(_image), sampler() {}
		// sample the edge with linear weights
		void AddEdge(const TexCoord& p0, const TexCoord& p1) {
			const TexCoord p01(p1 - p0);
			const float length(norm(p01));
			ASSERT(length > 0.f);
			const int nSamples(ROUND2INT(MAXF(length, 1.f) * 2.f)-1);
			AccumColor edgeAccumColor;
			for (int s=0; s<nSamples; ++s) {
				const float len(static_cast<float>(s) / nSamples);
				const TexCoord samplePos(p0 + p01 * len);
				const Color color(image.sample<Sampler,Color>(sampler, samplePos));
				edgeAccumColor.Add(RGB2YCBCR(color), 1.f-len);
			}
			accumColor.Add(edgeAccumColor.Normalized(), length);
		}
		// returns accumulated color
		Color GetColor() const {
			return accumColor.Normalized();
		}
	};

	// used to interpolate adjustments color over the whole texture patch
	typedef TImage<Color> ColorMap;


public:
	MeshTexture(Scene& _scene, unsigned _nResolutionLevel=0, unsigned _nMinResolution=640);
	~MeshTexture();

	void ListVertexFaces();

	bool ListCameraFaces(FaceDataViewArr&, float fOutlierThreshold, int nIgnoreMaskLabel, const IIndexArr& views);

	#if TEXOPT_FACEOUTLIER != TEXOPT_FACEOUTLIER_NA
	bool FaceOutlierDetection(FaceDataArr& faceDatas, float fOutlierThreshold) const;
	#endif
	
	void CreateVirtualFaces(const FaceDataViewArr& facesDatas, FaceDataViewArr& virtualFacesDatas, VirtualFaceIdxsArr& virtualFaces, unsigned minCommonCameras=2, float thMaxNormalDeviation=25.f) const;
	IIndexArr SelectBestView(const FaceDataArr& faceDatas, FIndex fid, unsigned minCommonCameras, float ratioAngleToQuality) const;

	bool FaceViewSelection(unsigned minCommonCameras, float fOutlierThreshold, float fRatioDataSmoothness, int nIgnoreMaskLabel, const IIndexArr& views);
	
	void CreateSeamVertices();
	void GlobalSeamLeveling();
	void LocalSeamLeveling();
	void GenerateTexture(bool bGlobalSeamLeveling, bool bLocalSeamLeveling, unsigned nTextureSizeMultiple, unsigned nRectPackingHeuristic, Pixel8U colEmpty, float fSharpnessWeight, int maxTextureSize);

	template <typename PIXEL>
	static inline PIXEL RGB2YCBCR(const PIXEL& v) {
		typedef typename PIXEL::Type T;
		return PIXEL(
			v[0] * T(0.299) + v[1] * T(0.587) + v[2] * T(0.114),
			v[0] * T(-0.168736) + v[1] * T(-0.331264) + v[2] * T(0.5) + T(128),
			v[0] * T(0.5) + v[1] * T(-0.418688) + v[2] * T(-0.081312) + T(128)
		);
	}
	template <typename PIXEL>
	static inline PIXEL YCBCR2RGB(const PIXEL& v) {
		typedef typename PIXEL::Type T;
		const T v1(v[1] - T(128));
		const T v2(v[2] - T(128));
		return PIXEL(
			v[0]/* * T(1) + v1 * T(0)*/ + v2 * T(1.402),
			v[0]/* * T(1)*/ + v1 * T(-0.34414) + v2 * T(-0.71414),
			v[0]/* * T(1)*/ + v1 * T(1.772)/* + v2 * T(0)*/
		);
	}


protected:
	static void ProcessMask(Image8U& mask, int stripWidth);
	static void PoissonBlending(const Image32F3& src, Image32F3& dst, const Image8U& mask, float bias=1.f);


public:
	const unsigned nResolutionLevel; // how many times to scale down the images before mesh optimization
	const unsigned nMinResolution; // how many times to scale down the images before mesh optimization

	// store found texture patches
	TexturePatchArr texturePatches;

	// used to compute the seam leveling
	PairIdxArr seamEdges; // the (face-face) edges connecting different texture patches
	Mesh::FaceIdxArr components; // for each face, stores the texture patch index to which belongs
	IndexArr mapIdxPatch; // remap texture patch indices after invalid patches removal
	SeamVertices seamVertices; // array of vertices on the border between two or more patches

	// valid the entire time
	Mesh::VertexFacesArr& vertexFaces; // for each vertex, the list of faces containing it
	BoolArr& vertexBoundary; // for each vertex, stores if it is at the boundary or not
	Mesh::FaceFacesArr& faceFaces; // for each face, the list of adjacent faces, NO_ID for border edges (optional)
	Mesh::TexCoordArr& faceTexcoords; // for each face, the texture-coordinates of the vertices
	Mesh::TexIndexArr& faceTexindices; // for each face, the texture-coordinates of the vertices
	Mesh::Image8U3Arr& texturesDiffuse; // texture containing the diffuse color

	// constant the entire time
	Mesh::VertexArr& vertices;
	Mesh::FaceArr& faces;
	ImageArr& images;

	Scene& scene; // the mesh vertices and faces
};

// creating an invalid mask for the given image corresponding to
// the invalid pixels generated during image correction for the lens distortion;
// the returned mask has the same size as the image and is set to zero for invalid pixels
static Image8U DetectInvalidImageRegions(const Image8U3& image)
{
	const cv::Scalar upDiff(3);
	const int flags(8 | (255 << 8));
	Image8U mask(image.rows + 2, image.cols + 2);
	mask.memset(0);
	Image8U imageGray;
	cv::cvtColor(image, imageGray, cv::COLOR_BGR2GRAY);
	if (imageGray(0, 0) == 0)
		cv::floodFill(imageGray, mask, cv::Point(0, 0), 255, NULL, cv::Scalar(0), upDiff, flags);
	if (imageGray(image.rows / 2, 0) == 0)
		cv::floodFill(imageGray, mask, cv::Point(0, image.rows / 2), 255, NULL, cv::Scalar(0), upDiff, flags);
	if (imageGray(image.rows - 1, 0) == 0)
		cv::floodFill(imageGray, mask, cv::Point(0, image.rows - 1), 255, NULL, cv::Scalar(0), upDiff, flags);
	if (imageGray(image.rows - 1, image.cols / 2) == 0)
		cv::floodFill(imageGray, mask, cv::Point(image.cols / 2, image.rows - 1), 255, NULL, cv::Scalar(0), upDiff, flags);
	if (imageGray(image.rows - 1, image.cols - 1) == 0)
		cv::floodFill(imageGray, mask, cv::Point(image.cols - 1, image.rows - 1), 255, NULL, cv::Scalar(0), upDiff, flags);
	if (imageGray(image.rows / 2, image.cols - 1) == 0)
		cv::floodFill(imageGray, mask, cv::Point(image.cols - 1, image.rows / 2), 255, NULL, cv::Scalar(0), upDiff, flags);
	if (imageGray(0, image.cols - 1) == 0)
		cv::floodFill(imageGray, mask, cv::Point(image.cols - 1, 0), 255, NULL, cv::Scalar(0), upDiff, flags);
	if (imageGray(0, image.cols / 2) == 0)
		cv::floodFill(imageGray, mask, cv::Point(image.cols / 2, 0), 255, NULL, cv::Scalar(0), upDiff, flags);
	mask = (mask(cv::Rect(1,1, imageGray.cols,imageGray.rows)) == 0);
	return mask;
}

MeshTexture::MeshTexture(Scene& _scene, unsigned _nResolutionLevel, unsigned _nMinResolution)
	:
	nResolutionLevel(_nResolutionLevel),
	nMinResolution(_nMinResolution),
	vertexFaces(_scene.mesh.vertexFaces),
	vertexBoundary(_scene.mesh.vertexBoundary),
	faceFaces(_scene.mesh.faceFaces),
	faceTexcoords(_scene.mesh.faceTexcoords),
	faceTexindices(_scene.mesh.faceTexindices),
	texturesDiffuse(_scene.mesh.texturesDiffuse),
	vertices(_scene.mesh.vertices),
	faces(_scene.mesh.faces),
	images(_scene.images),
	scene(_scene)
{
}
MeshTexture::~MeshTexture()
{
	vertexFaces.Release();
	vertexBoundary.Release();
	faceFaces.Release();
}

// extract array of triangles incident to each vertex
// and check each vertex if it is at the boundary or not
void MeshTexture::ListVertexFaces()
{
	scene.mesh.EmptyExtra();
	scene.mesh.ListIncidentFaces();
	scene.mesh.ListBoundaryVertices();
	scene.mesh.ListIncidentFaceFaces();
}

// extract array of faces viewed by each image
bool MeshTexture::ListCameraFaces(FaceDataViewArr& facesDatas, float fOutlierThreshold, int nIgnoreMaskLabel, const IIndexArr& _views)
{
	// create faces octree
	Mesh::Octree octree;
	Mesh::FacesInserter::CreateOctree(octree, scene.mesh);

	// extract array of faces viewed by each image
	IIndexArr views(_views);
	if (views.empty()) {
		views.resize(images.size());
		std::iota(views.begin(), views.end(), IIndex(0));
	}
	facesDatas.resize(faces.size());
	Util::Progress progress(_T("Initialized views"), views.size());
	typedef float real;
	TImage<real> imageGradMag;
	TImage<real>::EMat mGrad[2];
	FaceMap faceMap;
	DepthMap depthMap;
	#ifdef TEXOPT_USE_OPENMP
	bool bAbort(false);
	#pragma omp parallel for private(imageGradMag, mGrad, faceMap, depthMap)
	for (int_t idx=0; idx<(int_t)views.size(); ++idx) {
		#pragma omp flush (bAbort)
		if (bAbort) {
			++progress;
			continue;
		}
		const IIndex idxView(views[(IIndex)idx]);
	#else
	for (IIndex idxView: views) {
	#endif
		Image& imageData = images[idxView];
		if (!imageData.IsValid()) {
			++progress;
			continue;
		}
		// load image
		unsigned level(nResolutionLevel);
		const unsigned imageSize(imageData.RecomputeMaxResolution(level, nMinResolution));
		if ((imageData.image.empty() || MAXF(imageData.width,imageData.height) != imageSize) && !imageData.ReloadImage(imageSize)) {
			#ifdef TEXOPT_USE_OPENMP
			bAbort = true;
			#pragma omp flush (bAbort)
			continue;
			#else
			return false;
			#endif
		}
		imageData.UpdateCamera(scene.platforms);
		// compute gradient magnitude
		imageData.image.toGray(imageGradMag, cv::COLOR_BGR2GRAY, true);
		cv::Mat grad[2];
		mGrad[0].resize(imageGradMag.rows, imageGradMag.cols);
		grad[0] = cv::Mat(imageGradMag.rows, imageGradMag.cols, cv::DataType<real>::type, (void*)mGrad[0].data());
		mGrad[1].resize(imageGradMag.rows, imageGradMag.cols);
		grad[1] = cv::Mat(imageGradMag.rows, imageGradMag.cols, cv::DataType<real>::type, (void*)mGrad[1].data());
		#if 1
		cv::Sobel(imageGradMag, grad[0], cv::DataType<real>::type, 1, 0, 3, 1.0/8.0);
		cv::Sobel(imageGradMag, grad[1], cv::DataType<real>::type, 0, 1, 3, 1.0/8.0);
		#elif 1
		const TMatrix<real,3,5> kernel(CreateDerivativeKernel3x5());
		cv::filter2D(imageGradMag, grad[0], cv::DataType<real>::type, kernel);
		cv::filter2D(imageGradMag, grad[1], cv::DataType<real>::type, kernel.t());
		#else
		const TMatrix<real,5,7> kernel(CreateDerivativeKernel5x7());
		cv::filter2D(imageGradMag, grad[0], cv::DataType<real>::type, kernel);
		cv::filter2D(imageGradMag, grad[1], cv::DataType<real>::type, kernel.t());
		#endif
		(TImage<real>::EMatMap)imageGradMag = (mGrad[0].cwiseAbs2()+mGrad[1].cwiseAbs2()).cwiseSqrt();
		// apply some blur on the gradient to lower noise/glossiness effects onto face-quality score
		cv::GaussianBlur(imageGradMag, imageGradMag, cv::Size(15, 15), 0, 0, cv::BORDER_DEFAULT);
		// select faces inside view frustum
		Mesh::FaceIdxArr cameraFaces;
		Mesh::FacesInserter inserter(cameraFaces);
		const TFrustum<float,5> frustum(Matrix3x4f(imageData.camera.P), (float)imageData.width, (float)imageData.height);
		octree.Traverse(frustum, inserter);
		// project all triangles in this view and keep the closest ones
		faceMap.create(imageData.GetSize());
		depthMap.create(imageData.GetSize());
		RasterMesh rasterer(vertices, imageData.camera, depthMap, faceMap);
		RasterMesh::Triangle triangle;
		RasterMesh::TriangleRasterizer triangleRasterizer(triangle, rasterer);
		if (nIgnoreMaskLabel >= 0) {
			// import mask
			BitMatrix bmask;
			DepthEstimator::ImportIgnoreMask(imageData, imageData.GetSize(), (uint8_t)OPTDENSE::nIgnoreMaskLabel, bmask, &rasterer.mask);
		} else if (nIgnoreMaskLabel == -1) {
			// creating mask to discard invalid regions created during image radial undistortion
			rasterer.mask = DetectInvalidImageRegions(imageData.image);
			#if TD_VERBOSE != TD_VERBOSE_OFF
			if (VERBOSITY_LEVEL > 3)
				cv::imwrite(String::FormatString("umask%04d.png", idxView), rasterer.mask);
			#endif
		}
		rasterer.Clear();
		for (FIndex idxFace : cameraFaces) {
			rasterer.validFace = true;
			const Face& facet = faces[idxFace];
			rasterer.idxFace = idxFace;
			rasterer.Project(facet, triangleRasterizer);
			if (!rasterer.validFace)
				rasterer.Project(facet, triangleRasterizer);
		}
		// compute the projection area of visible faces
		#if TEXOPT_FACEOUTLIER != TEXOPT_FACEOUTLIER_NA
		CLISTDEF0IDX(uint32_t,FIndex) areas(faces.size());
		areas.Memset(0);
		#endif

		#ifdef TEXOPT_USE_OPENMP
		#pragma omp critical
		#endif
		{
		// faceQuality is influenced by :
		// + area: the higher the area the more gradient scores will be added to the face quality
		// + sharpness: sharper image or image resolution or how close is to the face will result in higher gradient on the same face
		//				ON GLOSS IMAGES it happens to have a high volatile sharpness depending on how the light reflects under different angles
		// + angle: low angle increases the surface area
		for (int j=0; j<faceMap.rows; ++j) {
			for (int i=0; i<faceMap.cols; ++i) {
				const FIndex& idxFace = faceMap(j,i);
				ASSERT((idxFace == NO_ID && depthMap(j,i) == 0) || (idxFace != NO_ID && depthMap(j,i) > 0));
				if (idxFace == NO_ID)
					continue;
				FaceDataArr& faceDatas = facesDatas[idxFace];
				#if TEXOPT_FACEOUTLIER != TEXOPT_FACEOUTLIER_NA
				uint32_t& area = areas[idxFace];
				if (area++ == 0) {
				#else
				if (faceDatas.empty() || faceDatas.back().idxView != idxView) {
				#endif
					// create new face-data
					FaceData& faceData = faceDatas.emplace_back();
					faceData.idxView = idxView;
					faceData.quality = imageGradMag(j,i);
					#if TEXOPT_FACEOUTLIER != TEXOPT_FACEOUTLIER_NA
					faceData.color = imageData.image(j,i);
					#endif
				} else {
					// update face-data
					ASSERT(!faceDatas.empty());
					FaceData& faceData = faceDatas.back();
					ASSERT(faceData.idxView == idxView);
					faceData.quality += imageGradMag(j,i);
					#if TEXOPT_FACEOUTLIER != TEXOPT_FACEOUTLIER_NA
					faceData.color += Color(imageData.image(j,i));
					#endif
				}
			}
		}
		// adjust face quality with camera angle relative to face normal
		// tries to increase chances of a camera with perpendicular view on the surface (smoothened normals) to be selected
		FOREACH(idxFace, facesDatas) {
			FaceDataArr& faceDatas = facesDatas[idxFace];
			if (faceDatas.empty() || faceDatas.back().idxView != idxView)
				continue;
			const Face& f = faces[idxFace];
			const Vertex faceCenter((vertices[f[0]] + vertices[f[1]] + vertices[f[2]]) / 3.f);
			const Point3f camDir(Cast<Mesh::Type>(imageData.camera.C) - faceCenter);
			const Normal& faceNormal = scene.mesh.faceNormals[idxFace];
			const float cosFaceCam(MAXF(0.001f, ComputeAngle(camDir.ptr(), faceNormal.ptr())));
			faceDatas.back().quality *= SQUARE(cosFaceCam);
		}
		#if TEXOPT_FACEOUTLIER != TEXOPT_FACEOUTLIER_NA
		FOREACH(idxFace, areas) {
			const uint32_t& area = areas[idxFace];
			if (area > 0) {
				Color& color = facesDatas[idxFace].back().color;
				color = RGB2YCBCR(Color(color * (1.f/(float)area)));
			}
		}
		#endif
		}
		++progress;
	}
	#ifdef TEXOPT_USE_OPENMP
	if (bAbort)
		return false;
	#endif
	progress.close();

	#if TEXOPT_FACEOUTLIER != TEXOPT_FACEOUTLIER_NA
	if (fOutlierThreshold > 0) {
		// try to detect outlier views for each face
		// (views for which the face is occluded by a dynamic object in the scene, ex. pedestrians)
		for (FaceDataArr& faceDatas: facesDatas)
			FaceOutlierDetection(faceDatas, fOutlierThreshold);
	}
	#endif
	return true;
}

// order the camera view scores with highest score first and return the list of first <minCommonCameras> cameras
// ratioAngleToQuality represents the ratio in witch we combine normal angle to quality for a face to obtain the selection score
//  - a ratio of 1 means only angle is considered
//  - a ratio of 0.5 means angle and quality are equally important
//  - a ratio of 0 means only camera quality is considered when sorting
IIndexArr MeshTexture::SelectBestView(const FaceDataArr& faceDatas, FIndex fid, unsigned minCommonCameras, float ratioAngleToQuality) const
{
	ASSERT(!faceDatas.empty());
	#if 1
	
	// compute scores based on the view quality and its angle to the face normal
	float maxQuality = 0;
	for (const FaceData& faceData: faceDatas)
		maxQuality = MAXF(maxQuality, faceData.quality);
	const Face& f = faces[fid];
	const Vertex faceCenter((vertices[f[0]] + vertices[f[1]] + vertices[f[2]]) / 3.f);
	CLISTDEF0IDX(float,IIndex) scores(faceDatas.size());
	FOREACH(idxFaceData, faceDatas) {
		const FaceData& faceData = faceDatas[idxFaceData];
		const Image& imageData = images[faceData.idxView];
		const Point3f camDir(Cast<Mesh::Type>(imageData.camera.C) - faceCenter);
		const Normal& faceNormal = scene.mesh.faceNormals[fid];
		const float cosFaceCam(ComputeAngle(camDir.ptr(), faceNormal.ptr()));
		scores[idxFaceData] = ratioAngleToQuality*cosFaceCam + (1.f-ratioAngleToQuality)*faceData.quality/maxQuality;
	}
	// and sort the scores from to highest to smallest to get the best overall cameras
	IIndexArr scorePodium(faceDatas.size());
	std::iota(scorePodium.begin(), scorePodium.end(), 0);
	scorePodium.Sort([&scores](IIndex i, IIndex j) {
		return scores[i] > scores[j];
	});

	#else
	
	// sort qualityPodium in relation to faceDatas[index].quality decreasing
	IIndexArr qualityPodium(faceDatas.size());
	std::iota(qualityPodium.begin(), qualityPodium.end(), 0);
	qualityPodium.Sort([&faceDatas](IIndex i, IIndex j) {
		return faceDatas[i].quality > faceDatas[j].quality;
	});

	// sort anglePodium in relation to face angle to camera increasing
	const Face& f = faces[fid];
	const Vertex faceCenter((vertices[f[0]] + vertices[f[1]] + vertices[f[2]]) / 3.f);
	CLISTDEF0IDX(float,IIndex) cameraAngles(0, faceDatas.size());
	for (const FaceData& faceData: faceDatas) {
		const Image& imageData = images[faceData.idxView];
		const Point3f camDir(Cast<Mesh::Type>(imageData.camera.C) - faceCenter);
		const Normal& faceNormal = scene.mesh.faceNormals[fid];
		const float cosFaceCam(ComputeAngle(camDir.ptr(), faceNormal.ptr()));
		cameraAngles.emplace_back(cosFaceCam);
	}
	IIndexArr anglePodium(faceDatas.size());
	std::iota(anglePodium.begin(), anglePodium.end(), 0);
	anglePodium.Sort([&cameraAngles](IIndex i, IIndex j) {
		return cameraAngles[i] > cameraAngles[j];
	});

	// combine podium scores to get overall podium
	// and sort the scores in smallest to highest to get the best overall camera for current virtual face
	CLISTDEF0IDX(float,IIndex) scores(faceDatas.size());
	scores.Memset(0);
	FOREACH(sIdx, faceDatas) {
		scores[anglePodium[sIdx]] += ratioAngleToQuality * (sIdx+1);
		scores[qualityPodium[sIdx]] += (1.f - ratioAngleToQuality) * (sIdx+1);
	}
	IIndexArr scorePodium(faceDatas.size());
	std::iota(scorePodium.begin(), scorePodium.end(), 0);
	scorePodium.Sort([&scores](IIndex i, IIndex j) {
		return scores[i] < scores[j];
	});
	
	#endif
	IIndexArr cameras(MIN(minCommonCameras, faceDatas.size()));
	FOREACH(i, cameras)
		cameras[i] = faceDatas[scorePodium[i]].idxView;
	return cameras;
}

static bool IsFaceVisible(const MeshTexture::FaceDataArr& faceDatas, const IIndexArr& cameraList) {
	size_t camFoundCounter(0);
	for (const MeshTexture::FaceData& faceData : faceDatas) {
		const IIndex cfCam = faceData.idxView;
		for (IIndex camId : cameraList) {
			if (cfCam == camId) {
				if (++camFoundCounter == cameraList.size())
					return true;	
				break;
			}
		}
	}
	return camFoundCounter == cameraList.size();
}

// build virtual faces with:
// - similar normal
// - high percentage of common images that see them
void MeshTexture::CreateVirtualFaces(const FaceDataViewArr& facesDatas, FaceDataViewArr& virtualFacesDatas, VirtualFaceIdxsArr& virtualFaces, unsigned minCommonCameras, float thMaxNormalDeviation) const
{
	const float ratioAngleToQuality(0.67f);
	const float cosMaxNormalDeviation(COS(FD2R(thMaxNormalDeviation)));
	Mesh::FaceIdxArr remainingFaces(faces.size());
	std::iota(remainingFaces.begin(), remainingFaces.end(), 0);
	std::vector<bool> selectedFaces(faces.size(), false);
	cQueue<FIndex, FIndex, 0> currentVirtualFaceQueue;
	std::unordered_set<FIndex> queuedFaces;
	do {
		const FIndex startPos = RAND() % remainingFaces.size();
		const FIndex virtualFaceCenterFaceID = remainingFaces[startPos];
		ASSERT(currentVirtualFaceQueue.IsEmpty());
		const Normal& normalCenter = scene.mesh.faceNormals[virtualFaceCenterFaceID];
		const FaceDataArr& centerFaceDatas = facesDatas[virtualFaceCenterFaceID];
		// select the common cameras
		Mesh::FaceIdxArr virtualFace;
		FaceDataArr virtualFaceDatas;
		if (centerFaceDatas.empty()) {
			virtualFace.emplace_back(virtualFaceCenterFaceID);
			selectedFaces[virtualFaceCenterFaceID] = true;
			const auto posToErase = remainingFaces.FindFirst(virtualFaceCenterFaceID);
			ASSERT(posToErase != Mesh::FaceIdxArr::NO_INDEX);
			remainingFaces.RemoveAtMove(posToErase);
		} else {
			const IIndexArr selectedCams = SelectBestView(centerFaceDatas, virtualFaceCenterFaceID, minCommonCameras, ratioAngleToQuality);
			currentVirtualFaceQueue.AddTail(virtualFaceCenterFaceID);
			queuedFaces.clear();
			do {
				const FIndex currentFaceId = currentVirtualFaceQueue.GetHead();
				currentVirtualFaceQueue.PopHead();
				// check for condition to add in current virtual face
				// normal angle smaller than thMaxNormalDeviation degrees
				const Normal& faceNormal = scene.mesh.faceNormals[currentFaceId];
				const float cosFaceToCenter(ComputeAngleN(normalCenter.ptr(), faceNormal.ptr()));
				if (cosFaceToCenter < cosMaxNormalDeviation)
					continue;
				// check if current face is seen by all cameras in selectedCams
				ASSERT(!selectedCams.empty());
				if (!IsFaceVisible(facesDatas[currentFaceId], selectedCams))
					continue;
				// remove it from remaining faces and add it to the virtual face
				{
					const auto posToErase = remainingFaces.FindFirst(currentFaceId);
					ASSERT(posToErase != Mesh::FaceIdxArr::NO_INDEX);
					remainingFaces.RemoveAtMove(posToErase);
					selectedFaces[currentFaceId] = true;
					virtualFace.push_back(currentFaceId);
				}
				// add all new neighbors to the queue
				const Mesh::FaceFaces& ffaces = faceFaces[currentFaceId];
				for (int i = 0; i < 3; ++i) {
					const FIndex fIdx = ffaces[i];
					if (fIdx == NO_ID)
						continue;
					if (!selectedFaces[fIdx] && queuedFaces.find(fIdx) == queuedFaces.end()) {
						currentVirtualFaceQueue.AddTail(fIdx);
						queuedFaces.emplace(fIdx);
					}
				}
			} while (!currentVirtualFaceQueue.IsEmpty());
			// compute virtual face quality and create virtual face
			for (IIndex idxView: selectedCams) {
				FaceData& virtualFaceData = virtualFaceDatas.emplace_back();
				virtualFaceData.quality = 0;
				virtualFaceData.idxView = idxView;
				#if TEXOPT_FACEOUTLIER != TEXOPT_FACEOUTLIER_NA
				virtualFaceData.color = Point3f::ZERO;
				#endif
				unsigned processedFaces(0);
				for (FIndex fid : virtualFace) {
					const FaceDataArr& faceDatas = facesDatas[fid];
					for (FaceData& faceData: faceDatas) {
						if (faceData.idxView == idxView) {
							virtualFaceData.quality += faceData.quality;
							#if TEXOPT_FACEOUTLIER != TEXOPT_FACEOUTLIER_NA
							virtualFaceData.color += faceData.color;
							#endif
							++processedFaces;
							break;
						}
					}
				}
				ASSERT(processedFaces > 0);
				virtualFaceData.quality /= processedFaces;
				#if TEXOPT_FACEOUTLIER != TEXOPT_FACEOUTLIER_NA
				virtualFaceData.color /= processedFaces;
				#endif
			}
			ASSERT(!virtualFaceDatas.empty());
		}
		virtualFacesDatas.emplace_back(std::move(virtualFaceDatas));
		virtualFaces.emplace_back(std::move(virtualFace));
	} while (!remainingFaces.empty());
}

#if TEXOPT_FACEOUTLIER == TEXOPT_FACEOUTLIER_MEDIAN

// decrease the quality of / remove all views in which the face's projection
// has a much different color than in the majority of views
bool MeshTexture::FaceOutlierDetection(FaceDataArr& faceDatas, float thOutlier) const
{
	// consider as outlier if the absolute difference to the median is outside this threshold
	if (thOutlier <= 0)
		thOutlier = 0.15f*255.f;

	// init colors array
	if (faceDatas.size() <= 3)
		return false;
	FloatArr channels[3];
	for (int c=0; c<3; ++c)
		channels[c].resize(faceDatas.size());
	FOREACH(i, faceDatas) {
		const Color& color = faceDatas[i].color;
		for (int c=0; c<3; ++c)
			channels[c][i] = color[c];
	}

	// find median
	for (int c=0; c<3; ++c)
		channels[c].Sort();
	const unsigned idxMedian(faceDatas.size() >> 1);
	Color median;
	for (int c=0; c<3; ++c)
		median[c] = channels[c][idxMedian];

	// abort if there are not at least 3 inliers
	int nInliers(0);
	BoolArr inliers(faceDatas.size());
	FOREACH(i, faceDatas) {
		const Color& color = faceDatas[i].color;
		for (int c=0; c<3; ++c) {
			if (ABS(median[c]-color[c]) > thOutlier) {
				inliers[i] = false;
				goto CONTINUE_LOOP;
			}
		}
		inliers[i] = true;
		++nInliers;
		CONTINUE_LOOP:;
	}
	if (nInliers == faceDatas.size())
		return true;
	if (nInliers < 3)
		return false;

	// remove outliers
	RFOREACH(i, faceDatas)
		if (!inliers[i])
			faceDatas.RemoveAt(i);
	return true;
}

#elif TEXOPT_FACEOUTLIER != TEXOPT_FACEOUTLIER_NA

// A multi-variate normal distribution which is NOT normalized such that the integral is 1
// - centered is the vector for which the function is to be evaluated with the mean subtracted [Nx1]
// - X is the vector for which the function is to be evaluated [Nx1]
// - mu is the mean around which the distribution is centered [Nx1]
// - covarianceInv is the inverse of the covariance matrix [NxN]
// return exp(-1/2 * (X-mu)^T * covariance_inv * (X-mu))
template <typename T, int N>
inline T MultiGaussUnnormalized(const Eigen::Matrix<T,N,1>& centered, const Eigen::Matrix<T,N,N>& covarianceInv) {
	return EXP(T(-0.5) * T(centered.adjoint() * covarianceInv * centered));
}
template <typename T, int N>
inline T MultiGaussUnnormalized(const Eigen::Matrix<T,N,1>& X, const Eigen::Matrix<T,N,1>& mu, const Eigen::Matrix<T,N,N>& covarianceInv) {
	return MultiGaussUnnormalized<T,N>(X - mu, covarianceInv);
}

// decrease the quality of / remove all views in which the face's projection
// has a much different color than in the majority of views
bool MeshTexture::FaceOutlierDetection(FaceDataArr& faceDatas, float thOutlier) const
{
	// reject all views whose gauss value is below this threshold
	if (thOutlier <= 0)
		thOutlier = 6e-2f;

	const float minCovariance(1e-3f); // if all covariances drop below this the outlier detection aborted

	const unsigned maxIterations(10);
	const unsigned minInliers(4);

	// init colors array
	if (faceDatas.size() <= minInliers)
		return false;
	Eigen::Matrix3Xd colorsAll(3, faceDatas.size());
	BoolArr inliers(faceDatas.size());
	FOREACH(i, faceDatas) {
		colorsAll.col(i) = ((const Color::EVec)faceDatas[i].color).cast<double>();
		inliers[i] = true;
	}

	// perform outlier removal; abort if something goes wrong
	// (number of inliers below threshold or can not invert the covariance)
	size_t numInliers(faceDatas.size());
	Eigen::Vector3d mean;
	Eigen::Matrix3d covariance;
	Eigen::Matrix3d covarianceInv;
	for (unsigned iter = 0; iter < maxIterations; ++iter) {
		// compute the mean color and color covariance only for inliers
		const Eigen::Block<Eigen::Matrix3Xd,3,Eigen::Dynamic,!Eigen::Matrix3Xd::IsRowMajor> colors(colorsAll.leftCols(numInliers));
		mean = colors.rowwise().mean();
		const Eigen::Matrix3Xd centered(colors.colwise() - mean);
		covariance = (centered * centered.transpose()) / double(colors.cols() - 1);

		// stop if all covariances gets very small
		if (covariance.array().abs().maxCoeff() < minCovariance) {
			// remove the outliers
			RFOREACH(i, faceDatas)
				if (!inliers[i])
					faceDatas.RemoveAt(i);
			return true;
		}

		// invert the covariance matrix
		// (FullPivLU not the fastest, but gives feedback about numerical stability during inversion)
		const Eigen::FullPivLU<Eigen::Matrix3d> lu(covariance);
		if (!lu.isInvertible())
			return false;
		covarianceInv = lu.inverse();

		// filter inliers
		// (all views with a gauss value above the threshold)
		numInliers = 0;
		bool bChanged(false);
		FOREACH(i, faceDatas) {
			const Eigen::Vector3d color(((const Color::EVec)faceDatas[i].color).cast<double>());
			const double gaussValue(MultiGaussUnnormalized<double,3>(color, mean, covarianceInv));
			bool& inlier = inliers[i];
			if (gaussValue > thOutlier) {
				// set as inlier
				colorsAll.col(numInliers++) = color;
				if (inlier != true) {
					inlier = true;
					bChanged = true;
				}
			} else {
				// set as outlier
				if (inlier != false) {
					inlier = false;
					bChanged = true;
				}
			}
		}
		if (numInliers == faceDatas.size())
			return true;
		if (numInliers < minInliers)
			return false;
		if (!bChanged)
			break;
	}

	#if TEXOPT_FACEOUTLIER == TEXOPT_FACEOUTLIER_GAUSS_DAMPING
	// select the final inliers
	const float factorOutlierRemoval(0.2f);
	covarianceInv *= factorOutlierRemoval;
	RFOREACH(i, faceDatas) {
		const Eigen::Vector3d color(((const Color::EVec)faceDatas[i].color).cast<double>());
		const double gaussValue(MultiGaussUnnormalized<double,3>(color, mean, covarianceInv));
		ASSERT(gaussValue >= 0 && gaussValue <= 1);
		faceDatas[i].quality *= gaussValue;
	}
	#endif
	#if TEXOPT_FACEOUTLIER == TEXOPT_FACEOUTLIER_GAUSS_CLAMPING
	// remove outliers
	RFOREACH(i, faceDatas)
		if (!inliers[i])
			faceDatas.RemoveAt(i);
	#endif
	return true;
}
#endif

bool MeshTexture::FaceViewSelection(unsigned minCommonCameras, float fOutlierThreshold, float fRatioDataSmoothness, int nIgnoreMaskLabel, const IIndexArr& views)
{
	// extract array of triangles incident to each vertex
	ListVertexFaces();

	// create texture patches
	{
		// compute face normals and smoothen them
		scene.mesh.SmoothNormalFaces();

		// list all views for each face
		FaceDataViewArr facesDatas;
		if (!ListCameraFaces(facesDatas, fOutlierThreshold, nIgnoreMaskLabel, views))
			return false;

		// create faces graph
		typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS> Graph;
		typedef boost::graph_traits<Graph>::edge_iterator EdgeIter;
		typedef boost::graph_traits<Graph>::out_edge_iterator EdgeOutIter;
		Graph graph;
		LabelArr labels;

		// construct and use virtual faces for patch creation instead of actual mesh faces;
		// the virtual faces are composed of coplanar triangles sharing same views
		const bool bUseVirtualFaces(minCommonCameras > 0);
		if (bUseVirtualFaces) {
			// 1) create FaceToVirtualFaceMap
			FaceDataViewArr virtualFacesDatas;
			VirtualFaceIdxsArr virtualFaces; // stores each virtual face as an array of mesh face ID
			CreateVirtualFaces(facesDatas, virtualFacesDatas, virtualFaces, minCommonCameras);
			Mesh::FaceIdxArr mapFaceToVirtualFace(faces.size()); // for each mesh face ID, store the virtual face ID witch contains it
			size_t controlCounter(0);
			FOREACH(idxVF, virtualFaces) {
				const Mesh::FaceIdxArr& vf = virtualFaces[idxVF];
				for (FIndex idxFace : vf) {
					mapFaceToVirtualFace[idxFace] = idxVF;
					++controlCounter;
				}
			}
			ASSERT(controlCounter == faces.size());
			// 2) create function to find virtual faces neighbors
			VirtualFaceIdxsArr virtualFaceNeighbors; { // for each virtual face, the list of virtual faces with at least one vertex in common
				virtualFaceNeighbors.resize(virtualFaces.size());
				FOREACH(idxVF, virtualFaces) {
					const Mesh::FaceIdxArr& vf = virtualFaces[idxVF];
					Mesh::FaceIdxArr& vfNeighbors = virtualFaceNeighbors[idxVF];
					for (FIndex idxFace : vf) {
						const Mesh::FaceFaces& adjFaces = faceFaces[idxFace];
						for (int i = 0; i < 3; ++i) {
							const FIndex fAdj(adjFaces[i]);
							if (fAdj == NO_ID)
								continue;
							if (mapFaceToVirtualFace[fAdj] == idxVF)
								continue;
							if (fAdj != idxFace && vfNeighbors.Find(mapFaceToVirtualFace[fAdj]) == Mesh::FaceIdxArr::NO_INDEX) {
								vfNeighbors.emplace_back(mapFaceToVirtualFace[fAdj]);
							}
						}
					}
				}
			}
			// 3) use virtual faces to build the graph
			// 4) assign images to virtual faces
			// 5) spread image ID to each mesh face from virtual face
			FOREACH(idxFace, virtualFaces) {
				MAYBEUNUSED const Mesh::FIndex idx((Mesh::FIndex)boost::add_vertex(graph));
				ASSERT(idx == idxFace);
			}
			FOREACH(idxVirtualFace, virtualFaces) {
				const Mesh::FaceIdxArr& afaces = virtualFaceNeighbors[idxVirtualFace];
				for (FIndex idxVirtualFaceAdj: afaces) {
					if (idxVirtualFace >= idxVirtualFaceAdj)
						continue;
					const bool bInvisibleFace(virtualFacesDatas[idxVirtualFace].empty());
					const bool bInvisibleFaceAdj(virtualFacesDatas[idxVirtualFaceAdj].empty());
					if (bInvisibleFace || bInvisibleFaceAdj)
						continue;
					boost::add_edge(idxVirtualFace, idxVirtualFaceAdj, graph);
				}
			}
			ASSERT((Mesh::FIndex)boost::num_vertices(graph) == virtualFaces.size());
			// assign the best view to each face
			labels.resize(faces.size()); {
				// normalize quality values
				float maxQuality(0);
				for (const FaceDataArr& faceDatas: virtualFacesDatas) {
					for (const FaceData& faceData: faceDatas)
						if (maxQuality < faceData.quality)
							maxQuality = faceData.quality;
				}
				Histogram32F hist(std::make_pair(0.f, maxQuality), 1000);
				for (const FaceDataArr& faceDatas: virtualFacesDatas) {
					for (const FaceData& faceData: faceDatas)
						hist.Add(faceData.quality);
				}
				const float normQuality(hist.GetApproximatePermille(0.95f));

				#if TEXOPT_INFERENCE == TEXOPT_INFERENCE_LBP
				// initialize inference structures
				const LBPInference::EnergyType MaxEnergy(fRatioDataSmoothness*LBPMaxEnergy);
				LBPInference inference; {
					inference.SetNumNodes(virtualFaces.size());
					inference.SetSmoothCost(SmoothnessPotts);
					EdgeOutIter ei, eie;
					FOREACH(f, virtualFaces) {
						for (boost::tie(ei, eie) = boost::out_edges(f, graph); ei != eie; ++ei) {
							ASSERT(f == (FIndex)ei->m_source);
							const FIndex fAdj((FIndex)ei->m_target);
							if (f < fAdj) // add edges only once
								inference.SetNeighbors(f, fAdj);
						}
					}
				}

				// set data costs for all labels (except label 0 - undefined)
				FOREACH(f, virtualFacesDatas) {
					const FaceDataArr& faceDatas = virtualFacesDatas[f];
					if (faceDatas.empty()) {
						// set costs for label 0 (undefined)
						inference.SetDataCost(Label(0), f, MaxEnergy);
						continue;
					}
					for (const FaceData& faceData: faceDatas) {
						const Label label((Label)faceData.idxView+1);
						const float normalizedQuality(faceData.quality>=normQuality ? 1.f : faceData.quality/normQuality);
						const float dataCost((1.f-normalizedQuality)*MaxEnergy);
						inference.SetDataCost(label, f, dataCost);
					}
				}

				// assign the optimal view (label) to each face
				// (label 0 is reserved as undefined)
				inference.Optimize();

				// extract resulting labeling
				LabelArr virtualLabels(virtualFaces.size());
				virtualLabels.Memset(0xFF);
				FOREACH(l, virtualLabels) {
					const Label label(inference.GetLabel(l));
					ASSERT(label < images.size()+1);
					if (label > 0)
						virtualLabels[l] = label-1;
				}
				FOREACH(l, labels) {
					labels[l] = virtualLabels[mapFaceToVirtualFace[l]];
				}
				#endif
			}

			graph.clear();
		}
		
		// create the graph of faces: each vertex is a face and the edges are the edges shared by the faces
		FOREACH(idxFace, faces) {
			MAYBEUNUSED const Mesh::FIndex idx((Mesh::FIndex)boost::add_vertex(graph));
			ASSERT(idx == idxFace);
		}
		FOREACH(idxFace, faces) {
			const Mesh::FaceFaces& afaces = faceFaces[idxFace];
			for (int v=0; v<3; ++v) {
				const FIndex idxFaceAdj = afaces[v];
				if (idxFaceAdj == NO_ID || idxFace >= idxFaceAdj)
					continue;
				const bool bInvisibleFace(facesDatas[idxFace].empty());
				const bool bInvisibleFaceAdj(facesDatas[idxFaceAdj].empty());
				if (bInvisibleFace || bInvisibleFaceAdj) {
					if (bInvisibleFace != bInvisibleFaceAdj)
						seamEdges.emplace_back(idxFace, idxFaceAdj);
					continue;
				}
				boost::add_edge(idxFace, idxFaceAdj, graph);
			}
		}
		faceFaces.Release();
		ASSERT((Mesh::FIndex)boost::num_vertices(graph) == faces.size());

		// start patch creation starting directly from individual faces
		if (!bUseVirtualFaces) {
			// assign the best view to each face
			labels.resize(faces.size()); {
				// normalize quality values
				float maxQuality(0);
				for (const FaceDataArr& faceDatas: facesDatas) {
					for (const FaceData& faceData: faceDatas)
						if (maxQuality < faceData.quality)
							maxQuality = faceData.quality;
				}
				Histogram32F hist(std::make_pair(0.f, maxQuality), 1000);
				for (const FaceDataArr& faceDatas: facesDatas) {
					for (const FaceData& faceData: faceDatas)
						hist.Add(faceData.quality);
				}
				const float normQuality(hist.GetApproximatePermille(0.95f));

				#if TEXOPT_INFERENCE == TEXOPT_INFERENCE_LBP
				// initialize inference structures
				const LBPInference::EnergyType MaxEnergy(fRatioDataSmoothness*LBPMaxEnergy);
				LBPInference inference; {
					inference.SetNumNodes(faces.size());
					inference.SetSmoothCost(SmoothnessPotts);
					EdgeOutIter ei, eie;
					FOREACH(f, faces) {
						for (boost::tie(ei, eie) = boost::out_edges(f, graph); ei != eie; ++ei) {
							ASSERT(f == (FIndex)ei->m_source);
							const FIndex fAdj((FIndex)ei->m_target);
							if (f < fAdj) // add edges only once
								inference.SetNeighbors(f, fAdj);
						}
					}
				}

				// set data costs for all labels (except label 0 - undefined)
				FOREACH(f, facesDatas) {
					const FaceDataArr& faceDatas = facesDatas[f];
					if (faceDatas.empty()) {
						// set costs for label 0 (undefined)
						inference.SetDataCost(Label(0), f, MaxEnergy);
						continue;
					}
					for (const FaceData& faceData: faceDatas) {
						const Label label((Label)faceData.idxView+1);
						const float normalizedQuality(faceData.quality>=normQuality ? 1.f : faceData.quality/normQuality);
						const float dataCost((1.f-normalizedQuality)*MaxEnergy);
						inference.SetDataCost(label, f, dataCost);
					}
				}

				// assign the optimal view (label) to each face
				// (label 0 is reserved as undefined)
				inference.Optimize();

				// extract resulting labeling
				labels.Memset(0xFF);
				FOREACH(l, labels) {
					const Label label(inference.GetLabel(l));
					ASSERT(label < images.size()+1);
					if (label > 0)
						labels[l] = label-1;
				}
				#endif
			}
		}

		// create texture patches
		{
			// divide graph in sub-graphs of connected faces having the same label
			EdgeIter ei, eie;
			const PairIdxArr::IDX startLabelSeamEdges(seamEdges.size());
			for (boost::tie(ei, eie) = boost::edges(graph); ei != eie; ++ei) {
				const FIndex fSource((FIndex)ei->m_source);
				const FIndex fTarget((FIndex)ei->m_target);
				ASSERT(components.empty() || components[fSource] == components[fTarget]);
				if (labels[fSource] != labels[fTarget])
					seamEdges.emplace_back(fSource, fTarget);
			}
			for (const PairIdx *pEdge=seamEdges.Begin()+startLabelSeamEdges, *pEdgeEnd=seamEdges.End(); pEdge!=pEdgeEnd; ++pEdge)
				boost::remove_edge(pEdge->i, pEdge->j, graph);

			// find connected components: texture patches
			ASSERT((FIndex)boost::num_vertices(graph) == faces.size());
			components.resize(faces.size());
			const FIndex nComponents(boost::connected_components(graph, components.data()));

			// create texture patches;
			// last texture patch contains all faces with no texture
			LabelArr sizes(nComponents);
			sizes.Memset(0);
			FOREACH(c, components)
				++sizes[components[c]];
			texturePatches.resize(nComponents+1);
			texturePatches.back().label = NO_ID;
			FOREACH(f, faces) {
				const Label label(labels[f]);
				const FIndex c(components[f]);
				TexturePatch& texturePatch = texturePatches[c];
				ASSERT(texturePatch.label == label || texturePatch.faces.empty());
				if (label == NO_ID) {
					texturePatch.label = NO_ID;
					texturePatches.back().faces.Insert(f);
				} else {
					if (texturePatch.faces.empty()) {
						texturePatch.label = label;
						texturePatch.faces.reserve(sizes[c]);
					}
					texturePatch.faces.Insert(f);
				}
			}
			// remove all patches with invalid label (except the last one)
			// and create the map from the old index to the new one
			mapIdxPatch.resize(nComponents);
			std::iota(mapIdxPatch.Begin(), mapIdxPatch.End(), 0);
			for (FIndex t = nComponents; t-- > 0; ) {
				if (texturePatches[t].label == NO_ID) {
					texturePatches.RemoveAtMove(t);
					mapIdxPatch.RemoveAtMove(t);
				}
			}
			const unsigned numPatches(texturePatches.size()-1);
			uint32_t idxPatch(0);
			for (IndexArr::IDX i=0; i<mapIdxPatch.size(); ++i) {
				while (i < mapIdxPatch[i])
					mapIdxPatch.InsertAt(i++, numPatches);
				mapIdxPatch[i] = idxPatch++;
			}
			while (mapIdxPatch.size() <= nComponents)
				mapIdxPatch.Insert(numPatches);
		}
	}
	return true;
}


// create seam vertices and edges
void MeshTexture::CreateSeamVertices()
{
	// each vertex will contain the list of patches it separates,
	// except the patch containing invisible faces;
	// each patch contains the list of edges belonging to that texture patch, starting from that vertex
	// (usually there are pairs of edges in each patch, representing the two edges starting from that vertex separating two valid patches)
	VIndex vs[2];
	uint32_t vs0[2], vs1[2];
	std::unordered_map<VIndex, uint32_t> mapVertexSeam;
	const unsigned numPatches(texturePatches.size()-1);
	for (const PairIdx& edge: seamEdges) {
		// store edge for the later seam optimization
		ASSERT(edge.i < edge.j);
		const uint32_t idxPatch0(mapIdxPatch[components[edge.i]]);
		const uint32_t idxPatch1(mapIdxPatch[components[edge.j]]);
		ASSERT(idxPatch0 != idxPatch1 || idxPatch0 == numPatches);
		if (idxPatch0 == idxPatch1)
			continue;
		seamVertices.ReserveExtra(2);
		scene.mesh.GetEdgeVertices(edge.i, edge.j, vs0, vs1);
		ASSERT(faces[edge.i][vs0[0]] == faces[edge.j][vs1[0]]);
		ASSERT(faces[edge.i][vs0[1]] == faces[edge.j][vs1[1]]);
		vs[0] = faces[edge.i][vs0[0]];
		vs[1] = faces[edge.i][vs0[1]];

		const auto itSeamVertex0(mapVertexSeam.emplace(std::make_pair(vs[0], seamVertices.size())));
		if (itSeamVertex0.second)
			seamVertices.emplace_back(vs[0]);
		SeamVertex& seamVertex0 = seamVertices[itSeamVertex0.first->second];

		const auto itSeamVertex1(mapVertexSeam.emplace(std::make_pair(vs[1], seamVertices.size())));
		if (itSeamVertex1.second)
			seamVertices.emplace_back(vs[1]);
		SeamVertex& seamVertex1 = seamVertices[itSeamVertex1.first->second];

		if (idxPatch0 < numPatches) {
			const TexCoord offset0(texturePatches[idxPatch0].rect.tl());
			SeamVertex::Patch& patch00 = seamVertex0.GetPatch(idxPatch0);
			SeamVertex::Patch& patch10 = seamVertex1.GetPatch(idxPatch0);
			ASSERT(patch00.edges.Find(itSeamVertex1.first->second) == NO_ID);
			patch00.edges.emplace_back(itSeamVertex1.first->second).idxFace = edge.i;
			patch00.proj = faceTexcoords[edge.i*3+vs0[0]]+offset0;
			ASSERT(patch10.edges.Find(itSeamVertex0.first->second) == NO_ID);
			patch10.edges.emplace_back(itSeamVertex0.first->second).idxFace = edge.i;
			patch10.proj = faceTexcoords[edge.i*3+vs0[1]]+offset0;
		}
		if (idxPatch1 < numPatches) {
			const TexCoord offset1(texturePatches[idxPatch1].rect.tl());
			SeamVertex::Patch& patch01 = seamVertex0.GetPatch(idxPatch1);
			SeamVertex::Patch& patch11 = seamVertex1.GetPatch(idxPatch1);
			ASSERT(patch01.edges.Find(itSeamVertex1.first->second) == NO_ID);
			patch01.edges.emplace_back(itSeamVertex1.first->second).idxFace = edge.j;
			patch01.proj = faceTexcoords[edge.j*3+vs1[0]]+offset1;
			ASSERT(patch11.edges.Find(itSeamVertex0.first->second) == NO_ID);
			patch11.edges.emplace_back(itSeamVertex0.first->second).idxFace = edge.j;
			patch11.proj = faceTexcoords[edge.j*3+vs1[1]]+offset1;
		}
	}
	seamEdges.Release();
}

void MeshTexture::GlobalSeamLeveling()
{
	ASSERT(!seamVertices.empty());
	const unsigned numPatches(texturePatches.size()-1);

	// find the patch ID for each vertex
	PatchIndices patchIndices(vertices.size());
	patchIndices.Memset(0);
	FOREACH(f, faces) {
		const uint32_t idxPatch(mapIdxPatch[components[f]]);
		const Face& face = faces[f];
		for (int v=0; v<3; ++v)
			patchIndices[face[v]].idxPatch = idxPatch;
	}
	FOREACH(i, seamVertices) {
		const SeamVertex& seamVertex = seamVertices[i];
		ASSERT(!seamVertex.patches.empty());
		PatchIndex& patchIndex = patchIndices[seamVertex.idxVertex];
		patchIndex.bIndex = true;
		patchIndex.idxSeamVertex = i;
	}

	// assign a row index within the solution vector x to each vertex/patch
	ASSERT(vertices.size() < static_cast<VIndex>(std::numeric_limits<MatIdx>::max()));
	MatIdx rowsX(0);
	typedef std::unordered_map<uint32_t,MatIdx> VertexPatch2RowMap;
	cList<VertexPatch2RowMap> vertpatch2rows(vertices.size());
	FOREACH(i, vertices) {
		const PatchIndex& patchIndex = patchIndices[i];
		VertexPatch2RowMap& vertpatch2row = vertpatch2rows[i];
		if (patchIndex.bIndex) {
			// vertex is part of multiple patches
			const SeamVertex& seamVertex = seamVertices[patchIndex.idxSeamVertex];
			ASSERT(seamVertex.idxVertex == i);
			for (const SeamVertex::Patch& patch: seamVertex.patches) {
				ASSERT(patch.idxPatch != numPatches);
				vertpatch2row[patch.idxPatch] = rowsX++;
			}
		} else
		if (patchIndex.idxPatch < numPatches) {
			// vertex is part of only one patch
			vertpatch2row[patchIndex.idxPatch] = rowsX++;
		}
	}

	// fill Tikhonov's Gamma matrix (regularization constraints)
	const float lambda(0.1f);
	MatIdx rowsGamma(0);
	Mesh::VertexIdxArr adjVerts;
	CLISTDEF0(MatEntry) rows(0, vertices.size()*4);
	FOREACH(v, vertices) {
		adjVerts.Empty();
		scene.mesh.GetAdjVertices(v, adjVerts);
		VertexPatchIterator itV(patchIndices[v], seamVertices);
		while (itV.Next()) {
			const uint32_t idxPatch(itV);
			if (idxPatch == numPatches)
				continue;
			const MatIdx col(vertpatch2rows[v].at(idxPatch));
			for (const VIndex vAdj: adjVerts) {
				if (v >= vAdj)
					continue;
				VertexPatchIterator itVAdj(patchIndices[vAdj], seamVertices);
				while (itVAdj.Next()) {
					const uint32_t idxPatchAdj(itVAdj);
					if (idxPatch == idxPatchAdj) {
						const MatIdx colAdj(vertpatch2rows[vAdj].at(idxPatchAdj));
						rows.emplace_back(rowsGamma, col, lambda);
						rows.emplace_back(rowsGamma, colAdj, -lambda);
						++rowsGamma;
					}
				}
			}
		}
	}
	ASSERT(rows.size()/2 < static_cast<IDX>(std::numeric_limits<MatIdx>::max()));

	SparseMat Gamma(rowsGamma, rowsX);
	Gamma.setFromTriplets(rows.Begin(), rows.End());
	rows.Empty();

	// fill the matrix A and the coefficients for the Vector b of the linear equation system
	IndexArr indices;
	Colors vertexColors;
	Colors coeffB;
	for (const SeamVertex& seamVertex: seamVertices) {
		if (seamVertex.patches.size() < 2)
			continue;
		seamVertex.SortByPatchIndex(indices);
		vertexColors.resize(indices.size());
		FOREACH(i, indices) {
			const SeamVertex::Patch& patch0 = seamVertex.patches[indices[i]];
			ASSERT(patch0.idxPatch < numPatches);
			SampleImage sampler(images[texturePatches[patch0.idxPatch].label].image);
			for (const SeamVertex::Patch::Edge& edge: patch0.edges) {
				const SeamVertex& seamVertex1 = seamVertices[edge.idxSeamVertex];
				const SeamVertex::Patches::IDX idxPatch1(seamVertex1.patches.Find(patch0.idxPatch));
				ASSERT(idxPatch1 != SeamVertex::Patches::NO_INDEX);
				const SeamVertex::Patch& patch1 = seamVertex1.patches[idxPatch1];
				sampler.AddEdge(patch0.proj, patch1.proj);
			}
			vertexColors[i] = sampler.GetColor();
		}
		const VertexPatch2RowMap& vertpatch2row = vertpatch2rows[seamVertex.idxVertex];
		for (IDX i=0; i<indices.size()-1; ++i) {
			const uint32_t idxPatch0(seamVertex.patches[indices[i]].idxPatch);
			const Color& color0 = vertexColors[i];
			const MatIdx col0(vertpatch2row.at(idxPatch0));
			for (IDX j=i+1; j<indices.size(); ++j) {
				const uint32_t idxPatch1(seamVertex.patches[indices[j]].idxPatch);
				const Color& color1 = vertexColors[j];
				const MatIdx col1(vertpatch2row.at(idxPatch1));
				ASSERT(idxPatch0 < idxPatch1);
				const MatIdx rowA((MatIdx)coeffB.size());
				coeffB.Insert(color1 - color0);
				ASSERT(ISFINITE(coeffB.back()));
				rows.emplace_back(rowA, col0,  1.f);
				rows.emplace_back(rowA, col1, -1.f);
			}
		}
	}
	ASSERT(coeffB.size() < static_cast<IDX>(std::numeric_limits<MatIdx>::max()));

	const MatIdx rowsA((MatIdx)coeffB.size());
	SparseMat A(rowsA, rowsX);
	A.setFromTriplets(rows.Begin(), rows.End());
	rows.Release();

	SparseMat Lhs(A.transpose() * A + Gamma.transpose() * Gamma);
	// CG uses only the lower triangle, so prune the rest and compress matrix
	Lhs.prune([](const int& row, const int& col, const float&) -> bool {
		return col <= row;
	});

	// globally solve for the correction colors
	Eigen::Matrix<float,Eigen::Dynamic,3,Eigen::RowMajor> colorAdjustments(rowsX, 3);
	{
		// init CG solver
		Eigen::ConjugateGradient<SparseMat, Eigen::Lower> solver;
		solver.setMaxIterations(1000);
		solver.setTolerance(0.0001f);
		solver.compute(Lhs);
		ASSERT(solver.info() == Eigen::Success);
		#ifdef TEXOPT_USE_OPENMP
		#pragma omp parallel for
		#endif
		for (int channel=0; channel<3; ++channel) {
			// init right hand side vector
			const Eigen::Map< Eigen::VectorXf, Eigen::Unaligned, Eigen::Stride<0,3> > b(coeffB.front().ptr()+channel, rowsA);
			const Eigen::VectorXf Rhs(SparseMat(A.transpose()) * b);
			// solve for x
			const Eigen::VectorXf x(solver.solve(Rhs));
			ASSERT(solver.info() == Eigen::Success);
			// subtract mean since the system is under-constrained and
			// we need the solution with minimal adjustments
			Eigen::Map< Eigen::VectorXf, Eigen::Unaligned, Eigen::Stride<0,3> >(colorAdjustments.data()+channel, rowsX) = x.array() - x.mean();
			DEBUG_LEVEL(3, "\tcolor channel %d: %d iterations, %g residual", channel, solver.iterations(), solver.error());
		}
	}

	// adjust texture patches using the correction colors
	#ifdef TEXOPT_USE_OPENMP
	#pragma omp parallel for schedule(dynamic)
	for (int i=0; i<(int)numPatches; ++i) {
	#else
	for (unsigned i=0; i<numPatches; ++i) {
	#endif
		const uint32_t idxPatch((uint32_t)i);
		TexturePatch& texturePatch = texturePatches[idxPatch];
		ColorMap imageAdj(texturePatch.rect.size());
		imageAdj.memset(0);
		// interpolate color adjustments over the whole patch
		struct RasterPatch {
			const TexCoord* tri;
			Color colors[3];
			ColorMap& image;
			inline RasterPatch(ColorMap& _image) : image(_image) {}
			inline cv::Size Size() const { return image.size(); }
			inline void operator()(const ImageRef& pt, const Point3f& bary) {
				ASSERT(image.isInside(pt));
				image(pt) = colors[0]*bary.x + colors[1]*bary.y + colors[2]*bary.z;
			}
		} data(imageAdj);
		for (const FIndex idxFace: texturePatch.faces) {
			const Face& face = faces[idxFace];
			data.tri = faceTexcoords.Begin()+idxFace*3;
			for (int v=0; v<3; ++v)
				data.colors[v] = colorAdjustments.row(vertpatch2rows[face[v]].at(idxPatch));
			// render triangle and for each pixel interpolate the color adjustment
			// from the triangle corners using barycentric coordinates
			ColorMap::RasterizeTriangleBary(data.tri[0], data.tri[1], data.tri[2], data);
		}
		// dilate with one pixel width, in order to make sure patch border smooths out a little
		imageAdj.DilateMean<1>(imageAdj, Color::ZERO);
		// apply color correction to the patch image
		cv::Mat image(images[texturePatch.label].image(texturePatch.rect));
		for (int r=0; r<image.rows; ++r) {
			for (int c=0; c<image.cols; ++c) {
				const Color& a = imageAdj(r,c);
				if (a == Color::ZERO)
					continue;
				Pixel8U& v = image.at<Pixel8U>(r,c);
				const Color col(RGB2YCBCR(Color(v)));
				const Color acol(YCBCR2RGB(Color(col+a)));
				for (int p=0; p<3; ++p)
					v[p] = (uint8_t)CLAMP(ROUND2INT(acol[p]), 0, 255);
			}
		}
	}
}

// set to one in order to dilate also on the diagonal of the border
// (normally not needed)
#define DILATE_EXTRA 0
void MeshTexture::ProcessMask(Image8U& mask, int stripWidth)
{
	typedef Image8U::Type Type;

	// dilate and erode around the border,
	// in order to fill all gaps and remove outside pixels
	// (due to imperfect overlay of the raster line border and raster faces)
	#define DILATEDIR(rd,cd) { \
		Type& vi = mask(r+(rd),c+(cd)); \
		if (vi != border) \
			vi = interior; \
	}
	const int HalfSize(1);
	const int RowsEnd(mask.rows-HalfSize);
	const int ColsEnd(mask.cols-HalfSize);
	for (int r=HalfSize; r<RowsEnd; ++r) {
		for (int c=HalfSize; c<ColsEnd; ++c) {
			const Type v(mask(r,c));
			if (v != border)
				continue;
			#if DILATE_EXTRA
			for (int i=-HalfSize; i<=HalfSize; ++i) {
				const int rw(r+i);
				for (int j=-HalfSize; j<=HalfSize; ++j) {
					const int cw(c+j);
					Type& vi = mask(rw,cw);
					if (vi != border)
						vi = interior;
				}
			}
			#else
			DILATEDIR(-1, 0);
			DILATEDIR(1, 0);
			DILATEDIR(0, -1);
			DILATEDIR(0, 1);
			#endif
		}
	}
	#undef DILATEDIR
	#define ERODEDIR(rd,cd) { \
		const int rl(r-(rd)), cl(c-(cd)), rr(r+(rd)), cr(c+(cd)); \
		const Type vl(mask.isInside(ImageRef(cl,rl)) ? mask(rl,cl) : uint8_t(empty)); \
		const Type vr(mask.isInside(ImageRef(cr,rr)) ? mask(rr,cr) : uint8_t(empty)); \
		if ((vl == border && vr == empty) || (vr == border && vl == empty)) { \
			v = empty; \
			continue; \
		} \
	}
	#if DILATE_EXTRA
	for (int i=0; i<2; ++i)
	#endif
	for (int r=0; r<mask.rows; ++r) {
		for (int c=0; c<mask.cols; ++c) {
			Type& v = mask(r,c);
			if (v != interior)
				continue;
			ERODEDIR(0, 1);
			ERODEDIR(1, 0);
			ERODEDIR(1, 1);
			ERODEDIR(-1, 1);
		}
	}
	#undef ERODEDIR

	// mark all interior pixels with empty neighbors as border
	for (int r=0; r<mask.rows; ++r) {
		for (int c=0; c<mask.cols; ++c) {
			Type& v = mask(r,c);
			if (v != interior)
				continue;
			if (mask(r-1,c) == empty ||
				mask(r,c-1) == empty ||
				mask(r+1,c) == empty ||
				mask(r,c+1) == empty)
				v = border;
		}
	}

	#if 0
	// mark all interior pixels with border neighbors on two sides as border
	{
	Image8U orgMask;
	mask.copyTo(orgMask);
	for (int r=0; r<mask.rows; ++r) {
		for (int c=0; c<mask.cols; ++c) {
			Type& v = mask(r,c);
			if (v != interior)
				continue;
			if ((orgMask(r+1,c+0) == border && orgMask(r+0,c+1) == border) ||
				(orgMask(r+1,c+0) == border && orgMask(r-0,c-1) == border) ||
				(orgMask(r-1,c-0) == border && orgMask(r+0,c+1) == border) ||
				(orgMask(r-1,c-0) == border && orgMask(r-0,c-1) == border))
				v = border;
		}
	}
	}
	#endif

	// compute the set of valid pixels at the border of the texture patch
	#define ISEMPTY(mask, x,y) (mask(y,x) == empty)
	const int width(mask.width()), height(mask.height());
	typedef std::unordered_set<ImageRef,std::hash<ImageRef::Base>> PixelSet;
	PixelSet borderPixels;
	for (int y=0; y<height; ++y) {
		for (int x=0; x<width; ++x) {
			if (ISEMPTY(mask, x,y))
				continue;
			// valid border pixels need no invalid neighbors
			if (x == 0 || x == width - 1 || y == 0 || y == height - 1) {
				borderPixels.insert(ImageRef(x,y));
				continue;
			}
			// check the direct neighborhood of all invalid pixels
			for (int j=-1; j<=1; ++j) {
				for (int i=-1; i<=1; ++i) {
					// if the valid pixel has an invalid neighbor...
					const int xn(x+i), yn(y+j);
					if (ISINSIDE(xn, 0, width) &&
						ISINSIDE(yn, 0, height) &&
						ISEMPTY(mask, xn,yn)) {
						// add the pixel to the set of valid border pixels
						borderPixels.insert(ImageRef(x,y));
						goto CONTINUELOOP;
					}
				}
			}
			CONTINUELOOP:;
		}
	}

	// iteratively erode all border pixels
	{
	Image8U orgMask;
	mask.copyTo(orgMask);
	typedef std::vector<ImageRef> PixelVector;
	for (int s=0; s<stripWidth; ++s) {
		PixelVector emptyPixels(borderPixels.begin(), borderPixels.end());
		borderPixels.clear();
		// mark the new empty pixels as empty in the mask
		for (PixelVector::const_iterator it=emptyPixels.cbegin(); it!=emptyPixels.cend(); ++it)
			orgMask(*it) = empty;
		// find the set of valid pixels at the border of the valid area
		for (PixelVector::const_iterator it=emptyPixels.cbegin(); it!=emptyPixels.cend(); ++it) {
			for (int j=-1; j<=1; j++) {
				for (int i=-1; i<=1; i++) {
					const int xn(it->x+i), yn(it->y+j);
					if (ISINSIDE(xn, 0, width) &&
						ISINSIDE(yn, 0, height) &&
						!ISEMPTY(orgMask, xn, yn))
						borderPixels.insert(ImageRef(xn,yn));
				}
			}
		}
	}
	#undef ISEMPTY

	// mark all remaining pixels empty in the mask
	for (int y=0; y<height; ++y) {
		for (int x=0; x<width; ++x) {
			if (orgMask(y,x) != empty)
				mask(y,x) = empty;
		}
	}
	}

	// mark all border pixels
	for (PixelSet::const_iterator it=borderPixels.cbegin(); it!=borderPixels.cend(); ++it)
		mask(*it) = border;

	#if 0
	// dilate border
	{
	Image8U orgMask;
	mask.copyTo(orgMask);
	for (int r=HalfSize; r<RowsEnd; ++r) {
		for (int c=HalfSize; c<ColsEnd; ++c) {
			const Type v(orgMask(r, c));
			if (v != border)
				continue;
			for (int i=-HalfSize; i<=HalfSize; ++i) {
				const int rw(r+i);
				for (int j=-HalfSize; j<=HalfSize; ++j) {
					const int cw(c+j);
					Type& vi = mask(rw, cw);
					if (vi == empty)
						vi = border;
				}
			}
		}
	}
	}
	#endif
}

inline MeshTexture::Color ColorLaplacian(const Image32F3& img, int i) {
	const int width(img.width());
	return img(i-width) + img(i-1) + img(i+1) + img(i+width) - img(i)*4.f;
}

void MeshTexture::PoissonBlending(const Image32F3& src, Image32F3& dst, const Image8U& mask, float bias)
{
	ASSERT(src.width() == mask.width() && src.width() == dst.width());
	ASSERT(src.height() == mask.height() && src.height() == dst.height());
	ASSERT(src.channels() == 3 && dst.channels() == 3 && mask.channels() == 1);
	ASSERT(src.type() == CV_32FC3 && dst.type() == CV_32FC3 && mask.type() == CV_8U);

	#ifndef _RELEASE
	// check the mask border has no pixels marked as interior
	for (int x=0; x<mask.cols; ++x)
		ASSERT(mask(0,x) != interior && mask(mask.rows-1,x) != interior);
	for (int y=0; y<mask.rows; ++y)
		ASSERT(mask(y,0) != interior && mask(y,mask.cols-1) != interior);
	#endif

	const int n(dst.area());
	const int width(dst.width());

	TImage<MatIdx> indices(dst.size());
	indices.memset(0xff);
	MatIdx nnz(0);
	for (int i = 0; i < n; ++i)
		if (mask(i) != empty)
			indices(i) = nnz++;
	if (nnz <= 0)
		return;

	Colors coeffB(nnz);
	CLISTDEF0(MatEntry) coeffA(0, nnz);
	for (int i = 0; i < n; ++i) {
		switch (mask(i)) {
		case border: {
			const MatIdx idx(indices(i));
			ASSERT(idx != -1);
			coeffA.emplace_back(idx, idx, 1.f);
			coeffB[idx] = (const Color&)dst(i);
		} break;
		case interior: {
			const MatIdx idxUp(indices(i - width));
			const MatIdx idxLeft(indices(i - 1));
			const MatIdx idxCenter(indices(i));
			const MatIdx idxRight(indices(i + 1));
			const MatIdx idxDown(indices(i + width));
			// all indices should be either border conditions or part of the optimization
			ASSERT(idxUp != -1 && idxLeft != -1 && idxCenter != -1 && idxRight != -1 && idxDown != -1);
			coeffA.emplace_back(idxCenter, idxUp, 1.f);
			coeffA.emplace_back(idxCenter, idxLeft, 1.f);
			coeffA.emplace_back(idxCenter, idxCenter,-4.f);
			coeffA.emplace_back(idxCenter, idxRight, 1.f);
			coeffA.emplace_back(idxCenter, idxDown, 1.f);
			// set target coefficient
			coeffB[idxCenter] = (bias == 1.f ?
								 ColorLaplacian(src,i) :
								 ColorLaplacian(src,i)*bias + ColorLaplacian(dst,i)*(1.f-bias));
		} break;
		}
	}

	SparseMat A(nnz, nnz);
	A.setFromTriplets(coeffA.Begin(), coeffA.End());
	coeffA.Release();

	#ifdef TEXOPT_SOLVER_SPARSELU
	// use SparseLU factorization
	// (faster, but not working if EIGEN_DEFAULT_TO_ROW_MAJOR is defined, bug inside Eigen)
	const Eigen::SparseLU< SparseMat, Eigen::COLAMDOrdering<MatIdx> > solver(A);
	#else
	// use BiCGSTAB solver
	const Eigen::BiCGSTAB< SparseMat, Eigen::IncompleteLUT<float> > solver(A);
	#endif
	ASSERT(solver.info() == Eigen::Success);
	for (int channel=0; channel<3; ++channel) {
		const Eigen::Map< Eigen::VectorXf, Eigen::Unaligned, Eigen::Stride<0,3> > b(coeffB.front().ptr()+channel, nnz);
		const Eigen::VectorXf x(solver.solve(b));
		ASSERT(solver.info() == Eigen::Success);
		for (int i = 0; i < n; ++i) {
			const MatIdx index(indices(i));
			if (index != -1)
				dst(i)[channel] = x[index];
		}
	}
}

void MeshTexture::LocalSeamLeveling()
{
	ASSERT(!seamVertices.empty());
	const unsigned numPatches(texturePatches.size()-1);

	// adjust texture patches locally, so that the border continues smoothly inside the patch
	#ifdef TEXOPT_USE_OPENMP
	#pragma omp parallel for schedule(dynamic)
	for (int i=0; i<(int)numPatches; ++i) {
	#else
	for (unsigned i=0; i<numPatches; ++i) {
	#endif
		const uint32_t idxPatch((uint32_t)i);
		const TexturePatch& texturePatch = texturePatches[idxPatch];
		// extract image
		const Image8U3& image0(images[texturePatch.label].image);
		Image32F3 image, imageOrg;
		image0(texturePatch.rect).convertTo(image, CV_32FC3, 1.0/255.0);
		image.copyTo(imageOrg);
		// render patch coverage
		Image8U mask(image.size()); {
			mask.memset(0);
			struct RasterMesh {
				Image8U& image;
				inline void operator()(const ImageRef& pt) {
					ASSERT(image.isInside(pt));
					image(pt) = interior;
				}
			} data{mask};
			for (const FIndex idxFace: texturePatch.faces) {
				const TexCoord* tri = faceTexcoords.data()+idxFace*3;
				ColorMap::RasterizeTriangle(tri[0], tri[1], tri[2], data);
			}
		}
		// render the patch border meeting neighbor patches
		const Sampler sampler;
		const TexCoord offset(texturePatch.rect.tl());
		for (const SeamVertex& seamVertex0: seamVertices) {
			if (seamVertex0.patches.size() < 2)
				continue;
			const uint32_t idxVertPatch0(seamVertex0.patches.Find(idxPatch));
			if (idxVertPatch0 == SeamVertex::Patches::NO_INDEX)
				continue;
			const SeamVertex::Patch& patch0 = seamVertex0.patches[idxVertPatch0];
			const TexCoord p0(patch0.proj-offset);
			// for each edge of this vertex belonging to this patch...
			for (const SeamVertex::Patch::Edge& edge0: patch0.edges) {
				// select the same edge leaving from the adjacent vertex
				const SeamVertex& seamVertex1 = seamVertices[edge0.idxSeamVertex];
				const uint32_t idxVertPatch0Adj(seamVertex1.patches.Find(idxPatch));
				ASSERT(idxVertPatch0Adj != SeamVertex::Patches::NO_INDEX);
				const SeamVertex::Patch& patch0Adj = seamVertex1.patches[idxVertPatch0Adj];
				const TexCoord p0Adj(patch0Adj.proj-offset);
				// find the other patch sharing the same edge (edge with same adjacent vertex)
				FOREACH(idxVertPatch1, seamVertex0.patches) {
					if (idxVertPatch1 == idxVertPatch0)
						continue;
					const SeamVertex::Patch& patch1 = seamVertex0.patches[idxVertPatch1];
					const uint32_t idxEdge1(patch1.edges.Find(edge0.idxSeamVertex));
					if (idxEdge1 == SeamVertex::Patch::Edges::NO_INDEX)
						continue;
					const TexCoord& p1(patch1.proj);
					// select the same edge belonging to the second patch leaving from the adjacent vertex
					const uint32_t idxVertPatch1Adj(seamVertex1.patches.Find(patch1.idxPatch));
					ASSERT(idxVertPatch1Adj != SeamVertex::Patches::NO_INDEX);
					const SeamVertex::Patch& patch1Adj = seamVertex1.patches[idxVertPatch1Adj];
					const TexCoord& p1Adj(patch1Adj.proj);
					// this is an edge separating two (valid) patches;
					// draw it on this patch as the mean color of the two patches
					const Image8U3& image1(images[texturePatches[patch1.idxPatch].label].image);
					struct RasterPatch {
						Image32F3& image;
						Image8U& mask;
						const Image32F3& image0;
						const Image8U3& image1;
						const TexCoord p0, p0Dir;
						const TexCoord p1, p1Dir;
						const float length;
						const Sampler sampler;
						inline RasterPatch(Image32F3& _image, Image8U& _mask, const Image32F3& _image0, const Image8U3& _image1,
							const TexCoord& _p0, const TexCoord& _p0Adj, const TexCoord& _p1, const TexCoord& _p1Adj)
							: image(_image), mask(_mask), image0(_image0), image1(_image1),
							p0(_p0), p0Dir(_p0Adj-_p0), p1(_p1), p1Dir(_p1Adj-_p1), length((float)norm(p0Dir)), sampler() {}
						inline void operator()(const ImageRef& pt) {
							const float l((float)norm(TexCoord(pt)-p0)/length);
							// compute mean color
							const TexCoord samplePos0(p0 + p0Dir * l);
							const Color color0(image0.sample<Sampler,Color>(sampler, samplePos0));
							const TexCoord samplePos1(p1 + p1Dir * l);
							const Color color1(image1.sample<Sampler,Color>(sampler, samplePos1)/255.f);
							image(pt) = Color((color0 + color1) * 0.5f);
							// set mask edge also
							mask(pt) = border;
						}
					} data(image, mask, imageOrg, image1, p0, p0Adj, p1, p1Adj);
					Image32F3::DrawLine(p0, p0Adj, data);
					// skip remaining patches,
					// as a manifold edge is shared by maximum two face (one in each patch), which we found already
					break;
				}
			}
			// render the vertex at the patch border meeting neighbor patches
			AccumColor accumColor;
			// for each patch...
			for (const SeamVertex::Patch& patch: seamVertex0.patches) {
				// add its view to the vertex mean color
				const Image8U3& img(images[texturePatches[patch.idxPatch].label].image);
				accumColor.Add(img.sample<Sampler,Color>(sampler, patch.proj)/255.f, 1.f);
			}
			const ImageRef pt(ROUND2INT(patch0.proj-offset));
			image(pt) = accumColor.Normalized();
			mask(pt) = border;
		}
		// make sure the border is continuous and
		// keep only the exterior tripe of the given size
		ProcessMask(mask, 20);
		// compute texture patch blending
		PoissonBlending(imageOrg, image, mask);
		// apply color correction to the patch image
		cv::Mat imagePatch(image0(texturePatch.rect));
		for (int r=0; r<image.rows; ++r) {
			for (int c=0; c<image.cols; ++c) {
				if (mask(r,c) == empty)
					continue;
				const Color& a = image(r,c);
				Pixel8U& v = imagePatch.at<Pixel8U>(r,c);
				for (int p=0; p<3; ++p)
					v[p] = (uint8_t)CLAMP(ROUND2INT(a[p]*255.f), 0, 255);
			}
		}
	}
}

void MeshTexture::GenerateTexture(bool bGlobalSeamLeveling, bool bLocalSeamLeveling, unsigned nTextureSizeMultiple, unsigned nRectPackingHeuristic, Pixel8U colEmpty, float fSharpnessWeight, int maxTextureSize)
{
	// project patches in the corresponding view and compute texture-coordinates and bounding-box
	const int border(2);
	faceTexcoords.resize(faces.size()*3);
	faceTexindices.resize(faces.size());
	#ifdef TEXOPT_USE_OPENMP
	const unsigned numPatches(texturePatches.size()-1);
	#pragma omp parallel for schedule(dynamic)
	for (int_t idx=0; idx<(int_t)numPatches; ++idx) {
		TexturePatch& texturePatch = texturePatches[(uint32_t)idx];
	#else
	for (TexturePatch *pTexturePatch=texturePatches.Begin(), *pTexturePatchEnd=texturePatches.End()-1; pTexturePatch<pTexturePatchEnd; ++pTexturePatch) {
		TexturePatch& texturePatch = *pTexturePatch;
	#endif
		const Image& imageData = images[texturePatch.label];
		AABB2f aabb(true);
		for (const FIndex idxFace: texturePatch.faces) {
			const Face& face = faces[idxFace];
			TexCoord* texcoords = faceTexcoords.data()+idxFace*3;
			for (int i=0; i<3; ++i) {
				texcoords[i] = imageData.camera.ProjectPointP(vertices[face[i]]);
				ASSERT(imageData.image.isInsideWithBorder(texcoords[i], border));
				aabb.InsertFull(texcoords[i]);
			}
		}
		// compute relative texture coordinates
		ASSERT(imageData.image.isInside(Point2f(aabb.ptMin)));
		ASSERT(imageData.image.isInside(Point2f(aabb.ptMax)));
		texturePatch.rect.x = FLOOR2INT(aabb.ptMin[0])-border;
		texturePatch.rect.y = FLOOR2INT(aabb.ptMin[1])-border;
		texturePatch.rect.width = CEIL2INT(aabb.ptMax[0]-aabb.ptMin[0])+border*2;
		texturePatch.rect.height = CEIL2INT(aabb.ptMax[1]-aabb.ptMin[1])+border*2;
		ASSERT(imageData.image.isInside(texturePatch.rect.tl()));
		ASSERT(imageData.image.isInside(texturePatch.rect.br()));
		const TexCoord offset(texturePatch.rect.tl());
		for (const FIndex idxFace: texturePatch.faces) {
			TexCoord* texcoords = faceTexcoords.data()+idxFace*3;
			for (int v=0; v<3; ++v)
				texcoords[v] -= offset;
		}
	}
	{
		// init last patch to point to a small uniform color patch
		TexturePatch& texturePatch = texturePatches.back();
		const int sizePatch(border*2+1);
		texturePatch.rect = cv::Rect(0,0, sizePatch,sizePatch);
		for (const FIndex idxFace: texturePatch.faces) {
			TexCoord* texcoords = faceTexcoords.data()+idxFace*3;
			for (int i=0; i<3; ++i)
				texcoords[i] = TexCoord(0.5f, 0.5f);
		}
	}

	// perform seam leveling
	if (texturePatches.size() > 2 && (bGlobalSeamLeveling || bLocalSeamLeveling)) {
		// create seam vertices and edges
		CreateSeamVertices();

		// perform global seam leveling
		if (bGlobalSeamLeveling) {
			TD_TIMER_STARTD();
			GlobalSeamLeveling();
			DEBUG_ULTIMATE("\tglobal seam leveling completed (%s)", TD_TIMER_GET_FMT().c_str());
		}

		// perform local seam leveling
		if (bLocalSeamLeveling) {
			TD_TIMER_STARTD();
			LocalSeamLeveling();
			DEBUG_ULTIMATE("\tlocal seam leveling completed (%s)", TD_TIMER_GET_FMT().c_str());
		}
	}

	// merge texture patches with overlapping rectangles
	for (unsigned i=0; i<texturePatches.size()-1; ++i) {
		TexturePatch& texturePatchBig = texturePatches[i];
		for (unsigned j=1; j<texturePatches.size(); ++j) {
			if (i == j)
				continue;
			TexturePatch& texturePatchSmall = texturePatches[j];
			if (texturePatchBig.label != texturePatchSmall.label)
				continue;
			if (!RectsBinPack::IsContainedIn(texturePatchSmall.rect, texturePatchBig.rect))
				continue;
			// translate texture coordinates
			const TexCoord offset(texturePatchSmall.rect.tl()-texturePatchBig.rect.tl());
			for (const FIndex idxFace: texturePatchSmall.faces) {
				TexCoord* texcoords = faceTexcoords.data()+idxFace*3;
				for (int v=0; v<3; ++v)
					texcoords[v] += offset;
			}
			// join faces lists
			texturePatchBig.faces.JoinRemove(texturePatchSmall.faces);
			// remove the small patch
			texturePatches.RemoveAtMove(j--);
		}
	}

	// create texture
	{
		// arrange texture patches to fit the smallest possible texture image
		RectsBinPack::RectWIdxArr unplacedRects(texturePatches.size());
		FOREACH(i, texturePatches) {
			if (maxTextureSize > 0 && (texturePatches[i].rect.width > maxTextureSize || texturePatches[i].rect.height > maxTextureSize)) {
			    DEBUG("error: a patch of size %u x %u does not fit the texture", texturePatches[i].rect.width, texturePatches[i].rect.height);
			    ABORT("the maximum texture size chosen cannot fit a patch");
			}
			unplacedRects[i] = {texturePatches[i].rect, i};
		}

		// pack patches: one pack per texture file
		CLISTDEF2IDX(RectsBinPack::RectWIdxArr, TexIndex) placedRects; {
			// increase texture size till all patches fit
			const unsigned typeRectsBinPack(nRectPackingHeuristic/100);
			const unsigned typeSplit((nRectPackingHeuristic-typeRectsBinPack*100)/10);
			const unsigned typeHeuristic(nRectPackingHeuristic%10);
			int textureSize = 0;
			while (!unplacedRects.empty()) {
				TD_TIMER_STARTD();
				if (textureSize == 0) {
					textureSize = RectsBinPack::ComputeTextureSize(unplacedRects, nTextureSizeMultiple);
					if (maxTextureSize > 0 && textureSize > maxTextureSize)
						textureSize = maxTextureSize;
				}

				RectsBinPack::RectWIdxArr newPlacedRects;
				switch (typeRectsBinPack) {
				case 0: {
					MaxRectsBinPack pack(textureSize, textureSize);
					newPlacedRects = pack.Insert(unplacedRects, (MaxRectsBinPack::FreeRectChoiceHeuristic)typeHeuristic);
					break; }
				case 1: {
					SkylineBinPack pack(textureSize, textureSize, typeSplit!=0);
					newPlacedRects = pack.Insert(unplacedRects, (SkylineBinPack::LevelChoiceHeuristic)typeHeuristic);
					break; }
				case 2: {
					GuillotineBinPack pack(textureSize, textureSize);
					newPlacedRects = pack.Insert(unplacedRects, false, (GuillotineBinPack::FreeRectChoiceHeuristic)typeHeuristic, (GuillotineBinPack::GuillotineSplitHeuristic)typeSplit);
					break; }
				default:
					ABORT("error: unknown RectsBinPack type");
				}
				DEBUG_ULTIMATE("\tpacking texture completed: %u initial patches, %u placed patches, %u texture-size, %u textures (%s)", texturePatches.size(), newPlacedRects.size(), textureSize, placedRects.size(), TD_TIMER_GET_FMT().c_str());

				if (textureSize == maxTextureSize || unplacedRects.empty()) {
					// create texture image
					placedRects.emplace_back(std::move(newPlacedRects));
					texturesDiffuse.emplace_back(textureSize, textureSize).setTo(cv::Scalar(colEmpty.b, colEmpty.g, colEmpty.r));
					textureSize = 0;
				} else {
					// try again with a bigger texture
					textureSize *= 2;
					if (maxTextureSize > 0)
						textureSize = std::max(textureSize, maxTextureSize);
					unplacedRects.JoinRemove(newPlacedRects);
				}
			}
		}

		#ifdef TEXOPT_USE_OPENMP
		#pragma omp parallel for schedule(dynamic)
		for (int_t i=0; i<(int_t)placedRects.size(); ++i) {
			for (int_t j=0; j<(int_t)placedRects[(TexIndex)i].size(); ++j) {
				const TexIndex idxTexture((TexIndex)i);
				const uint32_t idxPlacedPatch((uint32_t)j);
		#else
		FOREACH(idxTexture, placedRects) {
			FOREACH(idxPlacedPatch, placedRects[idxTexture]) {
		#endif
				const TexturePatch& texturePatch = texturePatches[placedRects[idxTexture][idxPlacedPatch].patchIdx];
				const RectsBinPack::Rect& rect = placedRects[idxTexture][idxPlacedPatch].rect;
				// copy patch image
				ASSERT((rect.width == texturePatch.rect.width && rect.height == texturePatch.rect.height) ||
					(rect.height == texturePatch.rect.width && rect.width == texturePatch.rect.height));
				int x(0), y(1);
				if (texturePatch.label != NO_ID) {
					const Image& imageData = images[texturePatch.label];
					cv::Mat patch(imageData.image(texturePatch.rect));
					if (rect.width != texturePatch.rect.width) {
						// flip patch and texture-coordinates
						patch = patch.t();
						x = 1; y = 0;
					}
					patch.copyTo(texturesDiffuse[idxTexture](rect));
				}
				// compute final texture coordinates
				const TexCoord offset(rect.tl());
				for (const FIndex idxFace: texturePatch.faces) {
					TexCoord* texcoords = faceTexcoords.data()+idxFace*3;
					faceTexindices[idxFace] = idxTexture;
					for (int v=0; v<3; ++v) {
						TexCoord& texcoord = texcoords[v];
						texcoord = TexCoord(
							texcoord[x]+offset.x,
							texcoord[y]+offset.y
						);
					}
				}
			}
		}
		if (texturesDiffuse.size() == 1)
			faceTexindices.Release();
		// apply some sharpening
		if (fSharpnessWeight > 0) {
			constexpr double sigma = 1.5;
			for (auto &textureDiffuse: texturesDiffuse) {
			    Image8U3 blurryTextureDiffuse;
			    cv::GaussianBlur(textureDiffuse, blurryTextureDiffuse, cv::Size(), sigma);
			    cv::addWeighted(textureDiffuse, 1+fSharpnessWeight, blurryTextureDiffuse, -fSharpnessWeight, 0, textureDiffuse);
			}
		}
	}
}

// texture mesh
//  - minCommonCameras: generate texture patches using virtual faces composed of coplanar triangles sharing at least this number of views (0 - disabled, 3 - good value)
//  - fSharpnessWeight: sharpness weight to be applied on the texture (0 - disabled, 0.5 - good value)
//  - nIgnoreMaskLabel: label value to ignore in the image mask, stored in the MVS scene or next to each image with '.mask.png' extension (-1 - auto estimate mask for lens distortion, -2 - disabled)
bool Scene::TextureMesh(unsigned nResolutionLevel, unsigned nMinResolution, unsigned minCommonCameras, float fOutlierThreshold, float fRatioDataSmoothness,
	bool bGlobalSeamLeveling, bool bLocalSeamLeveling, unsigned nTextureSizeMultiple, unsigned nRectPackingHeuristic, Pixel8U colEmpty, float fSharpnessWeight,
	int nIgnoreMaskLabel, int maxTextureSize, const IIndexArr& views)
{
	MeshTexture texture(*this, nResolutionLevel, nMinResolution);

	// assign the best view to each face
	{
		TD_TIMER_STARTD();
		if (!texture.FaceViewSelection(minCommonCameras, fOutlierThreshold, fRatioDataSmoothness, nIgnoreMaskLabel, views))
			return false;
		DEBUG_EXTRA("Assigning the best view to each face completed: %u faces, %u patches (%s)", mesh.faces.size(), texture.texturePatches.size(), TD_TIMER_GET_FMT().c_str());
	}

	// generate the texture image and atlas
	{
		TD_TIMER_STARTD();
		texture.GenerateTexture(bGlobalSeamLeveling, bLocalSeamLeveling, nTextureSizeMultiple, nRectPackingHeuristic, colEmpty, fSharpnessWeight, maxTextureSize);
		DEBUG_EXTRA("Generating texture atlas and image completed: %u patches, %u image size, %u textures (%s)", texture.texturePatches.size(), mesh.texturesDiffuse[0].width(), mesh.texturesDiffuse.size(), TD_TIMER_GET_FMT().c_str());
	}

	return true;
} // TextureMesh
/*----------------------------------------------------------------*/