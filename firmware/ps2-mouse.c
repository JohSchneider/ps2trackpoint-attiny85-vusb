#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/delay.h>

#include "ps2-mouse.h"

#define GLUE(a, b)     a##b
#define PORT(x)        GLUE(PORT, x)
#define PIN(x)         GLUE(PIN, x)
#define DDR(x)         GLUE(DDR, x)


// ===========================================================================
// PS/2 port
// ===========================================================================
/* usefull links:
 * https://wiki.osdev.org/PS/2_Mouse
 * http://www-ug.eecg.utoronto.ca/desl/nios_devices_SoC/datasheets/PS2%20Mouse%20Protocol.htm
 * https://www.burtonsys.com/ps2_chapweske.htm
 */

// receive buffer size
#define  PS2_BUF_SIZE 256

// The pin to which the clock signal is connected. PS/2
#define  PS2_CLK_PORT B
#define  PS2_CLK_PIN 2
// The pin to which the PS/2 data signal is connected
#define  PS2_DATA_PORT B
#define  PS2_DATA_PIN 0


#define  ps2_data() (PIN(PS2_DATA_PORT) & _BV( PS2_DATA_PIN ))

// https://wiki.osdev.org/PS/2_Mouse
enum PS2Commands {
RESET                    = 0XFF,
RESEND                   = 0XFE,
SET_DEFAULTS             = 0XF6,
DISABLE_DATA_REPORTING   = 0XF5,
ENABLE_DATA_REPORTING    = 0XF4,
SET_SAMPLE_RATE          = 0XF3,
GET_DEVICE_ID            = 0XF2,
SET_REMOTE_MODE          = 0XF0,
SET_WRAP_MODE            = 0XEE,
RESET_WRAP_MODE          = 0XEC,
READ_DATA                = 0XEB,
SET_STREAM_MODE          = 0XEA,
STATUS_REQUEST           = 0XE9,
SET_RESOLUTION           = 0XE8,
SET_SCALING              = 0XE6
};

enum ps_state_t {
    ps2_state_error,
    ps2_state_read,
    ps2_state_write
};

volatile  uint8_t ps2_state; // port state (ps_state_t)
volatile  uint8_t ps2_bitcount; // handler bit counter
volatile  uint8_t ps2_data; // buffer per byte
volatile  uint8_t ps2_parity;
volatile  uint8_t ps2_rx_buf[PS2_BUF_SIZE]; // PS/2 port receive buffer
volatile  uint8_t ps2_rx_buf_w;
volatile  uint8_t ps2_rx_buf_r;
volatile  uint8_t ps2_rx_buf_count;
volatile  uint8_t ps2_error_count;

// ----------------------------------------------------------------------------
// Store the received byte in the PS/2 port's receive buffer. Called only from an interrupt handler.

void  ps2_rx_push( uint8_t c) {
    // If the buffer is full and a byte is lost, then the program will not be able to correctly
    // decrypt all further packets, so restart the controller.
    if (ps2_rx_buf_count >= sizeof(ps2_rx_buf)) {
        ps2_state = ps2_state_error;
        return ;
    }
    // Save to buffer
    ps2_rx_buf[ps2_rx_buf_w] = c;
    ps2_rx_buf_count++;
    if (++ps2_rx_buf_w == sizeof(ps2_rx_buf)) {
        ps2_rx_buf_w = 0 ;
    }
}

// ----------------------------------------------------------------------------
// Get a byte from the PS/2 port's receive buffer

uint8_t  ps2_aread( void ) {
    uint8_t d ;

    cli(); // Turn off interrupts, since the interrupt handler also modifies these variables.
    // If buffer is empty, return null
    if (ps2_rx_buf_count == 0 ) {
        d = 0;
    } else {
        // Read bytes from buffer
        d = ps2_rx_buf[ps2_rx_buf_r];
        ps2_rx_buf_count--;
        if (++ps2_rx_buf_r == sizeof (ps2_rx_buf)) {
            ps2_rx_buf_r = 0 ;
        }
    }

    sei(); // Enable interrupts
    return d;
}


