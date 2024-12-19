
#include "Zigbee.h"

/* Zigbee temperature sensor configuration */
#define TEMP_SENSOR_ENDPOINT_NUMBER 10
uint8_t button = BOOT_PIN;

ZigbeeTempSensor zbTempSensor = ZigbeeTempSensor(TEMP_SENSOR_ENDPOINT_NUMBER);

/************************ Temp sensor *****************************/
static void temp_sensor_value_update(void *arg)
{
	for (;;)
	{
		// Read temperature sensor value
		float tsens_value = temperatureRead();
		Serial.printf("Updated temperature sensor value to %.2f°C\r\n", tsens_value);
		// Update temperature value in Temperature sensor EP
		zbTempSensor.setTemperature(tsens_value);
		delay(1000);
	}
}

/********************* Arduino functions **************************/
void setup()
{
	Serial.begin(115200);

	// Init button switch
	pinMode(button, INPUT_PULLUP);

	// Optional: set Zigbee device name and model
	zbTempSensor.setManufacturerAndModel("Espressif", "ZigbeeTempSensor");

	// Set minimum and maximum temperature measurement value (10-50°C is default range for chip temperature measurement)
	zbTempSensor.setMinMaxValue(10, 50);

	// Optional: Set tolerance for temperature measurement in °C (lowest possible value is 0.01°C)
	zbTempSensor.setTolerance(1);

	// Add endpoint to Zigbee Core
	Zigbee.addEndpoint(&zbTempSensor);

	Serial.println("Starting Zigbee...");
	// When all EPs are registered, start Zigbee in End Device mode
	if (!Zigbee.begin())
	{
		Serial.println("Zigbee failed to start!");
		Serial.println("Rebooting...");
		ESP.restart();
	}
	else
	{
		Serial.println("Zigbee started successfully!");
	}
	Serial.println("Connecting to network");
	while (!Zigbee.connected())
	{
		Serial.print(".");
		delay(100);
	}
	Serial.println();

	// Start Temperature sensor reading task
	xTaskCreate(temp_sensor_value_update, "temp_sensor_update", 2048, NULL, 10, NULL);

	// Set reporting interval for temperature measurement in seconds, must be called after Zigbee.begin()
	// min_interval and max_interval in seconds, delta (temp change in 0,1 °C)
	// if min = 1 and max = 0, reporting is sent only when temperature changes by delta
	// if min = 0 and max = 10, reporting is sent every 10 seconds or temperature changes by delta
	// if min = 0, max = 10 and delta = 0, reporting is sent every 10 seconds regardless of temperature change
	zbTempSensor.setReporting(1, 0, 1);
}

void loop()
{
	// Checking button for factory reset
	if (digitalRead(button) == LOW)
	{ // Push button pressed
		// Key debounce handling
		delay(100);
		int startTime = millis();
		while (digitalRead(button) == LOW)
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
		zbTempSensor.reportTemperature();
	}
	delay(100);
}