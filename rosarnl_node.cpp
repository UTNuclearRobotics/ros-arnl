
#include "Aria/Aria.h"
#include "Arnl.h"
#include "ArPathPlanningInterface.h"
#include "ArLocalizationTask.h"
#include "ArServerClasses.h"
#include "ArDocking.h"

#include "ArnlSystem.h"
#include "LaserPublisher.h"

#include <ros/ros.h>
#include <geometry_msgs/Twist.h>
#include <geometry_msgs/Pose.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/PoseWithCovariance.h>
#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <geometry_msgs/TransformStamped.h>
#include <nav_msgs/Odometry.h>
#include <tf/tf.h>
#include <tf/transform_listener.h>  
#include <tf/transform_broadcaster.h>
#include <tf/transform_datatypes.h>
#include <std_msgs/Bool.h>
#include <std_msgs/String.h>
#include <std_msgs/Float64.h>
#include <std_msgs/Float32.h>
#include <std_msgs/Int8.h>
#include <std_srvs/Empty.h>
#include <actionlib/server/simple_action_server.h>
#include <move_base_msgs/MoveBaseAction.h>

class RosArnlNode
{
  public:
    RosArnlNode(ros::NodeHandle n, ArnlSystem& arnlsys);
    virtual ~RosArnlNode();

  public:
    int Setup();
    void spin();
    void publish();

  protected:
    ros::NodeHandle n;
    ArnlSystem &arnl;

    ArFunctorC<RosArnlNode> myPublishCB;

    ArPose rosPoseToArPose(const geometry_msgs::Pose& p);
    ArPose rosPoseToArPose(const geometry_msgs::PoseStamped& p) { return rosPoseToArPose(p.pose); }
    ArPoseWithTime rosPoseStampedToArPoseWithTime(const geometry_msgs::PoseStamped& p); 
    geometry_msgs::Pose rosPoseToArPose(const ArPose& arpose);

    ros::ServiceServer enable_srv;
    ros::ServiceServer disable_srv;
    ros::ServiceServer wander_srv;
    ros::ServiceServer stop_srv;
    ros::ServiceServer dock_srv;
    ros::ServiceServer undock_srv;

    bool enable_motors_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response);
    bool disable_motors_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response);
    bool wander_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response);
    bool stop_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response);
    bool dock_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response);
    bool undock_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response);

    ros::Publisher motors_state_pub;
    ros::Publisher dock_state_pub;
    std_msgs::Bool motors_state;
    std_msgs::String dock_state;
    bool published_motors_state;
    bool published_dock_state;

    ros::Time veltime;

    geometry_msgs::PoseWithCovarianceStamped pose_msg;
    ros::Publisher pose_pub;

    std_msgs::Float64 params_msg;
    ros::Publisher params_pub;

    ros::Publisher battery_charge_percent_pub;
    ros::Publisher battery_charge_state_pub;

    geometry_msgs::TransformStamped map_trans;
    tf::TransformBroadcaster map_broadcaster;

    std::string tf_prefix;
    std::string frame_id_map;
    std::string frame_id_base_link;
    std::string frame_id_bumper;
    std::string frame_id_sonar;

    ros::Subscriber initialpose_sub;
    void initialpose_sub_cb(const geometry_msgs::PoseWithCovarianceStampedConstPtr &msg);
    
    ros::ServiceServer global_localization_srv;
    bool global_localization_srv_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response);

    ros::Publisher arnl_server_mode_pub;
    ros::Publisher arnl_server_status_pub;

    ros::Publisher arnl_path_state_pub;
    void arnl_path_state_change_cb();

    std::string lastServerStatus;
    std::string lastServerMode;

    // aria call back for cmdvel
    ros::Subscriber cmd_drive_sub;
    void cmdvel_cb( const geometry_msgs::TwistConstPtr &msg);

    // just request a goal, no actionlib interface:
    ros::Subscriber simple_goal_sub;
    void simple_goal_sub_cb(const geometry_msgs::PoseStampedConstPtr &msg);

    // request goal by name
    ros::Subscriber goalname_sub;
    void goalname_sub_cb(const std_msgs::StringConstPtr &msg);

    // request rosarnl to shutdown
    ros::Subscriber shutdown_sub;
    void shutdown_rosarnl_cb(const std_msgs::BoolConstPtr &msg);

    ros::Publisher current_goal_pub;
    void arnl_new_goal_cb(ArPose p);

    // TODO may replace with MoveBase action definitions?
    typedef actionlib::SimpleActionServer<move_base_msgs::MoveBaseAction> ArnlActionServer;
    ArnlActionServer actionServer;
    void execute_action_cb(const move_base_msgs::MoveBaseGoalConstPtr& goal);
    bool arnl_goal_done;
    bool action_executing;
    bool rosarnl_inventory_running;
    void arnl_goal_reached_cb(ArPose p);
    void arnl_goal_failed_cb(ArPose p);
    void arnl_goal_interrupted_cb(ArPose p);
    move_base_msgs::MoveBaseActionFeedback move_action_feedback_;
    move_base_msgs::MoveBaseActionResult move_action_result_;
    
    // If robot has e--top button pressed, print a warning and return true. Otherwise return false.
    bool check_estop(const char *s) {
        arnl.robot->lock();
        bool e = arnl.robot->isEStopPressed();
        arnl.robot->unlock();
        if(e) ROS_WARN_NAMED("rosarnl_node", "rosarnl_node: Warning: Robot e-stop button pressed, cannot %s", s);
        return e;
    }

};


