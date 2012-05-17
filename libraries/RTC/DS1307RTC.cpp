/*
 * Copyright (C) 2012 Southern Storm Software, Pty Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "DS1307RTC.h"
#include "../I2C/BitBangI2C.h"
#include <WProgram.h>

/**
 * \class DS1307RTC DS1307RTC.h <DS1307RTC.h>
 * \brief Communicates with a DS1307 realtime clock chip via I2C.
 *
 * This class simplifies the process of reading and writing the time and
 * date information in a DS1307 realtime clock chip.  The class also
 * provides support for reading and writing information about alarms
 * and other clock settings.
 *
 * If there is no DS1307 chip on the I2C bus, this class will fall back to
 * the RTC class to simulate the current time and date based on the value
 * of millis().
 *
 * The DS1307 uses a 2-digit year so this class is limited to dates between
 * 2000 and 2099 inclusive.
 *
 * Note: if this class has not been used with the DS1307 chip before,
 * then the contents of NVRAM will be cleared.  Any previous contents
 * will be lost.
 *
 * \sa RTC
 */

// I2C address of the RTC chip (7-bit).
#define DS1307_I2C_ADDRESS  0x68

// Registers.
#define DS1307_SECOND       0x00
#define DS1307_MINUTE       0x01
#define DS1307_HOUR         0x02
#define DS1307_DAY_OF_WEEK  0x03
#define DS1307_DATE         0x04
#define DS1307_MONTH        0x05
#define DS1307_YEAR         0x06
#define DS1307_CONTROL      0x07
#define DS1307_NVRAM        0x08

// Alarm storage at the end of the RTC's NVRAM.
#define DS1307_ALARM_SIZE   3
#define DS1307_ALARMS       (64 - RTC::ALARM_COUNT * DS1307_ALARM_SIZE - 1)
#define DS1307_ALARM_MAGIC  63

/**
 * \brief Attaches to a realtime clock slave device on \a bus.
 *
 * If \a oneHzPin is not 255, then it indicates a digital input pin
 * that is connected to the 1 Hz square wave output on the realtime clock.
 * This input is used by hasUpdates() to determine if the time information
 * has changed in a non-trivial manner.
 *
 * \sa hasUpdates()
 */
DS1307RTC::DS1307RTC(BitBangI2C &bus, uint8_t oneHzPin)
    : _bus(&bus)
    , _oneHzPin(oneHzPin)
    , prevOneHz(false)
    , _isRealTime(true)
{
    // Make sure the CH bit in register 0 is off or the clock won't update.
    if (_bus->startWrite(DS1307_I2C_ADDRESS) == BitBangI2C::ACK) {
        _bus->write(DS1307_SECOND);
        _bus->startRead(DS1307_I2C_ADDRESS);
        uint8_t value = _bus->read(BitBangI2C::NACK);
        _bus->stop();
        if ((value & 0x80) != 0) {
            _bus->startWrite(DS1307_I2C_ADDRESS);
            _bus->write(DS1307_SECOND);
            _bus->write(value & 0x7F);
            _bus->stop();
        }
    } else {
        // Did not get an acknowledgement from the RTC chip.
        _isRealTime = false;
        _bus->stop();
    }

    // Turn on the 1 Hz square wave signal if required.
    if (oneHzPin != 255 && _isRealTime) {
        pinMode(oneHzPin, INPUT);
        digitalWrite(oneHzPin, HIGH);
        _bus->startWrite(DS1307_I2C_ADDRESS);
        _bus->write(DS1307_CONTROL);
        _bus->write(0x10);
        _bus->stop();
    }

    // Initialize the alarms in the RTC chip's NVRAM.
    if (_isRealTime)
        initAlarms();
}

DS1307RTC::~DS1307RTC()
{
}

/**
 * \fn bool DS1307RTC::isRealTime() const
 * \brief Returns true if the realtime clock is on the I2C bus; false if the time and date are simulated.
 */

bool DS1307RTC::hasUpdates()
{
    // If not using a 1 Hz pin or there is no RTC chip available,
    // then assume that there is an update available.
    if (_oneHzPin == 255 || !_isRealTime)
        return true;

    // The DS1307 updates the internal registers on the falling edge of the
    // 1 Hz clock.  The values should be ready to read on the rising edge.
    bool value = digitalRead(_oneHzPin);
    if (value && !prevOneHz) {
        prevOneHz = value;
        return true;
    } else {
        prevOneHz = value;
        return false;
    }
}

inline uint8_t fromBCD(uint8_t value)
{
    return (value >> 4) * 10 + (value & 0x0F);
}

inline uint8_t fromHourBCD(uint8_t value)
{
    if ((value & 0x40) != 0) {
        // 12-hour mode.
        uint8_t result = ((value >> 4) & 0x01) * 10 + (value & 0x0F);
        if ((value & 0x20) != 0)
            return (result == 12) ? 12 : (result + 12);     // PM
        else
            return (result == 12) ? 0 : result;             // AM
    } else {
        // 24-hour mode.
        return fromBCD(value);
    }
}

