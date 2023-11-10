#include <jni.h>
#include <string>
#include <opencv2/opencv.hpp>
#include <orb-slam3/include/System.h>
#include <opencv2/core/eigen.hpp>
#include "include/System.h"
#include "Common.h"
#include "Plane.h"
#include "UIUtils.h"
#include "Matrix.h"

extern "C" {
//std::string modelPath;

ORB_SLAM3::System *slamSys;
Plane *pPlane;
float fx,fy,cx,cy;
double timeStamp;
bool slamInited=false;
vector<ORB_SLAM3::MapPoint*> vMPs;
vector<cv::KeyPoint> vKeys;
Sophus::SE3f Tcw;

const int PLANE_DETECTED=233;
const int PLANE_NOT_DETECTED=1234;

const bool DRAW_STATUS= false;

int processImage(cv::Mat &image,cv::Mat &outputImage,int statusBuf[]){
    timeStamp+=1/30.0;
    //Martin:about 140-700ms to finish last part
    LOGD("Started.");
    cv::Mat imgSmall;
    cv::resize(image,imgSmall,cv::Size(image.cols/2,image.rows/2));
    Tcw= slamSys->TrackMonocular(imgSmall,timeStamp);
    LOGD("finished. %d %d",imgSmall.rows,imgSmall.cols);
    //Martin:about 5ms to finish last part
    //Martin:about 5-10ms to await next image
    int status = slamSys->GetTrackingState();
    vMPs = slamSys->GetTrackedMapPoints();
    vKeys = slamSys->GetTrackedKeyPointsUn();
    if(DRAW_STATUS)
        printStatus(status,outputImage);
    drawTrackedPoints(vKeys,vMPs,outputImage);

    //cv::imwrite(modelPath+"/lala2.jpg",outputImage);
    if(status==2) {
        if(DRAW_STATUS){
            char buf[22];
            if(pPlane){
                sprintf(buf,"Plane Detected");
                cv::putText(outputImage,buf,cv::Point(10,30),cv::FONT_HERSHEY_PLAIN,1.5,cv::Scalar(255,0,255),2,8);
            }
            else {
                sprintf(buf,"Plane not detected.");
                cv::putText(outputImage,buf,cv::Point(10,30),cv::FONT_HERSHEY_PLAIN,1.5,cv::Scalar(255,255,0),2,8);
            }
        }
    }
    return status;
}

JNIEXPORT void JNICALL
Java_com_martin_ads_slamar_NativeHelper_initSLAM(JNIEnv *env, jobject instance,
                                                               jstring path_) {
    const char *path = env->GetStringUTFChars(path_, 0);

    if(slamInited) return;
    slamInited=true;
//    modelPath=path;
//    LOGE("Model Path is %s",modelPath.c_str());
    env->ReleaseStringUTFChars(path_, path);
//    ifstream in;
//    in.open(modelPath+"config.txt");
//    string vocName,settingName;
//    getline(in,vocName);
//    getline(in,settingName);
//
//    // 去掉vocName和settingName中的空格和回车
//    vocName.erase(std::remove_if(vocName.begin(), vocName.end(), ::isspace), vocName.end());
//    settingName.erase(std::remove_if(settingName.begin(), settingName.end(), ::isspace), settingName.end());
//
//    vocName=modelPath+vocName;
//    settingName=modelPath+settingName;
//
//    cv::FileStorage fSettings(settingName, cv::FileStorage::READ);
//    fx = fSettings["Camera.fx"];
//    fy = fSettings["Camera.fy"];
//    cx = fSettings["Camera.cx"];
//    cy = fSettings["Camera.cy"];
//
//    timeStamp=0;
//    LOGD("%s %c %s %c",vocName.c_str(),vocName[vocName.length()-1],settingName.c_str(),settingName[settingName.length()-1]);
    slamSys=new ORB_SLAM3::System("/storage/emulated/0/SLAM/VOC/ORBvoc.bin","/storage/emulated/0/SLAM/Calibration/PARAconfig.yaml",ORB_SLAM3::System::MONOCULAR,false);
//    slamSys=new ORB_SLAM3::System(vocName,settingName,ORB_SLAM3::System::MONOCULAR);
}

JNIEXPORT void JNICALL
Java_com_martin_ads_slamar_NativeHelper_nativeProcessFrameMat(JNIEnv *env, jobject instance,
                                                              jlong matAddrGr, jlong matAddrRgba,
                                                              jintArray statusBuf_) {
    jint *statusBuf = env->GetIntArrayElements(statusBuf_, NULL);

    cv::Mat &mGr = *(cv::Mat *) matAddrGr;
    cv::Mat &mRgba = *(cv::Mat *) matAddrRgba;
    statusBuf[0]=processImage(mGr,mRgba,statusBuf);

    env->ReleaseIntArrayElements(statusBuf_, statusBuf, 0);
}
JNIEXPORT void JNICALL
Java_com_martin_ads_slamar_NativeHelper_detect(JNIEnv *env, jobject instance,
                                               jintArray statusBuf_) {
    jint *statusBuf = env->GetIntArrayElements(statusBuf_, NULL);
    if (Tcw.translation().hasNaN() || Tcw.rotationMatrix().hasNaN()){
        //no thing
    } else{
        pPlane=detectPlane(Tcw,vMPs,50);
        if(pPlane && slamSys->MapChanged())
            pPlane->Recompute();
        statusBuf[1]=pPlane? PLANE_DETECTED:PLANE_NOT_DETECTED;
    }
//    if(!Tcw.empty()){
//    }
    env->ReleaseIntArrayElements(statusBuf_, statusBuf, 0);
}

JNIEXPORT void JNICALL
Java_com_martin_ads_slamar_NativeHelper_getM(JNIEnv *env, jobject instance, jfloatArray modelM_) {
    jfloat *modelM = env->GetFloatArrayElements(modelM_, NULL);

    getRUBModelMatrixFromRDF(pPlane->glTpw,modelM);

    env->ReleaseFloatArrayElements(modelM_, modelM, 0);
}
JNIEXPORT void JNICALL
Java_com_martin_ads_slamar_NativeHelper_getV(JNIEnv *env, jobject instance, jfloatArray viewM_) {
    jfloat *viewM = env->GetFloatArrayElements(viewM_, NULL);

    float tmpM[16];

    Eigen::Matrix3f rotationMatrix = Tcw.rotationMatrix();
    Eigen::Vector3f translationVector = Tcw.translation();
    Eigen::Matrix4f transformationMatrix = Eigen::Matrix4f::Identity();
    transformationMatrix.block(0, 0, 3, 3) = rotationMatrix;
    transformationMatrix.block(0, 3, 3, 1) = translationVector;
    cv::Mat cvMatrix(4, 4, CV_32F);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            cvMatrix.at<float>(i, j) = transformationMatrix(i, j);
        }
    }

    getColMajorMatrixFromMat(tmpM,cvMatrix);
