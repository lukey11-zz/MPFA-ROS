#include <ros/ros.h>

// ROS libraries
#include <angles/angles.h>
#include <random_numbers/random_numbers.h>
#include <tf/transform_datatypes.h>
#include <tf/transform_listener.h>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

// ROS messages
#include <std_msgs/Float32.h>
#include <std_msgs/Int16.h>
#include <std_msgs/UInt8.h>
#include <std_msgs/String.h>
#include <sensor_msgs/Joy.h>
#include <sensor_msgs/Range.h>
#include <geometry_msgs/Pose2D.h>
#include <geometry_msgs/Twist.h>
#include <nav_msgs/Odometry.h>
#include <apriltags_ros/AprilTagDetectionArray.h>
#include <std_msgs/Float32MultiArray.h>
#include "swarmie_msgs/Waypoint.h"
#include "swarmie_msgs/PheromoneTrail.h" //qilu 12/2017
// Include Controllers
#include "LogicController.h"
#include <vector>
#include <map>

#include "Point.h"
#include "Tag.h"

// To handle shutdown signals so the node quits
// properly in response to "rosnode kill"
#include <ros/ros.h>
#include <signal.h>

#include <exception> // For exception handling

#define CENTER_TAG_ID 256

using namespace std;

// Define Exceptions
// Define an exception to be thrown if the user tries to create
// a RangeShape using invalid dimensions
class ROSAdapterRangeShapeInvalidTypeException : public std::exception {
public:
  ROSAdapterRangeShapeInvalidTypeException(std::string msg) {
    this->msg = msg;
  }
  
  virtual const char* what() const throw()
  {
    std::string message = "Invalid RangeShape type provided: " + msg;
    return message.c_str();
  }
  
private:
  std::string msg;
};


// Random number generator
random_numbers::RandomNumberGenerator* rng;

std_msgs::UInt8 collision_msg;

// Create logic controller

LogicController logicController;

void humanTime();

// Behaviours Logic Functions
void sendDriveCommand(double linearVel, double angularVel);
void openFingers(); // Open fingers to 90 degrees
void closeFingers();// Close fingers to 0 degrees
void raiseWrist();  // Return wrist back to 0 degrees
void lowerWrist();  // Lower wrist to 50 degrees
void resultHandler();


Point updateCenterLocation();
void transformMapCentertoOdom();

float distanceToCenter(); //qilu 12/2017
// Numeric Variables for rover positioning
geometry_msgs::Pose2D currentLocation;
geometry_msgs::Pose2D currentLocationMap;
geometry_msgs::Pose2D currentLocationAverage;

geometry_msgs::Pose2D centerLocation;
geometry_msgs::Pose2D centerLocationMap;
geometry_msgs::Pose2D centerLocationOdom;
geometry_msgs::Pose2D centerLocationMapRef;

int currentMode = 0;
const float behaviourLoopTimeStep = 0.1; // time between the behaviour loop calls
const float status_publish_interval = 1;
const float heartbeat_publish_interval = 2;
//const float waypointTolerance = 1; //10 cm tolerance. //qilu no reference


// used for calling code once but not in main
bool initilized = false;

float linearVelocity = 0;
float angularVelocity = 0;

float prevWrist = 0;
float prevFinger = 0;
long int startTime = 0;
float minutesTime = 0;
float hoursTime = 0;

float drift_tolerance = 1.0; // meters

Result result;

std_msgs::String msg;

float arena_dim =14.0;

//vector<Point> roverPositions;
vector<string> roverNames;
Point roverInitPos;
	
geometry_msgs::Twist velocity;
char host[128];
string publishedName;
char prev_state_machine[128];

vector<string> rover_names;
size_t swarm_size = 0;
// Publishers
ros::Publisher stateMachinePublisher;
ros::Publisher status_publisher;
ros::Publisher fingerAnglePublisher;
ros::Publisher wristAnglePublisher;
ros::Publisher infoLogPublisher;
ros::Publisher driveControlPublisher;
ros::Publisher heartbeatPublisher;
ros::Publisher waypointFeedbackPublisher;
ros::Publisher pheromoneTrailPublisher;//qilu 12/2017
ros::Publisher obstaclePublisher;
ros::Publisher centerLocationOffsetPublisher;