RosArnlNode::RosArnlNode(ros::NodeHandle nh, ArnlSystem& arnlsys)  :
  arnl(arnlsys),
  myPublishCB(this, &RosArnlNode::publish),
  actionServer(nh, "move_base", boost::bind(&RosArnlNode::execute_action_cb, this, _1), false),
  arnl_goal_done(false),
  action_executing(false),
  rosarnl_inventory_running(true)
{
  n = nh;
  
  // Figure out what frame_id's to use. if a tf_prefix param is specified,
  // it will be added to the beginning of the frame_ids.
  //
  // e.g. rosrun ... _tf_prefix:=MyRobot (or equivalently using <param>s in
  // roslaunch files)
  // will result in the frame_ids being set to /MyRobot/map etc,
  // rather than /map. This is useful for Multi Robot Systems.
  // See ROS Wiki for further details.

  tf_prefix = tf::getPrefixParam(n);
  frame_id_map = tf::resolve(tf_prefix, "map");
  frame_id_base_link = tf::resolve(tf_prefix, "base_link");
  frame_id_bumper = tf::resolve(tf_prefix, "bumpers_frame");
  frame_id_sonar = tf::resolve(tf_prefix, "sonar_frame");

  // initialize to all invalid
  pose_msg.pose.covariance.assign(-1);

  motors_state_pub = n.advertise<std_msgs::Bool>("motors_state", 1, true /*latch*/ );
  motors_state.data = false;
  published_motors_state = false;
  
  dock_state_pub = n.advertise<std_msgs::String>("dock_state", 1, true);
  dock_state.data = "";
  published_dock_state = false;

  pose_pub = n.advertise<geometry_msgs::PoseWithCovarianceStamped>("amcl_pose", 5, true /*latch*/);

  params_pub = n.advertise<std_msgs::Float64>("params",1, true /*latch*/);

  enable_srv = n.advertiseService("enable_motors", &RosArnlNode::enable_motors_cb, this);
  disable_srv = n.advertiseService("disable_motors", &RosArnlNode::disable_motors_cb, this);
  wander_srv = n.advertiseService("wander", &RosArnlNode::wander_cb, this);
  stop_srv = n.advertiseService("stop", &RosArnlNode::stop_cb, this);
  dock_srv = n.advertiseService("dock", &RosArnlNode::dock_cb, this);
  undock_srv = n.advertiseService("undock", &RosArnlNode::undock_cb, this);

  veltime = ros::Time::now();

  global_localization_srv = n.advertiseService("global_localization", &RosArnlNode::global_localization_srv_cb, this);

  initialpose_sub = n.subscribe("initialpose", 1, (boost::function <void(const geometry_msgs::PoseWithCovarianceStampedConstPtr&)>) boost::bind(&RosArnlNode::initialpose_sub_cb, this, _1));

  arnl_server_mode_pub = n.advertise<std_msgs::String>("arnl_server_mode", -1);
  arnl_server_status_pub = n.advertise<std_msgs::String>("arnl_server_status", -1);
  
  arnl_path_state_pub = n.advertise<std_msgs::String>("arnl_path_state", -1);
  
  battery_charge_percent_pub = n.advertise<std_msgs::Float32>("battery_state_of_charge", -1);
  battery_charge_state_pub = n.advertise<std_msgs::Int8>("battery_charging_state", -1);

  // TODO the move_base and move_bas_simple topics should be in separate node
  // handles?
  simple_goal_sub = n.subscribe("move_base_simple/goal", 1, (boost::function <void(const geometry_msgs::PoseStampedConstPtr&)>) boost::bind(&RosArnlNode::simple_goal_sub_cb, this, _1));
  cmd_drive_sub = n.subscribe("cmd_vel", 1, (boost::function <void(const geometry_msgs::TwistConstPtr&)>) boost::bind(&RosArnlNode::cmdvel_cb, this, _1));
  goalname_sub = n.subscribe("goalname", 1, (boost::function <void(const std_msgs::StringConstPtr&)>) boost::bind(&RosArnlNode::goalname_sub_cb, this, _1));
  shutdown_sub = n.subscribe("inventory_status", 1, (boost::function <void(const std_msgs::BoolConstPtr&)>) boost::bind(&RosArnlNode::shutdown_rosarnl_cb, this, _1));

  current_goal_pub = n.advertise<geometry_msgs::Pose>("current_goal", 1, true);
  arnl.pathTask->addNewGoalCB(new ArFunctor1C<RosArnlNode, ArPose>(this, &RosArnlNode::arnl_new_goal_cb));
  
  arnl.pathTask->addGoalFailedCB(new ArFunctor1C<RosArnlNode, ArPose>(this, &RosArnlNode::arnl_goal_failed_cb));
  arnl.pathTask->addGoalDoneCB(new ArFunctor1C<RosArnlNode, ArPose>(this, &RosArnlNode::arnl_goal_reached_cb));
  arnl.pathTask->addGoalInterruptedCB(new ArFunctor1C<RosArnlNode, ArPose>(this, &RosArnlNode::arnl_goal_interrupted_cb));
  arnl.pathTask->addStateChangeCB(new ArFunctorC<RosArnlNode>(this, &RosArnlNode::arnl_path_state_change_cb));


  // Publish data triggered by ARIA sensor interpretation task
  arnl.robot->lock();
  arnl.robot->addSensorInterpTask("ROSPublishingTask", 100, &myPublishCB);
  arnl.robot->unlock();
}

