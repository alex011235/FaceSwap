//
// Created by alexander on 2016-06-29.
//

#include <jni.h>
#include <stdio.h>
#include <opencv2/opencv.hpp>
#include "opencv2/core/core.hpp"
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <android/log.h>

#define  LOG_TAG    "someTag"

#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)
#define  LOGW(...)  __android_log_print(ANDROID_LOG_WARN,LOG_TAG,__VA_ARGS__)
#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__)
#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)

#define JNI_FALSE  0
#define JNI_TRUE   1

using namespace std;
using namespace cv;


// Transforming java-vectors to C++ data structures
vector<Point2f> readPoints(JNIEnv *env, jintArray landmarksX1, jintArray landmarksY1);
// Local function for swapping the faces, the javastuff must have been converted
// before this step.
Mat faceswap_main(Mat img1, Mat img2, vector<Point2f> points1, vector<Point2f> points2);

static void calculateDelaunayTriangles(Rect rect, vector<Point2f> &points, vector< vector<int> > &delaunayTri);

void warpTriangle(Mat &img1, Mat &img2, vector<Point2f> &t1, vector<Point2f> &t2);

void applyAffineTransform(Mat &warpImage, Mat &src, vector<Point2f> &srcTri, vector<Point2f> &dstTri);



extern "C" {

JNIEXPORT void JNICALL
Java_com_alex_faceswap_FaceSwap_portraitSwapNative( JNIEnv *env,
                                                    jobject obj,
                                                    jlong addrImg1,
                                                    jlong addrImg2,
                                                    jintArray landmarksX1,
                                                    jintArray landmarksY1,
                                                    jintArray landmarksX2,
                                                    jintArray landmarksY2,
                                                    jlong addrResult );

JNIEXPORT void JNICALL
Java_com_alex_faceswap_FaceSwap_portraitSwapNative( JNIEnv *env,
                                                    jobject obj,
                                                    jlong addrImg1,
                                                    jlong addrImg2,
                                                    jintArray landmarksX1,
                                                    jintArray landmarksY1,
                                                    jintArray landmarksX2,
                                                    jintArray landmarksY2,
                                                    jlong addrResult )
{
    // Transform java points to readable OpenCV points
    vector<Point2f> points1 = readPoints(env, landmarksX1, landmarksY1);
    vector<Point2f> points2 = readPoints(env, landmarksX2, landmarksY2);

    // Get the OpenCV Mats
    Mat img1 = *(Mat*)addrImg1;
    Mat img2 = *(Mat*)addrImg2;

    Mat* retImg = (Mat*)addrResult;

    // Call faceswap function to swap faces
    Mat swapImg = faceswap_main(img1, img2, points1, points2);

    swapImg.convertTo(swapImg, CV_8UC3);

    swapImg.copyTo(*retImg);
}
}


Mat faceswap_main(Mat img1, Mat img2, vector<Point2f> points1, vector<Point2f> points2)
{
    Mat img1Warped = img2.clone();

    //convert Mat to float data type
    img1.convertTo(img1, CV_32F);
    img1Warped.convertTo(img1Warped, CV_32F);

  	Mat img11 = img1, img22 = img2;
  	img11.convertTo(img11, CV_8UC3);
  	img22.convertTo(img22, CV_8UC3);

    // Find convex hull
    vector<Point2f> hull1;
    vector<Point2f> hull2;
    vector<int> hullIndex;

    convexHull(points2, hullIndex, false, false);

    for (size_t i = 0; i < hullIndex.size(); i++) {
        hull1.push_back(points1[hullIndex[i]]);
        hull2.push_back(points2[hullIndex[i]]);
    }

    // Find delaunay triangulation for points on the convex hull
    vector< vector<int> > dt;
  	Rect rect(0, 0, img1Warped.cols, img1Warped.rows);
  	calculateDelaunayTriangles(rect, hull2, dt);


  	// Apply affine transformation to Delaunay triangles
  	for (size_t i = 0; i < dt.size(); i++) {
        vector<Point2f> t1, t2;
          // Get points for img1, img2 corresponding to the triangles
  		for(size_t j = 0; j < 3; j++) {
  			t1.push_back(hull1[dt[i][j]]);
  			t2.push_back(hull2[dt[i][j]]);
  		}
        warpTriangle(img1, img1Warped, t1, t2);
  	}

    // Calculate mask
    vector<Point> hull8U;
    for (size_t i = 0; i < hull2.size(); i++) {
        Point pt(hull2[i].x, hull2[i].y);
        hull8U.push_back(pt);
    }

    Mat mask = Mat::zeros(img2.rows, img2.cols, img2.depth());
    fillConvexPoly(mask,&hull8U[0], hull8U.size(), Scalar(255,255,255));

    // Clone seamlessly.
    Rect r = boundingRect(hull2);
    img1Warped.convertTo(img1Warped, CV_8UC3);
	Mat img1WarpedSub = img1Warped(r);
	Mat img2Sub       = img2(r);
	Mat maskSub       = mask(r);

	Point center(r.width/2, r.height/2);

    Mat output;
	seamlessClone(img1WarpedSub, img2Sub, maskSub, center, output, NORMAL_CLONE);
	output.copyTo(img2(r));


	return img2;
}


