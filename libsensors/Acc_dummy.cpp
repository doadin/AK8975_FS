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

/*****************************************************************************/

AccSensor::AccSensor()
    : SensorBase(NULL, NULL),
      mEnabled(0),
      mFusionEnabled(0),
      mDelay(-1),
      mInputReader(4),
      mHasPendingEvent(false)
{
    mPendingEvent.version = sizeof(sensors_event_t);
    mPendingEvent.sensor = ID_A;
    mPendingEvent.type = SENSOR_TYPE_ACCELEROMETER;
    memset(mPendingEvent.data, 0, sizeof(mPendingEvent.data));

	input_sysfs_path[0] = '\0';
	input_sysfs_path_len = 0;
	ALOGD("AccSensor: Initialized empty.");
}

AccSensor::~AccSensor() {
}

int AccSensor::setInitialState() {
    return 0;
}

bool AccSensor::hasPendingEvents() const {
    return mHasPendingEvent;
}

int AccSensor::setEnable(int32_t handle, int enabled) {
	ALOGD("AccSensor: setEnable=%d", enabled);
    return 0;
}

int AccSensor::setDelay(int32_t handle, int64_t delay_ns) {
	ALOGD("AccSensor: setDelay=%lld", delay_ns);
    return 0;
}

int AccSensor::readEvents(sensors_event_t* data, int count)
{
    if (count < 1) {
        return -EINVAL;
	}

    return 0;
}

