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
#define NUM_CLOCK_OUTPUTS 2

// Change these two numbers to the pins connected to your encoder.
//   Best Performance: both pins have interrupt capability
//   Good Performance: only the first pin has interrupt capability
//   Low Performance:  neither pin has interrupt capability
//   ......avoid using pins with LEDs attached
Encoder m_encoder(PIN_ENCODER_1, PIN_ENCODER_2);

// SPI declaration
MicroOLED m_oled(PIN_RESET, PIN_DC, PIN_CS);

Bounce m_mainPlayButtonBouncer =	Bounce(PIN_MAIN_PLAY_BUTTON,		BUTTON_BOUNCE_TIME_MS);
Bounce m_stopButtonBouncer =		Bounce(PIN_PLAY_FROM_START_BUTTON,	BUTTON_BOUNCE_TIME_MS);
Bounce m_clock1PlayButtonBouncer =	Bounce(PIN_CLOCK_1_PLAY_BUTTON,		BUTTON_BOUNCE_TIME_MS);
Bounce m_clock2PlayButtonBouncer =	Bounce(PIN_CLOCK_2_PLAY_BUTTON,		BUTTON_BOUNCE_TIME_MS);

long m_previousEncoderPosition = 0;
long m_previousEncoderDivided = 0;
double m_bpm = 132.0;
volatile long m_currentPulseCount = 0;
volatile long m_currentBeatCount = 0;
bool m_lastMainPlayButtonState = LOW;
bool m_lastStopButtonState = LOW;
bool m_isClockInitialized = false;
bool m_isMainClockPlaying = false;
int m_beatsPerBar = 4;
int m_pulsesPerBar = PPQ * m_beatsPerBar;

HardwareSerial* m_clockSerials[NUM_CLOCK_OUTPUTS] = { &Serial1, &Serial3 };
Bounce* m_clockPlayButtonBouncers[NUM_CLOCK_OUTPUTS] = { &m_clock1PlayButtonBouncer, &m_clock2PlayButtonBouncer };
bool m_lastClockPlayButtonStates[NUM_CLOCK_OUTPUTS];
bool m_shouldClockStartNextBar[NUM_CLOCK_OUTPUTS];
bool m_isClockPlaying[NUM_CLOCK_OUTPUTS];
const int k_clockPlayButtonLedPins[NUM_CLOCK_OUTPUTS] = { PIN_CLOCK_1_PLAY_LED, PIN_CLOCK_2_PLAY_LED };
const int k_clockPlayButtonPins[NUM_CLOCK_OUTPUTS] = { PIN_CLOCK_1_PLAY_BUTTON, PIN_CLOCK_2_PLAY_BUTTON };


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
		pinMode(k_clockPlayButtonLedPins[i], OUTPUT);
		pinMode(k_clockPlayButtonPins[i], INPUT);
	}
  
	updateTimer();
	Timer1.stop();

	m_oled.begin();		// Initialize the OLED
	m_oled.clear(ALL);	// Clear the display's internal memory
	//oled.display();	// Display what's in the buffer (splashscreen)
	//delay(500);		// Delay 1000 ms
	m_oled.clear(PAGE);	// Clear the buffer.
}

void updateTimer()
{
	double microsecondsPerBeat = 60000000.0 / m_bpm;
	double microsecondsPerPulse = (microsecondsPerBeat / (double)PPQ);
  
	//Serial.println((long)microsecondsPerPulse);
  
	if(m_isClockInitialized)
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
	
	bool isStartOfNewBar = m_currentPulseCount % m_pulsesPerBar == 0;

	for (int i = 0; i < NUM_CLOCK_OUTPUTS; i++)
	{
		if (isStartOfNewBar)
		{
			if (m_shouldClockStartNextBar[i] && !m_isClockPlaying[i])
			{
				m_shouldClockStartNextBar[i] = false;
				m_isClockPlaying[i] = true;
				m_clockSerials[i]->write(START_STATUS_BYTE);
				digitalWrite(k_clockPlayButtonLedPins[i], HIGH);
			}
		}
		m_clockSerials[i]->write(CLOCK_STATUS_BYTE);
	}

	m_currentPulseCount++;
	m_currentBeatCount = m_currentPulseCount / 24;


	interrupts();
}