// Publishes swarmie_msgs::Waypoint messages on "/<robot>/waypooints"
// to indicate when waypoints have been reached.
// Subscribers
ros::Subscriber joySubscriber;
ros::Subscriber modeSubscriber; 
ros::Subscriber targetSubscriber;
ros::Subscriber odometrySubscriber;
ros::Subscriber mapSubscriber;
ros::Subscriber pheromoneTrailSubscriber;//qilu 12/2017
ros::Subscriber roverSubscriber;
ros::Subscriber virtualFenceSubscriber;
ros::Subscriber arenaDimSubscriber;

// manualWaypointSubscriber listens on "/<robot>/waypoints/cmd" for
// swarmie_msgs::Waypoint messages.
ros::Subscriber manualWaypointSubscriber;

// Timers
ros::Timer stateMachineTimer;
ros::Timer publish_status_timer;
ros::Timer publish_heartbeat_timer;

// records time for delays in sequanced actions, 1 second resolution.
time_t timerStartTime;

// An initial delay to allow the rover to gather enough position data to 
// average its location.
unsigned int startDelayInSeconds = 30;
float timerTimeElapsed = 0;

//Transforms
tf::TransformListener *tfListener;

// OS Signal Handler
void sigintEventHandler(int signal);

//Callback handlers
void joyCmdHandler(const sensor_msgs::Joy::ConstPtr& message);
void modeHandler(const std_msgs::UInt8::ConstPtr& message);
void targetHandler(const apriltags_ros::AprilTagDetectionArray::ConstPtr& tagInfo);
void odometryHandler(const nav_msgs::Odometry::ConstPtr& message);
void mapHandler(const nav_msgs::Odometry::ConstPtr& message);
void virtualFenceHandler(const std_msgs::Float32MultiArray& message);
void arenaDimHandler(const std_msgs::Float32::ConstPtr& message);
//void roverHandler(const swarmie_msgs::RoverInfo& message);
void pheromoneTrailHandler(const swarmie_msgs::PheromoneTrail& message); //qilu 12/2017
void manualWaypointHandler(const swarmie_msgs::Waypoint& message);
void behaviourStateMachine(const ros::TimerEvent&);
void publishStatusTimerEventHandler(const ros::TimerEvent& event);
void publishHeartBeatTimerEventHandler(const ros::TimerEvent& event);
void sonarHandler(const sensor_msgs::Range::ConstPtr& sonarLeft, const sensor_msgs::Range::ConstPtr& sonarCenter, const sensor_msgs::Range::ConstPtr& sonarRight);

// Converts the time passed as reported by ROS (which takes Gazebo simulation rate into account) into milliseconds as an integer.
long int getROSTimeInMilliSecs();

