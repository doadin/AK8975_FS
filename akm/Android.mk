# TODO: Change device number!

#export AKMD_DEVICE_TYPE=8975
#export AKMD_DEVICE_TYPE=8963
#export AKMD_DEVICE_TYPE=8975
#export AKMD_DEVICE_TYPE=9911
export AKMD_SENSOR_ACC=adxl346
hardware_modules := akmdfs libsensors
include $(call all-named-subdir-makefiles,$(hardware_modules))
