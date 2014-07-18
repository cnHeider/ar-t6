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

#include <string.h>

#include "stm32f10x.h"
#include "tasks.h"
#include "lcd.h"
#include "logo.h"

#include "lcd_font_medium.h"
#include "lcd_font_large.h"

#define LCD_PIN_MASK	(0x1FFF)

#define LCD_DATA		(0xFF)		// D0-D7
#define LCD_RD			(1 << 8)	// RD / E
#define LCD_WR			(1 << 9)	// RD / #WR
#define LCD_A0			(1 << 10)	// A0 / RS / Data / #CMD
#define LCD_RES			(1 << 11)	// Reset
#define LCD_CS1			(1 << 12)	// Chip Select 1

#define LCD_BACKLIGHT	(1 << 2)

#define KS0713_DISP_ON_OFF		(0xAE)
#define KS0713_DISPLAY_LINE		(0x40)
#define KS0713_SET_REF_VOLTAGE	(0x81)	// 2-byte cmd
#define KS0713_SET_PAGE_ADDR	(0xB0)
#define KS0713_SET_COL_ADDR_MSB	(0x10)
#define KS0713_SET_COL_ADDR_LSB	(0x00)
#define KS0713_ADC_SELECT		(0xA0)
#define KS0713_REVERSE_DISP		(0xA6)
#define KS0713_ENTIRE_DISP		(0xA4)
#define KS0713_LCD_BIAS			(0xA2)
#define KS0713_SET_MOD_READ		(0xE0)
#define KS0713_UNSET_MOD_READ	(0xEE)
#define KS0713_RESET			(0xE2)
#define KS0713_SHL_SELECT		(0xC0)
#define KS0713_POWER_CTRL		(0x28)
#define KS0713_REG_RES_SEL		(0x20)
#define KS0713_STATIC_IND_MODE	(0xAC)	// 2-byte cmd
//#define KS0713_POWER_SAVE		Display off, Entire display ON.

static uint8_t contrast = 0x28;
static uint8_t lcd_buffer[LCD_WIDTH * LCD_HEIGHT / 8];

static uint8_t char_height = 7;
static uint8_t char_width = 5;

static uint8_t cursor_x = 0;
static uint8_t cursor_y = 0;

static const unsigned char *font = font_medium;
static uint16_t font_width = 255 * 5;

/**
  * @brief  Send a command to the LCD.
  * @note	Switch the MPU interface to command mode and send
  * @param  cmd: Command to send
  * @retval None
  */
static void lcd_send_command(uint8_t cmd)
{
	uint16_t gpio = GPIO_ReadOutputData(GPIOC);

	gpio &= ~(LCD_WR | LCD_DATA | LCD_A0);
	gpio |= cmd;
	GPIO_Write(GPIOC, gpio);

	// Toggle the Enable lines.
	gpio &= ~LCD_CS1;
	gpio |= LCD_RD;
	GPIO_Write(GPIOC, gpio);
	gpio |= LCD_CS1;
	gpio &= ~LCD_RD;
	GPIO_Write(GPIOC, gpio);
}

/**
  * @brief  Send data to the LCD.
  * @note	Switch the MPU interface to data mode and send
  * @param  data: pointer to LCD data
  * @param  len: Number of bytes of data to send
  * @retval None
  */
static void lcd_send_data(uint8_t *data, uint16_t len)
{
	uint16_t i;
	uint16_t gpio = GPIO_ReadOutputData(GPIOC);

	gpio &= ~(LCD_WR | LCD_DATA);
	gpio |= LCD_A0;

	data += (len-1);

	for (i=0; i<len; ++i)
	{
		gpio &= ~LCD_DATA;
		gpio |= *data--;
		GPIO_Write(GPIOC, gpio);

		// Toggle the Enable lines.
		gpio &= ~LCD_CS1;
		gpio |= LCD_RD;
		GPIO_Write(GPIOC, gpio);
		gpio |= LCD_CS1;
		gpio &= ~LCD_RD;
		GPIO_Write(GPIOC, gpio);
	}
}

/**
  * @brief  Initialise the lcd panel.
  * @note   Sets up the controller and displays our logo.
  * @param  None
  * @retval None
  */