int main(int argc, char **argv) {
  
  gethostname(host, sizeof (host));
  string hostname(host);
  
  if (argc >= 2) 
  {
    publishedName = argv[1];
    cout << "Welcome to the world of tomorrow " << publishedName
         << "!  Behaviour turnDirectionule started." << endl;
  } 
  else 
  {
    publishedName = hostname;
    cout << "No Name Selected. Default is: " << publishedName << endl;
  }
  
  // NoSignalHandler so we can catch SIGINT ourselves and shutdown the node
  ros::init(argc, argv, (publishedName + "_BEHAVIOUR"), ros::init_options::NoSigintHandler);
  ros::NodeHandle mNH;
  
  // Register the SIGINT event handler so the node can shutdown properly
  signal(SIGINT, sigintEventHandler);
  
  joySubscriber = mNH.subscribe((publishedName + "/joystick"), 10, joyCmdHandler);
  modeSubscriber = mNH.subscribe((publishedName + "/mode"), 1, modeHandler);
  targetSubscriber = mNH.subscribe((publishedName + "/targets"), 10, targetHandler);
  odometrySubscriber = mNH.subscribe((publishedName + "/odom/filtered"), 10, odometryHandler);
  mapSubscriber = mNH.subscribe((publishedName + "/odom/ekf"), 10, mapHandler);
  
  //roverSubscriber = mNH.subscribe("/rovers", 10, roverHandler);
  pheromoneTrailSubscriber = mNH.subscribe("/pheromones", 10, pheromoneTrailHandler);//qilu 12/2017
  arenaDimSubscriber = mNH.subscribe("/arena_dim", 10, arenaDimHandler); //qilu 08/2017
  manualWaypointSubscriber = mNH.subscribe((publishedName + "/waypoints/cmd"), 10, manualWaypointHandler);
  message_filters::Subscriber<sensor_msgs::Range> sonarLeftSubscriber(mNH, (publishedName + "/sonarLeft"), 10);
  message_filters::Subscriber<sensor_msgs::Range> sonarCenterSubscriber(mNH, (publishedName + "/sonarCenter"), 10);
  message_filters::Subscriber<sensor_msgs::Range> sonarRightSubscriber(mNH, (publishedName + "/sonarRight"), 10);
  
  obstaclePublisher = mNH.advertise<std_msgs::UInt8>((publishedName + "/obstacle"), 10, true);
  status_publisher = mNH.advertise<std_msgs::String>((publishedName + "/status"), 1, true);
  stateMachinePublisher = mNH.advertise<std_msgs::String>((publishedName + "/state_machine"), 1, true);
  fingerAnglePublisher = mNH.advertise<std_msgs::Float32>((publishedName + "/fingerAngle/cmd"), 1, true);
  wristAnglePublisher = mNH.advertise<std_msgs::Float32>((publishedName + "/wristAngle/cmd"), 1, true);
  infoLogPublisher = mNH.advertise<std_msgs::String>("/infoLog", 1, true);
  driveControlPublisher = mNH.advertise<geometry_msgs::Twist>((publishedName + "/driveControl"), 10);
  heartbeatPublisher = mNH.advertise<std_msgs::String>((publishedName + "/behaviour/heartbeat"), 1, true);
  pheromoneTrailPublisher = mNH.advertise<swarmie_msgs::PheromoneTrail>("/pheromones", 10, true);
  waypointFeedbackPublisher = mNH.advertise<swarmie_msgs::Waypoint>((publishedName + "/waypoints"), 1, true);

  centerLocationOffsetPublisher = mNH.advertise<std_msgs::Float32MultiArray>((publishedName + "/centerLocationOffset"), 10, true);
  publish_status_timer = mNH.createTimer(ros::Duration(status_publish_interval), publishStatusTimerEventHandler);
  stateMachineTimer = mNH.createTimer(ros::Duration(behaviourLoopTimeStep), behaviourStateMachine);
  
  publish_heartbeat_timer = mNH.createTimer(ros::Duration(heartbeat_publish_interval), publishHeartBeatTimerEventHandler);
  
  typedef message_filters::sync_policies::ApproximateTime<sensor_msgs::Range, sensor_msgs::Range, sensor_msgs::Range> sonarSyncPolicy;
  
  message_filters::Synchronizer<sonarSyncPolicy> sonarSync(sonarSyncPolicy(10), sonarLeftSubscriber, sonarCenterSubscriber, sonarRightSubscriber);
  sonarSync.registerCallback(boost::bind(&sonarHandler, _1, _2, _3));
  tfListener = new tf::TransformListener();
  std_msgs::String msg;
  msg.data = "Log Started";
  infoLogPublisher.publish(msg);
  stringstream ss;
  ss << "Rover start delay set to " << startDelayInSeconds << " seconds";
  msg.data = ss.str();
  infoLogPublisher.publish(msg);

  if(currentMode != 2 && currentMode != 3)
  {
    // ensure the logic controller starts in the correct mode.
    ss<<"Set the logic controller in manual mode"<<endl;
    logicController.SetModeManual();
  }

  timerStartTime = time(0);
  ros::spin();
  
  return EXIT_SUCCESS;
}


