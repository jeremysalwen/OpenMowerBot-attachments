################################
##     Important Settings     ##
################################

# Most of the time you must modify those settings, so they are moved to the beginning of document from their respective sections
# Refer to documentation for more info: https://openmower.de/docs/robot-assembly/prepare-the-parts/prepare-sd-card/#step-2-configure-open-mower

# Your Hardware Version (more a firmware version, really). Check the OpenMower docs (https://openmower.de/docs/versions/) for the firmware versions.
# Supported values as of today:
# 0_13_X: Use this if you have 0.13.x mainboard with LSM6DSOTR (default).
# 0_12_X_LSM6DSO: Use this if you have an LSM6DSOTR and have a 0.12.x mainboard.
# 0_11_X_WT901: Use this if you have an WT901 and have a 0.11.x mainboard.
# 0_10_X_WT901: Use this if you have an WT901 and have a 0.10.x mainboard.
# 0_10_X_MPU9250: Use this if you have an MPU9250 and have a 0.10.x mainboard (be aware that there are many fake chips on the market. So probably not your hardware version).
# 0_9_X_WT901_INSTEAD_OF_SOUND: Use this if you have soldered the WT901 in the sound module's slot and have a 0.9.x mainboard.
# 0_9_X_MPU9250: Use this if you have an MPU9250 and have a 0.9.x mainboard (be aware that there are many fake chips on the market. So probably not your hardware version).
export OM_HARDWARE_VERSION="0_13_X"

# Uncomment and set to coordinates near your future docking station, this will be your map origin.
# There might be a case that you don't need those if you using OM_USE_RELATIVE_POSITION=True
export OM_DATUM_LAT=change.me
export OM_DATUM_LONG=change.me


# NTRIP Settings
# Set to False if using external radio plugged into the Ardusimple board.
export OM_USE_NTRIP=False
export OM_NTRIP_HOSTNAME=192.168.178.55
export OM_NTRIP_PORT=2101
export OM_NTRIP_USER=gps
export OM_NTRIP_PASSWORD=gps
export OM_NTRIP_ENDPOINT=BASE1

################################
## Hardware Specific Settings ##
################################

# The type of mower you're using, used to get some hardware parameters automatically
# Currently supported:
# YardForce500
# YardForceSA650
# VikingMI632
# CUSTOM (put your configs in ~/mower_params/)
export OM_MOWER="VikingMI632"

# Select your ESC type
# Supported values as of today:
# xesc_mini: for the STM32 version (VESC)
# xesc_2040: for the RP2040 version (very experimental!)
export OM_MOWER_ESC_TYPE="xesc_mini"

# Select your default cutter height (Viking MI-632 only)
# Supported values 20-60mm
export OM_MOWER_DEFAULT_MOW_HEIGHT="60"

# Select your gamepad
# Currently supported: ps3, steam_stick, steam_touch, xbox360
export OM_MOWER_GAMEPAD="xbox360"

# Set to true to record your session.
# Output will be stored in your $HOME
export OM_ENABLE_RECORDING_ALL=False

################################
##        GPS Settings        ##
################################
# Relative Positioning vs LatLng coordinates
# If OM_USE_RELATIVE_POSITION=False, we're using an arbitrary point as map origin. This point is called the DATUM point and
# needs to be set using OM_DATUM_LAT and OM_DATUM_LONG below.
# If OM_USE_RELATIVE_POSITION=True, we're using the ublox NAVRELPOSNED messages as position.
# This makes your base station the map origin
# For it recommended to set OM_USE_RELATIVE_POSITION to False. This way you can move your base station without re-recording your maps and it's also more compatible overall.
export OM_USE_RELATIVE_POSITION=False

# GPS protocol. Use UBX for u-blox chipsets and NMEA for everything else
export OM_GPS_PROTOCOL=UBX

# If you use a different gps board you maybe want to set a different baudrate.
export OM_GPS_BAUDRATE="115200"

# If you want to use F9R's sensor fusion, set this to true (you will also need to set DATUM_LAT and DATUM_LONG).
# Consider this option unstable, since I don't have the F9R anymore, so I'm not able to test this.
# IF YOU DONT KNOW WHAT THIS IS, SET IT TO FALSE
export OM_USE_F9R_SENSOR_FUSION=False


################################
##    Mower Logic Settings    ##
################################
# The distance from the dock to the start of the approach path
export OM_DOCKING_APPROACH_DISTANCE=0.5

# The distance to drive forward AFTER reaching the second docking point
export OM_DOCKING_DISTANCE=0.5

# The first stage distance to drive for undocking. As a minimum it needs to clear the dock.  
# If the additional angled move isn't used then this needs to be large enough for the robot to have GPS reception 
export OM_UNDOCK_DISTANCE=0.5

# The additional second stage distance to drive at an angle for undocking. This needs to be large enough for the robot to have GPS reception
# Note - this section may still be driven without gps so don't expect high positional accuracy.
export OM_UNDOCK_ANGLED_DISTANCE=0.5

