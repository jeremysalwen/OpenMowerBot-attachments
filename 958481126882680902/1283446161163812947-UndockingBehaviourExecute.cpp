Behavior *UndockingBehavior::execute() {

    static bool seedRqd = true;

    // get robot's current pose from odometry.
    xbot_msgs::AbsolutePose pose = getPose();
    tf2::Quaternion quat;
    tf2::fromMsg(pose.pose.pose.orientation, quat);
    tf2::Matrix3x3 m(quat);
    double roll, pitch, yaw;
    m.getRPY(roll, pitch, yaw);

    mbf_msgs::ExePathGoal exePathGoal;

    nav_msgs::Path path;

    geometry_msgs::PoseStamped docking_pose_stamped_front;
    docking_pose_stamped_front.pose = pose.pose.pose;
    docking_pose_stamped_front.header = pose.header;      

    int undock_point_count = 10;
    double incremental_distance = config.undock_distance/undock_point_count;
    for (int i = 0; i < undock_point_count; i++) {      
        docking_pose_stamped_front.pose.position.x -= cos(yaw) * incremental_distance;
        docking_pose_stamped_front.pose.position.y -= sin(yaw) * incremental_distance;
        path.poses.push_back(docking_pose_stamped_front);
    }

    double angle;
    if(config.undock_fixed_angle) {
        angle = config.undock_angle * (M_PI+M_PI)/360.0;
        ROS_INFO_STREAM("Fixed angle undock: " << config.undock_angle);
    }
    else {
        //seed based on first undock time rather than boot so should be ok even without RTC
        if(seedRqd) {
            srand(ros::Time::now().toSec()); 
            ROS_INFO_STREAM("Random angle undock: Seeded rand()");
            seedRqd = false;
        }
        double ranNum = (((((double)rand())/RAND_MAX) - 0.5) * 2.0);
        double ranAngle = abs(config.undock_angle) * ranNum;
        ROS_INFO_STREAM("Random angle undock: " << ranAngle);
        angle = ranAngle * (M_PI+M_PI)/360.0;
    }

    undock_point_count = 10;
    incremental_distance = config.undock_angled_distance/undock_point_count;
    for (int i = 0; i < undock_point_count; i++) {
        double orientation;
        if(config.undock_use_curve)
            orientation = yaw + ((i+1)*angle/undock_point_count);
        else
            orientation = yaw + angle;
        docking_pose_stamped_front.pose.position.x -= cos(orientation) * incremental_distance;
        docking_pose_stamped_front.pose.position.y -= sin(orientation) * incremental_distance;

        tf2::Quaternion q;
        q.setRPY(0.0, 0.0, orientation);
        docking_pose_stamped_front.pose.orientation = tf2::toMsg(q);
        path.poses.push_back(docking_pose_stamped_front);
    }
    
    exePathGoal.path = path;
    exePathGoal.angle_tolerance = 1.0 * (M_PI / 180.0);
    exePathGoal.dist_tolerance = 0.1;
    exePathGoal.tolerance_from_action = true;
    exePathGoal.controller = "DockingFTCPlanner";

    auto result = mbfClientExePath->sendGoalAndWait(exePathGoal);

    bool success = result.state_ == actionlib::SimpleClientGoalState::SUCCEEDED;

    // stop the bot for now
    stopMoving();

    if (!success) {
        ROS_ERROR_STREAM("Error during undock");
        return &IdleBehavior::INSTANCE;
    }

    ROS_INFO_STREAM("Undock success. Waiting for GPS.");
    bool hasGps = waitForGPS();

    if (!hasGps) {
        ROS_ERROR_STREAM("Could not get GPS.");
        return &IdleBehavior::INSTANCE;
    }

    // TODO return mow area
    return nextBehavior;

}