void lcd_init(void)
{
	GPIO_InitTypeDef gpioInit;

	// Enable the GPIO block clocks and setup the pins.
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

	GPIO_SetBits(GPIOC, LCD_PIN_MASK);
	GPIO_ResetBits(GPIOC, LCD_RD | LCD_WR);

	// Configure the LCD pins.
	gpioInit.GPIO_Speed = GPIO_Speed_2MHz;
	gpioInit.GPIO_Mode = GPIO_Mode_Out_PP;
	gpioInit.GPIO_Pin = LCD_PIN_MASK;
	GPIO_Init(GPIOC, &gpioInit);

	gpioInit.GPIO_Pin = LCD_BACKLIGHT;
	GPIO_Init(GPIOD, &gpioInit);

	// Reset LCD
	GPIO_ResetBits(GPIOC, LCD_RES);
	delay_us(5);
	GPIO_SetBits(GPIOC, LCD_RES);

	// Wait for reset to complete.
	delay_us(50);

	lcd_send_command(KS0713_RESET); 				// Initialize the internal functions (Reset)
	lcd_send_command(KS0713_DISP_ON_OFF | 0x00); 	// Turn off LCD panel (DON = 0)
	lcd_send_command(KS0713_ADC_SELECT | 0x01);  	// Select SEG output direction reversed (ADC = 1)
	lcd_send_command(KS0713_REVERSE_DISP | 0x00); 	// Select normal / reverse display (REV = 0)
	lcd_send_command(KS0713_ENTIRE_DISP | 0x00); 	// Select normal display ON (EON = 0)
	lcd_send_command(KS0713_LCD_BIAS | 0x00); 		// Select LCD bias (0)
	lcd_send_command(KS0713_SHL_SELECT | 0x08); 	// Select COM output direction normal (SHL = 0)
	lcd_send_command(KS0713_POWER_CTRL | 0x07); 	// Control power circuit operation (VC,VR,VF on)
	lcd_send_command(KS0713_REG_RES_SEL | 0x04); 	// Select internal resistance ratio (0x05)
	lcd_send_command(KS0713_SET_REF_VOLTAGE); 		// Set reference voltage Mode (2-part cmd)
	lcd_send_command(contrast); 						// Set reference voltage register
	lcd_send_command(KS0713_DISP_ON_OFF | 0x01); 	// Turn on LCD panel (DON = 1)

	// Put the logo into out frame buffer
	memcpy(lcd_buffer, logo, LCD_WIDTH * LCD_HEIGHT / 8);

	lcd_update();
	lcd_backlight(TRUE);
}

/**
  * @brief  Turn the backlight on / off.
  * @note
  * @param  on: Whether the backlight should be on or off.
  * @retval None
  */
void lcd_backlight(bool state)
{
	if (state)
		GPIO_SetBits(GPIOD, LCD_BACKLIGHT);
	else
		GPIO_ResetBits(GPIOD, LCD_BACKLIGHT);
}


/**
  * @brief  Adjust the LCD contrast.
  * @note
  * @param  val: signed integer to increase / decrease contrast by.
  * @retval None
  */
void lcd_adj_contrast(int8_t val)
{
	contrast += val;
	if ((contrast + val) > 0xff) contrast = 0xff;
	else if (contrast + val < 0) contrast = 0;

	lcd_send_command(KS0713_SET_REF_VOLTAGE); 		// Set reference voltage Mode (2-part cmd)
	lcd_send_command(contrast); 						// Set reference voltage register
}

/**
  * @brief  Transfer frame buffer to LCD.
  * @note
  * @param  None
  * @retval None
  */
void lcd_update(void)
{
	int row;

	// Update CGRAM
	for (row = 0; row < LCD_HEIGHT / 8; ++row)
	{
		lcd_send_command(KS0713_SET_PAGE_ADDR | row);
		lcd_send_command(KS0713_SET_COL_ADDR_LSB | 0x04); // low col
		lcd_send_command(KS0713_SET_COL_ADDR_MSB | 0x00);
		lcd_send_data(lcd_buffer + (LCD_WIDTH * row), LCD_WIDTH);
	}
}

/**
  * @brief  Set / Clean a specific pixel.
  * @note	Top left is (0,0)
  * @param  x: horizontal pixel location
  * @param  y: vertical pixel location
  * @param  colour: 0 - off, !0 - on
  * @retval None
  */