RosArnlNode::~RosArnlNode()
{
  Aria::exit(0);
}

int RosArnlNode::Setup()
{
  actionServer.start();
  return 0;
}

void RosArnlNode::spin()
{
  ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: Running ROS node...");
  while (rosarnl_inventory_running)
  {
    ros::spinOnce();
  }
  ROS_INFO("Shutdown request for rosarnl_node");
}



void RosArnlNode::publish()
{
  // todo could only publish if robot not stopped (unless arnl has TriggerTime
  // set in which case it might update localization even ifnot moving), or
  // use a callback from arnl for robot pose updates rather than every aria
  // cycle.  In particular, getting the covariance is a bit computational
  // intensive and not everyone needs it.

  ArTime tasktime;

  // Note, this is called via SensorInterpTask callback (myPublishCB, named "ROSPublishingTask"). ArRobot object 'robot' sholud not be locked or unlocked.
  ArPose pos = arnl.robot->getPose();

  // convert mm and degrees to position meters and quaternion angle in ros pose
  tf::poseTFToMsg(tf::Transform(tf::createQuaternionFromYaw(pos.getTh()*M_PI/180), tf::Vector3(pos.getX()/1000,
    pos.getY()/1000, 0)), pose_msg.pose.pose); 

  pose_msg.header.frame_id = "map";

  // ARIA/ARNL times are in reference to an arbitrary starting time, not OS
  // clock, so find the time elapsed between now and last ARNL localization
  // to adjust the time stamp in ROS time vs. now accordingly.
  //pose_msg.header.stamp = ros::Time::now();
  ArTime loctime = arnl.locTask->getLastLocaTime();
  ArTime arianow;
  const double dtsec = (double) loctime.mSecSince(arianow) / 1000.0;
  //printf("localization was %f seconds ago\n", dtsec);
  pose_msg.header.stamp = ros::Time(ros::Time::now().toSec() - dtsec);

  // TODO if robot is stopped, ARNL won't re-localize (unless TriggerTime option is
  // configured), so should we just use Time::now() in that case? or do users
  // expect long ages for poses if robot stopped?

#if 0
  {
    printf("ros now is   %12d sec + %12d nsec = %f seconds\n", ros::Time::now().sec, ros::Time::now().nsec, ros::Time::now().toSec());
    ArTime t;
    printf("aria now is  %12lu sec + %12lu ms\n", t.getSec(), t.getMSec());
    printf("arnl loc is  %12lu sec + %12lu ms\n", loctime.getSec(), loctime.getMSec());
    printf("pose stamp:= %12d sec + %12d nsec = %f seconds\n", pose_msg.header.stamp.sec, pose_msg.header.stamp.nsec, pose_msg.header.stamp.toSec());
    double d = ros::Time::now().toSec() - pose_msg.header.stamp.toSec();
    printf("diff is  %12f sec, \n", d);
    puts("----");
  }
#endif

#ifndef ROS_ARNL_NO_COVARIANCE
  ArMatrix var;
  ArPose meanp;
  if(arnl.locTask->findLocalizationMeanVar(meanp, var))
  {
    // ROS pose covariance is 6x6 with position and orientation in 3
    // dimensions each x, y, z, roll, pitch, yaw (but placed all in one 1-d
    // boost::array container)
    //
    // ARNL has x, y, yaw (aka theta):
    //    0     1     2
    // 0  x*x   x*y   x*yaw
    // 1  y*x   y*y   y*yaw
    // 2  yaw*x yaw*y yaw*yaw
    //
    // Also convert mm to m and degrees to radians.
    //
    // all elements in pose_msg.pose.covariance were initialized to -1 (invalid
    // marker) in the RosArnlNode constructor, so just update elements that
    // contain x, y and yaw.
  
    pose_msg.pose.covariance[6*0 + 0] = var(0,0)/1000.0;  // x/x
    pose_msg.pose.covariance[6*0 + 1] = var(0,1)/1000.0;  // x/y
    pose_msg.pose.covariance[6*0 + 5] = ArMath::degToRad(var(0,2)/1000.0);    //x/yaw
    pose_msg.pose.covariance[6*1 + 0] = var(1,0)/1000.0;  //y/x
    pose_msg.pose.covariance[6*1 + 1] = var(1,1)/1000.0;  // y/y
    pose_msg.pose.covariance[6*1 + 5] = ArMath::degToRad(var(1,2)/1000.0);  // y/yaw
    pose_msg.pose.covariance[6*5 + 0] = ArMath::degToRad(var(2,0)/1000.0);  //yaw/x
    pose_msg.pose.covariance[6*5 + 1] = ArMath::degToRad(var(2,1)/1000.0);  // yaw*y
    pose_msg.pose.covariance[6*5 + 5] = ArMath::degToRad(var(2,2)); // yaw*yaw
  }
#endif
  
  pose_pub.publish(pose_msg);


  //get the robot length parameter from the arnl.robot
 // const ArRobotParams *robot_params = arnl.robot->getRobotParams();
  double robot_length = arnl.robot->getRobotLength();

  params_msg.data = robot_length;
  params_pub.publish(params_msg);
  
  //Battery info publishing
  std_msgs::Float32 battery_percent_msg;
  battery_percent_msg.data = arnl.robot->getStateOfCharge();
  battery_charge_percent_pub.publish(battery_percent_msg);
  
  std_msgs::Int8 battery_charging_state_msg;
  battery_charging_state_msg.data = arnl.robot->getChargeState();
  battery_charge_state_pub.publish(battery_charging_state_msg);

  if(action_executing) 
  {
    move_base_msgs::MoveBaseFeedback feedback;
    feedback.base_position.pose = pose_msg.pose.pose;
    actionServer.publishFeedback(feedback);
  }


  // publishing transform map->base_link
  map_trans.header.stamp = ros::Time::now();
  map_trans.header.frame_id = frame_id_map;
  map_trans.child_frame_id = frame_id_base_link;
  
  map_trans.transform.translation.x = pos.getX()/1000;
  map_trans.transform.translation.y = pos.getY()/1000;
  map_trans.transform.translation.z = 0.0;
  map_trans.transform.rotation = tf::createQuaternionMsgFromYaw(pos.getTh()*M_PI/180);

  map_broadcaster.sendTransform(map_trans);

  
  // publish motors state if changed
  bool e = arnl.robot->areMotorsEnabled();
  if(e != motors_state.data || !published_motors_state)
  {
    ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: publishing new motors state: %s.", e?"yes":"no");
    motors_state.data = e;
    motors_state_pub.publish(motors_state);
    published_motors_state = true;
  }
  
  // publish dock state if changed
  std::string state(arnl.modeDock->toString(arnl.modeDock->getState()));
  if(state != dock_state.data)
  {
    ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: publishing new dock state: %s.", e?"yes":"no");
    dock_state.data = state;
    dock_state_pub.publish(dock_state);
    published_dock_state = true;
  }

  const char *s = arnl.getServerStatus();
  if(s != NULL && lastServerStatus != s)
  {
    ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: publishing new server status: %s", s);
    lastServerStatus = s;
    std_msgs::String msg;
    msg.data = lastServerStatus;
    arnl_server_status_pub.publish(msg);
  }

  const char *m = arnl.getServerMode();
  if(m != NULL && lastServerMode != m)
  {
    ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: publishing now server mode: %s", m);
    lastServerMode = m;
    std_msgs::String msg;
    msg.data = lastServerMode;
    arnl_server_mode_pub.publish(msg);
  }


  ROS_WARN_COND_NAMED((tasktime.mSecSince() > 20), "rosarnl_node", "rosarnl_node: publish aria task took %ld ms", tasktime.mSecSince());
}

