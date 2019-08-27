#include <Encoder.h>
#include <SPI.h>
#include <TimerOne.h>
#include <SFE_MicroOLED.h>
#include <Bounce.h>

// MIDI realtime message bytes
#define CLOCK_STATUS_BYTE 0xF8 // clock
#define START_STATUS_BYTE 0xFA // start
#define STOP_STATUS_BYTE  0xFC // stop

// OLED pinout
//CS  RST D/C SDO SCK SDI 3v3 GND
//10  9   7   12  13  11  ~   ~

// pins
#define PIN_RESET 9
#define PIN_DC    7
#define PIN_CS    10
#define PIN_ENCODER_1 2
#define PIN_ENCODER_2 3
#define PIN_MAIN_PLAY_BUTTON 23
#define PIN_MAIN_PLAY_LED 22
#define PIN_CLOCK_1_PLAY_BUTTON 21
#define PIN_CLOCK_1_PLAY_LED 20
#define PIN_CLOCK_2_PLAY_BUTTON 19
#define PIN_CLOCK_2_PLAY_LED 18
#define PIN_PLAY_FROM_START_BUTTON 17

// constants
#define BUTTON_BOUNCE_TIME_MS 5
#define MIDI_BAUD_RATE 31250
#define PPQ 24
#define BEATS_PER_MEASURE 4

// Change these two numbers to the pins connected to your encoder.
//   Best Performance: both pins have interrupt capability
//   Good Performance: only the first pin has interrupt capability
//   Low Performance:  neither pin has interrupt capability
//   ......avoid using pins with LEDs attached
Encoder encoder(PIN_ENCODER_1, PIN_ENCODER_2);

// SPI declaration
MicroOLED oled(PIN_RESET, PIN_DC, PIN_CS);

// MIDI Serial 1 = PIN 1  (clock 1)
// MIDI Serial 2 = PIN 10 (unused)
// MIDI Serial 3 = PIN 8  (clock 2)

Bounce mainPlayButtonBouncer =		Bounce(PIN_MAIN_PLAY_BUTTON,		BUTTON_BOUNCE_TIME_MS);
Bounce playFromStartButtonBouncer = Bounce(PIN_PLAY_FROM_START_BUTTON,	BUTTON_BOUNCE_TIME_MS);
Bounce clock1PlayButtonBouncer =	Bounce(PIN_CLOCK_1_PLAY_BUTTON,		BUTTON_BOUNCE_TIME_MS);
Bounce clock2PlayButtonBouncer =	Bounce(PIN_CLOCK_2_PLAY_BUTTON,		BUTTON_BOUNCE_TIME_MS);

bool lastMainPlayButtonState = LOW;
bool lastClock1PlayButtonState = LOW;
bool lastClock2PlayButtonState = LOW;
bool lastPlayFromStartButtonState = LOW;

long previousEncoderPosition = 0;
long previousEncoderDivided = 0;

double bpm = 120.0;
volatile long currentPulseCount = 0;
volatile long currentBeatCount = 0;

bool isClockInitialized = false;
bool isMainClockPlaying = false;
bool isClock1Playing = false;
bool isClock2Playing = false;


void setup()
{
	pinMode(PIN_MAIN_PLAY_BUTTON,		INPUT);
	pinMode(PIN_MAIN_PLAY_LED,			OUTPUT);
	pinMode(PIN_CLOCK_1_PLAY_BUTTON,	INPUT);
	pinMode(PIN_CLOCK_1_PLAY_LED,		OUTPUT);
	pinMode(PIN_CLOCK_2_PLAY_BUTTON,	INPUT);
	pinMode(PIN_CLOCK_2_PLAY_LED,		OUTPUT);
	pinMode(PIN_PLAY_FROM_START_BUTTON,	INPUT);

	Serial1.begin(MIDI_BAUD_RATE);
	Serial3.begin(MIDI_BAUD_RATE);
  
	Serial.begin(9600);
	//Serial.println("Basic Encoder Test:");

	updateTimer();

	oled.begin();		// Initialize the OLED
	oled.clear(ALL);	// Clear the display's internal memory
	//oled.display();	// Display what's in the buffer (splashscreen)
	//delay(500);		// Delay 1000 ms
	oled.clear(PAGE);	// Clear the buffer.
}

