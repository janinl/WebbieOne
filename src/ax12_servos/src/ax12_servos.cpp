#include "ax12Serial.cpp"
#include "ros/ros.h"
#include "std_msgs/Float64.h"
#include "std_msgs/UInt8MultiArray.h"
#include "std_msgs/Int16MultiArray.h"

#include "../../Phoenix_walk/mytypes.h"
#include "../../Phoenix_walk/Hex_Cfg.h"

vector<ros::Publisher> servo_status_channels;


void callback(const std_msgs::Float64::ConstPtr& msg, int servoId, bool isReverse)
{
  ROS_INFO("I heard: servoId=%d [%f]", servoId, msg->data);

   // Convert pos from gazebo units (radians) to ax12 units (0-1023 for -150deg to +150deg)
   double posRad = msg->data;
   const double PI = 3.14159265359;
   double posDeg = std::fmod(posRad/PI*180.0,180);
   if (posDeg < -150 || posDeg > 150) {
     cout << "ERROR: servo position out of range" << endl;
     posDeg /= 0;
   }
   if (isReverse) posDeg = -posDeg;

   double pos = posDeg/150 + 1;
   int posInt = std::nearbyint(pos * 512);
   if (posInt < 0) posInt=0;
   if (posInt > 1023) posInt=1023;
  ROS_INFO(" => %d", posInt);

  ax12SetRegister(servoId, AX_GOAL_POSITION_L, posInt, 2);
}


void callback_allJointsPosAndSpeed(const std_msgs::UInt8MultiArray::ConstPtr& msg)
{
  ROS_INFO("I heard: callback_allJointsPosAndSpeed");//servoId=%d [%f]", servoId, msg->data);

  int num_servos = msg->layout.dim[0].size / 5;
  const uint8_t *servoIds = &msg->data[0];
  const uint8_t *bVals = &msg->data[num_servos];

  // convert const uint8[] to non-const uint8[]
  uint8_t bVals2[100];
  if (num_servos*4 > 100) { cout << "ERROR: static array too small" << endl; exit(1); }
  for (int i=0; i<num_servos*4; ++i)
    bVals2[i] = bVals[i];

  ax12GroupSyncWriteDetailed(AX_GOAL_POSITION_L, 4, bVals2, servoIds, num_servos);
}

void getAndPublishNextOf18ServosData()
{
  static int currentServo = 1;

  uint8_t regstart = AX_PRESENT_POSITION_L;
  uint8_t length = 8;
  uint8_t outVal[8];
  uint32_t err;
  ax12GetRegister(currentServo, regstart, length, &err, outVal);

      std_msgs::Int16MultiArray msg;

      // set up dimensions
      msg.layout.dim.push_back(std_msgs::MultiArrayDimension());
      msg.layout.dim[0].size = 8; // pos, speed, load, voltage, temp, error
      msg.layout.dim[0].stride = 1;
      msg.layout.dim[0].label = "pos,vel,load,volt,temp,err";

      // copy in the data
      //msg.data.clear();
      int16_t pos = (outVal[1]<<8) | outVal[0];
      int16_t vel = (outVal[3]<<8) | outVal[2];
      if (vel>1023) vel=1024-vel;
      int16_t load = (outVal[5]<<8) | outVal[4];
      if (load>1023) load=1024-load;
      msg.data.push_back( pos );
      msg.data.push_back( vel );
      msg.data.push_back( load );
      msg.data.push_back( outVal[6] );
      msg.data.push_back( outVal[7] );
      msg.data.push_back( err );

  servo_status_channels[currentServo].publish(msg);
//    ax12Get18ServosData();

  // next servo
  currentServo = currentServo % 18 + 1;
}

long unsigned int millis() {
  struct timeval tp;
  gettimeofday(&tp, NULL);
  long unsigned int ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
  return ms;
}


int main(int argc, char **argv)
{
  ax12Init(1000000);

//#define SPEED_TEST
#ifdef SPEED_TEST
  unsigned long lastTime = millis();
  int count = 0;
  while (1) {
    ++count;
    unsigned long currentTime = millis();
    cout << currentTime << endl;
/*
    for (int servoId=1; servoId<=1; servoId++)
    {
      ax12GetRegister(servoId,AX_PRESENT_POSITION_L,8);
    }
*/

    if (currentTime >= lastTime+10000) {
      cout << " *** Times per second: " << count*0.1 << endl;
      sleep(1);
      count = 0;
      lastTime = millis(); //currentTime;
    }
  }
#endif

  ros::init(argc, argv, "ax12_servos");
  ros::NodeHandle n;

vector<string> servoId2jointName;
    servoId2jointName.resize(18+1);
    servoId2jointName[cRRCoxaPin] = "c1_rr";
    servoId2jointName[cRRFemurPin] = "thigh_rr";
    servoId2jointName[cRRTibiaPin] = "tibia_rr";
    servoId2jointName[cRMCoxaPin] = "c1_rm";
    servoId2jointName[cRMFemurPin] = "thigh_rm";
    servoId2jointName[cRMTibiaPin] = "tibia_rm";
    servoId2jointName[cRFCoxaPin] = "c1_rf";
    servoId2jointName[cRFFemurPin] = "thigh_rf";
    servoId2jointName[cRFTibiaPin] = "tibia_rf";
    servoId2jointName[cLRCoxaPin] = "c1_lr";
    servoId2jointName[cLRFemurPin] = "thigh_lr";
    servoId2jointName[cLRTibiaPin] = "tibia_lr";
    servoId2jointName[cLMCoxaPin] = "c1_lm";
    servoId2jointName[cLMFemurPin] = "thigh_lm";
    servoId2jointName[cLMTibiaPin] = "tibia_lm";
    servoId2jointName[cLFCoxaPin] = "c1_lf";
    servoId2jointName[cLFFemurPin] = "thigh_lf";
    servoId2jointName[cLFTibiaPin] = "tibia_lf";

    vector<ros::Subscriber> joint_channels;// = n.subscribe("/hexapd/hj_.../", 10, callback);
    joint_channels.resize(1); // adding empty space for unused servo 0
    servo_status_channels.resize(1); // adding empty space for unused servo 0
    for (int servoId=1; servoId<=18; ++servoId) {
      string jointName = "/phantomx/j_" + servoId2jointName[servoId] + "_position_controller/command";
      bool isReverse = false; //servoId2jointName[servoId].substr(0,5) == "tibia" || servoId2jointName[servoId].substr(0,2) == "c1";
      joint_channels.push_back( n.subscribe<std_msgs::Float64>(jointName, 10, boost::bind(&callback, _1, servoId, isReverse)) );

      string statusChanName = "/servo/" + to_string(servoId) + "/status";
      servo_status_channels.push_back( n.advertise<std_msgs::Int16MultiArray>(statusChanName, 2) );
    }

    ros::Subscriber allJointsPosAndSpeed(n.subscribe<std_msgs::UInt8MultiArray>("/phantomx/allJointsPosAndSpeed", 10, callback_allJointsPosAndSpeed));

  while (ros::ok()) {
    ros::spinOnce();
    getAndPublishNextOf18ServosData();
  }

  ax12Finish();
  return 0;
}



