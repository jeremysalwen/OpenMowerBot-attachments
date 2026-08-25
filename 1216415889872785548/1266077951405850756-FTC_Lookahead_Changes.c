    //new function in ftc_planner.cpp
    double FTCPlanner::velocityLookahead()
    {
        if (global_plan.size() < 2)
        {
            return 0;
        }

        //work out how many points look ahead we need to decelerate to 0 from current speed
        double decelDist = (current_movement_speed * current_movement_speed) / (2.0 * config.acceleration);
        double total_dist = 0.0;
        std::vector<double> distances;
        std::vector<double> rotations;
        Eigen::Affine3d last_point = current_control_point;
        Eigen::Quaternion<double> last_rot(current_control_point.linear());
        uint32_t i = 0;
        for (i = current_index + 1; i < global_plan.size(); i++)
        {
            Eigen::Affine3d next_point;
            tf2::fromMsg(global_plan[i].pose, next_point);
            double dist = abs((next_point.translation() - last_point.translation()).norm());
            distances.push_back(dist);
            Eigen::Quaternion<double> next_rot(next_point.linear());
            rotations.push_back(abs(next_rot.angularDistance(last_rot)));
            total_dist += dist;
            last_point = next_point;
            last_rot = next_rot;
            if(total_dist >= decelDist)
                break;
        }

        double max_speed = config.speed_fast;
        if(distances.empty()) 
        {
            return 0;
        }
        else{
            //now go back through the points to calculate max permissible speed
            
            for(int32_t i = distances.size()-1;i>=0;i--)
            {
                //calculate max speed to allow time for rotations
                double angle = rotations[i] * (180.0 / M_PI);
                double time_to_rotate = angle / config.speed_angular;
                double speed = config.speed_fast;
                if(time_to_rotate > 0.0)
                    speed = distances[i]/time_to_rotate;

                //calculate max speed with acceleration from previous step (actually decel but going backwards)
                max_speed = sqrt((max_speed * max_speed) + (2 * config.acceleration * distances[i]));
                if(max_speed > config.speed_fast)
                    max_speed = config.speed_fast;
                if(speed < max_speed)
                    max_speed = speed;                 
            }
        }

        return max_speed;
    }
    
    
    //changes to existing module 
    void FTCPlanner::update_control_point(double dt)
    {
        //linear velocity limiting
        vlim_error = config.mow_current_limit - mow_current;
        i_vlim_error += vlim_error * dt;
        if (i_vlim_error > config.ki_vlim_max)
            i_vlim_error = config.ki_vlim_max;
        else if (i_vlim_error < 0)
            i_vlim_error = 0;
        d_vlim = (vlim_error - last_vlim_error) / dt;
        last_vlim_error = vlim_error;
        vlim_speed = vlim_error * config.kp_vlim + i_vlim_error * config.ki_vlim + d_vlim * config.kd_vlim;

        switch (current_state)
        {
        case PRE_ROTATE:
            tf2::fromMsg(global_plan[0].pose, current_control_point);
            break;
        case FOLLOWING:
        {
            double speed = 0.0;
            if(config.speed_slow > 0.0)
            {
                // Normal planner operation
                double straight_dist = distanceLookahead();
                
                if (straight_dist >= config.speed_fast_threshold)
                {
                    speed = config.speed_fast;
                }
                else
                {
                    speed = config.speed_slow;
                }
            }
            else
            {
                speed = velocityLookahead();
            }