bool RosArnlNode::enable_motors_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response)
{
    ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: Enable motors request.");
    arnl.robot->lock();
    check_estop("enable motors");
    arnl.robot->enableMotors();
    arnl.robot->unlock();
	// todo could wait and see if motors do become enabled, and send a response with an error flag if not
    return true;
}

bool RosArnlNode::disable_motors_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response)
{
    ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: Disable motors request.");
    arnl.robot->lock();
    arnl.robot->disableMotors();
    arnl.robot->unlock();
	// todo could wait and see if motors do become disabled, and send a response with an error flag if not
    return true;
}

bool RosArnlNode::wander_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response)
{
    ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: Enable wander mode request.");
    if(check_estop("enter wander mode"))
      return false;
    arnl.modeWander->activate();
    return true;
}

bool RosArnlNode::stop_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response)
{
    ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: Stop request.");
    arnl.modeStop->activate();
    return true;
}

bool RosArnlNode::dock_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response)
{
    ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: Docking procedure request.");
    if(check_estop("dock")) {
      return false;
    }
    arnl.modeDock->dock();
    return true;
}

bool RosArnlNode::undock_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response)
{
    ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: Undocking procedure request.");
    if(check_estop("undock")) {
      return false;
    }
    arnl.modeDock->undock();
    return true;
}

bool RosArnlNode::global_localization_srv_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response)
{
  ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: Localize init (global_localization service) request...");
//  arnl.locTask->localizeRobotInMapInit();
  if(! arnl.locTask->localizeRobotAtHomeBlocking() )
    ROS_WARN_NAMED("rosarnl_node", "rosarnl_node: Error in initial localization.");
  return true;
}