void updateTimer()
{
	double microsecondsPerBeat = 60000000.0 / bpm;
	double microsecondsPerPulse = (microsecondsPerBeat / (double)PPQ);
  
	//Serial.println((long)microsecondsPerPulse);
  
	if(isClockInitialized)
	{
		Timer1.setPeriod(microsecondsPerPulse);
	}
	else
	{
		Timer1.initialize(microsecondsPerPulse);
		Timer1.attachInterrupt(timerCallback);
	}
}

void timerCallback()
{
	//Serial.println(currentPulseCount);
	noInterrupts();
	if(isClock1Playing) Serial1.write(CLOCK_STATUS_BYTE);
	if(isClock2Playing) Serial3.write(CLOCK_STATUS_BYTE);
	currentPulseCount++;
	currentBeatCount = currentPulseCount / 24;
	interrupts();
}

void readMainPlayButton()
{
	// read main play button
	mainPlayButtonBouncer.update();
	if(mainPlayButtonBouncer.read() != lastMainPlayButtonState)
	{
		lastMainPlayButtonState = !lastMainPlayButtonState;
		if(lastMainPlayButtonState == HIGH)
		{
			isMainClockPlaying = !isMainClockPlaying;
	  
			if(isMainClockPlaying)
			{
				Timer1.start();
				Serial1.write(START_STATUS_BYTE);
				digitalWrite(PIN_MAIN_PLAY_LED, HIGH);
			}
			else
			{
				Timer1.stop();
				Serial1.write(STOP_STATUS_BYTE);
				Serial1.write(STOP_STATUS_BYTE);
				digitalWrite(PIN_MAIN_PLAY_LED, LOW);
			}
		}
		//Serial.println(lastMainPlayButtonState);
	} 
}

void readPlayFromStartButton()
{
	playFromStartButtonBouncer.update();
	if(playFromStartButtonBouncer.read() != lastPlayFromStartButtonState)
	{
		lastPlayFromStartButtonState = !lastPlayFromStartButtonState;
		if(lastPlayFromStartButtonState == HIGH)
		{
			currentBeatCount = 0;
			currentPulseCount = 0;
			Timer1.restart();
			Timer1.resume();
			Serial.println("restart!");
		}
	}
}

void readClock1PlayButton()
{
	// read clock 1 play button
	clock1PlayButtonBouncer.update();
	if(clock1PlayButtonBouncer.read() != lastClock1PlayButtonState)
	{
		lastClock1PlayButtonState = !lastClock1PlayButtonState;

		if(lastClock1PlayButtonState == HIGH)
		{
			isClock1Playing = !isClock1Playing;
			digitalWrite(PIN_CLOCK_1_PLAY_LED, isClock1Playing);
		}
	}
}

void readClock2PlayButton()
{
	clock2PlayButtonBouncer.update();

	if(clock2PlayButtonBouncer.read() != lastClock2PlayButtonState)
	{
		lastClock2PlayButtonState = !lastClock2PlayButtonState;

		if(lastClock2PlayButtonState == HIGH)
		{
			isClock2Playing = !isClock2Playing;
			digitalWrite(PIN_CLOCK_2_PLAY_LED, isClock2Playing);
		}
	}
}

void readEncoder()
{
	long newEncoderPosition = encoder.read();

	if(newEncoderPosition != previousEncoderPosition)
	{
		previousEncoderPosition = newEncoderPosition;
		//Serial.println(newEncoderPosition);

		// between the detents in the knob, there are 4 encoder signals sent.
		// i only want to adjust bpm once the knob is settled into a detent,
		// so divide by 4.
		long newEncoderDivided = newEncoderPosition / 4;
		if(newEncoderDivided != previousEncoderDivided)
		{
			bpm += newEncoderDivided > previousEncoderDivided ? 1 : -1;
			previousEncoderDivided = newEncoderDivided;
			updateTimer();
		}
	}
}

void drawOLED()
{
	// setup OLED
	oled.clear(PAGE);  // is this necessary?
	oled.setCursor(0, 0);


	// display bpm
	oled.setFontType(2);
	oled.println(bpm);


	// display current beat HUD
	oled.setFontType(1);
	switch(currentBeatCount % BEATS_PER_MEASURE)
	{
		case 0:
		  oled.println("  0xxx");
		  break;

		case 1:
		  oled.println("  x0xx");
		  break;

		case 2:
		  oled.println("  xx0x");
		  break;
		  
		case 3:
		  oled.println("  xxx0");
		  break;
	}

	oled.display();
}

void loop()
{
	readMainPlayButton();
	readPlayFromStartButton();
	readClock1PlayButton();
	readClock2PlayButton();
	readEncoder();
}
