hardware_modules := akmdfs libsensors
include $(call all-named-subdir-makefiles,$(hardware_modules))