void RosArnlNode::arnl_path_state_change_cb()
{
  ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: ARNL path planning task state changed to %s", arnl.getPathStateName());
  std_msgs::String msg;
  msg.data = arnl.getPathStateName();
  arnl_path_state_pub.publish(msg);
}


ArPose RosArnlNode::rosPoseToArPose(const geometry_msgs::Pose& p)
{
  return ArPose( p.position.x * 1000.0, p.position.y * 1000.0, tf::getYaw(p.orientation) / (M_PI/180.0) );
}

geometry_msgs::Pose arPoseToRosPose(const ArPose& arpose)
{
  // TODO use tf::poseTFToMsg instead?
  geometry_msgs::Pose rospose;
  rospose.position.x = arpose.getX() / 1000.0;
  rospose.position.y = arpose.getY() / 1000.0;
  rospose.position.z = 0;
  tf::quaternionTFToMsg(tf::createQuaternionFromYaw(arpose.getTh()*M_PI/180.0), rospose.orientation);
  return rospose;
}

ArPoseWithTime RosArnlNode::rosPoseStampedToArPoseWithTime(const geometry_msgs::PoseStamped& p)
{ 
  ArPoseWithTime arp(rosPoseToArPose(p));
  ArTime t;
  t.setSec(p.header.stamp.sec);
  t.setMSec(p.header.stamp.nsec * 1.0e-6);
  arp.setTime(t);
  return arp;
}


