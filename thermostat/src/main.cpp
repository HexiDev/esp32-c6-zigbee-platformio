#include "Zigbee.h"

#define LED_PIN RGB_BUILTIN
#define BUTTON_PIN 9 // ESP32-C6/H2 Boot button

#define LIGHT_ENDPOINT_NUMBER 10
#define SWITCH_ENDPOINT_NUMBER 5
#define THERMOSTAT_ENDPOINT_NUMBER 6
#define TEMP_SENSOR_ENDPOINT_NUMBER 11

#define GPIO_INPUT_IO_TOGGLE_SWITCH 9
#define PAIR_SIZE(TYPE_STR_PAIR) (sizeof(TYPE_STR_PAIR) / sizeof(TYPE_STR_PAIR[0]))

ZigbeeThermostat zbThermostat = ZigbeeThermostat(THERMOSTAT_ENDPOINT_NUMBER);

// Save temperature sensor data
float sensor_temp;
float sensor_max_temp;
float sensor_min_temp;
float sensor_tolerance;

typedef enum
{
	SWITCH_ON_CONTROL,
	SWITCH_OFF_CONTROL,
	SWITCH_ONOFF_TOGGLE_CONTROL,
	SWITCH_LEVEL_UP_CONTROL,
	SWITCH_LEVEL_DOWN_CONTROL,
	SWITCH_LEVEL_CYCLE_CONTROL,
	SWITCH_COLOR_CONTROL,
} SwitchFunction;

typedef struct
{
	uint8_t pin;
	SwitchFunction func;
} SwitchData;

typedef enum
{
	SWITCH_IDLE,
	SWITCH_PRESS_ARMED,
	SWITCH_PRESS_DETECTED,
	SWITCH_PRESSED,
	SWITCH_RELEASE_DETECTED,
} SwitchState;

static SwitchData buttonFunctionPair[] = {{GPIO_INPUT_IO_TOGGLE_SWITCH, SWITCH_ONOFF_TOGGLE_CONTROL}};

/********************* Zigbee functions **************************/

void recieveSensorConfig(float min_temp, float max_temp, float tolerance)
{
	Serial.printf("Temperature sensor settings: min %.2f°C, max %.2f°C, tolerance %.2f°C\n", min_temp, max_temp, tolerance);
	sensor_min_temp = min_temp;
	sensor_max_temp = max_temp;
	sensor_tolerance = tolerance;
}
/********************* GPIO functions **************************/
static QueueHandle_t gpio_evt_queue = NULL;

static void IRAM_ATTR onGpioInterrupt(void *arg)
{
	xQueueSendFromISR(gpio_evt_queue, (SwitchData *)arg, NULL);
}

static void enableGpioInterrupt(bool enabled)
{
	for (int i = 0; i < PAIR_SIZE(buttonFunctionPair); ++i)
	{
		if (enabled)
		{
			enableInterrupt((buttonFunctionPair[i]).pin);
		}
		else
		{
			disableInterrupt((buttonFunctionPair[i]).pin);
		}
	}
}
/********************* Arduino functions **************************/
void setup()
{
	// Init LED and turn it OFF (if LED_PIN == RGB_BUILTIN, the rgbLedWrite() will be used under the hood)
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, LOW);

	// Init button for factory reset
	pinMode(BUTTON_PIN, INPUT_PULLUP);

	// Optional: set Zigbee device name and model
	zbThermostat.setManufacturerAndModel("Espressif", "Thermostat");
	zbThermostat.allowMultipleBinding(true);
	// Add endpoint to Zigbee Core
	Zigbee.addEndpoint(&zbThermostat);

	for (int i = 0; i < PAIR_SIZE(buttonFunctionPair); i++)
	{
		pinMode(buttonFunctionPair[i].pin, INPUT_PULLUP);
		/* create a queue to handle gpio event from isr */
		gpio_evt_queue = xQueueCreate(10, sizeof(SwitchData));
		if (gpio_evt_queue == 0)
		{
			log_e("Queue was not created and must not be used");
			while (1)
				;
		}
		attachInterruptArg(buttonFunctionPair[i].pin, onGpioInterrupt, (void *)(buttonFunctionPair + i), FALLING);
	}

	Zigbee.setRebootOpenNetwork(180);

	// When all EPs are registered, start Zigbee. By default acts as ZIGBEE_END_DEVICE
	log_d("Calling Zigbee.begin()");
	Zigbee.begin(ZIGBEE_END_DEVICE);

	Serial.println("Waiting for Sensor to bound to the thermostat");
	// Wait for switch to bound to a light:
	// while (!zbThermostat.bound())
	// {
	// 	Serial.printf(".");
	// 	delay(500);
	// }

	// Get temperature sensor configuration
}

void loop()
{
	if (digitalRead(BUTTON_PIN) == LOW)
	{ // Push button pressed
		// Key debounce handling
		delay(100);
		int startTime = millis();
		while (digitalRead(BUTTON_PIN) == LOW)
		{
			delay(50);
			if ((millis() - startTime) > 3000)
			{
				// If key pressed for more than 3secs, factory reset Zigbee and reboot
				Serial.println("Resetting Zigbee to factory and rebooting in 1s.");
				delay(1000);
				Zigbee.factoryReset();
			}
		}
		zbThermostat.getSensorSettings();

		zbThermostat.printBoundDevices();
	}
	delay(100);
}