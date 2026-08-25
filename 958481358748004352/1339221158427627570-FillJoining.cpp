Slic3r::Polylines extract_lines(slic3r_coverage_planner::PlanPathRequest req, Slic3r::Polylines *fill_lines)
{
    Slic3r::Polylines output_lines;
    //split any lines that are partially inside priority zones
    Slic3r::Polygons priorities;
    for (auto &priority: req.priority) {
        //convert each priority area message to polygon
        Slic3r::Polygon priority_poly;
        for (auto &pt: priority.points) {
            priority_poly.points.push_back(Point(scale_(pt.x), scale_(pt.y)));
        }
        priorities.push_back(priority_poly);
    }

    Slic3r::Polylines priority_lines;
    //ROS_INFO_STREAM("Processong priority areas, lines:" << fill_lines->size() << ", priority_lines:" << priority_lines.size());
    int num = 0;
    for(auto &pa: priorities) {
        append_to(priority_lines, extract_single_area_lines(pa, fill_lines));
        //ROS_INFO_STREAM("Finished processing priority areas:" << num++ << ", lines:" << fill_lines->size() << ", priority_lines:" << priority_lines.size());
    }

    return priority_lines;
}

struct fill_line_struct{
    Slic3r::Polyline line;
    bool available;
};

struct perim_points_struct{
    int line_index;
    bool first_match;
    Slic3r::Point match_point;
};

#define SEARCH_TOLERANCE (scale_(0.001)) //quantisation on rotations can cause errors to creep in so use higher value than standard (=10x SCALED_EPSILON)
//should really create lines in the same rotated frame to avoid this problem but not worth the effort at this point int time.