// This is the top-most logic control block organised as a state machine.
// This function calls the dropOff, pickUp, and search controllers.
// This block passes the goal location to the proportional-integral-derivative
// controllers in the abridge package.
void behaviourStateMachine(const ros::TimerEvent&)
{
  int roverIdx=0;
  std_msgs::String stateMachineMsg;
  
  // time since timerStartTime was set to current time
  timerTimeElapsed = time(0) - timerStartTime;
  
  // Robot is in automode
  if (currentMode == 2 || currentMode == 3) 
  {
  // init code goes here. (code that runs only once at start of
  // auto mode but wont work in main goes here)
      if (!initilized)
      {

        if (timerTimeElapsed > startDelayInSeconds)
        {

	      // initialization has run
	      //cout<<"initialization has run..."<<endl;
	      initilized = true;
	      //TODO: this just sets center to 0 over and over and needs to change
	      Point centerOdom;
	      centerOdom.x = 1.308 * cos(currentLocation.theta);
	      centerOdom.y = 1.308 * sin(currentLocation.theta);
	      centerOdom.theta = centerLocation.theta;
	      logicController.SetCenterLocationOdom(centerOdom);
	      
        // Set the global offset for physical rovers; we assume that a robot
        // is placed a specified distance from the center... This position
        // will be the negative of centerOdom.
        std_msgs::Float32MultiArray centerOdomOffsetMessage;
        centerOdomOffsetMessage.data.push_back(-centerOdom.x);
        centerOdomOffsetMessage.data.push_back(-centerOdom.y);
        centerOdomOffsetMessage.data.push_back(centerOdom.theta);
        centerLocationOffsetPublisher.publish(centerOdomOffsetMessage);

	      Point centerMap;
	      centerMap.x = currentLocationMap.x + (1.308 * cos(currentLocationMap.theta));
	      centerMap.y = currentLocationMap.y + (1.308 * sin(currentLocationMap.theta));
	      centerMap.theta = centerLocationMap.theta;
	      logicController.SetCenterLocationMap(centerMap);
	      
	      centerLocationMap.x = centerMap.x;
	      centerLocationMap.y = centerMap.y;
	      
	      centerLocationOdom.x = centerOdom.x;
	      centerLocationOdom.y = centerOdom.y;
	      
	  Point pos(-centerLocationOdom.x, -centerLocationOdom.y, 0);  
	  roverInitPos = pos;
	  
      logicController.SetRoverInitLocation(roverInitPos);
	      startTime = getROSTimeInMilliSecs();
	      
	  logicController.SetArenaSize(arena_dim);
	    }

	    else
	    {
	      return;
	    }
    
      }

    
    humanTime();
    

    if (logicController.layPheromone()) 
    {
  	 /* for(int i=0; i<roverNames.size(); i++)
      {
		if(roverNames[i] == publishedName)
		{
			roverIdx = i;
			break;
		}
	  }*/
	  
      //logicController.SetRoverInitLocation(roverPositions[roverIdx]);
       
      swarmie_msgs::PheromoneTrail trail;
      Point pheromone_location_point = logicController.GetCurrentLocation();//qilu 12/2017
      //cout<<"TestStatus: ROSAdapter, GetCurrentLocation()=["<<logicController.GetCurrentLocation().x<<", "<<logicController.GetCurrentLocation().y<<"]"<<endl;
      //cout<<"TestStatus: centerLocation=["<<centerLocation.x<<", "<<centerLocation.y<<"]"<<endl;
      //cout<<"TestStatus: centerLocationOdom=["<<centerLocationOdom.x<<", "<<centerLocationOdom.y<<"]"<<endl;
      //cout<<"TestStatus: Rover "<<roverNames[roverIdx]<<" position=["<<roverPositions[roverIdx].x<<", "<<roverPositions[roverIdx].y<<"]"<<endl;
      geometry_msgs::Pose2D pheromone_location;
      pheromone_location.x = pheromone_location_point.x - centerLocation.x;
      pheromone_location.y = pheromone_location_point.y - centerLocation.y;
      
      //map to the global location related to the rover's initial location
      cout<<"wpTestStatusLocationTest: rovername="<<publishedName<<endl;
      pheromone_location.x += roverInitPos.x;   
      pheromone_location.y += roverInitPos.y;
      
      
      //cout << "*****logicController.GetCenterIdx() = "<<logicController.GetCenterIdx()<<endl;
      //cout << "*****current location = "<<currentLocation<<endl;
      //cout << "TestStatus: transformed pheromone_location = ("<<pheromone_location.x << ", "<< pheromone_location.y<< ")"<<endl;
      trail.waypoints.push_back(pheromone_location);
      //trail.centerIdx = logicController.GetCenterIdx();//this is for MPFA
      pheromoneTrailPublisher.publish(trail);

      //cout << "TestStatus: ===================================" << endl;
      // Return to allow pheromone trail handler to receive the pheromone location
      return;
    }
    
    
    //update the time used by all the controllers
    logicController.SetCurrentTimeInMilliSecs( getROSTimeInMilliSecs() );
    
    //update center location
    logicController.SetCenterLocationOdom( updateCenterLocation() );
    
    //ask logic controller for the next set of actuator commands
    result = logicController.DoWork();
    
    bool wait = false;
    
    //if a wait behaviour is thrown sit and do nothing untill logicController is ready
    if (result.type == behavior)
    {
      if (result.b == wait)
      {
        wait = true;
      }
    }
    
    //do this when wait behaviour happens
    if (wait)
    {
      sendDriveCommand(0.0,0.0);
      std_msgs::Float32 angle;
      
      angle.data = prevFinger;
      fingerAnglePublisher.publish(angle);
      angle.data = prevWrist;
      wristAnglePublisher.publish(angle);
    }
    
    //normally interpret logic controllers actuator commands and deceminate them over the appropriate ROS topics
    else
    {
      
      sendDriveCommand(result.pd.left,result.pd.right);
      

      //Alter finger and wrist angle is told to reset with last stored value if currently has -1 value
      std_msgs::Float32 angle;
      if (result.fingerAngle != -1)
      {
        angle.data = result.fingerAngle;
        fingerAnglePublisher.publish(angle);
        prevFinger = result.fingerAngle;
      }

      if (result.wristAngle != -1)
      {
	angle.data = result.wristAngle;
        wristAnglePublisher.publish(angle);
        prevWrist = result.wristAngle;
      }
    }
  collision_msg.data = logicController.getCollisionCalls();
  obstaclePublisher.publish(collision_msg);
    //publishHandeling here
    //logicController.getPublishData(); suggested
    //adds a blank space between sets of debugging data to easily tell one tick from the next
    cout << endl;
  }
  
  // mode is NOT auto
  else
  {
    humanTime();

    logicController.SetCurrentTimeInMilliSecs( getROSTimeInMilliSecs() );

    // publish current state for the operator to see
    stateMachineMsg.data = "WAITING";

    // poll the logicController to get the waypoints that have been
    // reached.
    std::vector<int> cleared_waypoints = logicController.GetClearedWaypoints();

    for(std::vector<int>::iterator it = cleared_waypoints.begin();
        it != cleared_waypoints.end(); it++)
    {
      swarmie_msgs::Waypoint wpt;
      wpt.action = swarmie_msgs::Waypoint::ACTION_REACHED;
      wpt.id = *it;
      waypointFeedbackPublisher.publish(wpt);
    }
    result = logicController.DoWork();
    if(result.type != behavior || result.b != wait)
    {
      // if the logic controller requested that the robot drive, then
      // drive. Otherwise there are no manual waypoints and the robot
      // should sit idle. (ie. only drive according to joystick
      // input).
      sendDriveCommand(result.pd.left,result.pd.right);
    }
  }

  // publish state machine string for user, only if it has changed, though
  if (strcmp(stateMachineMsg.data.c_str(), prev_state_machine) != 0)
  {
    stateMachinePublisher.publish(stateMachineMsg);
    sprintf(prev_state_machine, "%s", stateMachineMsg.data.c_str());
  }
}