void lcd_set_pixel(uint8_t x, uint8_t y, uint8_t colour)
{
  if (colour) lcd_buffer[x+(y/8)*LCD_WIDTH] |= (1 << (y%8));
  else lcd_buffer[x+ (y/8)*LCD_WIDTH] &= ~(1 << (y%8));
}

/**
  * @brief  Set cursor position in pixels.
  * @note	Top left is (0,0)
  * @param  x: horizontal cursor location
  * @param  y: vertical cusros location
  * @retval None
  */
void lcd_set_cursor(uint8_t x, uint8_t y)
{
    if ((y+char_height) >= LCD_HEIGHT) return;
    if ((x+char_width) >= LCD_WIDTH) return;

	cursor_x = x;
	cursor_y = y;
}

/**
  * @brief  Set the character size (and select the font).
  * @note
  * @param  s: Size enum
  * @retval None
  */
void lcd_set_char_size(LCD_CHAR_SIZE s)
{
	switch (s)
	{
		case LCD_CHAR_SIZE_SMALL:
			char_height = 7;
			char_width = 5;
			font_width = 255 * char_width;
			font = font_medium;
		break;

		case LCD_CHAR_SIZE_MEDIUM:
			char_height = 15;
			char_width = 11;
			font = font_large;
			font_width = 14 * char_width;
		break;
	}
}

/**
  * @brief  Write a character.
  * @note	colour is used to invert the output (highlight mode)
  * @param  c: ASCII character to write
  * @param  colour: 0 - off, !0 - on
  * @retval None
  */
void lcd_write_char(uint8_t c, uint8_t colour)
{
	uint8_t x, y;
	uint8_t row = 0;
	if ((cursor_y+char_height) >= LCD_HEIGHT) return;
	else if ((cursor_x+char_width) >= LCD_WIDTH) return;

	if (font == font_large)
	{
		if (c <= '9' && c >= '0')
			c -= '0';
		else if (c == ' ')
			c = 13;
		else if (c == '-')
			c = 12;
		else if (c == '+')
			c = 11;
		else return;
	}

	for (x=0; x<char_width; x++ ) {
		row = 0;
		for (y=0; y<char_height + 1; y++) {
			uint8_t state;
			uint8_t d = font[(c*char_width)+x+row*font_width];
			if (d & (1 << y%8))
				state = colour;
			else
				state = 1 - colour;
			lcd_set_pixel(cursor_x+x, cursor_y+y, state);
			if (y%8 == 7)
				row++;
		}
	}
	for (y=0; y<char_height+1; y++) lcd_set_pixel(cursor_x+char_width, cursor_y+y, 1-colour);

	cursor_x += char_width + 1;
	if (cursor_x >= LCD_WIDTH)
	cursor_y += char_height + 1;
}

/**
  * @brief  Write a string.
  * @note	Iterate through a null terminated string.
  * @param  s: ASCII string to write
  * @param  colour: 0 - off, !0 - on
  * @retval None
  */
void lcd_write_string(char *s, uint8_t colour)
{
	char *ptr = s;
	uint8_t n = strlen(s);

	for (ptr = s; n > 0; ptr++, n--)
		lcd_write_char(*ptr, colour);
}


// Reduce stack usage.
static uint8_t tth;
static uint8_t th;
static uint8_t h;
static uint8_t t;
static int16_t u;

/**
  * @brief  Write an int (up to 5 digits)
  * @note
  * @param  val: Signed integer (-99999 to 99999)
  * @param  colour: 0 - off, !0 - on
  * @retval None
  */
void lcd_write_int(int32_t val, uint8_t colour, bool show_sign)
{
	if (val < 0) u = -val;
	else u = val;
	tth = u / 10000;
	u -= tth * 10000;
	th = u / 1000;
	u -= th * 1000;
	h = u / 100;
	u -= h * 100;
	t = u / 10;
	u -= t * 10;

	if (val < 0) lcd_write_char('-', colour);
	if (show_sign)
	{
		if (val >= 0) lcd_write_char('+', colour);
	}

	if (tth > 0)
		lcd_write_char(tth + '0', colour);
	if (tth > 0 || th > 0)
		lcd_write_char(th + '0', colour);

	if (tth > 0 || th > 0 || h > 0)
		lcd_write_char(h + '0', colour);
	else if (!show_sign)
		lcd_write_char(' ', colour);

	if (tth > 0 || th > 0 || h > 0 || t > 0 || !show_sign)
		lcd_write_char(t + '0', colour);
	lcd_write_char(u + '0', colour);
}

