//stl
#include <string>
#include <queue>
#include <mutex>
#include <thread>
//pcl
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
//ros
#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>
#include <geometry_msgs/PoseWithCovarianceStamped.h>
//tf
#include <tf2_ros/transform_broadcaster.h>
#include <geometry_msgs/TransformStamped.h>
#include <tf/tf.h>
#include <tf_conversions/tf_eigen.h>

//local lib
#include "ndtMatching.hpp"

//publisher
ros::Publisher map_pub;
ros::Publisher ndt_pc_pub;
ros::Publisher ndt_pose_pub;
ros::Publisher gps_pose_pub;

std::queue<sensor_msgs::PointCloud2ConstPtr> lidar_buf;
std::queue<geometry_msgs::PoseWithCovarianceStampedConstPtr> filtered_pose_buf;

std::mutex mutex_control;

NdtMatching::ndtMatching ndt_matching;

bool is_init;
int odom_frame = 0;

void lidarHandler(const sensor_msgs::PointCloud2ConstPtr &filtered_msg)
{
  mutex_control.lock();
  lidar_buf.push(filtered_msg);
  //printf("\nlidar: %d\n", lidar_buf.size());
  mutex_control.unlock();
}

void poseCallback(const geometry_msgs::PoseWithCovarianceStampedConstPtr &pose_msg)
{
  mutex_control.lock();
  filtered_pose_buf.push(pose_msg);
  //printf("\nimu: %d\n", filtered_pose_buf.size());
  mutex_control.unlock();
}

void finalOdometry()
{
  while(1){
    if(!lidar_buf.empty() && !filtered_pose_buf.empty()){
      //point ros to pointcloud
      mutex_control.lock();
      pcl::PointCloud<pcl::PointXYZI>::Ptr point_in(new pcl::PointCloud<pcl::PointXYZI>());
      pcl::PointCloud<pcl::PointXYZI>::Ptr point_out(new pcl::PointCloud<pcl::PointXYZI>());
      //lidar ros to pointcloud
      pcl::fromROSMsg(*lidar_buf.back(), *point_in);
      ros::Time point_in_time = lidar_buf.back()->header.stamp;
      Eigen::Matrix4d nav_pose = Eigen::Matrix4d::Identity();
      Eigen::Quaterniond local_orientation = Eigen::Quaterniond(filtered_pose_buf.back()->pose.pose.orientation.w, filtered_pose_buf.back()->pose.pose.orientation.x, filtered_pose_buf.back()->pose.pose.orientation.y, filtered_pose_buf.back()->pose.pose.orientation.z);
      nav_pose.block<3,3>(0,0) = local_orientation.toRotationMatrix();
      nav_pose(0,3) = filtered_pose_buf.back()->pose.pose.position.x;
      nav_pose(1,3) = filtered_pose_buf.back()->pose.pose.position.y;
      nav_pose(2,3) = filtered_pose_buf.back()->pose.pose.position.z;
      if(is_init == false){
        tf::Matrix3x3 tf_rot_matrix;
        tf::matrixEigenToTF(nav_pose.block<3,3>(0,0), tf_rot_matrix);
        double roll = 0.0, pitch = 0.0, yaw = 0.0;
        tf_rot_matrix.getRPY(roll, pitch, yaw);
        ndt_matching.setInitPosition(nav_pose(0,3), nav_pose(1,3), nav_pose(2,3), yaw);
        is_init = true;
        filtered_pose_buf.pop();
        lidar_buf.pop();
        mutex_control.unlock();
      }
      else{
          filtered_pose_buf.pop();
          lidar_buf.pop();
          mutex_control.unlock();  
      }
      
      //pose after ndt
      Eigen::Matrix4f ndt_pose = Eigen::Matrix4f::Identity();
      ndt_matching.processNdt(point_in, point_out, nav_pose.cast<float>(), ndt_pose);
      odom_frame++;
      Eigen::Quaterniond result_orientation(ndt_pose.block<3,3>(0,0).cast<double>());
      result_orientation.normalize();

      //pointcloud after ndt
      sensor_msgs::PointCloud2 ndt_pc_msg;
      pcl::toROSMsg(*point_out, ndt_pc_msg);
      ndt_pc_msg.header.stamp = point_in_time;
      ndt_pc_msg.header.frame_id = "map";
      ndt_pc_pub.publish(ndt_pc_msg);
      //ndt odometry
      if(ndt_matching.mp_pose_inited == true){
        geometry_msgs::PoseWithCovarianceStamped ndt_pose_msg;
        ndt_pose_msg.header.frame_id = "map";
        ndt_pose_msg.header.stamp = point_in_time;
        ndt_pose_msg.pose.pose.position.x = ndt_pose(0,3);
        ndt_pose_msg.pose.pose.position.y = ndt_pose(1,3);
        ndt_pose_msg.pose.pose.position.z = ndt_pose(2,3);
        ndt_pose_msg.pose.pose.orientation.w = result_orientation.w();
        ndt_pose_msg.pose.pose.orientation.x = result_orientation.x();
        ndt_pose_msg.pose.pose.orientation.y = result_orientation.y();
        ndt_pose_msg.pose.pose.orientation.z = result_orientation.z();
        ndt_pose_pub.publish(ndt_pose_msg);
      }
      //ndt pose transform
      static tf2_ros::TransformBroadcaster tf2_ndt_br;
      geometry_msgs::TransformStamped transformStamped_ndt;
      transformStamped_ndt.header.stamp = ros::Time::now();
      transformStamped_ndt.header.frame_id = "map";
      transformStamped_ndt.child_frame_id = "ndt";
      transformStamped_ndt.transform.translation.x = ndt_pose(0,3);
      transformStamped_ndt.transform.translation.y = ndt_pose(1,3);
      transformStamped_ndt.transform.translation.z = 0.0;
      transformStamped_ndt.transform.rotation.w = result_orientation.w();
      transformStamped_ndt.transform.rotation.x = result_orientation.x();
      transformStamped_ndt.transform.rotation.y = result_orientation.y();
      transformStamped_ndt.transform.rotation.z = result_orientation.z();
      tf2_ndt_br.sendTransform(transformStamped_ndt);

      //gps odometry
      geometry_msgs::PoseWithCovarianceStamped gps_msg;
      gps_msg.header.frame_id = "map";
      gps_msg.header.stamp = point_in_time;
      gps_msg.pose.pose.position.x = nav_pose(0,3);
      gps_msg.pose.pose.position.y = nav_pose(1,3);
      gps_msg.pose.pose.position.z = nav_pose(2,3);
      gps_msg.pose.pose.orientation.w = local_orientation.w();
      gps_msg.pose.pose.orientation.x = local_orientation.x();
      gps_msg.pose.pose.orientation.y = local_orientation.y();
      gps_msg.pose.pose.orientation.z = local_orientation.z();
      gps_pose_pub.publish(gps_msg);

      //gps pose transform
      static tf2_ros::TransformBroadcaster tf2_gps_br;
      geometry_msgs::TransformStamped transformStamped_gps;
      transformStamped_gps.header.stamp = ros::Time::now();
      transformStamped_gps.header.frame_id = "map";
      transformStamped_gps.child_frame_id = "local";
      transformStamped_gps.transform.translation.x = nav_pose(0,3);
      transformStamped_gps.transform.translation.y = nav_pose(1,3);
      transformStamped_gps.transform.translation.z = 0.0;
      transformStamped_gps.transform.rotation.w = local_orientation.w();
      transformStamped_gps.transform.rotation.x = local_orientation.x();
      transformStamped_gps.transform.rotation.y = local_orientation.y();
      transformStamped_gps.transform.rotation.z = local_orientation.z();
      tf2_gps_br.sendTransform(transformStamped_gps);

      //map publish
      if(odom_frame%30 == 0){
        sensor_msgs::PointCloud2 map_msg;
        pcl::toROSMsg(*(ndt_matching.mp_pcd_map), map_msg);
        map_msg.header.stamp = point_in_time;
        map_msg.header.frame_id = "map";
        map_pub.publish(map_msg);
        odom_frame = 0;
      }
    }
    std::chrono::milliseconds dura(3);
    std::this_thread::sleep_for(dura);
  }
}

