#include <Encoder.h>
#include <SPI.h>
#include <TimerOne.h>
#include <SFE_MicroOLED.h>
#include <Bounce.h>

// MIDI realtime message bytes
#define CLOCK_STATUS_BYTE 0xF8 // clock
#define START_STATUS_BYTE 0xFA // start
#define STOP_STATUS_BYTE  0xFC // stop

// OLED pinout:
// CS  RST D/C SDO SCK SDI 3v3 GND
// 10  9   7   12  13  11  ~   ~

// MIDI pinout:
// MIDI Serial 1 = PIN 1  (clock 1)
// MIDI Serial 2 = PIN 10 (unused)
// MIDI Serial 3 = PIN 8  (clock 2)

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

Bounce mainPlayButtonBouncer =		Bounce(PIN_MAIN_PLAY_BUTTON,		BUTTON_BOUNCE_TIME_MS);
Bounce playFromStartButtonBouncer = Bounce(PIN_PLAY_FROM_START_BUTTON,	BUTTON_BOUNCE_TIME_MS);
Bounce clock1PlayButtonBouncer =	Bounce(PIN_CLOCK_1_PLAY_BUTTON,		BUTTON_BOUNCE_TIME_MS);
Bounce clock2PlayButtonBouncer =	Bounce(PIN_CLOCK_2_PLAY_BUTTON,		BUTTON_BOUNCE_TIME_MS);

bool lastMainPlayButtonState = LOW;
bool lastPlayFromStartButtonState = LOW;

long previousEncoderPosition = 0;
long previousEncoderDivided = 0;

double bpm = 132.0;
volatile long currentPulseCount = 0;
volatile long currentBeatCount = 0;

bool isClockInitialized = false;
bool isMainClockPlaying = false;

int beatsPerBar = 4;
int pulsesPerBar = PPQ * beatsPerBar;

#define NUM_CLOCK_OUTPUTS 2
HardwareSerial* m_clockSerials[NUM_CLOCK_OUTPUTS] = { &Serial1, &Serial3 };
Bounce* m_clockPlayButtonBouncers[NUM_CLOCK_OUTPUTS] = { &clock1PlayButtonBouncer, &clock2PlayButtonBouncer };
bool m_lastClockPlayButtonStates[NUM_CLOCK_OUTPUTS];
bool m_shouldClockStartNextBar[NUM_CLOCK_OUTPUTS];
const int m_clockPlayButtonLedPins[NUM_CLOCK_OUTPUTS] = { PIN_CLOCK_1_PLAY_LED, PIN_CLOCK_2_PLAY_LED };
const int m_clockPlayButtonPins[NUM_CLOCK_OUTPUTS] = { PIN_CLOCK_1_PLAY_BUTTON, PIN_CLOCK_2_PLAY_BUTTON };
bool m_isClockPlaying[NUM_CLOCK_OUTPUTS];


void setup()
{
	Serial.begin(9600);
	Serial.println("setup()");

	pinMode(PIN_MAIN_PLAY_BUTTON,		INPUT);
	pinMode(PIN_MAIN_PLAY_LED,			OUTPUT);
	pinMode(PIN_PLAY_FROM_START_BUTTON,	INPUT);

	for (int i = 0; i < NUM_CLOCK_OUTPUTS; i++)
	{
		m_clockSerials[i]->begin(MIDI_BAUD_RATE);
		m_lastClockPlayButtonStates[i] = LOW;
		m_shouldClockStartNextBar[i] = false;
		m_isClockPlaying[i] = false;
		pinMode(m_clockPlayButtonLedPins[i], OUTPUT);
		pinMode(m_clockPlayButtonPins[i], INPUT);
	}
  
	updateTimer();
	Timer1.stop();

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
	noInterrupts();

	for (int i = 0; i < NUM_CLOCK_OUTPUTS; i++)
	{
		m_clockSerials[i]->write(CLOCK_STATUS_BYTE);
	}

	currentPulseCount++;
	currentBeatCount = currentPulseCount / 24;

	if (currentPulseCount % pulsesPerBar == 0)  // the start of a new bar
	{
		for (int i = 0; i < NUM_CLOCK_OUTPUTS; i++)
		{
			if (m_shouldClockStartNextBar[i] && !m_isClockPlaying[i])
			{
				m_shouldClockStartNextBar[i] = false;
				m_isClockPlaying[i] = true;
				m_clockSerials[i]->write(START_STATUS_BYTE);
				digitalWrite(m_clockPlayButtonLedPins[i], HIGH);
			}
		}
	}
	else
	{
		//update clock play buttons that are waiting for next bar to start
		const int ppqInThisBeat = currentPulseCount % PPQ;
		bool isLedOn = ppqInThisBeat % (PPQ) == 0;
		for (int i = 0; i < NUM_CLOCK_OUTPUTS; i++)
		{
			if (m_shouldClockStartNextBar[i] && !m_isClockPlaying[i])
			{
				digitalWrite(m_clockPlayButtonLedPins[i], isLedOn);
			}
		}
	}

	interrupts();
}

void readMainPlayButton()
{
	mainPlayButtonBouncer.update();

	if(mainPlayButtonBouncer.read() != lastMainPlayButtonState)
	{
		lastMainPlayButtonState = !lastMainPlayButtonState;
		if(lastMainPlayButtonState == HIGH)
		{
			isMainClockPlaying = !isMainClockPlaying;
	  
			digitalWrite(PIN_MAIN_PLAY_LED, isMainClockPlaying);

			if(isMainClockPlaying)
			{
				Timer1.start();
				for (int i = 0; i < NUM_CLOCK_OUTPUTS; i++)
				{
					if (m_isClockPlaying[i])
					{
						m_clockSerials[i]->write(START_STATUS_BYTE);
					}
				}
			}
			else
			{
				Timer1.stop();
				for (int i = 0; i < NUM_CLOCK_OUTPUTS; i++)
				{
					m_clockSerials[i]->write(STOP_STATUS_BYTE);
				}
			}
		}
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

void readClockPlayButtons()
{
	for(int i = 0; i < NUM_CLOCK_OUTPUTS; i++)
	{
		m_clockPlayButtonBouncers[i]->update();

		if (m_clockPlayButtonBouncers[i]->read() != m_lastClockPlayButtonStates[i])
		{
			m_lastClockPlayButtonStates[i] = !m_lastClockPlayButtonStates[i];
			if (m_lastClockPlayButtonStates[i] == HIGH)
			{
				if (m_isClockPlaying[i])
				{
					m_isClockPlaying[i] = false;
					digitalWrite(m_clockPlayButtonLedPins[i], LOW);
					m_clockSerials[i]->write(STOP_STATUS_BYTE);
				}
				else
				{
					m_shouldClockStartNextBar[i] = !m_shouldClockStartNextBar[i];
				}
			}
		}
	}
}

void readEncoder()
{
	long newEncoderPosition = encoder.read();

	if(newEncoderPosition != previousEncoderPosition)
	{
		previousEncoderPosition = newEncoderPosition;

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
	readClockPlayButtons();
	readEncoder();
	drawOLED();
}