// ----------------------------------------------------------------------------
// Calculate parity bit
uint8_t  parity( uint8_t p ) {
    p = p^(p >> 4 | p << 4 );
    p = p^(p >> 2 );
    p = p^(p >> 1 );
    return (p^ 1 ) & 1 ;
}

// ----------------------------------------------------------------------------
// Change PS/2 Clock
ISR(INT0_vect) {
    if (ps2_state == ps2_state_error) {
        // MSG("e");
        return ;
    }
    // MSG("?");
    if (ps2_state == ps2_state_write) {
        switch (ps2_bitcount) {
        default : // Data
            // if ((ps2_data & 1) ^ 1) {
            if (ps2_data & 1 ) {
                DDR(PS2_DATA_PORT) &=~ _BV(PS2_DATA_PIN);
                // MSG("d1");
            } else {
                DDR(PS2_DATA_PORT) |= _BV(PS2_DATA_PIN);
                // MSG("d0");
            }
            ps2_data >>= 1 ;
            break ;
        case  3 : // Parity bit
            // if (ps2_parity ^ 1) {
            // DDR(PS2_DATA_PORT) |= _BV(PS2_DATA_PIN);
            // } else {
            // DDR(PS2_DATA_PORT) &= ~_BV(PS2_DATA_PIN);
            // }
            if (ps2_parity) {
                DDR(PS2_DATA_PORT) &=~ _BV(PS2_DATA_PIN);
            } else {
                DDR(PS2_DATA_PORT) |= _BV(PS2_DATA_PIN);
            }
            break ;
        case  2 : // stop bit
            DDR(PS2_DATA_PORT) &=~ _BV(PS2_DATA_PIN);
            break ;
        case  1 : // Acknowledgment
            if ( ps2_data()) {
                ps2_state = ps2_state_error;
            } else {
                ps2_state = ps2_state_read;
            }
            ps2_bitcount = 12 ;
            break ;

        }
    } else {
        switch (ps2_bitcount) {
        case  11 : // start bit
            if ( ps2_data()) {
                ps2_state = ps2_state_error;
            }
            break ;
        default : // Data
            ps2_data >>= 1;
            if ( ps2_data()) {
                ps2_data |= 0x80 ;
            }
            break ;
        case  2 : // Parity bit
            if ( parity (ps2_data) != ( ps2_data() != 0 )) {
                ps2_state = ps2_state_error;
            }
            break ;
        case  1 : // stop bit
            if ( ps2_data()) {
                ps2_rx_push (ps2_data);
            } else {
                ps2_state = ps2_state_error;
            }
            ps2_bitcount = 12 ;
        }
    }
    ps2_bitcount--;
}

// ----------------------------------------------------------------------------
// Initialize PS/2

void  ps2_init ( void ) {

    // Switch PS/2 port to receive
    DDR(PS2_CLK_PORT) &= ~_BV(PS2_CLK_PIN);
    DDR(PS2_DATA_PORT) &= ~_BV(PS2_DATA_PIN);

    // Clear the receive buffer
    ps2_rx_buf_w = 0 ;
    ps2_rx_buf_r = 0 ;
    ps2_rx_buf_count = 0 ;

    // Set interrupt handler variables
    ps2_state = ps2_state_read;
    ps2_bitcount = 11 ;

    // Enable external Interrupt on PS/2 Clock pin
    GIFR |= _BV(INTF0);
    GIMSK |= _BV(INT0);
    MCUCR |= _BV(ISC01); // INT0 intterupt on falling edge
}

