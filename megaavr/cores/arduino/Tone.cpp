/* Tone.cpp

  A Tone Generator Library

  Written by Brett Hagman

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  -------- ----------- -------- --------
  0001    B Hagman    09/08/02 Initial coding
  0002    B Hagman    09/08/18 Multiple pins
  0003    B Hagman    09/08/18 Moved initialization from constructor to begin()
  0004    B Hagman    09/09/26 Fixed problems with ATmega8
  0005    B Hagman    09/11/23 Scanned prescalars for best fit on 8 bit timers
                      09/11/25 Changed pin toggle method to XOR
                      09/11/25 Fixed timer0 from being excluded
  0006    D Mellis    09/12/29 Replaced objects with functions
  0007    M Sproul    10/08/29 Changed #ifdefs from cpu to register
  0008    S Kanemoto  12/06/22 Fixed for Leonardo by @maris_HY
  0009    J Reucker   15/04/10 Issue #292 Fixed problems with ATmega8 (thanks to Pete62)
  0010    jipp        15/04/13 added additional define check #2923
  0011    E Roy       13/02/18 ported to ATmega4809
  *************************************************/

#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include "Arduino.h"
#include "pins_arduino.h"

/* For more than one tone, change AVAILABLE_TONE_PINS and uncomment the correct
    number of timers
*/
#define AVAILABLE_TONE_PINS 1

#if defined(MILLIS_USE_TIMERB0) && defined(TCB1)
#define USE_TIMERB1
#else
#define USE_TIMERB0
#endif

static volatile TCB_t* _timer =
#if defined(USE_TIMERB0)
&TCB0;
#endif
#if defined(USE_TIMERB1)
&TCB1;
#endif

static int _pin = NOT_A_PIN;

// timerx_toggle_count:
//  > 0 - duration specified
//  = 0 - stopped
//  < 0 - infinitely (until stop() method called, or new play() called)

volatile long timer_toggle_count;
volatile uint8_t *timer_outtgl_reg;
volatile uint8_t timer_bit_mask;
uint8_t timer_cycle_per_tgl;
uint8_t timer_cycle_per_tgl_count;

// helper functions
static void disableTimer();

// frequency (in hertz) and duration (in milliseconds).
void tone(uint8_t pin, unsigned int frequency, unsigned long duration)
{
    long toggle_count = 0;
    uint32_t compare_val = 0;

    if (_pin != pin) {
        pinMode(pin, OUTPUT);
        _pin = pin;
    }

    // Get pin related stuff
    PORT_t *port = digitalPinToPortStruct(_pin);
    uint8_t *port_outtgl = (uint8_t *)&(port->OUTTGL);
    uint8_t bit_mask = digitalPinToBitMask(_pin);

    if (frequency == 0) {
        bit_mask = 0;
        frequency = 1;
    }

    // Calculate compare value
    uint8_t divisionfactor = 1; //no prescale, toggles at half the frequency

    compare_val = ((F_CPU / frequency)>>1);
    while ((compare_val > 0x10000)&&(divisionfactor<8))
    {
      compare_val = compare_val>>1;
      divisionfactor++;
    }
    if (--compare_val > 0xFFFF) {
      //if still too high, divisionfactor reached 8 (/256), corresponding to 1Hz
      compare_val=0xFFFF; //do the best we can.
    }

    // Calculate the toggle count
    if (duration > 0) {    // Duration defined
        toggle_count = 2 * frequency * duration / 1000;
    } else {            // Duration not defined -- tone until noTone() call
        toggle_count = -1;
    }

    // Timer settings -- will be type B
    uint8_t status = SREG;
    cli();

    // Disable for now, set clk to divide by 2 if divisionfactor 2 or more
    if(divisionfactor==1)
    {
      _timer->CTRLA = TCB_CLKSEL_CLKDIV1_gc;
    } else { //division factor between 2 and 8
      _timer->CTRLA = TCB_CLKSEL_CLKDIV2_gc;
      divisionfactor--; //now between 1 and 7
    }
    divisionfactor--; //now between 0 and 6

    // Timer to Periodic interrupt mode
    // This write will also disable any active PWM outputs
    _timer->CTRLB = TCB_CNTMODE_INT_gc;
    // Write compare register
    _timer->CCMP = compare_val;

    // Enable interrupt
    _timer->INTCTRL = TCB_CAPTEI_bm;

    timer_outtgl_reg = port_outtgl;
    timer_bit_mask = bit_mask;
    timer_toggle_count = toggle_count;
    timer_cycle_per_tgl = 1<<divisionfactor; //1, 2, 4, 8, 16, 32, or 64 - toggle pin this often
    timer_cycle_per_tgl_count=timer_cycle_per_tgl;
    // Enable timer
    _timer->CTRLA |= TCB_ENABLE_bm;

    SREG = status;
}

// pin which currently is being used for a tone
void noTone(uint8_t pin)
{
    if (pin == _pin) {
        timer_toggle_count = 0;
        //disableTimer();
        // Keep pin low after disabling of timer
        digitalWrite(_pin, LOW);
        _pin = NOT_A_PIN;
    }
}

// helper function for noTone()
/* Works for all timers -- the timer being disabled will go back to the
    configuration it had to output PWM for analogWrite() */
static void disableTimer()
{
    // Reinit back to producing PWM -- timer will be type B
    // Disable interrupt
    _timer->INTCTRL = 0;
    // Disable timer
    _timer->CTRLA = 0;

#if 0
    // RESTORE PWM FUNCTIONALITY:
    /* 8 bit PWM mode, but do not enable output yet, will do in analogWrite() */
    _timer->CTRLB = (TCB_CNTMODE_PWM8_gc);
    /* Assign 8-bit period */
    _timer->CCMPL = PWM_TIMER_PERIOD;
    /* default duty 50%, set when output enabled */
    _timer->CCMPH = PWM_TIMER_COMPARE;
    /* Use TCA clock (250kHz) and enable */
    /* (sync update commented out, might try to synchronize later */
    _timer->CTRLA = (TCB_CLKSEL_CLKTCA_gc) | (TCB_ENABLE_bm);
#endif
}


#if defined USE_TIMERB0
ISR(TCB0_INT_vect)
#elif defined USE_TIMERB1
ISR(TCB1_INT_vect)
#endif
{
  if(!(--timer_cycle_per_tgl_count)) //Are we ready to toggle? pre-decrement, then see if 0 or more
  {
    timer_cycle_per_tgl_count=timer_cycle_per_tgl;
    // toggle the pin
    *timer_outtgl_reg = timer_bit_mask; //toggle the pin

    if (timer_toggle_count > 0){  //if duration was specified, decrement toggle count.
        timer_toggle_count--;
    } else if (timer_toggle_count == 0) {    // If toggle count = 0, stop
        disableTimer();
    } //otherwise timer_toggle_count wasn't supplied, go on until noTone() called
  }
  /* Clear flag */
  _timer->INTFLAGS = TCB_CAPT_bm;
}