int main(int argc, char** argv)
{
  ros::init(argc, argv, "ndtMatching");
  ros::NodeHandle nh;
  
  //NDT
  double pcd_map_resolution = 2.0;
  double search_radius = 150.0;
  int ndt_near_points = 500000;
  int ndt_max_iteration = 50;
  int ndt_max_threads = 10;
  bool submap_select=1;
  double odom_init_x = 0.0;
  double odom_init_y = 0.0;
  double odom_init_z = 0.0;
  double odom_init_rotation=0.0;
  double map_rotation_theta=0.0;
  double map_translation_x=0.0;
  double map_translation_y=0.0;
  double map_translation_z=0.0;
  is_init = false;
  std::string pcd_map_path = "/home/a/ace_ws/src/velodyne_ndt/map/";
  std::string pcd_map_name = "map.pcd";

  nh.getParam("pcd_map_resolution", pcd_map_resolution);
  nh.getParam("pcd_map_path", pcd_map_path);
  nh.getParam("pcd_map_name", pcd_map_name);
  nh.getParam("map_translation_x", map_translation_x);
  nh.getParam("map_translation_y", map_translation_y);
  nh.getParam("map_translation_z", map_translation_z);
  nh.getParam("map_rotation_theta", map_rotation_theta);
  nh.getParam("user_init_pose", is_init);
  nh.getParam("odom_init_x", odom_init_x);
  nh.getParam("odom_init_y", odom_init_y);
  nh.getParam("odom_init_z", odom_init_z);
  nh.getParam("odom_init_rotation", odom_init_rotation);
  nh.getParam("submap_select", submap_select);
  nh.getParam("kdtree_search_radius", search_radius);
  nh.getParam("ndt_near_points", ndt_near_points);
  nh.getParam("ndt_max_iteration", ndt_max_iteration);
  nh.getParam("ndt_max_threads", ndt_max_threads);
  
  ndt_matching.setMapTransformInfo(map_rotation_theta, map_translation_x, map_translation_y, map_translation_z);
  if(is_init == true){
    ROS_INFO("\n-----User init pose-----\n");
    ndt_matching.setInitPosition(odom_init_x, odom_init_y, odom_init_z, odom_init_rotation);
  }
  ndt_matching.init(pcd_map_resolution, pcd_map_path, pcd_map_name, submap_select, search_radius, ndt_near_points, ndt_max_iteration, ndt_max_threads);

  ros::Subscriber filtered_lidar_sub = nh.subscribe<sensor_msgs::PointCloud2>("/filtered_point", 1, lidarHandler);
  ros::Subscriber pose_sub = nh.subscribe<geometry_msgs::PoseWithCovarianceStamped>("/filtered_pose", 1, poseCallback);

  map_pub = nh.advertise<sensor_msgs::PointCloud2>("/pcd_map", 1);
  ndt_pc_pub = nh.advertise<sensor_msgs::PointCloud2>("/ndt", 1);
  ndt_pose_pub = nh.advertise<geometry_msgs::PoseWithCovarianceStamped>("/ndt_odom", 1);
  gps_pose_pub = nh.advertise<geometry_msgs::PoseWithCovarianceStamped>("/local_odom", 1);

  std::thread finalOdometryProcess{finalOdometry};

  ros::spin();

  return 0;
}