// ----------------------------------------------------------------------------
// Send a byte to the PS/2 port without acknowledgment
void  ps2_write ( uint8_t a) {
    // Disable PS/2 Clock Interrupt
    GIFR |= _BV(INTF0);
    GIMSK &= ~_BV(INT0);

    // Short the PS/2 clock signal to ground
    PORT(PS2_CLK_PORT) &= ~_BV(PS2_CLK_PIN);
    DDR(PS2_CLK_PORT) |= _BV(PS2_CLK_PIN);

    // wait for 100 µs
    _delay_us( 100 );

    // Shorting the PS/2 data line to ground
    PORT(PS2_DATA_PORT) &= ~_BV(PS2_DATA_PIN);
    DDR(PS2_DATA_PORT) |= _BV(PS2_DATA_PIN);

    // Release the clock signal
    DDR(PS2_CLK_PORT) &= ~_BV(PS2_CLK_PIN);

    // Clear the receive buffer
    ps2_rx_buf_count = 0 ;
    ps2_rx_buf_w = 0 ;
    ps2_rx_buf_r = 0 ;

    // Set up interrupt handler variables
    ps2_state = ps2_state_write;
    ps2_bitcount = 11 ;
    ps2_data = a;
    ps2_parity = parity(a);

    // Enable PS/2 Clock Interrupt
    GIFR |= _BV(INTF0);
    GIMSK |= _BV(INT0);
    MCUCR |= _BV(ISC01); // trigger on falling edge
}

// ----------------------------------------------------------------------------
// Receive byte from PS/2 port with wait
uint8_t  ps2_recv( void ) {
    while (ps2_rx_buf_count == 0 );
    return  ps2_aread();
}

// ----------------------------------------------------------------------------
// Send a byte to the PS/2 port with an acknowledgment

void  ps2_send( uint8_t c) {
    ps2_write(c);
    if ( ps2_recv() != 0xFA ) {
        ps2_state = ps2_state_error;
    }
}

// ===========================================================================
// Initialize PS/2 Mouse
// ===========================================================================

// ----------------------------------------------------------------------------
// Initialize PS/2 Mouse

bool  ps2mouse_init(void) {
    // Send "Reset" command
    ps2_send( 0xFF );
    if ( ps2_recv() != 0xAA ) { //TODO: this blocks and either has the watchdog bit, or might starve the usbPoll -> device does not enumerate; only happens on faulte/non-connected ps2mouse though... so a non-issue?
        ps2_state = ps2_state_error;
        return false;
    }
    if ( ps2_recv() != 0x00 ) {
        ps2_state = ps2_state_error;
        return false;
    }

# if ENABLE_WHEEL
    // Turn on the wheel and side-by-side set 80 pps.
    ps2_send( 0xF3 ); // set sample rate
    ps2_send( 0xC8 ); // 200
    ps2_send( 0xF3 );
    ps2_send( 0x64 ); // 100
    ps2_send( 0xF3 );
    ps2_send( 0x50 ); // 80

    // Find out if the wheel turned on
    ps2_send( 0xF2 ); // get device id
    ps2mouse_wheel = ps2_recv();
# endif

    // Resolution 8 dots per mm
    ps2_send( 0xE8 );
    ps2_send( 0x03 );

    // Set the number of samples/sec
    ps2_send( 0xF3 );
    ps2_send(PS2_SAMPLES_PER_SEC);

    // Enable streaming mode.
    ps2_send( 0xF4 );

    return true;
}

// ----------------------------------------------------------------------------
// Process data received from the PS/2 port

