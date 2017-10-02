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

#include <FastLED.h>
#include <PinChangeInterrupt.h>

#define INTERRUPT_PIN 12

#define LED_DATA_PIN     5
#define COLOR_ORDER GRB
#define CHIPSET     WS2811
#define NUM_LEDS    40
#define NUM_COLORS  6

// number of loop cycles before lights go off
#define MAX_SEQUENCE_LENGTH 2000

CRGB leds[NUM_LEDS];

CRGB colors[NUM_COLORS] = {
    CRGB::Red,
    CRGB::Green,
    CRGB::Blue,
    CRGB::Orange,
    CRGB::Pink,
    CRGB::White
};

volatile uint8_t newEvent = 0;
volatile uint8_t colorId = 0;
uint32_t ledSequenceCounter = 0;

// ----------------------------------------------------------------------------
void setup() {
    DDRB = 0; //all 8-13 are inputs
    PORTB = 0xFF; //all 8-13 pull-up, values will be inverted

    FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(leds, NUM_LEDS);

    delay(1000); //just don't use LEDs immediately anyway

    FastLED.show(); // Initialize all pixels to 'off'

    random16_add_entropy( random());

    attachPinChangeInterrupt(digitalPinToPinChangeInterrupt(INTERRUPT_PIN), intHandler, CHANGE);
}

// ----------------------------------------------------------------------------
void loop() {
    if (newEvent) {
        newEvent = 0;
        ledSequenceCounter = 0;
        fill_solid(leds, NUM_LEDS, colors[colorId]);
    }

    if (ledSequenceCounter > MAX_SEQUENCE_LENGTH) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
    } else {
        ledSequenceCounter++;
        for(uint8_t i = 0; i < NUM_LEDS/8; i++) {
            uint8_t idx = random(NUM_LEDS);
            fade_raw(&leds[idx], 1 , 10);
        }
        delay(2);
    }

    FastLED.show();
}

// ----------------------------------------------------------------------------
void intHandler(void) {
    // portb data is inverted
    colorId = (~PINB) & 0xF;
    newEvent = 1;
}