void sendDriveCommand(double left, double right)
{
  velocity.linear.x = left,
      velocity.angular.z = right;
  // publish the drive commands
  driveControlPublisher.publish(velocity);
}

/*************************
 * ROS CALLBACK HANDLERS *
 *************************/

void targetHandler(const apriltags_ros::AprilTagDetectionArray::ConstPtr& message) {

  // Don't pass April tag data to the logic controller if the robot is not in autonomous mode.
  // This is to make sure autonomous behaviours are not triggered while the rover is in manual mode. 
  if(currentMode == 0 || currentMode == 1) 
  { 
    return; 
  }

  bool ignored_tag = false;
  // Number of resource tags
  int num_cube_tags = 0;
  int num_center_tags =0;
  if (message->detections.size() > 0) {
    vector<Tag> tags;

    for (int i = 0; i < message->detections.size(); i++) 
    {

      // Package up the ROS AprilTag data into our own type that does not rely on ROS.
      Tag loc;
      loc.setID( message->detections[i].id );

      if (loc.getID() == 0) 
      {
        num_cube_tags++;
      }
      else if(loc.getID() == CENTER_TAG_ID)
      {
	    num_center_tags++;
	  }
	  
      // Pass the position of the AprilTag
      geometry_msgs::PoseStamped tagPose = message->detections[i].pose;
      loc.setPosition( make_tuple( tagPose.pose.position.x,
				   tagPose.pose.position.y,
				   tagPose.pose.position.z ) );

      // Pass the orientation of the AprilTag
      loc.setOrientation( ::boost::math::quaternion<float>( tagPose.pose.orientation.x,
							    tagPose.pose.orientation.y,
							    tagPose.pose.orientation.z,
							    tagPose.pose.orientation.w ) );
      tags.push_back(loc);
    }
    if(num_center_tags >= 1)// reset the location of the center
    {
		centerLocationMap.x = currentLocationMap.x + 1.0*cos(currentLocationMap.theta);
        centerLocationMap.y = currentLocationMap.y + 1.0*sin(currentLocationMap.theta);
        centerLocationOdom.x = currentLocation.x + 1.0*cos(currentLocation.theta);
        centerLocationOdom.y = currentLocation.y + 1.0*sin(currentLocation.theta);  
	}
    
        
    
    logicController.SetAprilTags(tags);
  }
  
}

