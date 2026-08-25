import arcpy


def processData(bufferWidth):
    
    # Buffer distance in mm, likely the sideways distance from the measured outline to the GPS antenna on the mower, i.e. half the width of the mower 
    bufferString =  str(bufferWidth) + " Millimeters"
    sqlQuery = "Area_Group = 'kfh' "

    # Create temporary working feature classes in the GDB
    arcpy.AddMessage("Cloning Selected Areas...")
    arcpy.analysis.Select("Docking Poses", "docking_poses")
    arcpy.analysis.Select("Mowing Areas", "mowing_areas", sqlQuery)
    arcpy.analysis.Select("Excluded Areas", "excluded_areas")
    arcpy.analysis.Select("Navigation Areas", "navigation_areas")
    arcpy.analysis.Select("Dividing Lines", "dividing_lines", sqlQuery)


    # Process docking point (hopefully only one has been selected...)
    arcpy.AddMessage("Process Docking Points...")
    fields = ['OBJECTID', 'Name']
    with arcpy.da.UpdateCursor('docking_poses', fields) as cursor:
        for row in cursor:
            row[1] = 'docking_point'
            cursor.updateRow(row)

    
    # Process navigation areas
    arcpy.AddMessage("Process Navigation Areas...")
    fields = ['OBJECTID', 'Name']
    with arcpy.da.UpdateCursor('navigation_areas', fields) as cursor:
        count = 0
        for row in cursor:
            row[1] = 'navigation_area_' + str(count) + '_area'
            cursor.updateRow(row)
            count+=1


    # Buffer the mowing areas table to a new table
    arcpy.AddMessage("Buffering Mowing Areas...")
    with arcpy.EnvManager(outputCoordinateSystem=None, ZDomain=None, outputZFlag="Enabled", outputZValue=0):
        arcpy.analysis.Buffer("mowing_areas", "mowing_buffered", "-" + bufferString)

    # Split Mowing Areas along the dividing lines
    arcpy.AddMessage("Splitting Mowing Areas...")
    arcpy.management.FeatureToPolygon("'dividing_lines';'mowing_buffered'", "split_mowing_areas", None, "ATTRIBUTES", None)

    # Buffer and dissolve the excluded areas table to a new table
    with arcpy.EnvManager(outputCoordinateSystem=None, ZDomain=None, outputZFlag="Enabled", outputZValue=0):
        arcpy.analysis.Buffer('excluded_areas', 'buffered_excluded_areas', bufferString)#, '', '', 'ALL') # The dissolving doesn't work at the moment because you lose the name field and all polygons get merged into one

    # Label mowing areas
    fields = ['OBJECTID', 'Name']
    with arcpy.da.UpdateCursor('split_mowing_areas', fields) as cursor:
        count = 0
        for row in cursor:
            row[1] = 'mowing_area_' + str(count) + '_area'
            cursor.updateRow(row)
            count+=1


    # Smooth Mowing Polygons a bit because when the buffer shrinks the outer edged you can get sharp corners. - turned off for now because mower is slow around vertices
    #arcpy.AddMessage("Smooth mowing area polygons...")
    #with arcpy.EnvManager(transferGDBAttributeProperties="NOT_TRANSFER_GDB_ATTRIBUTE_PROPERTIES"):
    #    arcpy.cartography.SmoothPolygon("split_mowing_areas", "smooth_mowing_areas", "PAEK", "0.5 Meters", "FIXED_ENDPOINT", "NO_CHECK", None)

    # Convert Mowing Areas to lines and flip
    arcpy.AddMessage("Convert mowing area polygons to lines and flip...")
    arcpy.management.PolygonToLine("split_mowing_areas", "mowing_area_lines", "IGNORE_NEIGHBORS")  
    arcpy.edit.FlipLine("mowing_area_lines")


    # Set Up layers for searching
    arcpy.AddMessage("Preparing Mowing Area Layer for Obstacle Clipping...")
    arcpy.management.MakeFeatureLayer('split_mowing_areas','mowing_areas_layer')

    # Create empty feature classes with same table structure for later use
    #arcpy.analysis.Select("excluded_areas", "buffered_excluded_areas")
    arcpy.analysis.Select("excluded_areas", "final_excluded_areas")
    arcpy.management.DeleteRows('final_excluded_areas')


    # Iterate through different mowing areas
    with arcpy.da.SearchCursor('mowing_areas_layer', fields) as cursor:
        count = 0
        for row in cursor:
            select = 'OBJECTID = {}'.format(row[0])
            arcpy.AddMessage("Finding Obstacles in Mowing Area " + str(count) + "...")
            # Select particular mowing area
            arcpy.SelectLayerByAttribute_management('mowing_areas_layer','NEW_SELECTION',select)
            arcpy.analysis.Clip('buffered_excluded_areas', 'mowing_areas_layer', 'clipped_excluded_areas')

            # Label obstacles
            with arcpy.da.UpdateCursor('clipped_excluded_areas', fields) as cursor:
                obstacleCount = 0
                for obstacle in cursor:
                    obstacle[1] = 'mowing_area_' + str(count) + '_obstacle_' + str(obstacleCount)
                    cursor.updateRow(obstacle)
                    obstacleCount+=1
                arcpy.AddMessage("Obstacles Found: " + str(obstacleCount))
            
            # Append the buffered features to the table
            arcpy.management.Append('clipped_excluded_areas', 'final_excluded_areas', "NO_TEST", None, '', '')
    
            count+=1

    
    arcpy.management.PolygonToLine('final_excluded_areas', 'excluded_area_lines', 'IGNORE_NEIGHBORS') 
    
    
    # Buffer the navigation areas
    with arcpy.EnvManager(outputCoordinateSystem=None, ZDomain=None, outputZFlag="Enabled", outputZValue=0):
        arcpy.analysis.Buffer('navigation_areas', 'buffered_navigation_areas', "-" + bufferString)
    #arcpy.management.Append('buffered_polygons', "final_polygons", "NO_TEST", None, '', '')
    
    # Convert Navigation Areas to lines and flip because they should be CCW
    arcpy.AddMessage("Convert navigation area polygons to lines and flip...")
    arcpy.management.PolygonToLine('buffered_navigation_areas', 'navigation_area_lines', "IGNORE_NEIGHBORS")  
    arcpy.edit.FlipLine('navigation_area_lines')

  

    # Merge all lines
    arcpy.AddMessage("Merging All Lines...")
    arcpy.management.Merge("'mowing_area_lines';'excluded_area_lines';'navigation_area_lines';'docking_poses'", "lines_merged", '', "NO_SOURCE_INFO")

    # Now export to GPX file using my custom modified Features to GPX export tools
    arcpy.ImportToolbox("Mower Maps.atbx")
    arcpy.AddMessage("Exporting Lines to GPX file...")
    arcpy.FeaturesToGPX_MowerMapsToolbox("lines_merged", "new_map.gpx", "Name", None, None, None)

    # Now Convert GPX File to BAG file
    arcpy.AddMessage("Converting GPX tracks to BAG file...")
    arcpy.GPXtoBAG_MowerMapsToolbox("new_map.gpx", "map.bag")


    # Now copy the file to the relevant mower
    arcpy.AddMessage("Copying map.bag file to mower...")
    arcpy.SCPPutFile_MowerMapsToolbox("map.bag", host, "openmower", "openmower", "/root/ros_home/.ros")


    arcpy.AddMessage("All Done! Finished!")

    return







# This is used to execute code if the file was run but not imported
if __name__ == '__main__':
    # Tool parameter accessed with GetParameter or GetParameterAsText
    bufferWidth = int(arcpy.GetParameterAsText(0))
    host = arcpy.GetParameterAsText(1)  
    #username = arcpy.GetParameterAsText(2)
    #password = arcpy.GetParameterAsText(3)
    
    processData(bufferWidth)
    
    #arcpy.FeaturesToGPX_MowerMapsToolbox("export_lines", "export_map2.gpx", "Name", None, None, None)
    #arcpy.GPXtoBAG_MowerMapsToolbox("export_map2.gpx", "map.bag")
    
    # Update derived parameter values using arcpy.SetParameter() or arcpy.SetParameterAsText()