void DS1307RTC::readTime(RTCTime *value)
{
    if (_isRealTime) {
        _bus->startWrite(DS1307_I2C_ADDRESS);
        _bus->write(DS1307_SECOND);
        _bus->startRead(DS1307_I2C_ADDRESS);
        value->second = fromBCD(_bus->read() & 0x7F);
        value->minute = fromBCD(_bus->read());
        value->hour = fromHourBCD(_bus->read(BitBangI2C::NACK));
        _bus->stop();
    } else {
        RTC::readTime(value);
    }
}

void DS1307RTC::readDate(RTCDate *value)
{
    if (!_isRealTime) {
        RTC::readDate(value);
        return;
    }
    _bus->startWrite(DS1307_I2C_ADDRESS);
    _bus->write(DS1307_DATE);
    _bus->startRead(DS1307_I2C_ADDRESS);
    value->day = fromBCD(_bus->read());
    value->month = fromBCD(_bus->read());
    value->year = fromBCD(_bus->read(BitBangI2C::NACK)) + 2000;
    _bus->stop();
}

inline uint8_t toBCD(uint8_t value)
{
    return ((value / 10) << 4) + (value % 10);
}

void DS1307RTC::writeTime(const RTCTime *value)
{
    if (_isRealTime) {
        _bus->startWrite(DS1307_I2C_ADDRESS);
        _bus->write(DS1307_SECOND);
        _bus->write(toBCD(value->second));
        _bus->write(toBCD(value->minute));
        _bus->write(toBCD(value->hour));    // Changes mode to 24-hour clock.
        _bus->stop();
    } else {
        RTC::writeTime(value);
    }
}

void DS1307RTC::writeDate(const RTCDate *value)
{
    if (_isRealTime) {
        _bus->startWrite(DS1307_I2C_ADDRESS);
        _bus->write(DS1307_DATE);
        _bus->write(toBCD(value->day));
        _bus->write(toBCD(value->month));
        _bus->write(toBCD(value->year % 100));
        _bus->stop();
    } else {
        RTC::writeDate(value);
    }
}

void DS1307RTC::readAlarm(uint8_t alarmNum, RTCAlarm *value)
{
    if (_isRealTime) {
        _bus->startWrite(DS1307_I2C_ADDRESS);
        _bus->write(DS1307_ALARMS + alarmNum * DS1307_ALARM_SIZE);
        _bus->startRead(DS1307_I2C_ADDRESS);
        value->hour = fromBCD(_bus->read());
        value->minute = fromBCD(_bus->read());
        value->flags = _bus->read(BitBangI2C::NACK);
        _bus->stop();
    } else {
        RTC::readAlarm(alarmNum, value);
    }
}

void DS1307RTC::writeAlarm(uint8_t alarmNum, const RTCAlarm *value)
{
    if (_isRealTime) {
        _bus->startWrite(DS1307_I2C_ADDRESS);
        _bus->write(DS1307_ALARMS + alarmNum * DS1307_ALARM_SIZE);
        _bus->write(toBCD(value->hour));
        _bus->write(toBCD(value->minute));
        _bus->write(value->flags);
        _bus->stop();
    } else {
        RTC::writeAlarm(alarmNum, value);
    }
}

uint8_t DS1307RTC::readByte(uint8_t offset)
{
    if (_isRealTime) {
        _bus->startWrite(DS1307_I2C_ADDRESS);
        _bus->write(DS1307_NVRAM + offset);
        _bus->startRead(DS1307_I2C_ADDRESS);
        uint8_t value = _bus->read(BitBangI2C::NACK);
        _bus->stop();
        return value;
    } else {
        return RTC::readByte(offset);
    }
}

void DS1307RTC::writeByte(uint8_t offset, uint8_t value)
{
    if (_isRealTime) {
        _bus->startWrite(DS1307_I2C_ADDRESS);
        _bus->write(DS1307_NVRAM + offset);
        _bus->write(value);
        _bus->stop();
    } else {
        RTC::writeByte(offset, value);
    }
}

void DS1307RTC::initAlarms()
{
    _bus->startWrite(DS1307_I2C_ADDRESS);
    _bus->write(DS1307_ALARM_MAGIC);
    _bus->startRead(DS1307_I2C_ADDRESS);
    uint8_t value = _bus->read(BitBangI2C::NACK);
    _bus->stop();
    if (value != (0xB0 + ALARM_COUNT)) {
        // This is the first time we have used this clock chip,
        // so initialize all alarms to their default state.
        RTCAlarm alarm;
        alarm.hour = 6;         // Default to 6am for alarms.
        alarm.minute = 0;
        alarm.flags = 0;
        for (uint8_t index = 0; index < ALARM_COUNT; ++index)
            writeAlarm(index, &alarm);
        _bus->startWrite(DS1307_I2C_ADDRESS);
        _bus->write(DS1307_ALARM_MAGIC);
        _bus->write(0xB0 + ALARM_COUNT);
        _bus->stop();

        // Also clear the rest of NVRAM so that it is in a known state.
        // Otherwise we'll have whatever garbage was present at power-on.
        _bus->startWrite(DS1307_I2C_ADDRESS);
        _bus->write(DS1307_NVRAM);
        for (uint8_t index = DS1307_NVRAM; index < DS1307_ALARMS; ++index)
            _bus->write(0);
        _bus->stop();
    }
}