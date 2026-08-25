
#===========================================
# Christoph Sobel 26.06.2023
# script to convert gpx files to openmower ros map.bag
#
# initial version published on https://wiki.openmower.de/index.php?title=Modify_map_using_drone_footage
#
# update the base point lat0 and lon0 coordinates below
#
# no need for the open_mower_ros repo,
# this version imports the _MapArea.py file with custom message
# uses argpase for file name
# run with "python gpx_to_bag_included.py GPX_FILENAME"
#
#===========================================

import gpxpy.gpx
import utm
import rosbag
import math
import os
from geometry_msgs.msg import Polygon, Point32, Pose
from _MapArea import MapArea  # Imports the custom message from its package
# from tf.transformations import quaternion_from_euler
import numpy as np
import argparse

#   !!! THESE ARE THE ONLY THREE LINES I HAVE ADDED along with the outputFC file at the end and gpx_file.close()!!!!
import arcpy
inputFC = arcpy.GetParameterAsText(0)
outputFC = arcpy.GetParameterAsText(1)
#   !!! --- Modification Ends --- !!!!

parser = argparse.ArgumentParser(
                    prog='gpx to bag converter',
                    description='converts gpx files to map.bag files using utm module',
                    epilog='Christoph Sobel')

# Base point
lat0 = 51.-YOUR-BASE-REF
lon0 = -0.-YOUR-BASE-REF
# lat0 = float(os.getenv('OM_DATUM_LAT'))
# lon0 = float(os.getenv('OM_DATUM_LONG'))
print("base_point", lat0, " ", lon0)
x0, y0, zone, ut = utm.from_latlon(lat0, lon0)
print("base_point in utm", x0, " ", y0)

gpx_file = open( inputFC, 'r')
# gpx_file = open('map_bag_ALL.gpx', 'r')
gpx = gpxpy.parse(gpx_file)
gpx_file.close()

mowing_area_dict = {}
navigation_area_dict = {}
base_point = []
docking_points = []

def quaternion_from_euler(roll, pitch, yaw):
  """
  Convert an Euler angle to a quaternion.
   
  Input
    :param roll: The roll (rotation around x-axis) angle in radians.
    :param pitch: The pitch (rotation around y-axis) angle in radians.
    :param yaw: The yaw (rotation around z-axis) angle in radians.
 
  Output
    :return qx, qy, qz, qw: The orientation in quaternion [x,y,z,w] format
  """
  qx = np.sin(roll/2) * np.cos(pitch/2) * np.cos(yaw/2) - np.cos(roll/2) * np.sin(pitch/2) * np.sin(yaw/2)
  qy = np.cos(roll/2) * np.sin(pitch/2) * np.cos(yaw/2) + np.sin(roll/2) * np.cos(pitch/2) * np.sin(yaw/2)
  qz = np.cos(roll/2) * np.cos(pitch/2) * np.sin(yaw/2) - np.sin(roll/2) * np.sin(pitch/2) * np.cos(yaw/2)
  qw = np.cos(roll/2) * np.cos(pitch/2) * np.cos(yaw/2) + np.sin(roll/2) * np.sin(pitch/2) * np.sin(yaw/2)
 
  return [qx, qy, qz, qw]

for track in gpx.tracks:
	print("reading from gpx: ", track.name)
	# get points
	point_list = []
	for segment in track.segments:
		print("Point count:{}".format(len(segment.points)))
		for point in segment.points:
			# print(f'Point at ({point.latitude},{point.longitude}) -> {point.elevation}')
			x, y, zone, ut = utm.from_latlon(point.latitude, point.longitude)
			point_list.append(Point32(x=x-x0, y=y-y0, z=0.0))
	# store in deciated dict
	if "mowing" in track.name:
		mowing_area_dict[track.name] = point_list
	elif "navigation" in track.name:
		navigation_area_dict[track.name] = point_list
	elif "base" in track.name:
		base_point = point_list
	elif "docking" in track.name:
		docking_points  = point_list

docking_pose = Pose()
if len(docking_points) == 2:
	yaw = math.atan2(docking_points[1].y - docking_points[0].y, docking_points[1].x - docking_points[0].x)
	quaternion = quaternion_from_euler(0,0,yaw)
	
	docking_pose.position.x = docking_points[0].x
	docking_pose.position.y = docking_points[0].y
	# Pose Orientation
	docking_pose.orientation.x = quaternion[0]
	docking_pose.orientation.y = quaternion[1]
	docking_pose.orientation.z = quaternion[2]
	docking_pose.orientation.w = quaternion[3]
		

def sort_dict_return_MapAreas(area_dict):
	areas = {}
	for track_name, point_list in sorted(area_dict.items()): # store sorted polygons to MapArea
		map_area = MapArea()
		print("converting:", track_name)
		area_number = track_name.split("_")[2]
		poly = Polygon(points=point_list)
		if "area" in track_name[-4:]:
			map_area.area = poly
			areas[area_number] = map_area
		elif "obstacle" in track_name:
			map_area = areas[area_number]
			map_area.obstacles.append(poly)
			areas[area_number] = map_area
	return areas

mowing_areas_list = sort_dict_return_MapAreas(mowing_area_dict).values()
navigation_areas_list = sort_dict_return_MapAreas(navigation_area_dict).values()

with rosbag.Bag(outputFC, 'w') as outbag:
	print('writing bag file')
	for mowing_area in mowing_areas_list:
		outbag.write("mowing_areas", mowing_area)
	for navigation_area in navigation_areas_list:	
		outbag.write("navigation_areas", navigation_area)
	outbag.write("docking_point", docking_pose)
	

