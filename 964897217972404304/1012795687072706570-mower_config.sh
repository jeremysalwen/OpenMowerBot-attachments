################################
## Hardware Specific Settings ##
################################

# The type of mower you're using, used to get some hardware parameters automatically
# Currently supported:
# YardForce500
# CUSTOM (put your configs in ~/mower_params/)
export OM_MOWER="YardForce500"

# Select your ESC type, xesc_mini for the STM32 version or xesc_2040 for the RP2040 version
export OM_MOWER_ESC_TYPE="xesc_2040"

# Set to true to record your session
export OM_ENABLE_RECORDING=true

# Offset between the point in between your wheels and the GPS antenna in m
export OM_GPS_ANTENNA_OFFSET=0.3
# Angle which gets added to the compass heading, if your imu is in a rotated coordinate frame
export OM_IMU_OFFSET=-90.0
# IMU Calibration and filter settings
export OM_MAG_BIAS_X=0.0240522
export OM_MAG_BIAS_Y=0.0110753
export OM_MAG_BIAS_Z=-0.00407707
export OM_IMU_FILTER_GAIN=0.05

# Serial ports of your hardware devices
export OM_LL_SERIAL_PORT=ttyAMA0
export OM_LEFT_SERIAL_PORT=ttyAMA4
export OM_RIGHT_SERIAL_PORT=ttyAMA2
export OM_MOW_SERIAL_PORT=ttyAMA3
export OM_GPS_SERIAL_PORT=ttyAMA1



################################
##        GPS Settings        ##
################################

# Relative Positioning vs LatLng coordinates
# If OM_USE_RELATIVE_POSITION=True, we're using the ublox NAVRELPOSNED messages as position.
# This makes your base station the map origin
# If OM_USE_RELATIVE_POSITION=False, we're using an arbitrary point as map origin. This point is called the DATUM point and
# needs to be set using OM_DATUM_LAT and OM_DATUM_LONG below.
# If you DON'T have your own base station (e.g. you're using an NTRIP service provider) you should use LAT_LONG positioning, else relative positioning
export OM_USE_RELATIVE_POSITION=False
#export OM_USE_LAT_LONG_POSITION=True

# If needed, uncomment and set to coordinates near you (these default coordinates are somewhere in Germany).
# This will be your map origin!
export OM_DATUM_LAT=51.045390857447
export OM_DATUM_LONG=4.874983261337186
export OM_DATUM_ALT=0

# NTRIP Settings
# Set to False if using external radio.
export OM_USE_NTRIP=True
export OM_NTRIP_HOSTNAME=flepos.vlaanderen.be
export OM_NTRIP_PORT=2101
export OM_NTRIP_USER=B055a001
export OM_NTRIP_PASSWORD=Wanneloveyou1
export OM_NTRIP_ENDPOINT=FLEPOSVRS32GREC
#export OM_NTRIP_AUTHENTICATE=True
export OM_NMEA=1      #NMEA cycle in ms
################################
##    Mower Logic Settings    ##
################################

# Voltages for battery to be considered full or empty
export OM_BATTERY_EMPTY_VOLTAGE=23.0
export OM_BATTERY_FULL_VOLTAGE=28.0

# Mower motor temperatures to stop and start mowing
export OM_MOWING_MOTOR_TEMP_HIGH=80.0
export OM_MOWING_MOTOR_TEMP_LOW=40.0

export OM_GPS_WAIT_TIME_SEC=10.0
export OM_GPS_TIMEOUT_SEC=5.0




# Mowing Behavior Settings
# True to enable mowing motor
export OM_ENABLE_MOWER=True

# True to start mowing automatically. If this is false, you need to start manually by pressing the start button
export OM_AUTOMATIC_START=false

export OM_OUTLINE_OFFSET=0.05
export OM_OUTLINE_COUNT=4
export OM_TOOL_WIDTH=0.13

export OM_DOCKING_DISTANCE=1.0
export OM_UNDOCK_DISTANCE=2.0
