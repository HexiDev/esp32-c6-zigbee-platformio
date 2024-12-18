#include "Zigbee.h"

#define LED_PIN RGB_BUILTIN
#define BUTTON_PIN 9 // ESP32-C6/H2 Boot button
#define ZIGBEE_LIGHT_ENDPOINT 10
#define SWITCH_ENDPOINT_NUMBER 5

#define GPIO_INPUT_IO_TOGGLE_SWITCH 9
#define PAIR_SIZE(TYPE_STR_PAIR) (sizeof(TYPE_STR_PAIR) / sizeof(TYPE_STR_PAIR[0]))

ZigbeeLight zbLight = ZigbeeLight(ZIGBEE_LIGHT_ENDPOINT);
ZigbeeSwitch zbSwitch = ZigbeeSwitch(SWITCH_ENDPOINT_NUMBER);
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
static void onZbButton(SwitchData *button_func_pair)
{
	if (button_func_pair->func == SWITCH_ONOFF_TOGGLE_CONTROL)
	{
		// Send toggle command to the light
		zbSwitch.lightToggle();
	}
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
	zbSwitch.setManufacturerAndModel("Espressif", "ZBSwitchBulb");

	// Optional to allow multiple light to bind to the switch
	zbSwitch.allowMultipleBinding(true);

	// Add endpoint to Zigbee Core
	log_d("Adding ZigbeeLight endpoint to Zigbee Core");
	Zigbee.addEndpoint(&zbSwitch);

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

	Serial.println("Waiting for Light to bound to the switch");
	// Wait for switch to bound to a light:
	while (!zbSwitch.bound())
	{
		Serial.printf(".");
		delay(500);
	}
}

void loop()
{
	// Handle button switch in loop()
	uint8_t pin = 0;
	SwitchData buttonSwitch;
	static SwitchState buttonState = SWITCH_IDLE;
	bool eventFlag = false;

	/* check if there is any queue received, if yes read out the buttonSwitch */
	if (xQueueReceive(gpio_evt_queue, &buttonSwitch, portMAX_DELAY))
	{
		pin = buttonSwitch.pin;
		enableGpioInterrupt(false);
		eventFlag = true;
	}
	while (eventFlag)
	{
		bool value = digitalRead(pin);
		switch (buttonState)
		{
		case SWITCH_IDLE:
			buttonState = (value == LOW) ? SWITCH_PRESS_DETECTED : SWITCH_IDLE;
			break;
		case SWITCH_PRESS_DETECTED:
			buttonState = SWITCH_PRESSED;
			(*onZbButton)(&buttonSwitch);
			break;
		case SWITCH_PRESSED:
			buttonState = (value == LOW) ? SWITCH_PRESSED : SWITCH_RELEASE_DETECTED;
			break;
		case SWITCH_RELEASE_DETECTED:
			buttonState = SWITCH_IDLE;
			(*onZbButton)(&buttonSwitch);
			/* callback to button_handler */
			break;
		default:
			break;
		}
		if (buttonState == SWITCH_IDLE)
		{
			enableGpioInterrupt(true);
			eventFlag = false;
			break;
		}
		vTaskDelay(10 / portTICK_PERIOD_MS);
	}

	// print the bound lights every 10 seconds
	static uint32_t lastPrint = 0;
	if (millis() - lastPrint > 10000)
	{
		lastPrint = millis();
		zbSwitch.printBoundDevices();
	}
}