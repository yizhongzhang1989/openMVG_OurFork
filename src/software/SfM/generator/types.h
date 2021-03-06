#pragma once
#ifndef TYPES_H_
#define TYPES_H_

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <map>
#include <utility>
#include <vector>
#include <set>
#include <string>
#include <iostream>

namespace Eigen
{

typedef Matrix<double,6,1> Vector6d;

}  // namespace Eigen

namespace generator
{

#define PI 3.141592653589793

template<typename ValueType>
using STLVector =  std::vector<ValueType,Eigen::aligned_allocator<ValueType>>;
template<typename KeyType, typename ValueType>
using STLMap = std::map<KeyType,ValueType, std::less<KeyType>, Eigen::aligned_allocator<std::pair<const KeyType, ValueType>>>;

struct Pose
{
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    // global -> local
    double time_stamp;
    Eigen::Quaterniond q;  // rotation
    Eigen::Vector3d t;  // translation
    Pose()
    {
        time_stamp = 0.0;
        t.setZero();
        q.setIdentity();
    }
    Pose operator*(const Pose& other) const
    {
        Pose res;
        res.q = this->q * other.q;
        res.t = this->t + this->q * other.t;
        return res;
    }
    Eigen::Vector3d operator*(const Eigen::Vector3d& X)
    {
        return (this->q * X + this->t);
    }
};

struct InversePose
{
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    // local -> global
    double time_stamp;
    Eigen::Quaterniond q;  // orientation
    Eigen::Vector3d p;  // position
    InversePose()
    {
        time_stamp = 0.0;
        p.setZero();
        q.setIdentity();
    }
};

struct Triangle
{
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    Eigen::Vector3d v1, v2, v3;
    Eigen::Vector3d normal;
    Eigen::Vector3d center;
    Triangle()
    {
        v1.setZero();
        v2.setZero();
        v3.setZero();
        normal.setZero();
        center.setZero();
    }
    Triangle(const Eigen::Vector3d& v1, const Eigen::Vector3d& v2, const Eigen::Vector3d& v3)
            : v1(v1), v2(v2), v3(v3)
    {
        Eigen::Vector3d AB = v2 - v1;
        Eigen::Vector3d AC = v3 - v1;
        normal = AB.cross(AC);
        normal.normalize();
        center = (v1 + v2 + v3) / 3;
    }
    double getArea() const
    {
        double a = (v1 - v2).norm();
        double b = (v1 - v3).norm();
        double c = (v2 - v3).norm();
        double p = 0.5 * (a + b + c);
        double area = p * (p - a) * (p - b) * (p - c);
        area = sqrt(area);
        return area;
    }
};

struct IMUMeasurement
{
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    Eigen::Vector3d acc;
    Eigen::Vector3d gyro;

    // timestamp in ms
    int timestamp;

    //  quaternion
	Eigen::Quaterniond q;

    IMUMeasurement()
    : timestamp(0)
    {
        acc.setZero();
        gyro.setZero();
        q.setIdentity();
    }
    IMUMeasurement(const Eigen::Vector3d& acc_, const Eigen::Vector3d& gyro_, int t)
    : acc(acc_), gyro(gyro_), timestamp(t)
    {
        ;
    }
	IMUMeasurement(const Eigen::Vector3d& acc_, const Eigen::Vector3d& gyro_, int t, const Eigen::Quaterniond& q_)
		: acc(acc_), gyro(gyro_), timestamp(t), q(q_)
	{
		;
	}
};
typedef STLVector<IMUMeasurement> IMUMeasurements;

struct Color
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
    Color()
    : r(0), g(0), b(0)
    {
        ;
    }
    Color(unsigned char r, unsigned char g, unsigned char b)
    : r(r), g(g), b(b)
    {
        ;
    }
};
struct MapPoint
{
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    unsigned int Id;
    Eigen::Vector3d X;
//    std::map<unsigned int, Eigen::Vector2d> obs;
    STLMap<unsigned int, Eigen::Vector2d> obs;
    Color color;
    MapPoint()
    :Id(-1)
    {
        obs.clear();
        X.setZero();
    }
    MapPoint(unsigned int id, const Eigen::Vector3d& X)
    : Id(id), X(X)
    {
        obs.clear();
    }
    MapPoint(unsigned int id, const Eigen::Vector3d& X, const Color& color)
    : Id(id), X(X), color(color)
    {
        obs.clear();
    }
    void addObservation(unsigned int KFId, const Eigen::Vector2d& p)
    {
        obs[KFId] = p;
    }
};
//typedef std::map<unsigned int, MapPoint> MapPoints;
typedef STLMap<unsigned int, MapPoint> MapPoints;

struct KeyFrame
{
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    double time_stamp;  //  unit in seconds
    unsigned int Id;
    Pose pose;
//    std::set<unsigned int> obs;
    std::vector<unsigned int> obs;
    KeyFrame()
    :time_stamp(0.0), Id(-1)
    {
        obs.clear();
    }
    KeyFrame(unsigned int id, const Pose& p)
    : time_stamp(0.0), Id(id), pose(p)
    {
        obs.clear();
    }
    KeyFrame(double timestamp_s, unsigned int id, const Pose& p)
        : time_stamp(timestamp_s), Id(id), pose(p)
    {
        obs.clear();
    }
    void addObservation(unsigned int PTId)
    {
//        obs.insert(PTId);
        obs.push_back(PTId);
    }
};
//typedef std::map<unsigned int, KeyFrame> KeyFrames;
typedef STLMap<unsigned int, KeyFrame> KeyFrames;

struct Simulation_Data
{
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    KeyFrames key_frames;
    MapPoints map_points;
    Simulation_Data()
    {
        key_frames.clear();
        map_points.clear();
    }
};

struct Bound
{
    double min_x,max_x;
    double min_y,max_y;
    double min_z,max_z;
    Bound()
    : min_x(0.0), max_x(0.0), min_y(0.0), max_y(0.0), min_z(0.0), max_z(0.0)
    {
        ;
    }
    Bound(double min_x, double max_x, double min_y, double max_y, double min_z, double max_z)
    : min_x(min_x), max_x(max_x), min_y(min_y), max_y(max_y), min_z(min_z), max_z(max_z)
    {
        ;
    }
    friend const std::ostream& operator<<(const std::ostream& out, const Bound& bound)
    {
        std::cout<<"min_x = "<<bound.min_x<<" , max_x = "<<bound.max_x<<std::endl;
        std::cout<<"min_y = "<<bound.min_y<<" , max_y = "<<bound.max_y<<std::endl;
        std::cout<<"min_z = "<<bound.min_z<<" , max_z = "<<bound.max_z<<std::endl;

        return out;
    }
    static void UpdateBound(const Eigen::Vector3d& point, Bound& bound)
    {
        if(point.x()<bound.min_x)
            bound.min_x = point.x();
        if(point.x()>bound.max_x)
            bound.max_x = point.x();
        if(point.y()<bound.min_y)
            bound.min_y = point.y();
        if(point.y()>bound.max_y)
            bound.max_y = point.y();
        if(point.z()<bound.min_z)
            bound.min_z = point.z();
        if(point.z()>bound.max_z)
            bound.max_z = point.z();
    }
};

}  // namespace generator

#endif  // TYPES_H_