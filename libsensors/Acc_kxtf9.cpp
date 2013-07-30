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
#include <linux/kxtf9.h>

#include "AKMLog.h"
#include "AccSensor.h"

#define KIONIX_IOCTL_ENABLE_OUTPUT	KXTF9_IOCTL_ENABLE_OUTPUT
#define KIONIX_IOCTL_DISABLE_OUTPUT	KXTF9_IOCTL_DISABLE_OUTPUT
#define KIONIX_IOCTL_GET_ENABLE		KXTF9_IOCTL_GET_ENABLE
#define KIONIX_IOCTL_UPDATE_ODR		KXTF9_IOCTL_UPDATE_ODR

#define KIONIX_UNIT_CONVERSION(value) ((value) * GRAVITY_EARTH / (1024.0f))

/*****************************************************************************/

AccSensor::AccSensor()
    : SensorBase(DIR_DEV, INPUT_NAME_ACC),
      mEnabled(0),
      mFusionEnabled(0),
      mDelayCur(-1),
      mDelayAcc(-1),
      mDelayFus(-1),
      mInputReader(32),
      mHasPendingEvent(false)
{
    mPendingEvent.version = sizeof(sensors_event_t);
    mPendingEvent.sensor = ID_A;
    mPendingEvent.type = SENSOR_TYPE_ACCELEROMETER;
    memset(mPendingEvent.data, 0, sizeof(mPendingEvent.data));

	open_device();
}

AccSensor::~AccSensor() {
    if (mEnabled) {
        setEnable(ID_A, 0);
    }
    if (mFusionEnabled) {
        setEnable(ID_OR, 0);
    }

	close_device();
}

int AccSensor::setInitialState() {
    struct input_absinfo absinfo;

	if (mEnabled | mFusionEnabled) {
    	if (!ioctl(data_fd, EVIOCGABS(EVENT_TYPE_ACCEL_X), &absinfo)) {
			mPendingEvent.acceleration.x = KIONIX_UNIT_CONVERSION(absinfo.value);
		}
    	if (!ioctl(data_fd, EVIOCGABS(EVENT_TYPE_ACCEL_Y), &absinfo)) {
			mPendingEvent.acceleration.y = KIONIX_UNIT_CONVERSION(absinfo.value);
		}
		if (!ioctl(data_fd, EVIOCGABS(EVENT_TYPE_ACCEL_Z), &absinfo)) {
			mPendingEvent.acceleration.z = KIONIX_UNIT_CONVERSION(absinfo.value);
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
	int opDone = 0;

	/* handle check */
	if (handle == ID_A) {
		if (flags) {
			if (!mFusionEnabled && !mEnabled) {
				err = ioctl(dev_fd, KIONIX_IOCTL_ENABLE_OUTPUT);
				opDone = 1;
			}
		} else {
			if (!mFusionEnabled && mEnabled) {
				err = ioctl(dev_fd, KIONIX_IOCTL_DISABLE_OUTPUT);
				opDone = 1;
			}
		}
	} else if (
			(handle == ID_OR) ||
			(handle == ID_RV)) {
		if (flags) {
			if (!mFusionEnabled && !mEnabled) {
				err = ioctl(dev_fd, KIONIX_IOCTL_ENABLE_OUTPUT);
				opDone = 1;
			}
		} else {
			if (mFusionEnabled && !mEnabled) {
				err = ioctl(dev_fd, KIONIX_IOCTL_DISABLE_OUTPUT);
				opDone = 1;
			}
		}
	} else {
		ALOGE("AccSensor: Invalid handle (%d)", handle);
		return -EINVAL;
	}

	if (err != 0) {
		ALOGE("AccSensor: IOCTL failed (%s)", strerror(errno));
		return err;
	}
	if (opDone) {
		ALOGD("AccSensor: Control set %d", enabled);
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
	int ms; 

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
		ms = delay_ns / 1000000;
        if (ioctl(dev_fd, KIONIX_IOCTL_UPDATE_ODR, &ms)) {
			return -errno;
		}
		mDelayCur = delay_ns;
		ALOGD("AccSensor: Control set delay %f ms.", delay_ns/1000000.0f);
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
            if (event->code == EVENT_TYPE_ACCEL_X) {
                mPendingEvent.acceleration.x = KIONIX_UNIT_CONVERSION(value);
            } else if (event->code == EVENT_TYPE_ACCEL_Y) {
                mPendingEvent.acceleration.y = KIONIX_UNIT_CONVERSION(value);
            } else if (event->code == EVENT_TYPE_ACCEL_Z) {
                mPendingEvent.acceleration.z = KIONIX_UNIT_CONVERSION(value);
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