# The angle at which to drive for the additional distance (neg values are to the left of the dock, pos to the right).
export OM_UNDOCK_ANGLE=0.0

# If true will allways use the angle specified.
# If false will vary the undocking angle between +abs(OM_UNDOCK_ANGLE) to -abs(OM_UNDOCK_ANGLE) on subsequent undocks to reduce grass wear
export OM_UNDOCK_FIXED_ANGLE=False

# If true will use a curved second stage undock move, if false will use a straight second stage undock move.
export OM_UNDOCK_USE_CURVE=True

################################
##    Mower Map Settings      ##
################################
# How many outlines should the mover drive. It's not recommended to set this below 4.
export OM_OUTLINE_COUNT=4

# Offset distance for outermost perimeter
export OM_OUTLINE_OFFSET=0.125

# How many outlines should the fill (lanes) overlap
export OM_OUTLINE_OVERLAP_COUNT=1

# Mowing angle offset -180 deg - +180 deg, 0 = east, -90 = north. If mowing angle offset is not absolute it gets added to the auto detected angle which is set by the first 2 m of recorded outline.
export OM_MOWING_ANGLE_OFFSET=0
export OM_MOWING_ANGLE_OFFSET_IS_ABSOLUTE=True
# The increment value will automatically add specified number of degrees to the mowing angle everytime the whole map is finished
export OM_MOWING_ANGLE_INCREMENT=0
# True to use standard Slic3r line joining, false to use custom line joining
export OM_USE_SLIC3R_JOINING=False
# True to reorder fill sequence to scan across lawn filling in unmown areas as it goes, False to mow to end and then go back and fill missed bits
# (needs OM_USE_SLIC3R_JOINING to be enabled)
export OM_REORDER_FILL=True
# True to reorder fill sequence to mow priority areas first (needs reorder_fill to be enabled)
export OM_PRIORITY_REORDER=True
# True to add a low cost (0.2) to priority areas to prevent traversing if can be avoided
export OM_ADD_PRIORITY_COST=True

# The width of mowing paths.
# Choose it smaller than your actual mowing tool in order to have some overlap.
# 0.13 works well for the Classic 500.
export OM_TOOL_WIDTH=0.25

# Voltages for battery to be considered full or empty
export OM_BATTERY_EMPTY_VOLTAGE=26.4
export OM_BATTERY_FULL_VOLTAGE=32.8
export OM_BATTERY_CRITICAL_VOLTAGE=25.6

# Mower motor temperatures to stop and start mowing
export OM_MOWING_MOTOR_TEMP_HIGH=70.0
export OM_MOWING_MOTOR_TEMP_LOW=45.0

# GPS timeouts
export OM_GPS_WAIT_TIME_SEC=10.0
export OM_GPS_TIMEOUT_SEC=5.0

# Set how to handle restarts on the perimeter
#  False - will restart whole outer perimeter
#  True - will restart at actual point but insert a fake obstacle to try and enforce approach path
export OM_ADD_FAKE_OBSTACLE=True

# Set how many times to trim path if can't find a path
export OM_MAX_FIRST_POINT_TRIM_ATTEMPTS=8

# Set whether to use a linear transition or a cosine transition
#  False - default - use a cosine transition, better for short transition paths
#  True - use a linear transition, better for very long transition paths
export OM_USE_LINEAR_TRANSITION=False

# Set the length of the transition path in meters
#  If not set a value of 3 x OM_WHEEL_DISTANCE_M is used which should give good results
# export OM_TRANSITION_DISTANCE_M=1.0

# Set costmap path persist mode, planner will try to avoid these paths when next routing to minimise grass wear tracks
#  0 - NONE - disable persistence
#  1 - PERSIST_AREAS - Persist paths only within persist areas
#  2 - NAV_AREAS - Persist paths only within navigation areas
#  3 - MAP_AREAS - Persist paths over all map areas
export OM_PERSIST_MODE=1

# Set the maximum number of paths to be saved and used for costmap persistence (saved across mow sessions/reboots).
export OM_PERSIST_NUM_PATHS=5

################################
##  Mower Behavior Settings   ##
################################
# True to enable mowing motor
export OM_ENABLE_MOWER=true

# Set the automatic start value based on required behaviour
#  2 - AUTO - mow whenever possible
#  1 - SEMIAUTO - mow the entire map once then wait for manual start atgain
#  0 - MANUAL - mowing requires manual start (default if unset)
export OM_AUTOMATIC_MODE=0

################################
##    External MQTT Broker    ##
################################
# Set thes in order to publish status data to your external MQTT broker.
# This is for use with smart home.

# export OM_MQTT_ENABLE=False
# export OM_MQTT_HOSTNAME="your_mqtt_broker"
# export OM_MQTT_PORT="1883"
# export OM_MQTT_USER=""
# export OM_MQTT_PASSWORD=""
# export OM_MQTT_TOPIC_PREFIX="openmower"


# source the default values for the hardware platform.
# you only need this line on non-docker installs. in the docker, it will be done automatically.
source $(rospack find open_mower)/params/hardware_specific/$OM_MOWER/default_environment.sh