void modeHandler(const std_msgs::UInt8::ConstPtr& message) {
  currentMode = message->data;
  if(currentMode == 2 || currentMode == 3) {
    logicController.SetModeAuto();
  }
  else {
    logicController.SetModeManual();
  }
  sendDriveCommand(0.0, 0.0);
}

void sonarHandler(const sensor_msgs::Range::ConstPtr& sonarLeft, const sensor_msgs::Range::ConstPtr& sonarCenter, const sensor_msgs::Range::ConstPtr& sonarRight) 
{ 
  logicController.SetSonarData(sonarLeft->range, sonarCenter->range, sonarRight->range);
}

void arenaDimHandler(const std_msgs::Float32::ConstPtr& message)
{
	arena_dim = message->data;
}


/*void roverHandler(const swarmie_msgs::RoverInfo& message)
{
	swarmie_msgs::RoverInfo rover_info = message;
	
	for(int i=0; i < rover_info.names.size(); i++)
	{
		//cout<<"TestStatus: rover["<<i<<"]="<<rover_info.names[i]<<endl;
		roverNames.push_back(rover_info.names[i]);
		
		//cout<<"TestStatus: rover["<<i<<"] pos=["<<rover_info.positions[i].x<<", "<<rover_info.positions[i].y<<"]"<<endl;
		
		Point pos(rover_info.positions[i].x, rover_info.positions[i].y, rover_info.positions[i].theta);
		roverPositions.push_back(pos);		 
	} 
}*/
	
void odometryHandler(const nav_msgs::Odometry::ConstPtr& message) {
  //Get (x,y) location directly from pose
  currentLocation.x = message->pose.pose.position.x;
  currentLocation.y = message->pose.pose.position.y;
  
  //Get theta rotation by converting quaternion orientation to pitch/roll/yaw
  tf::Quaternion q(message->pose.pose.orientation.x, message->pose.pose.orientation.y, message->pose.pose.orientation.z, message->pose.pose.orientation.w);
  tf::Matrix3x3 m(q);
  double roll, pitch, yaw;
  m.getRPY(roll, pitch, yaw);
  currentLocation.theta = yaw;
  
  linearVelocity = message->twist.twist.linear.x;
  angularVelocity = message->twist.twist.angular.z;
  
  
  Point currentLoc;
  currentLoc.x = currentLocation.x;
  currentLoc.y = currentLocation.y;
  currentLoc.theta = currentLocation.theta;
  logicController.SetPositionData(currentLoc);
  logicController.SetVelocityData(linearVelocity, angularVelocity);
}

// Allows a virtual fence to be defined and enabled or disabled through ROS
void virtualFenceHandler(const std_msgs::Float32MultiArray& message) 
{
  // Read data from the message array
  // The first element is an integer indicating the shape type
  // 0 = Disable the virtual fence
  // 1 = circle
  // 2 = rectangle
  int shape_type = static_cast<int>(message.data[0]); // Shape type
  
  if (shape_type == 0)
  {
    logicController.setVirtualFenceOff();
  }
  else
  {
    // Elements 2 and 3 are the x and y coordinates of the range center
    Point center;
    center.x = message.data[1]; // Range center x
    center.y = message.data[2]; // Range center y
    
    // If the shape type is "circle" then element 4 is the radius, if rectangle then width
    switch ( shape_type )
    {
    case 1: // Circle
    {
      if ( message.data.size() != 4 ) throw ROSAdapterRangeShapeInvalidTypeException("Wrong number of parameters for circle shape type in ROSAdapter.cpp:virtualFenceHandler()");
      float radius = message.data[3]; 
      logicController.setVirtualFenceOn( new RangeCircle(center, radius) );
      break;
    }
    case 2: // Rectangle 
    {
      if ( message.data.size() != 5 ) throw ROSAdapterRangeShapeInvalidTypeException("Wrong number of parameters for rectangle shape type in ROSAdapter.cpp:virtualFenceHandler()");
      float width = message.data[3]; 
      float height = message.data[4]; 
      logicController.setVirtualFenceOn( new RangeRectangle(center, width, height) );
      break;
    }
    default:
    { // Unknown shape type specified
      throw ROSAdapterRangeShapeInvalidTypeException("Unknown Shape type in ROSAdapter.cpp:virtualFenceHandler()");
    }
    }
  }
}

