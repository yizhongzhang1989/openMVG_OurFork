#pragma once
#ifndef POSE_GENERATOR_CIRCLE_SINE_H_
#define POSE_GENERATOR_CIRCLE_SINE_H_

#include "PoseGeneratorBase.h"
#include "types.h"
//#include <iostream>

namespace generator
{

class PoseGeneratorCircleSine : public PoseGeneratorBase<Pose, Eigen::aligned_allocator<Pose>>
{
public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    /// r: radius of circle;  A: amptitude of sine;
    /// fc: rotation frequency (r/s);  fs: frequency of sine;
    /// T: sampling period.
    PoseGeneratorCircleSine(double r, double A, double fc, double fs, double deltaT_s, int deltaT_IMU_ms, bool storeIMU = false, LookDirection direction = FORWARD)
    : r_(r), A_(A), direction(direction), deltaT(deltaT_s), deltaT_IMU(deltaT_IMU_ms), t_cam(0.0), t_imu_ms(0), storeIMU_(storeIMU)
    {
        omegaCircle = 2 * PI * fc;
        omegaSine = 2 * PI * fs;

        IMUs.clear();
    }

    Pose Generate() override
    {
        static const Eigen::Vector3d gravity(0.0,0.0,-GRAVITY);

        static Eigen::Quaterniond q_last = Eigen::Quaterniond::Identity();

        // angle of rotation (rad)
        double theta = omegaCircle * t_cam;
        // angle of sine (rad)
        double alpha = omegaSine * t_cam;

        Pose p;

        // generate position, t: local ->global, position of camera center, t_wc
        double x = r_ * cos(theta);
        double y = r_ * sin(theta);
        double z = A_ * sin(alpha);
        Eigen::Vector3d t(x,y,z);

        // dx = -r_ * sin(theta) * omegaCircle
        // dy = r_ * cos(theta) * omegaCircle
        // dz = A_ * cos(alpha) * omegaSine

        // dx2 = -r_ * cos(theta) * omegaCircle * omegaCircle
        // dy2 = -r_ * sin(theta) * omegaCircle * omegaCircle
        // dz2 = -A_ * sin(alpha) * omegaSine * omegaSine

        // generate orientation
        Eigen::Vector3d rz(-y*omegaCircle,x*omegaCircle,A_*cos(alpha)*omegaSine);
        Eigen::Vector3d ry(-x,-y,0);
        Eigen::Vector3d rx = ry.cross(rz);

//        Eigen::Vector3d v = rz;

        rx = rx / rx.norm();
        ry = ry / ry.norm();
        rz = rz / rz.norm();

        // R: local -> global, R_wc
        Eigen::Matrix3d R;
        R.col(0) = rx;
        R.col(1) = ry;
        R.col(2) = rz;

        // p.q: global -> local, q_cw
        p.q = Eigen::Quaterniond(R.transpose());
        {
            double norm1 = (p.q.coeffs() - q_last.coeffs()).norm();
            double norm2 = (-p.q.coeffs() - q_last.coeffs()).norm();
            if(norm1 > norm2)
            {
                Eigen::Quaterniond tmp = p.q;
                p.q.coeffs() = -tmp.coeffs();
            }
            q_last = p.q;
        }
        // p.t: global -> local, t_cw
        p.t = - R.transpose() * t;

//        std::cout<<"vG = "<<v.transpose()<<std::endl;
//        std::cout<<"vI = "<<(p.q*v).transpose()<<std::endl;

        // generate IMU measurements
        if(storeIMU_)
        {
            double t_imu = 1e-3 * t_imu_ms;
            while(t_imu <= t_cam)
            {
                theta = omegaCircle * t_imu;
                alpha = omegaSine * t_imu;

                double sin_theta = sin(theta);
                double cos_theta = cos(theta);
                double omegaCircle2 = omegaCircle * omegaCircle;
                double sin_alpha = sin(alpha);
                double cos_alpha = cos(alpha);
                double cos2_alpha = cos_alpha * cos_alpha;
                double omegaSine2 = omegaSine * omegaSine;

                double dx2 = -r_ * cos_theta * omegaCircle2;
                double dy2 = -r_ * sin_theta * omegaCircle2;
                double dz2 = -A_ * sin_alpha * omegaSine2;

                double h_2 = r_ * r_ * omegaCircle2 + A_ * A_ * omegaSine2 * cos2_alpha;
                double h2 = 1.0 / h_2;
                double h = sqrt(h2);
                double omega_x = - h * omegaCircle2 * r_;
                double omega_y = A_ * r_ * omegaCircle * omegaSine2 * sin_alpha * h2;
                double omega_z = A_ * omegaCircle * omegaSine * h * cos_alpha;

                x = r_ * cos_theta;
                y = r_ * sin_theta;
                rz = Eigen::Vector3d(-y*omegaCircle,x*omegaCircle,A_*cos_alpha*omegaSine);
                ry = Eigen::Vector3d(-x,-y,0);
                rx = ry.cross(rz);
                rx = rx / rx.norm();
                ry = ry / ry.norm();
                rz = rz / rz.norm();
                R.col(0) = rx;
                R.col(1) = ry;
                R.col(2) = rz;

                Eigen::Vector3d acc_local = R.transpose() * (Eigen::Vector3d(dx2,dy2,dz2) + gravity);
                IMUs.emplace_back(acc_local,Eigen::Vector3d(omega_x,omega_y,omega_z), t_imu_ms);

                t_imu_ms += deltaT_IMU;
                t_imu = 1e-3 * t_imu_ms;
            }
        }

        t_cam += deltaT;

        return p;
    }

    STLVector<Pose> Generate(int num_poses) override
    {
        STLVector<Pose> poses;
        for(int i = 0; i < num_poses; i++)
        {
            poses.push_back(Generate());
        }
        return poses;
    }

    double getDeltaT() const override
    {
        return deltaT;
    }

    const IMUMeasurements& getIMUMeasurements() const
    {
        return IMUs;
    }

    bool hasIMU() const
    {
        return storeIMU_;
    }

private:
    double r_;
    double A_;
    double omegaCircle;
    double omegaSine;
    LookDirection direction;
    // camera sampling period in s
    double deltaT;
    // IMU sampling period in ms
    int deltaT_IMU;
    // camera current time in s
    double t_cam;
    // IMU current time in ms
    int t_imu_ms;
    // IMU measurements
    IMUMeasurements IMUs;
    bool storeIMU_;

};  // PoseGeneratorCircleSine

}  // namespace generator

#endif  // POSE_GENERATOR_CIRCLE_SINE_H_
