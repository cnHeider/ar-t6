/*
 *                  Copyright 2014 ARTaylor.co.uk
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Richard Taylor (richard@artaylor.co.uk)
 */

/* Description:
 *
 * This module contains the entrypoint, setup and main loop.
 *
 */

#include "system.h"
#include "tasks.h"
#include "keypad.h"
#include "sticks.h"
#include "lcd.h"
#include "gui.h"
#include "myeeprom.h"
#include "pulses.h"
#include "mixer.h"
#include "sound.h"
#include "eeprom.h"
#include "settings.h"
#include "logo.h"

volatile EEGeneral  g_eeGeneral;
volatile ModelData  g_model;
volatile uint8_t g_modelInvalid = 1;
uint8_t SlaveMode;		// Trainer Slave

/**
  * @brief  Main Loop for non-IRQ based work
  * @note   Deals with init and non time critical work.
  * @param  None
  * @retval None
  */
int main(void)
{
	// initialize all things system/board related
	system_init();

	// Initialize the task loop.
	task_init();

	// Initialize the keypad scanner (with IRQ wakeup).
	keypad_init();

	// Initialize the LCD
	lcd_init();

	// gui interface code init
	gui_init();

	// Initialize the EEPROM chip access
	eeprom_init();

	// Initalize settings and read data from EEPROM
	settings_init();

	// set contrast but limit to a reasonable value in case settings were corrupted
	uint16_t contrast = g_eeGeneral.contrast;
	if( contrast < LCD_CONTRAST_MIN ) contrast = LCD_CONTRAST_MIN;
	if( contrast > LCD_CONTRAST_MAX ) contrast = LCD_CONTRAST_MAX;
	lcd_set_contrast(contrast);

	if( !g_eeGeneral.disableSplashScreen )
	{
		// Put the logo into out frame buffer
		memcpy(lcd_buffer, logo, LCD_WIDTH * LCD_HEIGHT / 8);
		lcd_update();
		delay_ms(2000);
	}

	// ToDo: Block here until all switches are set correctly.
	//check_switches();

	// Initialize the buzzer
	sound_init();

	mixer_init();

	// Initialize the ADC / DMA
	sticks_init();

	// Start the radio output.
	pulses_init();

	// move gui to the startup page
	gui_navigate(GUI_LAYOUT_MAIN1);

	/*
	 * The main loop will sit in low power mode waiting for an interrupt.
	 *
	 * The ADC is running in continuous scanning mode with DMA transfer of the results to memory.
	 * An interrupt will fire when the full conversion scan has completed.
	 * This will schedule the "PROCESS_STICKS" task.
	 * The switches (SWA-SWD) will be polled at this point.
	 *
	 * Keys (trim, buttons and scroll wheel) are interrupt driven. "PROCESS_KEYS" will be scheduled
	 * when any of them are pressed.
	 *
	 * PPM is driven by Timer0 in interrupt mode autonomously from pwm_data.
	 *
	 */

	while (1)
	{
		/*
		static uint8_t tick;
		lcd_set_cursor(0, 48);
		lcd_write_int(tick++, 1, 0);
		lcd_update();
		*/

		// Process any tasks.
		task_process_all();


		// Wait for an interrupt
		//PWR_EnterSTANDBYMode();
	}
}