void mapHandler(const nav_msgs::Odometry::ConstPtr& message) {
  //Get (x,y) location directly from pose
  currentLocationMap.x = message->pose.pose.position.x;
  currentLocationMap.y = message->pose.pose.position.y;
  
  //Get theta rotation by converting quaternion orientation to pitch/roll/yaw
  tf::Quaternion q(message->pose.pose.orientation.x, message->pose.pose.orientation.y, message->pose.pose.orientation.z, message->pose.pose.orientation.w);
  tf::Matrix3x3 m(q);
  double roll, pitch, yaw;
  m.getRPY(roll, pitch, yaw);
  currentLocationMap.theta = yaw;
  
  linearVelocity = message->twist.twist.linear.x;
  angularVelocity = message->twist.twist.angular.z;
  
  Point curr_loc;
  curr_loc.x = currentLocationMap.x;
  curr_loc.y = currentLocationMap.y;
  curr_loc.theta = currentLocationMap.theta;
  logicController.SetMapPositionData(curr_loc);
  logicController.SetMapVelocityData(linearVelocity, angularVelocity);
}

void joyCmdHandler(const sensor_msgs::Joy::ConstPtr& message) {
  const int max_motor_cmd = 255;
  if (currentMode == 0 || currentMode == 1) {
    float linear  = abs(message->axes[4]) >= 0.1 ? message->axes[4]*max_motor_cmd : 0.0;
    float angular = abs(message->axes[3]) >= 0.1 ? message->axes[3]*max_motor_cmd : 0.0;

    float left = linear - angular;
    float right = linear + angular;

    if(left > max_motor_cmd) {
      left = max_motor_cmd;
    }
    else if(left < -max_motor_cmd) {
      left = -max_motor_cmd;
    }

    if(right > max_motor_cmd) {
      right = max_motor_cmd;
    }
    else if(right < -max_motor_cmd) {
      right = -max_motor_cmd;
    }

    sendDriveCommand(left, right);
  }
}

void pheromoneTrailHandler(const swarmie_msgs::PheromoneTrail& message) {
    // Framework implemented for pheromone waypoints, but we are only using
    // the first index of the trail momentarily for the pheromone location
    swarmie_msgs::PheromoneTrail trail = message;
    //cout<<"PheromoneStatus: trail.waypoints.size="<<trail.waypoints.size()<<endl;
    //cout <<"trail.waypoints[0]="<<trail.waypoints[0].x<<", "<<trail.waypoints[0].y<<endl;
    // Adjust pheromone location to account for center location
    //trail.waypoints[0].x += centerLocation.x;
    //trail.waypoints[0].y += centerLocation.y;
    //cout <<"after****trail.waypoints[0]="<<trail.waypoints[0].x<<", "<<trail.waypoints[0].y<<endl;
    
    vector<Point> pheromone_trail;
    //cout<<"PheromoneStatus: trail.waypoints.size()="<<trail.waypoints.size()<<endl;
    
    for(int i = 0; i < trail.waypoints.size(); i++) {
      Point waypoint(trail.waypoints[i].x, trail.waypoints[i].y, trail.waypoints[i].theta);
      pheromone_trail.push_back(waypoint);
    }
    //cout<<"PheromoneStatus: pheromone_trail.size()="<<pheromone_trail.size()<<endl;
      
    //cout<<"Pheromone trail handler... "<<endl;
    //logicController.InsertPheromone(centerId, pheromone_trail);
    logicController.InsertPheromone(pheromone_trail);
    return;
}

void publishStatusTimerEventHandler(const ros::TimerEvent&) {
  std_msgs::String msg;
  msg.data = "online";
  status_publisher.publish(msg);
}

void manualWaypointHandler(const swarmie_msgs::Waypoint& message) {
  Point wp;
  wp.x = message.x;
  wp.y = message.y;
  wp.theta = 0.0;
  switch(message.action) {
  case swarmie_msgs::Waypoint::ACTION_ADD:
    logicController.AddManualWaypoint(wp, message.id);
    break;
  case swarmie_msgs::Waypoint::ACTION_REMOVE:
    logicController.RemoveManualWaypoint(message.id);
    break;
  }
}

