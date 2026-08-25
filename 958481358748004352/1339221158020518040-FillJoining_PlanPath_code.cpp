    if(req.reorder_fill && req.priority_reorder) {
            ROS_INFO_STREAM("Splitting lines");
            auto priority_lines = extract_lines(req, &fill_lines);
            priority_lines = join_fill_lines(req, inner, priority_lines, true);
            fill_lines = join_fill_lines(req, inner, fill_lines, false);
            append_to(priority_lines, fill_lines);
            fill_lines = priority_lines;
    } else {
        fill_lines = join_fill_lines(req, inner, fill_lines, false);
    }
    ROS_INFO_STREAM("Splitting complete, got " << fill_lines.size() << " lines:" );
