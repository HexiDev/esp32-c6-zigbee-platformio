#ifndef ZIGBEE_MODE_ZCZR
#error "Zigbee end device mode is not selected in Tools->Zigbee mode"
#endif

#include "Zigbee.h"
#include <Arduino.h>

#define LED_PIN RGB_BUILTIN
#define BUTTON_PIN 9 // ESP32-C6/H2 Boot button
#define ZIGBEE_LIGHT_ENDPOINT 10
#define RGB_TRANSITION_TIME 500

ZigbeeLight zbLight = ZigbeeLight(ZIGBEE_LIGHT_ENDPOINT);
int identifyTime = 0;

// Define an RGB structure for managing colors
struct RGB
{
	int r;
	int g;
	int b;
};
RGB currentRGB = {0, 0, 0}; // Current LED color
RGB targetRGB = {0, 0, 0};	// Target LED color

/********************* RGB LED Fade Task **************************/
void ledHandler(void *pvParameters)
{
	while (true)
	{
		// Gradually fade the LED from currentRGB to targetRGB
		if (currentRGB.r != targetRGB.r || currentRGB.g != targetRGB.g || currentRGB.b != targetRGB.b)
		{
			// Fade Red component
			if (currentRGB.r < targetRGB.r)
				currentRGB.r++;
			else if (currentRGB.r > targetRGB.r)
				currentRGB.r--;

			// Fade Green component
			if (currentRGB.g < targetRGB.g)
				currentRGB.g++;
			else if (currentRGB.g > targetRGB.g)
				currentRGB.g--;

			// Fade Blue component
			if (currentRGB.b < targetRGB.b)
				currentRGB.b++;
			else if (currentRGB.b > targetRGB.b)
				currentRGB.b--;

			// Update the LED
			rgbLedWrite(8, currentRGB.r, currentRGB.g, currentRGB.b);

			// Wait for transition time
			vTaskDelay(RGB_TRANSITION_TIME / 255 / portTICK_PERIOD_MS);
		}
		else
		{
			// No change, just idle
			vTaskDelay(100 / portTICK_PERIOD_MS);
		}
	}

	vTaskDelete(NULL);
}

/********************* Identify Blinking Task **************************/
void identifyTask(void *pvParameters)
{
	RGB storedRGB;
	while (true)
	{
		int time = *(int *)pvParameters;
		if (time == 0)
			storedRGB = targetRGB;
		while (time > 0)
		{
			for (int i = 0; i < 2; i++)
			{
				targetRGB = {255, 0, 0}; // White color
				vTaskDelay(500 / 2 / portTICK_PERIOD_MS);
				targetRGB = {0, 0, 0}; // Turn off
				vTaskDelay(500 / 2 / portTICK_PERIOD_MS);
			}
			time -= 1;
		}
		targetRGB = storedRGB;
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}

	vTaskDelete(NULL);
}

/********************* Set LED State **************************/
void setLED(bool value)
{
	log_i("Setting LED to %d", value);
	targetRGB = value ? RGB{255, 255, 255} : RGB{0, 0, 0}; // Fade to white or off
}

/********************* Identify Callback **************************/
void identify(uint16_t timeRemaining)
{
	log_i("Identify called with time %d", timeRemaining);
	identifyTime = timeRemaining;
}

/********************* Arduino Functions **************************/
void setup()
{
	// Init LED and turn it OFF
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, LOW);

	// Init button for factory reset
	pinMode(BUTTON_PIN, INPUT_PULLUP);

	// Optional: set Zigbee device name and model
	zbLight.setManufacturerAndModel("Espressif", "ZBLightBulb");

	// Set callback function for light change
	zbLight.onLightChange(setLED);
	zbLight.onIdentify(identify);

	// Add endpoint to Zigbee Core
	log_d("Adding ZigbeeLight endpoint to Zigbee Core");
	Zigbee.addEndpoint(&zbLight);

	// When all EPs are registered, start Zigbee
	log_d("Calling Zigbee.begin()");
	Zigbee.begin(ZIGBEE_ROUTER);

	// Create task for LED fade handler
	xTaskCreate(ledHandler, "ledHandler", 2048, NULL, 1, NULL);

	// Create task for identify blinking
	xTaskCreate(identifyTask, "identifyTask", 2048, &identifyTime, 1, NULL);
}

void loop()
{
	// Checking button for factory reset
	if (digitalRead(BUTTON_PIN) == LOW)
	{
		// Key debounce handling
		delay(100);
		int startTime = millis();
		while (digitalRead(BUTTON_PIN) == LOW)
		{
			delay(50);
			if ((millis() - startTime) > 3000)
			{
				// If key pressed for more than 3secs, factory reset Zigbee and reboot
				Serial.printf("Resetting Zigbee to factory settings, reboot.\n");
				Zigbee.factoryReset();
			}
		}
	}
	delay(100);
}