void sigintEventHandler(int sig) {
  // All the default sigint handler does is call shutdown()
  ros::shutdown();
}

void publishHeartBeatTimerEventHandler(const ros::TimerEvent&) {
  std_msgs::String msg;
  msg.data = "";
  heartbeatPublisher.publish(msg);
}

long int getROSTimeInMilliSecs()
{
  // Get the current time according to ROS (will be zero for simulated clock until the first time message is recieved).
  ros::Time t = ros::Time::now();
  // Convert from seconds and nanoseconds to milliseconds.
  return t.sec*1e3 + t.nsec/1e6;
  
}


Point updateCenterLocation()
{
  transformMapCentertoOdom();
  
  Point tmp;
  tmp.x = centerLocationOdom.x;
  tmp.y = centerLocationOdom.y;
  
  return tmp;
}

void transformMapCentertoOdom()
{
  
  // map frame
  geometry_msgs::PoseStamped mapPose;
  
  // setup msg to represent the center location in map frame
  mapPose.header.stamp = ros::Time::now();
  
  mapPose.header.frame_id = publishedName + "/map";
  mapPose.pose.orientation = tf::createQuaternionMsgFromRollPitchYaw(0, 0, centerLocationMap.theta);
  mapPose.pose.position.x = centerLocationMap.x;
  mapPose.pose.position.y = centerLocationMap.y;
  geometry_msgs::PoseStamped odomPose;
  string x = "";
  
  try
  { //attempt to get the transform of the center point in map frame to odom frame.
    tfListener->waitForTransform(publishedName + "/map", publishedName + "/odom", ros::Time::now(), ros::Duration(1.0));
    tfListener->transformPose(publishedName + "/odom", mapPose, odomPose);
  }
  
  catch(tf::TransformException& ex) {
    ROS_INFO("Received an exception trying to transform a point from \"map\" to \"odom\": %s", ex.what());
    x = "Exception thrown " + (string)ex.what();
    std_msgs::String msg;
    stringstream ss;
    ss << "Exception in mapAverage() " + (string)ex.what();
    msg.data = ss.str();
    infoLogPublisher.publish(msg);
    cout << msg.data << endl;
  }
  // Use the position and orientation provided by the ros transform.
  centerLocationMapRef.x = odomPose.pose.position.x; //set centerLocation in odom frame
  centerLocationMapRef.y = odomPose.pose.position.y;
  
 // cout << "x ref : "<< centerLocationMapRef.x << " y ref : " << centerLocationMapRef.y << endl;
  
  float xdiff = centerLocationMapRef.x - centerLocationOdom.x;
  float ydiff = centerLocationMapRef.y - centerLocationOdom.y;
  
  float diff = hypot(xdiff, ydiff);
  if (diff > drift_tolerance)
  {
    centerLocationOdom.x += xdiff/diff;
    centerLocationOdom.y += ydiff/diff;
  }
  
  //cout << "center x diff : " << centerLocationMapRef.x - centerLocationOdom.x << " center y diff : " << centerLocationMapRef.y - centerLocationOdom.y << endl;
  //cout << hypot(centerLocationMapRef.x - centerLocationOdom.x, centerLocationMapRef.y - centerLocationOdom.y) << endl;
          
}

void humanTime() {
  
  float timeDiff = (getROSTimeInMilliSecs()-startTime)/1e3;
  if (timeDiff >= 60) {
    minutesTime++;
    startTime += 60  * 1e3;
    if (minutesTime >= 60) {
      hoursTime++;
      minutesTime -= 60;
    }
  }
  timeDiff = floor(timeDiff*10)/10;
  
  double intP, frac;
  frac = modf(timeDiff, &intP);
  timeDiff -= frac;
  frac = round(frac*10);
  if (frac > 9) {
    frac = 0;
  }
  
  //cout << "System has been Running for :: " << hoursTime << " : hours " << minutesTime << " : minutes " << timeDiff << "." << frac << " : seconds" << endl; //you can remove or comment this out it just gives indication something is happening to the log file
}

float distanceToCenter() {
    // Returns the distance of the rover to the nest
    return hypot(centerLocation.x - currentLocation.x, centerLocation.y - currentLocation.y);
}
