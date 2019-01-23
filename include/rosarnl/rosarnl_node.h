
#include "Aria/Aria.h"
#include "Arnl.h"
#include "ArPathPlanningInterface.h"
#include "ArLocalizationTask.h"
#include "ArServerClasses.h"
#include "ArDocking.h"

#include "ArnlSystem.h"

#include "LaserPublisher.h"
#include <rosarnl/BatteryStatus.h>
#include <rosarnl/WheelLight.h>
#include <rosarnl/ChangeMap.h>
#include <rosarnl/Stop.h>

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
#include <std_msgs/Empty.h>
#include <std_msgs/Int8.h>
#include <std_srvs/Empty.h>
#include <actionlib/server/simple_action_server.h>
#include <move_base_msgs/MoveBaseAction.h>

// Speech synthesis. Requires optional build parameter ROSARNL_SPEECH
#ifdef ROSARNL_SPEECH
  #include "ArnlSystem.h"
  #include "ArCepstral.h"
  #include "ArSoundsQueue.h"
  #include "ArSpeech.h"
#endif

class RosArnlNode
{
public:
  RosArnlNode(ros::NodeHandle n, ArnlSystem& arnlsys);
  virtual ~RosArnlNode();

public:
  /**
   * @breif Starts the MoveAction server.
   * @return True on success.
   */
  bool Setup();
  
  /**
   * @breif Begin the main operating loop.
   */
  void spin();
  
  /**
   * @breif Update the published topics.
   */
  void publish();

protected:
  ros::NodeHandle n;
  ArnlSystem &arnl;
  
  ArFunctorC<RosArnlNode> myPublishCB;

  /**
   * @breif Convert ROS pose message to Aria ArPose type
   */
  ArPose rosPoseToArPose(const geometry_msgs::Pose& p);
  
  /**
   * @breif Convert ROS PoseStamped message to Aria ArPose type
   */
  ArPose rosPoseToArPose(const geometry_msgs::PoseStamped& p) { return rosPoseToArPose(p.pose); }
  
  /**
   * @breif Convert ROS PoseStamped message to Aria ARPoseWithTime type
   */
  ArPoseWithTime rosPoseStampedToArPoseWithTime(const geometry_msgs::PoseStamped& p);
  
  /**
   * @breif Convert Aria ArPose type to ROS pose message
   */
  geometry_msgs::Pose rosPoseToArPose(const ArPose& arpose);

  ros::ServiceServer enable_srv;
  ros::ServiceServer disable_srv;
  ros::ServiceServer wander_srv;
  ros::ServiceServer wander_no_map_srv;
  ros::ServiceServer change_map_srv; // request for a new map
  ros::ServiceServer stop_srv;
  ros::ServiceServer dock_srv;
  ros::ServiceServer undock_srv;
  ros::ServiceServer wheel_light_srv;
  ros::ServiceServer global_localization_srv;

  /**
   * @breif Enable drive motors. ROS service callback function.
   * @srv std_srvs::Empty
   * @return True on success, false on failure.
   */
  bool enable_motors_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response);
  
  /**
   * @breif Disable drive motors. ROS service callback function.
   * @srv std_srvs::Empty
   * @return True on success, false on failure.
   */
  bool disable_motors_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response);
    
  /**
   * @breif Activate wander mode. ROS service callback function.
   * @srv std_srvs::Empty
   * @return True on success, false if e-stop is active.
   */
  bool wander_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response);
  
  /**
   * @breif Change the map. ROS service callback function.
   * @srv ChangeMap::Request request string with filename
   * @return Always true.
   */
  bool change_map_cb(rosarnl::ChangeMap::Request& request, rosarnl::ChangeMap::Response& response);

  /**
   * @breif Stop the platform. ROS service callback function.
   * @srv std_srvs::Empty
   * @return Always true.
   */
  bool stop_cb(rosarnl::Stop::Request& request, rosarnl::Stop::Response& response);
  
  /**
   * @breif Go to dock. ROS service callback function.
   * @srv std_srvs::Empty
   * @return True on success, false on failure.
   */
  bool dock_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response);
  
  /**
   * @breif Undock. ROS service callback function.
   * @srv std_srvs::Empty
   * @return True on success, false on failure.
   */
  bool undock_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response);
  
  /**
   * @breif Perform global localization. ROS service callback function.
   * @srv std_srvs::Empty
   * @return True on success, false on failure.
   */
  bool wheel_light_cb(rosarnl::WheelLight::Request& request, rosarnl::WheelLight::Response& response);
  
  /**
   * @breif Perform global localization. ROS service callback function.
   * @srv std_srvs::Empty
   * @return True on success, false on failure.
   */
  bool global_localization_srv_cb(std_srvs::Empty::Request& request, std_srvs::Empty::Response& response);

  ros::Publisher motors_state_pub;
  ros::Publisher dock_state_pub;
  std_msgs::Bool motors_state;
  std_msgs::String dock_state;
  bool published_motors_state;
  bool published_dock_state;

  geometry_msgs::PoseWithCovarianceStamped pose_msg;
  std_msgs::Float64 loc_msg;
  ros::Publisher pose_pub;
  ros::Publisher loc_pub;

  // Battery publishing
  ros::Publisher battery_pub;
  ros::Time last_battery_pub_time;
  ros::Duration battery_pub_period;

  geometry_msgs::TransformStamped map_trans;
  tf::TransformBroadcaster map_broadcaster;

  std::string tf_prefix;
  std::string frame_id_map;
  std::string frame_id_base_link;
  std::string frame_id_bumper;
  std::string frame_id_sonar;

  tf::TransformListener listener;

  ros::Subscriber initialpose_sub;
  void initialpose_sub_cb(const geometry_msgs::PoseStampedConstPtr &msg);

  ros::Publisher arnl_server_mode_pub;
  ros::Publisher arnl_server_status_pub;
  ros::Publisher arnl_shutdown_confirm_pub;

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
  void shutdown_rosarnl_cb(const std_msgs::EmptyConstPtr &msg);

  


  ros::Publisher current_goal_pub;
  void arnl_new_goal_cb(ArPose p);

  // TODO may replace with MoveBase action definitions?
  typedef actionlib::SimpleActionServer<move_base_msgs::MoveBaseAction> ArnlActionServer;
  ArnlActionServer actionServer;
  void execute_action_cb(const move_base_msgs::MoveBaseGoalConstPtr& goal);
  bool arnl_goal_done;
  bool action_executing;
  bool shutdown_requested;
  void arnl_goal_reached_cb(ArPose p);
  void arnl_goal_failed_cb(ArPose p);
  void arnl_goal_interrupted_cb(ArPose p);
  
  // If robot has e--top button pressed, print a warning and return true. Otherwise return false.
  bool check_estop(const char *s);
  
  // Optional speech synthesis
  #ifdef ROSARNL_SPEECH
    ArCepstral cepstral;
    ros::Subscriber speech_sub_;
    void speech_cb(const std_msgs::StringConstPtr &msg);
  #endif
};
