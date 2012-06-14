/*
 * position_sensor.cpp
 *
 *  Created on: Sep 30, 2010
 *      Author: Weiss
 */

#include "position_sensor.h"
#include <ssf_core/eigen_utils.h>
#define nMeas_ 9 // measurement size

void PositionSensorHandler::subscribe(){

	ros::NodeHandle nh("ssf_core");
	subMeasurement_ = nh.subscribe("position_measurement", 1, &PositionSensorHandler::measurementCallback, this);

	measurements->ssf_core_.registerCallback(&PositionSensorHandler::noiseConfig, this);

	nh.param("meas_noise1",n_zp_,0.0001);	// default value is for laser tracker total station
}


void PositionSensorHandler::noiseConfig(ssf_core::SSF_CoreConfig& config, uint32_t level){
	//	if(level & ssf_core::SSF_Core_MISC)
	//	{
	this->n_zp_ = config.meas_noise1;
	//	}
}


void PositionSensorHandler::measurementCallback(const ssf_updates::PositionWithCovarianceStampedConstPtr & msg)
{
	//	ROS_INFO_STREAM("measurement received \n"
	//					<< "type is: " << typeid(msg).name());

	// init variables
	State state_old;
	ros::Time time_old = msg->header.stamp;
        Eigen::Matrix<double,nMeas_,nState_>H_old;
        Eigen::Matrix<double, nMeas_, 1> r_old;
        Eigen::Matrix<double,nMeas_,nMeas_> R;

        H_old.setZero();
        R.setZero();

	// get measurements
	z_p_ = Eigen::Matrix<double,3,1>(msg->position.x, msg->position.y, msg->position.z);

	// take covariance from sensor
//	R.block(0,0,3,3) = Eigen::Matrix<double,6,6>(&msg->covariance[0]);
//	Eigen::Matrix<double,6,1> buffvec = Eigen::Matrix<double,6,1>::Constant(1e-6);
//	R.block(3,3,6,6) = buffvec.asDiagonal(); // measurement noise for position, q_vw, q_ci

	//  alternatively take fix covariance from reconfigure GUI
	Eigen::Matrix<double,nMeas_,1> buffvec;
	buffvec  << n_zp_,n_zp_,n_zp_,1e-6,1e-6,1e-6,1e-6,1e-6,1e-6; // measurement noise for position, q_vw, q_ci
	R = buffvec.asDiagonal();

	// feedback for init case
	measurements->p_vc_ = z_p_;

	unsigned char idx = measurements->ssf_core_.getClosestState(&state_old,time_old,0);
	if (state_old.time_ == -1)
		return;	// // early abort // //

	// get rotation matrices
	Eigen::Matrix<double,3,3> C_wv = state_old.q_wv_.conjugate().toRotationMatrix();
	Eigen::Matrix<double,3,3> C_q = state_old.q_.conjugate().toRotationMatrix();
	Eigen::Matrix<double,3,3> C_ci = state_old.q_ci_.conjugate().toRotationMatrix();

	// preprocess for elements in H matrix
	Eigen::Matrix<double,3,1> vecold;
	vecold = (state_old.p_+C_q.transpose()*state_old.p_ic_)*state_old.L_;
	Eigen::Matrix<double,3,3> skewold = skew(vecold);

	Eigen::Matrix<double,3,3> pic_sk = skew(state_old.p_ic_);

	// construct H matrix using H-blockx :-)
	// position
	H_old.block<3,3>(0,0) = C_wv.transpose()*state_old.L_; // p
	H_old.block<3,3>(0,6) = -C_wv.transpose()*C_q.transpose()*pic_sk*state_old.L_; // q
	H_old.block<3,1>(0,15) = C_wv.transpose()*C_q.transpose()*state_old.p_ic_ + C_wv.transpose()*state_old.p_; // L
	H_old.block<3,3>(0,16) = -C_wv.transpose()*skewold; // q_wv
	H_old.block<3,3>(0,22) = C_wv.transpose()*C_q.transpose()*state_old.L_;	// use "camera"-IMU distance p_ic state here as position_sensor-IMU distance
	H_old.block<3,3>(3,16) = Eigen::Matrix<double,3,3>::Identity();	// fix vision world drift q_wv since it does not exist here
	H_old.block<3,3>(6,19) = Eigen::Matrix<double,3,3>::Identity();	// fix "camera"-IMU drift q_ci since it does not exist here

	// construct residuals
	// position
	r_old.block<3,1>(0,0) = z_p_ - C_wv.transpose()*(state_old.p_ + C_q.transpose()*state_old.p_ic_)*state_old.L_;
	// vision world drift q_wv
	r_old.block<3,1>(3,0) = -state_old.q_wv_.vec()/state_old.q_wv_.w()*2;
	// "camera"-IMU drift q_ci
	r_old.block<3,1>(6,0) = -state_old.q_ci_.vec()/state_old.q_ci_.w()*2;

	// call update step in core class
	measurements->ssf_core_.applyMeasurement(idx,H_old,r_old,R);
}