void RosArnlNode::initialpose_sub_cb(const geometry_msgs::PoseWithCovarianceStampedConstPtr& msg)
{
  ArPose p = rosPoseToArPose(msg->pose.pose);
  ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: Init localization pose received %.0fmm, %.0fmm, %.0fdeg", p.getX(), p.getY(), p.getTh());
  arnl.locTask->forceUpdatePose(p);
}

void RosArnlNode::simple_goal_sub_cb(const geometry_msgs::PoseStampedConstPtr &msg)
{
  ArPose p = rosPoseToArPose(msg->pose);
  bool heading = !ArMath::isNan(p.getTh());
  ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: Received goal %.0fmm, %.0fmm, %.0fdeg", p.getX(), p.getY(), p.getTh());
  //arnl.pathTask->pathPlanToPose(p, true);
  arnl.modeGoto->gotoPose(p, heading);
}

void RosArnlNode::goalname_sub_cb(const std_msgs::StringConstPtr &msg)
{
  ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: Received named goal \"%s\"", msg->data.c_str());
  //arnl.pathTask->pathPlanToGoal(msg->data.c_str());
  arnl.modeGoto->gotoGoal(msg->data.c_str());
}

void RosArnlNode::arnl_new_goal_cb(ArPose arpose)
{
  // TODO is it ok to call publish from another thread? (this is called by path
  // planning task thread)
  // TODO should we start executing action if not yet executing?
  current_goal_pub.publish(arPoseToRosPose(arpose));
}


void RosArnlNode::execute_action_cb(const move_base_msgs::MoveBaseGoalConstPtr &goal)
{
  // the action execute callback is initiated by the first goal sent.  it should
  // continue running until the goal is reached or failed.  we need to also
  // check here for new goals received while the previous goal is in progress,
  // in which case we will set the new goal.   arnl callbacks are used to handle
  // reaching the goal, failure, or recognizing that the goal has been
  // preempted, which allows it to work in combination with MobileEyes or other
  // clients as well as the ros action client.
  ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: action: begin execution for new goal.");
  action_executing  = true;
  ArPose goalpose = rosPoseToArPose(goal->target_pose);
  ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: action: planning to goal %.0fmm, %.0fmm, %.0fdeg", goalpose.getX(), goalpose.getY(), goalpose.getTh());
  bool heading = !ArMath::isNan(goalpose.getTh());
  //arnl.pathTask->pathPlanToPose(goalpose, heading);
  arnl.modeGoto->gotoPose(goalpose, heading);
  arnl_goal_done = false;
  
  while(n.ok() && actionServer.isActive())
  {
    // TODO check for localization lost

    if(arnl_goal_done)
    {
      ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: action: goal done, ending execution.");
      action_executing = false;
      return;
    }

    if(actionServer.isPreemptRequested())
    {
      if(actionServer.isNewGoalAvailable())
      {
        // we were preempted by a new goal
        move_base_msgs::MoveBaseGoalConstPtr newgoal = actionServer.acceptNewGoal();
        goalpose = rosPoseToArPose(newgoal->target_pose);
        ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: action: new goal interrupted current goal.  planning to new goal %.0fmm, %.0fmm, %.0fdeg", goalpose.getX(), goalpose.getY(), goalpose.getTh());
        bool heading = !ArMath::isNan(goalpose.getTh());
        arnl.modeGoto->gotoPose(goalpose, heading);
      
        // action server will be set to preempted state by arnl interrupt
        // callback.
      }
      else
      {
        // we were simply asked to just go to "preempted" end state, with no new
        // goal
        ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: action: forced to preempted, ending execution.");
        actionServer.setPreempted();
        action_executing = false;
        arnl.modeGoto->deactivate();
        
        return;
      }
    }

    // feedback is published in the publish() task callback 

  }
  // node is shutting down, n.ok() returned false
  ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: action: node shutting down, setting aborted state and ending execution.");
  actionServer.setAborted(move_base_msgs::MoveBaseResult(), "Setting aborted state since node is shutting down.");
  action_executing = false;
}

void RosArnlNode::arnl_goal_reached_cb(ArPose p)
{
  if(action_executing)
  {
    ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: action: goal succeeded");
    actionServer.setSucceeded(move_base_msgs::MoveBaseResult(), "Goal succeeded");
  }
  else {
    puts("action not executing");
  }
  arnl_goal_done = true;
}