Slic3r::Polylines join_fill_lines(slic3r_coverage_planner::PlanPathRequest req, Slic3r::Polygons inner, Slic3r::Polylines fill_lines, bool priority_area)
{
    Slic3r::Polylines reordered_lines;
    Slic3r::Point origin(scale_(0.0),scale_(0.0));
    double rot_angle = -req.angle;

    if(fill_lines.empty()) {
        ROS_INFO_STREAM("No lines to process");
        return reordered_lines;
    }

    // create perimeter to use for line joining
    Polygons perimeters;
    perimeters = offset(inner, -scale_(req.distance)/2.0);
    int path_num = 0;

    Slic3r::Polygons priorities;
    for (auto &priority: req.priority) {
        //convert each priority area message to polygon
        Slic3r::Polygon priority_poly;
        for (auto &pt: priority.points) {
            priority_poly.points.push_back(Point(scale_(pt.x), scale_(pt.y)));
        }
        if(priority_area)
            priority_poly.make_counter_clockwise();
        else
            priority_poly.make_clockwise();
        perimeters.push_back(priority_poly);
    }

    //rotate back to 0deg so that lines traverse in constant Y
    for (auto &perimeter: perimeters) {
        perimeter.rotate(rot_angle, origin);
    }

    //generate vector of lines
    std::vector<fill_line_struct> lines;
    for(auto &ln: fill_lines) {
        ln.rotate(rot_angle, origin);
        fill_line_struct line = {ln, true};
        lines.push_back(line);
    }
        
    //generate vector of multimap of lines for each perimeter
    std::vector<std::multimap<int, perim_points_struct>> perimeters_lines;
    int perim_num = 0;
    for(auto &perim: perimeters) {
        std::multimap<int, perim_points_struct> perimeter_lines;
        //ROS_INFO_STREAM("Processing perimeter:" << perim_num++);
        int perim_line_num = 0; //slightly messy way to keep track of index but allows MUCH faster loops!
        for(auto &perim_line: perim.lines()) {
            int line_num = 0;  
            for(auto &l: lines) {
                    bool first_match = (l.line.first_point().distance_to(l.line.first_point().projection_onto(perim_line))<SEARCH_TOLERANCE);
                    bool last_match = (l.line.last_point().distance_to(l.line.last_point().projection_onto(perim_line))<SEARCH_TOLERANCE);
                    if(first_match || last_match) {
                        Slic3r::Point pt = first_match?l.line.first_point():l.line.last_point();
                        perimeter_lines.insert({perim_line_num, {line_num, first_match, pt}});
                        //ROS_INFO_STREAM("Perim seg:" << perim_line_num << ", line num:" << line_num << ", first:" << first_match << ", last:" << last_match);
                    }
                line_num++;
            }
            perim_line_num++;
        }
        perimeters_lines.push_back(perimeter_lines);
    }
    //ROS_INFO_STREAM("Perimeters map created, " << perimeters_lines.size() << " items" );

    Slic3r::Polyline search_line;
    Slic3r::Point last_point_output;
    int search_line_index = -1;
    for(int v=0;v<lines.size();v++) {
        if(lines[v].available) {
            search_line = lines[v].line;
            last_point_output = lines[v].line.first_point(); //don't want to reverse first line
            search_line_index = v;
            //ROS_INFO_STREAM("First search line set, index:" << v);
            break;
        }
    }
    double min_area_X = search_line.last_point().x, max_area_X= search_line.first_point().x;
    

    ROS_INFO_STREAM("Processing fill lines");
    while(search_line_index>=0) {
        double dist_first = last_point_output.distance_to(search_line.first_point());
        double dist_last = last_point_output.distance_to(search_line.last_point());
        //ROS_INFO_STREAM("Starting new output line:" << search_line_index << 
        //        ", Dist to first:" << unscale(dist_first) << ", Dist to last:" << unscale(dist_last) << "----------------------------------------");
        if(dist_first > dist_last)
            search_line.reverse();
        auto search_point = search_line.last_point();
        
        Slic3r::Polyline output_line;
        output_line.append(search_line);
        lines[search_line_index].available = false;

        double minX = INT_MAX, maxX= INT_MIN;
        while(true) {
            int closest_line_index;
            std::multimap<int, perim_points_struct> perimeter;
            Slic3r::Polygon perimeter_points;
            
            int start_transition_index, end_transition_index;
            //find index of perimeter line that intersects search line
            //ROS_INFO_STREAM("Searching for perimeter on start line, index:" << search_line_index);
            bool perimeter_found = false;
            for(int i=0;(i<perimeters_lines.size()) && !perimeter_found;i++) {
                for(auto &p: perimeters_lines[i]) {
                    if((p.second.match_point.distance_to(search_point) < SEARCH_TOLERANCE) && (p.second.line_index == search_line_index)) {
                        perimeter = perimeters_lines[i];
                        perimeter_points = perimeters[i];
                        start_transition_index = p.first;
                        //ROS_INFO_STREAM("Start index found, perimeter:" << i << ", perimeter index:" << start_transition_index);
                        perimeter_found = true;
                        break;
                    }
                }
            }
            
            if(!perimeter_found) {
                //ROS_INFO_STREAM("No matching perimeter found, moving to next line");
                reordered_lines.push_back(output_line);
                last_point_output = output_line.last_point();
                break; //move to next line
            }

            //scan round perimeter to next fill line
            int search_increment = (search_line.last_point().x > search_line.first_point().x)?1 :-1;
            bool found_line = false;
            int index = start_transition_index;
            bool closest_line_needs_reversing = false;
            for(int i=0;(i<20) && !found_line;i++) {
                auto range = perimeter.equal_range(index);
                for(auto iter=range.first;(iter!=range.second) && !found_line;iter++) {
                    if(lines[iter->second.line_index].available) {
                        if((lines[iter->second.line_index].line.first_point().y >= search_point.y) && 
                                    (lines[iter->second.line_index].line.first_point().y < (search_point.y + scale_(req.distance + 0.005)))) {
                            end_transition_index = index;
                            closest_line_index = iter->second.line_index;
                            if(!iter->second.first_match) //can't actually reverse here as line may be binned if reordering needed
                                closest_line_needs_reversing = true; //and revrsing would mess up lookups so just flag
                            found_line = true;
                            //ROS_INFO_STREAM("Found match on perimeter index:" << end_transition_index << ", line index:" << 
                            //            closest_line_index <<  ", Reversed:" << !iter->second.first_match);
                        }
                    }
                }
                index+=search_increment;
                if(index >= perimeter_points.lines().size())
                    index = 0;
                if(index < 0)
                    index = perimeter_points.lines().size()-1;
            }
            
            Slic3r::Polyline transition_line;
            if(found_line) {
                //ROS_INFO_STREAM("Appending line--------------------------------------------------------");
                bool add_line = true;
                int start=0, end=0;
                if(start_transition_index != end_transition_index) {
                    if(search_increment>0) { //forwards
                        start = start_transition_index + 1;//only want last line point on start line
                        if(start >= perimeter_points.points.size()) start = 0;
                        end = end_transition_index;
                    } else {
                        start = start_transition_index;//only want last line point on start line
                        end = end_transition_index + 1;
                        if(end >= perimeter_points.points.size()) end = 0;
                    }
                    //ROS_INFO_STREAM("Outputing perimeter points from index:" << start << ", to:" << end);
                    while(true) {
                        Slic3r::coord_t pp_s_y = perimeter_points.points[start].y;
                        Slic3r::coord_t sp_s_y_min = (search_point.y - scale_(req.distance - 0.005));
                        Slic3r::coord_t sp_s_y_max = (search_point.y + scale_(req.distance + 0.005));
                        //ROS_INFO_STREAM("pp y:" << unscale(pp_s_y) << ", sp y_min:" << unscale(sp_s_y_min) << ", sp y_max:" << unscale(sp_s_y_max));
                        if((pp_s_y < sp_s_y_min) || (pp_s_y > sp_s_y_max)) {
                            add_line = false;
                            break;
                        }
                        transition_line.append(perimeter_points.points[start]);
                        if(start==end) {
                            break;
                        } else {
                            start = start + search_increment;
                            if(start < 0) start = perimeter_points.points.size() -1;
                            if(start >= perimeter_points.points.size()) start = 0;
                        }
                    }
                }
                if(add_line) {
                    //need to check whether reordering is needed
                    bool split = false;
                    if(req.reorder_fill) {
                        for (int sli=0; sli<lines.size();sli++) {
                            if(lines[sli].available && (sli < closest_line_index) &&
                                    (lines[closest_line_index].line.first_point().y > (lines[sli].line.first_point().y + SEARCH_TOLERANCE)) && 
                                    (((minX <= lines[sli].line.first_point().x) && (maxX >= lines[sli].line.first_point().x)) || 
                                    ((minX <= lines[sli].line.last_point().x) && (maxX >= lines[sli].line.last_point().x))))
                                split = true;
                        }
                    }
                    if(split) {
                        //ROS_INFO_STREAM("Output line due to reorder********************************************");
                        reordered_lines.push_back(output_line);
                        last_point_output = output_line.last_point();
                        break;
                    } else {
                        //ROS_INFO_STREAM("Merging line");
                        Slic3r::Polyline line_to_output = lines[closest_line_index].line;
                        if(closest_line_needs_reversing)
                            line_to_output.reverse();
                        Slic3r::coord_t first_pt_x = line_to_output.first_point().x;
                        Slic3r::coord_t last_pt_x = line_to_output.last_point().x;
                        minX = first_pt_x < last_pt_x?first_pt_x:last_pt_x;
                        maxX = first_pt_x >= last_pt_x?first_pt_x:last_pt_x;
                        if(minX < min_area_X) min_area_X = minX;
                        if(maxX > max_area_X) max_area_X = maxX;
                        output_line.append(transition_line);
                        output_line.append(line_to_output);
                        search_line = line_to_output;
                        search_line_index = closest_line_index;
                        search_point = search_line.last_point();
                        lines[closest_line_index].available = false;
                    }
                } else {
                    //ROS_INFO_STREAM("Merged cancelled due to perimeter excursion, start:" << start << ", end:" << end << ", size:" << perimeter_points.points.size());
                    reordered_lines.push_back(output_line);
                    last_point_output = output_line.last_point();
                    //ROS_INFO_STREAM("Output line***********************************************************");
                    break;
                }
            }
            else {
                reordered_lines.push_back(output_line);
                last_point_output = output_line.last_point();
                //ROS_INFO_STREAM("Output line due to no match found*************************************");
                break;
            }
        }
        //do we have another line to process
        search_line_index = -1;
        //first look for lines that fall into shadow of already mowed
        for(int v=0;v<lines.size();v++) {
            if(lines[v].available) {
                auto eq_points = lines[v].line.equally_spaced_points(scale_(0.5));
                for(auto &pt: eq_points) {
                    if(((pt.x > min_area_X) && (pt.x < max_area_X))) {
                        search_line = lines[v].line;
                        search_line_index = v;
                        goto POINT_IN_SHADOW;
                    }
                }
            }
        }
        POINT_IN_SHADOW:
        //if not found look through all lines
        if(search_line_index<0) {
            for(int v=0;v<lines.size();v++) {
                if(lines[v].available) {
                    search_line = lines[v].line;
                    search_line_index = v;
                    break;
                }
            }
        }
    }

    //rotate back to correct angle
    for(auto &ln: reordered_lines) {
        ln.rotate(-rot_angle, origin);
    }

    ROS_INFO_STREAM("Join complete");
    //return fill_lines;
    return reordered_lines;
}