// Warps and alpha blends triangular regions from img1 and img2 to img
void warpTriangle(Mat &img1, Mat &img2, vector<Point2f> &t1, vector<Point2f> &t2)
{
    Rect r1 = boundingRect(t1);
    Rect r2 = boundingRect(t2);

    // Offset points by left top corner of the respective rectangles
    vector<Point2f> t1Rect, t2Rect;
    vector<Point> t2RectInt;
    for (int i = 0; i < 3; i++) {
        t1Rect.push_back( Point2f( t1[i].x - r1.x, t1[i].y -  r1.y) );
        t2Rect.push_back( Point2f( t2[i].x - r2.x, t2[i].y - r2.y) );
        t2RectInt.push_back( Point(t2[i].x - r2.x, t2[i].y - r2.y) ); // for fillConvexPoly
    }

    // Get mask by filling triangle
    Mat mask = Mat::zeros(r2.height, r2.width, img1.type());
    fillConvexPoly(mask, t2RectInt, Scalar(1.0, 1.0, 1.0), 16, 0);

    // Apply warpImage to small rectangular patches
    Mat img1Rect;
    img1(r1).copyTo(img1Rect);

    Mat img2Rect = Mat::zeros(r2.height, r2.width, img1Rect.type());

    applyAffineTransform(img2Rect, img1Rect, t1Rect, t2Rect);

    multiply(img2Rect,mask, img2Rect);
    multiply(img2(r2), Scalar(1.0,1.0,1.0) - mask, img2(r2));
    img2(r2) = img2(r2) + img2Rect;
}


// Apply affine transform calculated using srcTri and dstTri to src
void applyAffineTransform(Mat &warpImage, Mat &src, vector<Point2f> &srcTri, vector<Point2f> &dstTri)
{
    // Given a pair of triangles, find the affine transform.
    Mat warpMat = getAffineTransform( srcTri, dstTri );
    // Apply the Affine Transform just found to the src image
    warpAffine( src, warpImage, warpMat, warpImage.size(), INTER_LINEAR, BORDER_REFLECT_101);
}


// Calculate Delaunay triangles for set of points
// Returns the vector of indices of 3 points for each triangle
static void calculateDelaunayTriangles(Rect rect, vector<Point2f> &points, vector< vector<int> > &delaunayTri){

	// Create an instance of Subdiv2D
    Subdiv2D subdiv(rect);

	// Insert points into subdiv
    for (vector<Point2f>::iterator it = points.begin(); it != points.end(); it++)
        subdiv.insert(*it);

	vector<Vec6f> triangleList;
	subdiv.getTriangleList(triangleList);
	vector<Point2f> pt(3);
	vector<int> ind(3);

	for (size_t i = 0; i < triangleList.size(); i++) {
		Vec6f t = triangleList[i];
		pt[0] = Point2f(t[0], t[1]);
		pt[1] = Point2f(t[2], t[3]);
		pt[2] = Point2f(t[4], t[5 ]);

		if (rect.contains(pt[0]) && rect.contains(pt[1]) && rect.contains(pt[2])){
			for (int j = 0; j < 3; j++)
				for (size_t k = 0; k < points.size(); k++)
					if (abs(pt[j].x - points[k].x) < 1.0 && abs(pt[j].y - points[k].y) < 1)
						ind[j] = k;

			delaunayTri.push_back(ind);
		}
	}

}


// Convert points
vector<Point2f> readPoints(JNIEnv *env, jintArray landmarksX, jintArray landmarksY)
{
    jsize len = env->GetArrayLength(landmarksX);

    jboolean isCopy = JNI_FALSE;

    jint *pts_x = env->GetIntArrayElements(landmarksX, &isCopy);
    jint *pts_y = env->GetIntArrayElements(landmarksY, &isCopy);

    vector<Point2f> points;

    for (int i = 0; i < len; ++i) {
        float x = (float) pts_x[i];
        float y = (float) pts_y[i];
        points.push_back(Point2f(x,y));
    }

    return points;
}