void readMainPlayButton()
{
	m_mainPlayButtonBouncer.update();

	if(m_mainPlayButtonBouncer.read() != m_lastMainPlayButtonState)
	{
		m_lastMainPlayButtonState = !m_lastMainPlayButtonState;
		if(m_lastMainPlayButtonState == HIGH)
		{
			m_currentBeatCount = 0;
			m_currentPulseCount = 0;

			if(m_isMainClockPlaying)
			{
				for (int i = 0; i < NUM_CLOCK_OUTPUTS; i++)
				{
					if (m_isClockPlaying[i] || m_shouldClockStartNextBar[i])
					{
						m_clockSerials[i]->write(START_STATUS_BYTE);
					}
				}
				Timer1.restart();
			}
			else
			{
				Timer1.start();
				m_isMainClockPlaying = true;
				digitalWrite(PIN_MAIN_PLAY_LED, HIGH);

				for (int i = 0; i < NUM_CLOCK_OUTPUTS; i++)
				{
					if (m_isClockPlaying[i] || m_shouldClockStartNextBar[i])
					{
						//m_clockSerials[i]->write(START_STATUS_BYTE);
					}
				}
			}
		}
	} 
}

void readStopButton()
{
	m_stopButtonBouncer.update();

	if(m_stopButtonBouncer.read() != m_lastStopButtonState)
	{
		m_lastStopButtonState = !m_lastStopButtonState;
		if(m_lastStopButtonState == HIGH)
		{
			Timer1.stop();
			m_currentBeatCount = 0;
			m_currentPulseCount = 0;
			m_isMainClockPlaying = false;
			digitalWrite(PIN_MAIN_PLAY_LED, LOW);

			for (int i = 0; i < NUM_CLOCK_OUTPUTS; i++)
			{
				m_clockSerials[i]->write(STOP_STATUS_BYTE);
				m_shouldClockStartNextBar[i] = m_isClockPlaying[i];
				m_isClockPlaying[i] = false;
			}

			Serial.println("stop!");
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
					digitalWrite(k_clockPlayButtonLedPins[i], LOW);
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
	long newEncoderPosition = m_encoder.read();

	if(newEncoderPosition != m_previousEncoderPosition)
	{
		m_previousEncoderPosition = newEncoderPosition;

		// between the detents in the knob, there are 4 encoder signals sent.
		// i only want to adjust bpm once the knob is settled into a detent,
		// so divide by 4.
		long newEncoderDivided = newEncoderPosition / 4;
		if(newEncoderDivided != m_previousEncoderDivided)
		{
			m_bpm += newEncoderDivided > m_previousEncoderDivided ? 1 : -1;
			m_previousEncoderDivided = newEncoderDivided;
			updateTimer();
		}
	}
}

void drawOLED()
{
	// setup OLED
	m_oled.clear(PAGE);  // is this necessary?
	m_oled.setCursor(0, 0);

	// display bpm
	m_oled.setFontType(2);
	char buffer[8];
	sprintf(buffer, "%.1f", m_bpm);
	m_oled.println(buffer);

	// display current beat HUD
	m_oled.setFontType(1);
	switch(m_currentBeatCount % m_beatsPerBar)
	{
		case 0:
		  m_oled.println("  0xxx");
		  break;

		case 1:
		  m_oled.println("  x0xx");
		  break;

		case 2:
		  m_oled.println("  xx0x");
		  break;
		  
		case 3:
		  m_oled.println("  xxx0");
		  break;
	}

	m_oled.display();
}

void updateClockButtonLeds()
{
	if (m_isMainClockPlaying)
	{
		//update clock play buttons that are waiting for next bar to start
		const int ppqInThisBeat = m_currentPulseCount % PPQ;
		bool isLedOn = ppqInThisBeat % PPQ == 0;
		for (int i = 0; i < NUM_CLOCK_OUTPUTS; i++)
		{
			if (m_shouldClockStartNextBar[i] && !m_isClockPlaying[i])
			{
				digitalWrite(k_clockPlayButtonLedPins[i], isLedOn);
			}
		}
	}
	else
	{
		for (int i = 0; i < NUM_CLOCK_OUTPUTS; i++)
		{
			digitalWrite(k_clockPlayButtonLedPins[i], m_isClockPlaying[i] || m_shouldClockStartNextBar[i]);
		}
	}
}


void loop()
{
	readMainPlayButton();
	readStopButton();
	readClockPlayButtons();
	readEncoder();
	updateClockButtonLeds();
	drawOLED();
}