/**
  * @brief  Write a float (up to 5 digits)
  * @note
  * @param  value: float (-99999 to 99999)
  * @param  colour: 0 - off, !0 - on
  * @retval None
  */
void lcd_write_float(float val, uint8_t colour, bool show_sign)
{
}

/**
  * @brief  Draw a line between two points.
  * @note	Top left is (0,0)
  * @param  x1: First pixel (x)
  * @param  y1: First pixel (y)
  * @param  x2: Second pixel (x)
  * @param  y2: Second pixel (y)
  * @param  colour: 0 - off, !0 - on
  * @retval None
  */
void lcd_draw_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t colour)
{
	uint8_t x = x1, y = y1;
	uint8_t max_steps;
	uint8_t xsteps;
	uint8_t ysteps;
	uint8_t step;

	if (x1 > x2 || y1 > y2)
		return;

	xsteps = x2 - x1;
	ysteps = y2 - y1;
	max_steps = ysteps;
	if (xsteps > max_steps)
		max_steps = xsteps;

	xsteps = max_steps / xsteps;
	ysteps = max_steps / ysteps;

	for (step = 0; step <= max_steps; step++) {
		if (step > 0) {
			if (xsteps > 0 && step % xsteps == 0)
				x++;
			if (ysteps > 0 && step % ysteps == 0)
				y++;
		}
		lcd_set_pixel(x, y, colour);
	}
}

/**
  * @brief  Draw a rectangle.
  * @note	Top left is (0,0)
  * @param  x1,y1: 1st corner
  * @param  x2,y2: 2nd corner
  * @param  colour: 0 - off, !0 - on
  * @param  fill: Fill?
  * @retval None
  */
void lcd_draw_rect(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t colour, uint8_t flags)
{
	uint8_t x, y;

	if (x1 > x2 || y1 > y2)
		return;

	for (y = y1; y <= y2; ++y)
	{
		for (x = x1; x <= x2; ++x)
		{
			if ((flags & RECT_FILL) || y == y1 || y == y2 || x == x1 || x == x2)
			{
				if (flags & RECT_ROUNDED)
				{
					if (!( (x == x1 && y == y1) ||
						   (x == x2 && y == y1) ||
						   (x == x1 && y == y2) ||
						   (x == x2 && y == y2) ))
					{
						lcd_set_pixel(x, y, colour);
					}

				}
				else
					lcd_set_pixel(x, y, colour);
			}
		}
	}
}

/**
  * @brief  Draw a message with line wrapping
  * @note	Starts at the cursor and uses the x offset as a margin.
  * @param  msg: ASCII message to write
  * @param  colour: 0 - off, !0 - on
  * @retval None
  */
void lcd_draw_message(const char *msg, uint8_t colour)
{
	int width = (LCD_WIDTH - 2*cursor_x) / (char_width + 1);
	char line_buffer[LCD_WIDTH / 6];
	int nchars;
	const char *ptr = msg;
	const char *needle1 = ptr;
	const char *needle2;
	int x = cursor_x;

	// Iterate through the string to find wrap points.
	while (ptr < msg + strlen(msg))
	{
		needle2 = ptr;
		for (needle1 = ptr; needle2 != 0; )
		{
			needle2 = strchr(needle1, ' ');
			if (needle2)
			{
				if (needle2 - ptr < width)
				{
					needle1 = needle2 + 1;
				}
				else
					break;
			}
			else
			{
				if (msg + strlen(msg) - ptr < width)
				{
					needle1 = msg + strlen(msg);
				}
				break;
			}
		}

		// Draw the line.
		nchars = needle1 - ptr;
		// Discard the trailing space if present
		if (*(needle1-1) == ' ')
			nchars--;
		memcpy(line_buffer, ptr, nchars);
		line_buffer[nchars] = 0;
		cursor_x = x + (width - nchars) * (char_width+1) / 2;
		lcd_write_string(line_buffer, colour);
		cursor_y += (char_height + 1);

		ptr = needle1;
	}
}