bool  ps2mouse_process(void) {

# if ENABLE_WHEEL
    while (ps2_rx_buf_count >= ( 3 + (ps2mouse_wheel ? 1 : 0 ))) {
#else
    while (ps2_rx_buf_count >= 3 ) {
#endif
        // TODO: handle sign and overflow bits?
        //PS2-Byte1: Y overflow, X overflow, Y sign bit, X sign bit, Always 1, Middle Btn, Right Btn, Left Btn
        ps2mouse_b = ps2_aread() & 7 ; // ! Here are the older bits!

        ps2mouse_x += ( int8_t ) ps2_aread();
        ps2mouse_y -= ( int8_t ) ps2_aread();

# if ENABLE_WHEEL
        if (ps2mouse_wheel) {
            ps2mouse_z += ( int8_t ) ps2_aread();
        }
# endif
    }

    if (ps2_state == ps2_state_error) {
        ps2_error_count++;
        //if (ps2_error_count > 5 ) {
            //_delay_us( 100 );
            return ps2mouse_init();
        //}
        //TODO: add another level: do an MCU/watchdog reset?
    }
    return true;
}


/*
static  void (*jump_to_bootloader)( void ) = ( void *)0x1c00;

// ----------------------------------------------------------------------------

// eeprom uint8_t eeprom_ps2m_multiplier;

void  main ( void ) {
    uint8_t mb1 = 0 ;

    // Restore settings
    ps2m_multiplier = eeprom_read_byte (EEPROM_OFFSET_MULTIPLIER);
    if (ps2m_multiplier > 2 ) {
        ps2m_multiplier = 1 ;
    }

    // run
    init ();

    // Start PS/2 port
    ps2_init();

    // Start PS/2 mouse protocol
    ps2mouse_init();

    // Start RS232 port and COM mouse protocol
    rs232m_init();

    // wink after initialization
    flash_led();

    for (;;) {
        // read data from PS/2
        ps2mouse_process();


        // Send a packet to the computer, if the send buffer is empty, the mouse is on,
        // pressed buttons or mouse position changed
        if (rs232_enabled) {
            if (ps2mouse_b != mb1 || ps2mouse_x != 0 || ps2mouse_y != 0 || ps2mouse_z != 0 || rs232_reset) {
                // if (rs232_tx_buf_count == 0) {
                int8_t cx = ps2mouse_x < - 128 ? - 128 : (ps2mouse_x > 127 ? 127 : ps2mouse_x);
                ps2mouse_x -= cx;
                int8_t cy = ps2mouse_y < - 128 ? - 128 : (ps2mouse_y > 127 ? 127 : ps2mouse_y);
                ps2mouse_y -= cy;
                int8_t cz = ps2mouse_z < - 8 ? - 8 : (ps2mouse_z > 7 ? 7 : ps2mouse_z);
                ps2mouse_z -= cz;

                mb1=ps2mouse_b;
                rs232m_send (cx, cy, cz, ps2mouse_b);
                flash_led ();
            }
        } else {
            // if the mouse is disabled, then check the configuration commands from the transition to the bootloader
            // '?' -> 'M' command to determine device type
            // 'tsbl' command to switch to bootloader
            // else -> '!'

            // If the byte is ready
            if (UCSRA & _BV (RXC)) {
                static  uint8_t goto_bootloader_cnt = 0 ;
                char ch = UDR;
                switch (ch) {
                case  ' ? ' :
                    rs232_send ( ' M ' );
                goto_bootloader_cnt = 0 ;
                break ;
                case  ' t ' :
                    goto_bootloader_cnt = 1 ;
                    break ;
                case  ' s ' :
                    if (goto_bootloader_cnt == 1 ) {
                        goto_bootloader_cnt++;
                    } else {
                        goto_bootloader_cnt = 0 ;
                    }
                    break ;
                case  ' b ' :
                    if (goto_bootloader_cnt == 2 ) {
                        goto_bootloader_cnt++;
                    } else {
                        goto_bootloader_cnt = 0 ;
                    }
                    break ;
                case  ' l ' :
                    if (goto_bootloader_cnt == 3 ) {
                        goto_bootloader_cnt = 0 ;
                        jump_to_bootloader();
                    } else {
                        goto_bootloader_cnt = 0 ;
                    }
                    break ;
                default :
                    goto_bootloader_cnt = 0 ;
                    rs232_send ( ' ! ' );
                    break ;
                }
            }
        }


        // Handling buttons
        if (pressed_button != 0xFF ) {
            ps2mouse_multiplier = pressed_button;
            eeprom_update_byte (EEPROM_OFFSET_MULTIPLIER, ps2mouse_multiplier);
            // flash_led();
            pressed_button = 0xFF ;
        }

        // Mouse speed control directly from the mouse
        if (rs232m_protocol == PROTOCOL_MICROSOFT && (ps2mouse_b & 3 ) == 3 ) {
            if (ps2mouse_z < 0 ) {
                if (ps2mouse_multiplier > 0 ) {
                    ps2mouse_multiplier--;
                }
                ps2mouse_z = 0 ;
            } else  if (ps2mouse_z > 0 ) {
                if (ps2mouse_multiplier < 2 ) {
                    ps2mouse_multiplier++;
                }
                ps2mouse_z = 0 ;
            }
        }


        // Reboot in case of error
        if (ps2_state != ps2_state_error) {
            wdt_reset ();
        }


    }
}
//*/
