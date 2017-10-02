/**
 * Pluggable LED module for Chorus RF Laptimer by Andrey Voroshkov (bshep)
 * fast port I/O code from http://masteringarduino.blogspot.com.by/2013/10/fastest-and-smallest-digitalread-and.html

The MIT License (MIT)

Copyright (c) 2017 by Andrey Voroshkov (bshep)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/


// #define DEBUG

#ifdef DEBUG
    #define DEBUG_CODE(x) do { x } while(0)
#else
    #define DEBUG_CODE(x) do { } while(0)
#endif

uint8_t MODULE_ID = 0;
uint8_t MODULE_ID_HEX = '0';

#define PORTB_BIT_PIN_INTERRUPT 4 // using PIN12 (bit 4 on PORTB) to generate interrupts on LED driving arduino

#define SERIAL_DATA_DELIMITER '\n'

#include <avr/pgmspace.h>

#define BAUDRATE 115200

// input control byte constants
#define CONTROL_START_RACE      'R'
#define CONTROL_END_RACE        'r'
#define CONTROL_DEC_MIN_LAP     'm'
#define CONTROL_INC_MIN_LAP     'M'
#define CONTROL_DEC_CHANNEL     'c'
#define CONTROL_INC_CHANNEL     'C'
#define CONTROL_DEC_THRESHOLD   't'
#define CONTROL_INC_THRESHOLD   'T'
#define CONTROL_SET_THRESHOLD   'S' // it either triggers or sets an uint16 depending on command length
#define CONTROL_SET_SOUND       'D'
#define CONTROL_DATA_REQUEST    'A'
#define CONTROL_INC_BAND        'B'
#define CONTROL_DEC_BAND        'b'
#define CONTROL_START_CALIBRATE 'I'
#define CONTROL_END_CALIBRATE   'i'
#define CONTROL_MONITOR_ON      'V'
#define CONTROL_MONITOR_OFF     'v'
#define CONTROL_SET_SKIP_LAP0   'F'
#define CONTROL_GET_VOLTAGE     'Y'
#define CONTROL_GET_API_VERSION '#'
// input control byte constants for uint16 "set value" commands
#define CONTROL_SET_FREQUENCY   'Q'
// input control byte constants for long "set value" commands
#define CONTROL_SET_MIN_LAP     'L'
#define CONTROL_SET_CHANNEL     'H'
#define CONTROL_SET_BAND        'N'

// output id byte constants
#define RESPONSE_CHANNEL        'C'
#define RESPONSE_RACE_STATE     'R'
#define RESPONSE_MIN_LAP_TIME   'M'
#define RESPONSE_THRESHOLD      'T'
#define RESPONSE_CURRENT_RSSI   'S'
#define RESPONSE_LAPTIME        'L'
#define RESPONSE_SOUND_STATE    'D'
#define RESPONSE_BAND           'B'
#define RESPONSE_CALIBR_TIME    'I'
#define RESPONSE_CALIBR_STATE   'i'
#define RESPONSE_MONITOR_STATE  'V'
#define RESPONSE_LAP0_STATE     'F'
#define RESPONSE_END_SEQUENCE   'X'
#define RESPONSE_IS_CONFIGURED  'P'
#define RESPONSE_VOLTAGE        'Y'
#define RESPONSE_FREQUENCY      'Q'
#define RESPONSE_API_VERSION    '#'

//----- other globals------------------------------
uint8_t isRaceStarted = 0;

//----- read/write bufs ---------------------------
#define READ_BUFFER_SIZE 20
uint8_t readBuf[READ_BUFFER_SIZE];
uint8_t proxyBuf[READ_BUFFER_SIZE];
uint8_t readBufFilledBytes = 0;
uint8_t proxyBufDataSize = 0;

// ----------------------------------------------------------------------------
#include "fastReadWrite.h"
#include "pinAssignments.h"
#include "sendSerialHex.h"

// ----------------------------------------------------------------------------
void setup() {
    // we'll use pins 8-11 to send 4 bits of data to the LED-driving-arduino, and  pin 12 to
    // let it know that data has been changed.
    // using a simple hack here, but device-dependent, targeting Atmega328 (Uno, Nano, Pro Mini)

    DDRB = 0xFF; // all output
    PORTB = 0xFF; // all high because corresponding module has pull-ups enabled and expects inverted data

    Serial.begin(BAUDRATE);
}

// ----------------------------------------------------------------------------
void loop() {
    readSerialDataChunk(); //read passing data and do something in specific cases
    sendProxyDataChunk(); //... and just pass all the data further
}

// ----------------------------------------------------------------------------
void handleSerialRequest(uint8_t *controlData, uint8_t length) {
    uint8_t deviceId = TO_BYTE(controlData[0]);
    uint8_t controlByte = controlData[1];

    switch (controlByte) {
        case CONTROL_START_RACE:
            isRaceStarted = 1;
            break;

        case CONTROL_END_RACE:
            isRaceStarted = 0;
            break;
    }
}

// ----------------------------------------------------------------------------
void handleSerialResponse(uint8_t *responseData, uint8_t length) {
    uint8_t deviceId = TO_BYTE(responseData[0]);
    uint8_t responseCode = responseData[1];

    switch (responseCode) {
        case RESPONSE_LAPTIME:
            if (!isRaceStarted) break;

            PORTB &= 0xF0; // clear low 4 bits
            PORTB |= 0xF & (~deviceId); // output inverted low 4 bits of deviceId to PORTB (pins 8 - 11)
            PORTB ^=(1<<PORTB_BIT_PIN_INTERRUPT); // toggle the value to generate interrupt on receiving controller
            break;
    }
};

// ----------------------------------------------------------------------------
void readSerialDataChunk () {
    uint8_t availBytes = Serial.available();
    if (availBytes && proxyBufDataSize == 0) {
        uint8_t freeBufBytes = READ_BUFFER_SIZE - readBufFilledBytes;

        //reset buffer if we couldn't find delimiter in its contents in prev step
        if (freeBufBytes == 0) {
            readBufFilledBytes = 0;
            freeBufBytes = READ_BUFFER_SIZE;
        }

        //read minimum of "available to read" and "free place in buffer"
        uint8_t canGetBytes = availBytes > freeBufBytes ? freeBufBytes : availBytes;
        Serial.readBytes(&readBuf[readBufFilledBytes], canGetBytes);
        readBufFilledBytes += canGetBytes;

        //try finding a delimiter
        uint8_t foundIdx = 255;
        for (uint8_t i = 0; i < readBufFilledBytes; i++) {
            if (readBuf[i] == SERIAL_DATA_DELIMITER) {
                foundIdx = i;
                break;
            }
        }

        // Yes, in this module we should always pass msg further!
        // uint8_t shouldPassMsgFurther = 1;

        //if delimiter found
        if (foundIdx < READ_BUFFER_SIZE) {
            switch (readBuf[0]) {
                case 'R': //read data from module
                    handleSerialRequest(&readBuf[1], foundIdx);
                    break;
                case 'S': //data sent by prev modules
                    handleSerialResponse(&readBuf[1], foundIdx);
                    break;
            }
            // Yes, in this module we should always pass msg further!
            // if (shouldPassMsgFurther) {
                memmove(proxyBuf, readBuf, foundIdx);
                proxyBufDataSize = foundIdx;
            // }
            //remove processed portion of data
            memmove(readBuf, &readBuf[foundIdx+1], readBufFilledBytes - foundIdx+1);
            readBufFilledBytes -= foundIdx+1;
        }
    }
}
// ----------------------------------------------------------------------------
void sendProxyDataChunk () {
    if (proxyBufDataSize && Serial.availableForWrite() > proxyBufDataSize) {
        Serial.write(proxyBuf, proxyBufDataSize);
        Serial.write(SERIAL_DATA_DELIMITER);
        proxyBufDataSize = 0;
    }
}
