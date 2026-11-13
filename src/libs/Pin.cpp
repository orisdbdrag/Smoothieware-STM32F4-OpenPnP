/*
 * HUMAN-READABLE HARDWARE PIN PARSING OPTIMIZATION (STM32 ENHANCEMENT)
 * This module replaces the legacy LPC1769 numeric pin mapping syntax (e.g., "1.18", "0.4")
 * with intuitive, human-readable STM32 port nomenclature (e.g., "PA12", "PB.5").
 * It features a fast string-to-peripheral pre-parser that decodes port letters (PA-PI)
 * directly into GPIO base memory register pointers, maps hardware modifiers (!, o, ^, v, -, @),
 * and provides low-level register configuration for Open-Drain, Pull-Up, and Pull-Down modes.
 */


#include "Pin.h"
#include "utils.h"

// mbed libraries for hardware pwm
#include "PwmOut.h"
#include "InterruptIn.h"
#include "PinNames.h"
#include "PeripheralPins.h"
#include "port_api.h"

extern "C" uint32_t Set_GPIO_Clock(uint32_t);

static GPIO_TypeDef* const gpios[] = {GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG, GPIOH, GPIOI};

Pin::Pin(){
    this->inverting= false;
    this->valid= false;
    this->pin= 16;
    this->port= nullptr;
}

// Helper function to parse port prefix (PA, PB, PC...) or a digital index
static int parse_port_prefix(const char* cs, const char** next_cs) {
    // Check if the string starts with the letter 'P' or 'p'
    if (cs[0] == 'P' || cs[0] == 'p') {
        char port_letter = cs[1];
        // Convert to uppercase
        if (port_letter >= 'a' && port_letter <= 'z') {
            port_letter -= 32;
        }
        // Calculate port index: 'A' -> 0, 'B' -> 1 ... 'I' -> 8
        if (port_letter >= 'A' && port_letter <= 'I') {
            *next_cs = cs + 2; // Move pointer past the "PA" letters
            return port_letter - 'A';
        }
    }

    // If there is no letter prefix, try to read the legacy digital index
    char* cn = NULL;
    int port_num = strtol(cs, &cn, 10);
    if (cn > cs) {
        *next_cs = cn;
        return port_num;
    }

    *next_cs = cs;
    return -1; // Port not recognized
}

// Make a new pin object from a string
Pin* Pin::from_string(std::string value){
    if(value == "nc") {
        this->valid= false;
        return this; // optimize the nc case
    }

    const char* cs = value.c_str();
    const char* next_cs = NULL;
    valid = true;

    // Try to parse port as a letter (PA) or a number (0)
    this->port_number = parse_port_prefix(cs, &next_cs);
    cs = next_cs;

    if (port_number < (sizeof(gpios)/sizeof(gpios[0]))){

        Set_GPIO_Clock(port_number); // enable clock domain

        this->port = gpios[(unsigned int) this->port_number];

        // Optionally skip the dot if the user wrote "PA.12" instead of "PA12"
        if (*cs == '.') {
            cs++;
        }

        // Read pin index
        char* cn = NULL;
        this->pin = strtol(cs, &cn, 10);

        if ((cn > cs) && (pin < 16)){
            // Parse modifiers (!, o, ^, v, -, @)
            for (;*cn;cn++) {
                switch(*cn) {
                    case '!':
                        this->inverting = true;
                        break;
                    case 'o':
                        as_open_drain();
                        break;
                    case '^':
                        pull_up();
                        break;
                    case 'v':
                        pull_down();
                        break;
                    case '-':
                        pull_none();
                        break;
                    case '@':
                        as_repeater();
                        break;
                    default:
                        if (!is_whitespace(*cn))
                            return this;
                }
            }
            return this;
        }
    }

    // from_string failed
    valid= false;
    port_number = 0;
    port = gpios[0];
    pin = 16;
    inverting = false;
    return this;
}


Pin* Pin::from_string_no_init(std::string value){
    if(value == "nc") {
        this->valid= false;
        return this; // optimize the nc case
    }

    const char* cs = value.c_str();
    const char* next_cs = NULL;
    valid = true;

    this->port_number = parse_port_prefix(cs, &next_cs);
    cs = next_cs;

    if (port_number < (sizeof(gpios)/sizeof(gpios[0]))){
        this->port = gpios[(unsigned int) this->port_number];

        if (*cs == '.') {
            cs++;
        }

        char* cn = NULL;
        this->pin = strtol(cs, &cn, 10);

        if ((cn > cs) && (pin < 16)){
            for (;*cn;cn++) {
                switch(*cn) {
                    case '!':
                        this->inverting = true;
                        break;
                    default:
                        if (!is_whitespace(*cn))
                            return this;
                }
            }
            return this;
        }
    }

    valid= false;
    port_number = 0;
    port = gpios[0];
    pin = 16;
    inverting = false;
    return this;
}

// Configure this pin as OD
Pin* Pin::as_open_drain(){
    if (!this->valid) return this;
    gpios[this->port_number]->OTYPER |= (1 << this->pin);
    pull_none();
    return this;
}

// Configure this pin as a repeater
Pin* Pin::as_repeater(){
    if (!this->valid) return this;
    return this;
}

// Configure this pin as no pullup or pulldown
Pin* Pin::pull_none(){
    if (!this->valid) return this;
    gpios[this->port_number]->PUPDR &= ~(0x3 << (2*this->pin));
	return this;
}

// Configure this pin as a pullup
Pin* Pin::pull_up(){
    if (!this->valid) return this;
    gpios[this->port_number]->PUPDR = (gpios[this->port_number]->PUPDR & ~(0x3 << (2*this->pin))) | (0x1 << (2*this->pin));
    return this;
}

// Configure this pin as a pulldown
Pin* Pin::pull_down(){
    if (!this->valid) return this;
    gpios[this->port_number]->PUPDR = (gpios[this->port_number]->PUPDR & ~(0x3 << (2*this->pin))) | (0x2 << (2*this->pin));
    return this;
}

// If available on this pin, return mbed hardware pwm class for this pin
mbed::PwmOut* Pin::hardware_pwm()
{
    PinName pin = port_pin((PortName)this->port_number, this->pin);
    if (pinmap_find_peripheral(pin, PinMap_PWM) != (uint32_t)NC)
        return new mbed::PwmOut(pin);
    return nullptr;
}

mbed::InterruptIn* Pin::interrupt_pin()
{
    if(!this->valid) return nullptr;
    as_input();
    PinName pinname = port_pin((PortName)port_number, pin);
    return new mbed::InterruptIn(pinname);
}
