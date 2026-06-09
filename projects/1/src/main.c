#include "stm32f4xx.h"

// A simple delay function that just spins the CPU.
// It's not perfectly timed, but good enough for a blink loop.
void delay(volatile uint32_t count) {
    while (count--) {
        // Do nothing
    }
}

int main(void) {
    // 1. Enable the clock for GPIO Port A
    // The RCC (Reset and Clock Control) manages the clocks.
    // AHB1ENR is the register that controls the clock for GPIO ports.
    // Bit 0 controls GPIOA.
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // 2. Configure PA5 as a General Purpose Output
    // The MODER register controls the mode of the pins.
    // Pin 5 mode is controlled by bits 10 and 11.
    // We want output mode, which is "01".
    // First, clear the bits for pin 5
    GPIOA->MODER &= ~GPIO_MODER_MODER5_Msk; 
    // Then set the correct mode
    GPIOA->MODER |= GPIO_MODER_MODER5_0;    

    // Endless loop where our embedded program lives forever
    while (1) {
        // 3. Turn the LED ON
        // BSRR is the Bit Set/Reset Register.
        // Writing to the lower 16 bits SETS the pin high.
        GPIOA->BSRR = GPIO_BSRR_BS_5;

        delay(10000000); // Wait

        // 4. Turn the LED OFF
        // Writing to the upper 16 bits (BS_5 shifted by 16) RESETS the pin low.
        GPIOA->BSRR = GPIO_BSRR_BR_5;

        delay(10000000); // Wait
    }

    return 0; // We will never actually reach here
}
