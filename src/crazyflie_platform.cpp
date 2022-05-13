#include "crazyflie_platform.hpp"
#include <iostream>
#include "as2_core/core_functions.hpp"

CrazyfliePlatform::CrazyfliePlatform() : as2::AerialPlatform()
{
    RCLCPP_INFO(this->get_logger(), "Init");
    // Connect to crazyflie
    cf_ = std::make_shared<Crazyflie>(std::string("radio://0/80/250K/E7E7E7E7E7"));
    cf_->setGroupMask(1);
    
    //float bat_level = cf_->vbat();
    //RCLCPP_INFO(this->get_logger(), "Battery at: %f",bat_level);

    //listVariables();
    configureSensors();

    
    // SENSORS
    cf_->requestLogToc();

    // Odom
    ori_rec_ = pos_rec_ = false;
    std::vector<std::string> vars_odom1 = {"kalman.q0","kalman.q1","kalman.q2","kalman.q3"};
    cb_odom_ori_ = std::bind(&CrazyfliePlatform::onLogOdomOri, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    odom_logBlock_ori_ = std::make_shared<LogBlockGeneric>(cf_.get(), vars_odom1, nullptr, cb_odom_ori_);
    odom_logBlock_ori_->start(10);

    std::vector<std::string> vars_odom2 = {"kalman.stateX","kalman.stateY","kalman.stateZ","kalman.statePX","kalman.statePY","kalman.statePZ"};
    cb_odom_pos_ = std::bind(&CrazyfliePlatform::onLogOdomPos, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    odom_logBlock_pos_ = std::make_shared<LogBlockGeneric>(cf_.get(), vars_odom2, nullptr, cb_odom_pos_);
    odom_logBlock_pos_->start(10);

    //IMU
    std::vector<std::string> vars_imu = {"gyro.x","gyro.y","gyro.z","acc.x","acc.y","acc.z"};
    cb_imu_ = std::bind(&CrazyfliePlatform::onLogIMU, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    imu_logBlock_ = std::make_shared<LogBlockGeneric>(cf_.get(), vars_imu, nullptr, cb_imu_);
    imu_logBlock_->start(10);


      
    ping_timer_ = this->create_wall_timer(
        std::chrono::milliseconds(10), [this]() { pingCB(); });

    RCLCPP_INFO(this->get_logger(), "Finished Init");
    

}

void CrazyfliePlatform::onLogIMU(uint32_t time_in_ms, std::vector<double>* values, void* /*userData*/)
{
  // Data is received as follows: {"gyro.x","gyro.y","gyro.z","acc.x","acc.y","acc.z"}
  // Acc. data is in g
  // Gyro. data is in º/s
  // The transformation array holds the unit transformations.
  int i = 0;
  double transformations[6]={0.01745329252,0.01745329252,0.01745329252, 9.81,9.81,9.81};
  for (double v : *values) {
    imu_buff_[i] = v * transformations[i];
    //std::cout << imu_buff_[i] << ","; 
    i++;
  }
  //std::cout << std::endl;

  // Update IMU state
  sensor_msgs::msg::Imu imu_msg;
  auto timestamp = this->get_clock()->now();
  imu_msg.header.stamp = timestamp;
  imu_msg.header.frame_id = "imu";
  imu_msg.linear_acceleration.x = imu_buff_[3];
  imu_msg.linear_acceleration.y = imu_buff_[4];
  imu_msg.linear_acceleration.z = imu_buff_[5];
  imu_msg.angular_velocity.x = imu_buff_[0];
  imu_msg.angular_velocity.y = imu_buff_[1];
  imu_msg.angular_velocity.z = imu_buff_[2];

  imu_sensor_ptr_->updateData(imu_msg);
}

void CrazyfliePlatform::onLogOdomOri(uint32_t time_in_ms, std::vector<double>* values, void* /*userData*/)
{
  // Data is received as follows: {"kalman.q0","kalman.q1","kalman.q2","kalman.q3"};
  int i = 0;
  double transformations[4]={1,1,1,1};
  for (double v : *values) {
    odom_buff_[i] = v * transformations[i];
    //std::cout << imu_buff_[i] << ","; 
    i++;
  }
  ori_rec_ = true;
  if(pos_rec_ && ori_rec_) updateOdom();
}

void CrazyfliePlatform::onLogOdomPos(uint32_t time_in_ms, std::vector<double>* values, void* /*userData*/)
{
  // Data is received as follows: {"kalman.stateX","kalman.stateY","kalman.stateZ","kalman.statePX","kalman.statePY","kalman.statePZ"}
  // Pos in m
  // Vel in m/s
  int i = 4;
  double transformations[6]={1,1,1,1,1,1};
  for (double v : *values) {
    odom_buff_[i] = v * transformations[i];
    //std::cout << imu_buff_[i] << ","; 
    i++;
  }
  pos_rec_ = true;
  if(pos_rec_ && ori_rec_) updateOdom();
}

void CrazyfliePlatform::updateOdom(){
  pos_rec_ = ori_rec_ = false;

  // Send the odom message from the data received from the drone
  auto timestamp = this->get_clock()->now();

  nav_msgs::msg::Odometry odom_msg;

  odom_msg.header.stamp = timestamp;
  odom_msg.header.frame_id = "odom";

  odom_msg.pose.pose.orientation.w = odom_buff_[0];
  odom_msg.pose.pose.orientation.x = odom_buff_[1];
  odom_msg.pose.pose.orientation.y = odom_buff_[2];
  odom_msg.pose.pose.orientation.z = odom_buff_[3];

  odom_msg.pose.pose.position.x = odom_buff_[4];
  odom_msg.pose.pose.position.y = odom_buff_[5];
  odom_msg.pose.pose.position.z = odom_buff_[6];

  odom_msg.twist.twist.linear.x = odom_buff_[7];
  odom_msg.twist.twist.linear.y = odom_buff_[8];
  odom_msg.twist.twist.linear.z = odom_buff_[9];

  odom_estimate_ptr_->updateData(odom_msg);
}



void CrazyfliePlatform::configureSensors(){
    
    RCLCPP_INFO(this->get_logger(), "Configure Sensors");

    // Hay que cambiar a as2_names::topics::sensor_measurements::imu
    imu_sensor_ptr_ = std::make_unique<as2::sensors::Imu>("imu", this);
    odom_estimate_ptr_ = std::make_unique<as2::sensors::Sensor<nav_msgs::msg::Odometry>>("odometry", this);
}

bool CrazyfliePlatform::ownSendCommand(){
    return true;
}
bool CrazyfliePlatform::ownSetArmingState(bool state){
    return true;
}
bool CrazyfliePlatform::ownSetOffboardControl(bool offboard){
    return true;
}
bool CrazyfliePlatform::ownSetPlatformControlMode(const as2_msgs::msg::ControlMode & msg){
    return true;
}
void CrazyfliePlatform::listVariables(){
    cf_->requestLogToc();
     std::for_each(cf_->logVariablesBegin(), cf_->logVariablesEnd(),
      [](const Crazyflie::LogTocEntry& entry)
      {
        std::cout << entry.group << "." << entry.name << " (";
        switch (entry.type) {
        case Crazyflie::LogTypeUint8:
          std::cout << "uint8";
          break;
        case Crazyflie::LogTypeInt8:
          std::cout << "int8";
          break;
        case Crazyflie::LogTypeUint16:
          std::cout << "uint16";
          break;
        case Crazyflie::LogTypeInt16:
          std::cout << "int16";
          break;
        case Crazyflie::LogTypeUint32:
          std::cout << "uint32";
          break;
        case Crazyflie::LogTypeInt32:
          std::cout << "int32";
          break;
        case Crazyflie::LogTypeFloat:
          std::cout << "float";
          break;
        case Crazyflie::LogTypeFP16:
          std::cout << "fp16";
          break;
        }
        std::cout << ")";
        std::cout << std::endl;
      }
    );
    
}

void CrazyfliePlatform::pingCB(){
    //RCLCPP_INFO(this->get_logger(), "PIN1");
    try{
        cf_->requestLogToc();
        cf_->sendPing();
    }
    catch(std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }
    
    //RCLCPP_INFO(this->get_logger(), "PIN2");
}