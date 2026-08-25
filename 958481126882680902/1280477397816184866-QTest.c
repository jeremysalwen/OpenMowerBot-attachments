{
    double y, p, r;
    ROS_WARN_STREAM("Set Quaternion(y, p, r): (0.0, 0.0, 0.1234)");    
    tf2::Quaternion q(0.0, 0.0, 0.1234);
    tf2::Matrix3x3 m(q);
    m.getRPY(r, p, y);
    ROS_WARN_STREAM("getRPY(r, p, y):        " << r << ", " << p << ", " << y);
    m.getEulerYPR(y, p, r);
    ROS_WARN_STREAM("getEulerYPR(y, p, r):   " << y << ", " << p << ", " << r);
    }
    {
    double y, p, r;
    ROS_WARN_STREAM(" ");
    ROS_WARN_STREAM("Set Quaternion(y, p, r): (0.1234, 0.0, 0.0)");    
    tf2::Quaternion q(0.1234, 0.0, 0.0);
    tf2::Matrix3x3 m(q);
    m.getRPY(r, p, y);
    ROS_WARN_STREAM("getRPY(r, p, y):        " << r << ", " << p << ", " << y);
    m.getEulerYPR(y, p, r);
    ROS_WARN_STREAM("getEulerYPR(y, p, r):   " << y << ", " << p << ", " << r);
    }
    {
    double y, p, r;
    ROS_WARN_STREAM(" ");
    ROS_WARN_STREAM("SetRPY(r, p, y):         (0.0, 0.0, 0.1234)");    
    tf2::Quaternion q;
    q.setRPY(0.0, 0.0, 0.1234);
    tf2::Matrix3x3 m(q);
    m.getRPY(r, p, y);
    ROS_WARN_STREAM("getRPY(r, p, y):        " << r << ", " << p << ", " << y);
    m.getEulerYPR(y, p, r);
    ROS_WARN_STREAM("getEulerYPR(y, p, r):   " << y << ", " << p << ", " << r);
    }
    {
    double y, p, r;
    ROS_WARN_STREAM(" ");
    ROS_WARN_STREAM("setEuler(y, p, r):       (0.0, 0.0, 0.1234)");    
    tf2::Quaternion q;
    q.setEuler(0.0, 0.0, 0.1234);
    tf2::Matrix3x3 m(q);
    m.getRPY(r, p, y);
    ROS_WARN_STREAM("getRPY(r, p, y):        " << r << ", " << p << ", " << y);
    m.getEulerYPR(y, p, r);
    ROS_WARN_STREAM("getEulerYPR(y, p, r):   " << y << ", " << p << ", " << r);
    }
    {
    double y, p, r;
    ROS_WARN_STREAM(" ");
    ROS_WARN_STREAM("setEuler(y, p, r):       (0.1234, 0.0, 0.0)");    
    tf2::Quaternion q;
    q.setEuler(0.1234, 0.0, 0.0);
    tf2::Matrix3x3 m(q);
    m.getRPY(r, p, y);
    ROS_WARN_STREAM("getRPY(r, p, y):        " << r << ", " << p << ", " << y);
    m.getEulerYPR(y, p, r);
    ROS_WARN_STREAM("getEulerYPR(y, p, r):   " << y << ", " << p << ", " << r);
    }