void RosArnlNode::arnl_goal_failed_cb(ArPose p)
{
  if(action_executing)
  {
    ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: action: goal failed");
    actionServer.setAborted(move_base_msgs::MoveBaseResult(), "Goal failed");
  }
  arnl_goal_done = true;
}

void RosArnlNode::arnl_goal_interrupted_cb(ArPose p)
{
  if(action_executing)
  {
    ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: action: goal interrupted");
    actionServer.setPreempted();
  }
}

void RosArnlNode::cmdvel_cb( const geometry_msgs::TwistConstPtr &msg)
{
  veltime = ros::Time::now();//declare
  //ROS_INFO( "new speed: [%0.2f,%0.2f](%0.3f)", msg->linear.x*1e3, msg->angular.z, veltime.toSec() );
  double transRatio = msg->linear.x*1e3;
  double rotRatio = msg->angular.z*180/M_PI*1e3;
  double throttleRatio = 20;
  double lateralRatio = msg->linear.y*1e3;
  arnl.robot->lock();
  arnl.modeRatioDrive->ratioDrive(transRatio, rotRatio, throttleRatio, true, lateralRatio);
  arnl.robot->unlock();
  //ROS_DEBUG("rosarnl_node: sent vels to to aria (time %f): x vel %f mm/s, y vel %f mm/s, ang vel %f deg/s", veltime.toSec(),
    //(double) msg->linear.x * 1e3, (double) msg->linear.y * 1.3, (double) msg->angular.z * 180/M_PI);
}

void RosArnlNode::shutdown_rosarnl_cb(const std_msgs::BoolConstPtr &msg)
{
  if (msg->data)
  {
    rosarnl_inventory_running = false;
  }
}





void ariaLogHandler(const char *msg, ArLog::LogLevel level)
{
  // node that ARIA logging is normally limited at Normal and Terse only. Set
  // ARLOG_LEVEL environment variable to override.
  switch(level)
  {
    case ArLog::Normal:
      ROS_INFO_NAMED("ARNL", "ARNL: %s", msg);
      return;
    case ArLog::Terse:
      ROS_WARN_NAMED("ARNL", "ARNL: %s", msg);
      return;
    case ArLog::Verbose:
      ROS_DEBUG_NAMED("ARNL", "ARNL: %s", msg);
      return;
  }
}






int main( int argc, char** argv )
{
  ros::init(argc,argv, "rosarnl_node");

  Aria::init();
  Arnl::init();
 /* set log type to None to only use
    ariaLogHandler to redirect ARNL log messages to rosconsole by deufault. This can
    be changed in the ARNL parameter file however.
  */
  ArLog::init(ArLog::None, ArLog::Normal); 
  ArLog::setFunctor(new ArGlobalFunctor2<const char *, ArLog::LogLevel>(&ariaLogHandler));

  ArnlSystem arnl;
  if( arnl.setup() != ArnlSystem::OK)
  {
    ROS_FATAL_NAMED("rosarnl_node", "rosarnl_node: ARNL and ARIA setup failed... \n" );
    return -2;
  }


  ArGlobalFunctor1<int> *ariaExitF = new ArGlobalFunctor1<int>(&Aria::exit, 9);
  arnl.robot->addDisconnectOnErrorCB(ariaExitF);
  for(std::map<int, ArLaser*>::iterator i = arnl.robot->getLaserMap()->begin();
      i != arnl.robot->getLaserMap()->end();
      ++i)
  {
    (*i).second->addDisconnectOnErrorCB(ariaExitF);
  }

  ros::NodeHandle n(std::string("~"));
  RosArnlNode *node = new RosArnlNode(n, arnl);
  if( node->Setup() != 0 )
  {
    ROS_FATAL_NAMED("rosarnl_node", "rosarnl_node: ROS node setup failed... \n" );
    return -1;
  }

  arnl.robot->lock();
  const std::map<int, ArLaser*> *lasers = arnl.robot->getLaserMap();
  for(std::map<int, ArLaser*>::const_iterator i = lasers->begin(); i != lasers->end(); ++i)
  {
    ArLaser *l = i->second;
    ROS_INFO_NAMED("rosarnl_node", "rosarnl_node: Creating publisher for laser %s\n", l->getName());
    new LaserPublisher(l, n);
  }
  arnl.robot->unlock();

  node->spin();
  
  delete node;  

  return 0;

}
