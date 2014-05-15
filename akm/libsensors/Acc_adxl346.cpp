/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/select.h>
#include "AKMLog.h"

#include "AccSensor.h"

#define ADXL_DATA_NAME				"ADXL34x accelerometer"
#define ADXL_MAX_SAMPLE_RATE_VAL	11 /* 200 Hz */

#define ADXL_UNIT_CONVERSION(value) ((value) * GRAVITY_EARTH / (256.0f))

/*****************************************************************************/

AccSensor::AccSensor()
    : SensorBase(NULL, ADXL_DATA_NAME),
      mEnabled(0),
      mFusionEnabled(0),
      mDelayFus(-1),
      mDelayAcc(-1),
      mDelayCur(-1),
      mInputReader(4),
      mHasPendingEvent(false)
{
	mPendingEvent.version = sizeof(sensors_event_t);
	mPendingEvent.sensor = ID_A;
	mPendingEvent.type = SENSOR_TYPE_ACCELEROMETER;
	memset(mPendingEvent.data, 0, sizeof(mPendingEvent.data));

	if (data_fd >= 0) {
		ssize_t ren = PATH_MAX;
		strncpy(input_sysfs_path, "/sys/class/input/", ren);
		ren = PATH_MAX - strnlen(input_sysfs_path, PATH_MAX);
		if (ren < 0) {
			ALOGE("AccSensor: Insufficient buffer.");
		}
		strncat(input_sysfs_path, input_name, ren);
		ren = PATH_MAX - strnlen(input_sysfs_path, PATH_MAX);
		if (ren < 0) {
			ALOGE("AccSensor: Insufficient buffer.");
		}
		strncat(input_sysfs_path, "/device/device/", ren);
		input_sysfs_path_len = strnlen(input_sysfs_path, PATH_MAX);
		ren = PATH_MAX - input_sysfs_path_len;
		if (ren < 0) {
			ALOGE("AccSensor: Insufficient buffer.");
		}
		ALOGD("AccSensor: sysfs_path=%s", input_sysfs_path);
	} else {
		input_sysfs_path[0] = '\0';
		input_sysfs_path_len = 0;
	}
}

AccSensor::~AccSensor() {
    if (mEnabled) {
        setEnable(ID_A, 0);
    }
    if (mFusionEnabled) {
        setEnable(ID_OR, 0);
    }
}

int AccSensor::setInitialState() {
    struct input_absinfo absinfo;

	if (mEnabled | mFusionEnabled) {
    	if (!ioctl(data_fd, EVIOCGABS(EVENT_TYPE_ACCEL_X), &absinfo)) {
			mPendingEvent.acceleration.x = ADXL_UNIT_CONVERSION(absinfo.value);
		}
    	if (!ioctl(data_fd, EVIOCGABS(EVENT_TYPE_ACCEL_Y), &absinfo)) {
			mPendingEvent.acceleration.y = ADXL_UNIT_CONVERSION(absinfo.value);
		}
		if (!ioctl(data_fd, EVIOCGABS(EVENT_TYPE_ACCEL_Z), &absinfo)) {
			mPendingEvent.acceleration.z = ADXL_UNIT_CONVERSION(absinfo.value);
		}
	}
    return 0;
}

bool AccSensor::hasPendingEvents() const {
    return mHasPendingEvent;
}

int AccSensor::setEnable(int32_t handle, int enabled) {
	int flags = enabled ? 1 : 0;
    int err = 0;
	char buffer[2];

	buffer[0] = '\0';
	buffer[1] = '\0';

	/* handle check */
	if (handle == ID_A) {
		if (flags) {
			if (!mFusionEnabled && !mEnabled) {
				buffer[0] = '0';	/* Turn On */
			}
		} else {
			if(!mFusionEnabled && mEnabled) {
				buffer[0] = '1';	/* Turn Off */
			}
		}
	} else if (
			(handle == ID_OR) ||
			(handle == ID_RV)) {
		if (flags) {
			if (!mFusionEnabled && !mEnabled) {
				buffer[0] = '0';	/* Turn On */
			}
		} else {
			if(mFusionEnabled && !mEnabled) {
				buffer[0] = '1';	/* Turn Off */
			}
		}
	} else {
		ALOGE("AccSensor: Invalid handle (%d)", handle);
		return -EINVAL;
	}

    if (buffer[0] != '\0') {
        strncpy(&input_sysfs_path[input_sysfs_path_len],
			"disable", PATH_MAX - input_sysfs_path_len);
		err = write_sys_attribute(input_sysfs_path, buffer, 1);
		if (err != 0) {
			return err;
		}
		ALOGD("AccSensor: Control set %s", buffer);
   		setInitialState();
    }

	if (handle == ID_A) {
		mEnabled = flags;
		ALOGD("AccSensor: mEnabled = %d", mEnabled);
	} else if (
			(handle == ID_OR) ||
			(handle == ID_RV)) {
		mFusionEnabled = flags;
		ALOGD("AccSensor: mFusionEnabled = %d", mFusionEnabled);
	}

    return err;
}

