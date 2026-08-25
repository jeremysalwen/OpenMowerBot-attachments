Slic3r::Polylines extract_single_area_lines(Slic3r::Polygon pa, Slic3r::Polylines *fill_lines)
{
    
    //ROS_INFO_STREAM("Splitting lines on priority boundary intersection");
    Slic3r::Polylines output_lines, priority_output_lines;
    for (Slic3r::Polylines::iterator fl = fill_lines->begin(); fl != fill_lines->end(); fl++) {
        Slic3r::Polyline l;
        if(pa.contains(fl->first_point()) && pa.contains(fl->last_point())) {
            //ROS_INFO_STREAM("Priority fill line added");
            priority_output_lines.push_back(*fl);
        } else { //not contained
            Slic3r::Point intersect_pt;
            if(pa.intersection(*fl, &intersect_pt)) //at least 1 intersection
            {
                if(pa.contains(fl->first_point()))
                {
                    l.points.clear();
                    l.points.push_back(fl->first_point());
                    l.points.push_back(intersect_pt);
                    priority_output_lines.push_back(l);
                    l.points.clear();
                    l.points.push_back(intersect_pt);
                    l.points.push_back(fl->last_point());
                    output_lines.push_back(l);
                    //ROS_INFO_STREAM("Modifying line first point");
                } 
                else 
                {
                    if(pa.contains(fl->last_point())) 
                    {
                        l.points.clear();
                        l.points.push_back(intersect_pt); 
                        l.points.push_back(fl->last_point());
                        priority_output_lines.push_back(l);
                        l.points.clear();
                        l.points.push_back(fl->first_point()); 
                        l.points.push_back(intersect_pt);
                        output_lines.push_back(l);
                        //ROS_INFO_STREAM("Modifying line last point");
                    } 
                    else //must be multiple intersections
                    {
                        //ROS_INFO_STREAM("Adding line");
                        std::vector<Slic3r::Point> pts;
                        for(auto &pl: pa.lines())
                            if(pl.intersection(*fl, &intersect_pt))
                                pts.push_back(intersect_pt);
                        
                        if(pts.size() == 2) {//two points is pass through so process, one point is tangent so leave
                            int index_first = 1, index_last = 0;
                            if(fl->first_point().distance_to(pts[index_first]) > fl->first_point().distance_to(pts[index_last])) {
                                index_first = 0;
                                index_last = 1;
                            }
                            l.points.clear();
                            l.points.push_back(pts[index_first]);
                            l.points.push_back(pts[index_last]);
                            priority_output_lines.push_back(l);
                            l.points.clear();
                            l.points.push_back(fl->first_point()); 
                            l.points.push_back(pts[index_first]);
                            output_lines.push_back(l);
                            l.points.clear();
                            l.points.push_back(pts[index_last]); 
                            l.points.push_back(fl->last_point());
                            output_lines.push_back(l);
                        }
                    }
                }
            } 
            else { //not fully contained no intersection and so low priority line
                output_lines.push_back(*fl);
            }
        }
    }

    *fill_lines = output_lines;
    //ROS_INFO_STREAM("Finished processong priority areas, lines:" << output_lines.size() << ", priority_lines:" << priority_output_lines.size());
    return priority_output_lines;
}
