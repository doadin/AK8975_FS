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

#include <hardware/sensors.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>

#include <linux/input.h>

#include <utils/Atomic.h>
#include "AKMLog.h"

#include "sensors.h"

#include "AccSensor.h"
#include "AkmSensor.h"

/*****************************************************************************/

#define DELAY_OUT_TIME 0x7FFFFFFF

#define LIGHT_SENSOR_POLLTIME    2000000000


#define SENSORS_ACCELERATION	(1<<ID_A)
#define SENSORS_MAGNETIC_FIELD	(1<<ID_M)
#define SENSORS_ORIENTATION		(1<<ID_OR)
#define SENSORS_ROTATION_VEC	(1<<ID_RV)

#define SENSORS_ACCELERATION_HANDLE		ID_A
#define SENSORS_MAGNETIC_FIELD_HANDLE	ID_M
#define SENSORS_ORIENTATION_HANDLE		ID_OR
#define SENSORS_ROTATION_VECTOR_HANDLE	ID_RV

/*****************************************************************************/

/* The SENSORS Module */
static const struct sensor_t sSensorList[] = {
#ifdef HAL_ACC_ADXL346
	{ "Analog Devices ADXL345/6 3-axis Accelerometer",
		"ADI",
		1,
		SENSORS_ACCELERATION_HANDLE,
		SENSOR_TYPE_ACCELEROMETER,
		(GRAVITY_EARTH * 16.0f),
		CONVERT_A,
		0.145f,
		10000,
		{ } },
#endif
#ifdef HAL_ACC_KXTF9
	{ "KXTF9 3-axis Accelerometer",
		"Kionix",
		1,
		SENSORS_ACCELERATION_HANDLE,
		SENSOR_TYPE_ACCELEROMETER,
		(GRAVITY_EARTH * 2),
		CONVERT_A,
		0.57f,
		20000,
		{ } },
#endif
#ifdef HAL_ACC_DUMMY
	{ "AKM 100-axis Accelerometer",
		"Asahi Kasei Microdevices",
		1,
		SENSORS_ACCELERATION_HANDLE,
		SENSOR_TYPE_ACCELEROMETER,
		(GRAVITY_EARTH),
		CONVERT_A,
		1.0f,
		10000,
		{ } },
#endif
#ifdef HAL_FOR_AK8963
	{ "AK8963 3-axis Magnetic field sensor",
		"Asahi Kasei Microdevices",
		1,
		SENSORS_MAGNETIC_FIELD_HANDLE,
		SENSOR_TYPE_MAGNETIC_FIELD,
		4915.2f,
		CONVERT_M,
		0.28f,
		10000,
		{ } },
#endif
#ifdef HAL_FOR_AK8975
	{ "AK8975 3-axis Magnetic field sensor",
		"Asahi Kasei Microdevices",
		1,  
		SENSORS_MAGNETIC_FIELD_HANDLE,
		SENSOR_TYPE_MAGNETIC_FIELD,
		1228.8f,
		CONVERT_M,
		0.35f,
		10000,
		{ } },
#endif
#ifdef HAL_FOR_AK09911
	{ "AK09911 3-axis Magnetic field sensor",
		"Asahi Kasei Microdevices",
		1,
		SENSORS_MAGNETIC_FIELD_HANDLE,
		SENSOR_TYPE_MAGNETIC_FIELD,
		4912.0f,
		CONVERT_M,
		0.24f,
		10000,
		{ } },
#endif
	{ "AKM Orientation sensor",
		"Asahi Kasei Microdevices",
		1,
		SENSORS_ORIENTATION_HANDLE,
		SENSOR_TYPE_ORIENTATION,
		360.0f,
		CONVERT_OR,
		1.0f,
		10000,
		{ } },
	{ "AKM Rotation vector sensor",
		"Asahi Kasei Microdevices",
		1,
		SENSORS_ROTATION_VECTOR_HANDLE,
		SENSOR_TYPE_ROTATION_VECTOR,
		34.907f,
		CONVERT_RV,
		1.0f,
		10000,
		{ } }
};


static int open_sensors(const struct hw_module_t* module, const char* id,
		struct hw_device_t** device);

static int sensors__get_sensors_list(struct sensors_module_t* module,
		struct sensor_t const** list) 
{
	*list = sSensorList;
	return ARRAY_SIZE(sSensorList);
}

static struct hw_module_methods_t sensors_module_methods = {
open: open_sensors
};

struct sensors_module_t HAL_MODULE_INFO_SYM = {
        common: {
                tag: HARDWARE_MODULE_TAG,
                version_major: 1,
                version_minor: 0,
                id: SENSORS_HARDWARE_MODULE_ID,
                name: "AKM 6D Sensor module",
                author: "Asahi Kasei Microdevices",
                methods: &sensors_module_methods,
        },
        get_sensors_list: sensors__get_sensors_list,
};

struct sensors_poll_context_t {
	struct sensors_poll_device_t device; // must be first

	sensors_poll_context_t();
	~sensors_poll_context_t();
	int activate(int handle, int enabled);
	int setDelay(int handle, int64_t ns);
	int pollEvents(sensors_event_t* data, int count);

private:
	enum {
		acc          = 0,
		akm          = 1,
		numSensorDrivers,
		numFds,
	};

	static const size_t wake = numFds - 1;
	static const char WAKE_MESSAGE = 'W';
	struct pollfd mPollFds[numFds];
	int mWritePipeFd;
	SensorBase* mSensors[numSensorDrivers];

	/* These function will be different depends on 
	 * which sensor is implemented in AKMD program.
	 */
	int handleToDriver(int handle);
	int proxy_enable(int handle, int enabled);
	int proxy_setDelay(int handle, int64_t ns);
};