int AccSensor::setDelay(int32_t handle, int64_t delay_ns)
{
	int err = 0;
	int rate_val;
	int32_t us; 
	char buffer[16];
	int bytes;

	/* handle check */
	if (handle == ID_A) {
		mDelayAcc = delay_ns;
	} else if (
			(handle == ID_OR) ||
			(handle == ID_RV)) {
		mDelayFus = delay_ns;
	} else {
		ALOGE("AccSensor: Invalid handle (%d)", handle);
		return -EINVAL;
	}

	/* Choose interval */
	if (mFusionEnabled & mEnabled) {
		/* If Acc and Fusion is enabled, choose shorter one */
		delay_ns = ((mDelayAcc < mDelayFus) ? (mDelayAcc) : (mDelayFus));
	} else if (mFusionEnabled & !mEnabled) {
		/* If only Fusion is enabled, choose fusion */
		delay_ns = mDelayFus;
	} else if (!mFusionEnabled & mEnabled) {
		/* If only Acc is enabled, choose acc */
		delay_ns = mDelayAcc;
	} else {
		ALOGE("AccSensor: delay setting is ignored (%lld)", delay_ns);
	}

	if (mDelayCur != delay_ns) {
		/*  
	 	* The ADXL34x Supports 16 sample rates ranging from 3200Hz-0.098Hz
	 	* Calculate best fit and limit to max 200Hz (rate_val 11)
	 	*/

		us = (int32_t)(delay_ns / 1000);
		for (rate_val = 0; rate_val < 16; rate_val++) {
			if (us  >= ((10000000) >> rate_val)) {
				break;
			}
		}

		if (rate_val > ADXL_MAX_SAMPLE_RATE_VAL) {
			rate_val = ADXL_MAX_SAMPLE_RATE_VAL;
		}

		strncpy(&input_sysfs_path[input_sysfs_path_len],
			"rate", PATH_MAX - input_sysfs_path_len);
		bytes = sprintf(buffer, "%d", rate_val);
		err = write_sys_attribute(input_sysfs_path, buffer, bytes);
		if (err == 0) {
			mDelayCur = delay_ns;
			ALOGD("AccSensor: Control set delay %f ms requetsed, using %f ms",
				delay_ns/1000000.0f, 1e6 / (3200000 >> (15 - rate_val)));
		}
	}

	return err;
}

int AccSensor::readEvents(sensors_event_t* data, int count)
{
    if (count < 1) {
        return -EINVAL;
	}

    if (mHasPendingEvent) {
        mHasPendingEvent = false;
        mPendingEvent.timestamp = getTimestamp();
        *data = mPendingEvent;
        return (mEnabled | mFusionEnabled) ? 1 : 0;
    }

    ssize_t n = mInputReader.fill(data_fd);
    if (n < 0) {
        return n;
	}

    int numEventReceived = 0;
    input_event const* event;

    while (count && mInputReader.readEvent(&event)) {
        int type = event->type;
        if (type == EV_ABS) {
            float value = event->value;
			/* Value and direction should be compliant with Android definition. */
            if (event->code == EVENT_TYPE_ACCEL_X) {
                mPendingEvent.acceleration.x = ADXL_UNIT_CONVERSION(value);
            } else if (event->code == EVENT_TYPE_ACCEL_Y) {
                mPendingEvent.acceleration.y = ADXL_UNIT_CONVERSION(value);
            } else if (event->code == EVENT_TYPE_ACCEL_Z) {
                mPendingEvent.acceleration.z = ADXL_UNIT_CONVERSION(value);
            }
        } else if (type == EV_SYN) {
            mPendingEvent.timestamp = timevalToNano(event->time);
            if (mEnabled | mFusionEnabled) {
                *data++ = mPendingEvent;
                count--;
                numEventReceived++;
            }
        } else {
            ALOGE("AccSensor: unknown event (type=%d, code=%d)",
                    type, event->code);
        }
        mInputReader.next();
    }

    return numEventReceived;
}

