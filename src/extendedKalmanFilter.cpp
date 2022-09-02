#include "extendedKalmanFilter.hpp"

namespace ExtendedKalmanFilter{
  void extendedKalmanFilter::setInitPosition(const Eigen::Isometry3d &pres_pose)
  {
    m_last_pose = pres_pose;
  }

  void extendedKalmanFilter::correctionInit(const int &orientation_window_size, const int &position_window_size)
  {
    m_orientation_window_size = orientation_window_size;
    m_position_window_size = position_window_size;
    m_orientation_exponential_weight = 1.0 - (1.0 / (double)m_orientation_window_size);
    m_position_exponential_weight = 1.0 - (1.0 / (double)m_position_window_size);
    m_last_pose = Eigen::Isometry3d::Identity();
  }

  void extendedKalmanFilter::processKalmanFilter(const Eigen::Isometry3d &pres_pose, Eigen::Isometry3d &pose_out)
  {
    exponentialWeight(pres_pose);
    pose_out = m_last_pose; 
  }

  void extendedKalmanFilter::exponentialWeight(const Eigen::Isometry3d &pres_pose)
  {
    m_last_pose.translation() = m_position_exponential_weight * m_last_pose.translation() + (1 - m_position_exponential_weight) * pres_pose.translation();
    m_last_pose.linear() = m_orientation_exponential_weight * m_last_pose.linear() + (1 - m_orientation_exponential_weight) * pres_pose.linear();
  }

  extendedKalmanFilter::extendedKalmanFilter(void)
  {
  }
}