//    getColMajorMatrixFromMat(tmpM,Tcw);
    getRUBViewMatrixFromRDF(tmpM,viewM);

    env->ReleaseFloatArrayElements(viewM_, viewM, 0);
}
JNIEXPORT void JNICALL
Java_com_martin_ads_slamar_NativeHelper_getP(JNIEnv *env, jobject instance, jint imageWidth,
                                             jint imageHeight, jfloatArray projectionM_) {
    jfloat *projectionM = env->GetFloatArrayElements(projectionM_, NULL);

    //TODO:/2
    frustumM_RUB(imageWidth/2,imageHeight/2,fx,fy,cx,cy,0.1,1000,projectionM);

    env->ReleaseFloatArrayElements(projectionM_, projectionM, 0);
}
}

//std::chrono::steady_clock::time_point t0;
//double tframe=0;
//extern "C"
//JNIEXPORT jint JNICALL
//Java_com_martin_ads_slamar_NativeHelper_trackMonocular(JNIEnv *env, jclass clazz,
//                                                         jlong mat_native_obj_addr) {
//    //处理单目相机获取的图像
//    cv::Mat *pMat = (cv::Mat*)mat_native_obj_addr;
////    cv::Mat *srcMat = (cv::Mat*)mat_native_obj_addr;
////    cv::Mat *pMat;
////    cv::resize(*srcMat,*pMat,cv::Size(srcMat->cols / 2, srcMat->rows / 2));
//
//    cv::Mat pose;
//
//    std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
//    tframe  = std::chrono::duration_cast < std::chrono::duration < double >> (t1 - t0).count();
//    // Pass the image to the SLAM system
//    cout << "tframe = " << tframe << endl;
//    clock_t start,end;
//    start=clock();
////    pose = slamSys->TrackMonocular(*pMat,tframe); // TODO change to monocular_inertial
//    Sophus::SE3f Tcw_SE3f = slamSys->TrackMonocular(*pMat,tframe); // TODO change to monocular_inertial
//    Eigen::Matrix4f Tcw_Matrix = Tcw_SE3f.matrix();
//
//    cv::eigen2cv(Tcw_Matrix, pose);
//    end = clock();
//
//    LOGI("Get Pose Use Time=%f\n",((double)end-start)/CLOCKS_PER_SEC);
//
////    static bool instialized =false;
////    static bool markerDetected =false;
//////    if(slamSys->MapChanged()){
//////        instialized = false;
//////        markerDetected =false;
//////    }
//
//    //Plane 平面计算
//    if(!pose.empty()){
//        //todo 计算Plane
//    }
//
////    // 获取矩阵的底层数据指针
////    const float* data_ptr = Tcw_Matrix.data();
////    // 获取矩阵的大小（4x4）
////    int rows = Tcw_Matrix.rows();
////    int cols = Tcw_Matrix.cols();
////    // 创建一个一维的 float 数组来存储数据
////    float Tcw_data[16]; // 4x4 矩阵有 16 个元素
////    // 复制数据到一维数组
////    for (int i = 0; i < rows; i++) {
////        for (int j = 0; j < cols; j++) {
////            Tcw_data[i * cols + j] = data_ptr[i * cols + j];
////        }
////    }
//
//    cv::Mat ima=pose;
//    jfloatArray resultArray = env->NewFloatArray(ima.rows * ima.cols);
//    jfloat *resultPtr;
//
//    resultPtr = env->GetFloatArrayElements(resultArray, 0);
//    for (int i = 0; i < ima.rows; i++)
//        for (int j = 0; j < ima.cols; j++) {
//            float tempdata=ima.at<float>(i,j);
//            resultPtr[i * ima.rows + j] =tempdata;
//        }
//    env->ReleaseFloatArrayElements(resultArray, resultPtr, 0);
//    return slamSys->GetTrackingState();
//}