/*****************************************************************************/

sensors_poll_context_t::sensors_poll_context_t()
{
	mSensors[acc] = new AccSensor();
	mPollFds[acc].fd = mSensors[acc]->getFd();
	mPollFds[acc].events = POLLIN;
	mPollFds[acc].revents = 0;

	mSensors[akm] = new AkmSensor();
	mPollFds[akm].fd = mSensors[akm]->getFd();
	mPollFds[akm].events = POLLIN;
	mPollFds[akm].revents = 0;

	int wakeFds[2];
	int result = pipe(wakeFds);
	ALOGE_IF(result<0, "error creating wake pipe (%s)", strerror(errno));
	fcntl(wakeFds[0], F_SETFL, O_NONBLOCK);
	fcntl(wakeFds[1], F_SETFL, O_NONBLOCK);
	mWritePipeFd = wakeFds[1];

	mPollFds[wake].fd = wakeFds[0];
	mPollFds[wake].events = POLLIN;
	mPollFds[wake].revents = 0;
}

sensors_poll_context_t::~sensors_poll_context_t() {
	for (int i=0 ; i<numSensorDrivers ; i++) {
		delete mSensors[i];
	}
	close(mPollFds[wake].fd);
	close(mWritePipeFd);
}

int sensors_poll_context_t::handleToDriver(int handle) {
	switch (handle) {
		case ID_A:
			return acc;
		case ID_M:
		case ID_OR:
		case ID_RV:
			return akm;
	}
	return -EINVAL;
}

int sensors_poll_context_t::activate(int handle, int enabled) {
	int drv = handleToDriver(handle);
	int err;

	if (drv < 0) {
		return drv;
	}

	err = mSensors[drv]->setEnable(handle, enabled);

	if (err) {
		return err;
	}
	if ((handle == ID_OR) ||
		(handle == ID_RV)) {
		err = mSensors[acc]->setEnable(handle, enabled);
	}
	if (enabled && !err) {
		const char wakeMessage(WAKE_MESSAGE);
		int result = write(mWritePipeFd, &wakeMessage, 1);
		ALOGE_IF(result<0, "error sending wake message (%s)", strerror(errno));
	}
	return err;
}

int sensors_poll_context_t::setDelay(int handle, int64_t ns) {
	int drv = handleToDriver(handle);
	int err;

	if (drv < 0) {
		return drv;
	}

	err = mSensors[drv]->setDelay(handle, ns);

	if (err) {
		return err;
	}
	if ((handle == ID_OR) ||
		(handle == ID_RV)) {
		err = mSensors[acc]->setDelay(handle, ns);
	}
	return err;
}

int sensors_poll_context_t::pollEvents(sensors_event_t* data, int count)
{
	int nbEvents = 0;
	int n = 0;

	do {
		// see if we have some leftover from the last poll()
		for (int i=0 ; count && i<numSensorDrivers ; i++) {
			SensorBase* const sensor(mSensors[i]);
			if ((mPollFds[i].revents & POLLIN) || (sensor->hasPendingEvents())) {
				int nb = sensor->readEvents(data, count);
				if (nb < count) {
					// no more data for this sensor
					mPollFds[i].revents = 0;
				}
				if ((0 != nb) && (acc == i)) {
					static_cast<AkmSensor*>(mSensors[akm])->setAccel(&data[nb-1]);
				}
				count -= nb;
				nbEvents += nb;
				data += nb;
			}
		}

		if (count) {
			// we still have some room, so try to see if we can get
			// some events immediately or just wait if we don't have
			// anything to return
			n = poll(mPollFds, numFds, nbEvents ? 0 : -1);
			if (n<0) {
				ALOGE("poll() failed (%s)", strerror(errno));
				return -errno;
			}
			if (mPollFds[wake].revents & POLLIN) {
				char msg;
				int result = read(mPollFds[wake].fd, &msg, 1);
				ALOGE_IF(result<0, "error reading from wake pipe (%s)", strerror(errno));
				ALOGE_IF(msg != WAKE_MESSAGE, "unknown message on wake queue (0x%02x)", int(msg));
				mPollFds[wake].revents = 0;
			}
		}
		// if we have events and space, go read them
	} while (n && count);

	return nbEvents;
}

/*****************************************************************************/

static int poll__close(struct hw_device_t *dev)
{
	sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
	if (ctx) {
		delete ctx;
	}
	return 0;
}

static int poll__activate(struct sensors_poll_device_t *dev,
		int handle, int enabled) {
	sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
	return ctx->activate(handle, enabled);
}

static int poll__setDelay(struct sensors_poll_device_t *dev,
		int handle, int64_t ns) {
	sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
	return ctx->setDelay(handle, ns);
}

static int poll__poll(struct sensors_poll_device_t *dev,
		sensors_event_t* data, int count) {
	sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
	return ctx->pollEvents(data, count);
}

/*****************************************************************************/

/** Open a new instance of a sensor device using name */
static int open_sensors(const struct hw_module_t* module, const char* id,
		struct hw_device_t** device)
{
	int status = -EINVAL;
	sensors_poll_context_t *dev = new sensors_poll_context_t();

	memset(&dev->device, 0, sizeof(sensors_poll_device_t));

	dev->device.common.tag = HARDWARE_DEVICE_TAG;
	dev->device.common.version  = 0;
	dev->device.common.module   = const_cast<hw_module_t*>(module);
	dev->device.common.close    = poll__close;
	dev->device.activate        = poll__activate;
	dev->device.setDelay        = poll__setDelay;
	dev->device.poll            = poll__poll;

	*device = &dev->device.common;
	status = 0;

	return status;
}

