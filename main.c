#include <stdint.h>
#include <string.h>
#include "stm32f7xx.h"

#if !defined(__SOFT_FP__) && defined(__ARM_FP)
  #warning "FPU is not initialized, but the project is compiling for an FPU. Please initialize the FPU before use."
#endif

// Define states for our sequence state machine
typedef enum {
    BLINK_LED2,  // Flash LED 2 on PB4 (5 times)
    BLINK_LED3,  // Flash LED 3 on PG7 (8 times)
    PAUSE_STATE  // 2-second pause before restarting
} SequenceState;

// Volatile variable to pass the halt state between the main loop and the ISR
volatile uint8_t exti_triggered = 0;
volatile uint8_t exti1_triggered = 0;

void simple_delay(uint32_t count) {
    while (count--) {
        __asm__("NOP");
    }
}

void LCD_DisplayString(char *str) {
//	while (*str) {
//		SomeCharFunction(*str++);
//	}
// Do nothing for now
}

// Only needs to convert up to 2 digits in length
char* toString(uint8_t num) {
	char str[2];

    str[0] = (char)('0' + num);
    str[1] = '\0';

	return str;
}

void TIM3_DelayMs(uint32_t ms) {
	if (ms == 0) return;

	TIM3->CNT = 0;
	TIM3->ARR = ms;
	TIM3->CR1 |= TIM_CR1_CEN;

	// Poll flag...
	while (!(TIM3->SR & TIM_SR_UIF));

	TIM2->SR &= ~TIM_SR_UIF; // Clear flag for next iteration
	TIM2->CR1 &= ~TIM_CR1_CEN; // Stop the timer
}

int main(void) {
    // ------------------------------------------------------------
    // 1. CLOCK ENABLES
    // ------------------------------------------------------------
    // Bit 8 = GPIOI, Bit 1 = GPIOB, Bit 6 = GPIOG, Bit 1 = GPIOA
    RCC->AHB1ENR |= (1UL << 8) | (1UL << 1) | (1UL << 6) | (1UL);

    // Bit 14 enables SYSCFG clock (for routing GPIO to EXTI)
    RCC->APB2ENR |= (1UL << 14);

    // Bit 0 enables TIM2 clock on the APB1 bus
    RCC->APB1ENR |= (1UL << 0);

    // Bit 1 enables TIM3 clock on the APB1 bus
    RCC->APB1ENR |= (1UL << 1);

    // ------------------------------------------------------------
    // 2. GPIO CONFIGURATION
    // ------------------------------------------------------------
    // LED 1 (PI3, D7) -> Output
    GPIOI->MODER  &= ~(3UL << (3 * 2));
    GPIOI->MODER  |=  (1UL << (3 * 2));
    GPIOI->OTYPER &= ~(1UL << 3);

    // Switch (PI2, D8) -> Input with Pull-up
    GPIOI->MODER  &= ~(3UL << (2 * 2));
    GPIOI->PUPDR  &= ~(3UL << (2 * 2));
    GPIOI->PUPDR  |=  (1UL << (2 * 2));

    // Switch (PI0, D10) -> Input with Pull-up
    GPIOI->MODER  &= ~(3UL << (2 * 0));
    GPIOI->PUPDR  &= ~(3UL << (2 * 0));
    GPIOI->PUPDR  |=  (1UL << (2 * 0));

    // LED 2 (PB4, D3) -> Output
    GPIOB->MODER  &= ~(3UL << (4 * 2));
    GPIOB->MODER  |=  (1UL << (4 * 2));
    GPIOB->OTYPER &= ~(1UL << 4);

    // LED 3 (PG7, D4) -> Output
    GPIOG->MODER  &= ~(3UL << (7 * 2));
    GPIOG->MODER  |=  (1UL << (7 * 2));
    GPIOG->OTYPER &= ~(1UL << 7);

    // LCD D4 (PB15, D11) -> Output
    GPIOB->MODER  &= ~(3UL << (15 * 2));
    GPIOB->MODER  |=  (1UL << (15 * 2));
    GPIOB->OTYPER &= ~(1UL << 15);
    GPIOB->OSPEEDR |= (0x3 << (15 * 2));

    // LCD D5 (PB14, D12) -> Output
    GPIOB->MODER  &= ~(3UL << (14 * 2));
    GPIOB->MODER  |=  (1UL << (14 * 2));
    GPIOB->OTYPER &= ~(1UL << 14);
    GPIOB->OSPEEDR |= (0x3 << (14 * 2));

    // LCD D6 (PB9, D14) -> Output
    GPIOB->MODER  &= ~(3UL << (9 * 2));
    GPIOB->MODER  |=  (1UL << (9 * 2));
    GPIOB->OTYPER &= ~(1UL << 9);
    GPIOB->OSPEEDR |= (0x3 << (9 * 2));

    // LCD D7 (PB8, D15) -> Output
    GPIOB->MODER  &= ~(3UL << (8 * 2));
    GPIOB->MODER  |=  (1UL << (8 * 2));
    GPIOB->OTYPER &= ~(1UL << 8);
    GPIOB->OSPEEDR |= (0x3 << (8 * 2));

    // LCD RS (PA8, D10) -> Output
    GPIOA->MODER  &= ~(3UL << (8 * 2));
    GPIOA->MODER  |=  (1UL << (8 * 2));
    GPIOA->OTYPER &= ~(1UL << 8);

    // LCD E (PA15, D9) -> Output
    GPIOA->MODER  &= ~(3UL << (15 * 2));
    GPIOA->MODER  |=  (1UL << (15 * 2));
    GPIOA->OTYPER &= ~(1UL << 15);

    // ------------------------------------------------------------
    // 3. EXTI INTERRUPT & PRIORITY CONFIGURATION (PI2)
    // ------------------------------------------------------------
    // Route Port I Pin 2 (D8) to EXTI2 (Bits [11:8] in EXTICR[0] gets 0x8)
    SYSCFG->EXTICR[0] &= ~(0xFUL << 8);
    SYSCFG->EXTICR[0] |=  (0x8UL << 8);

    EXTI->IMR  |= (1UL << 2);  // Unmask line 2
    EXTI->FTSR |= (1UL << 2);  // Fall on button press (GND)
    EXTI->RTSR &= ~(1UL << 2); // Disable rise trigger

    // EXTI2 is IRQ number 8 in the NVIC table.
    // NVIC->IP is a byte array indexed by IRQ number, EXTI2’s priority byte is IP[8]
    // Only upper 4 bits are implemented (16 levels)
    // lower number = higher priority
    NVIC->IP[8] |=  (5UL << 4);   	  // Assign Priority 5 to IRQ8

    // Enable IRQ8 (EXTI2); bit 8 of NVIC_ISER0
    NVIC->ISER[0] |= (1UL << 8);         //Enable IRQ 8

    // ------------------------------------------------------------
    // 4. EXTI INTERRUPT & PRIORITY CONFIGURATION (PI0)
    // ------------------------------------------------------------
    // Route Port I Pin 0 (D5) to EXTI0 (Bits [3:0] in EXTICR[0] gets 0x8)
    SYSCFG->EXTICR[0] &= ~(0xFUL << 0);
    SYSCFG->EXTICR[0] |=  (0x8UL << 0);

    EXTI->IMR  |= (1UL << 0);  // Unmask line 0
    EXTI->FTSR |= (1UL << 0);  // Fall on button press (GND)
    EXTI->RTSR &= ~(1UL << 0); // Disable rise trigger

    // EXTI0 is IRQ number 6 in the NVIC table.
    // NVIC->IP is a byte array indexed by IRQ number, EXTI0’s priority byte is IP[6]
    // Only upper 4 bits are implemented (16 levels)
    // lower number = higher priority
    NVIC->IP[6] =  (2UL << 4);   	  // Assign Priority 2 to IRQ6

    // Enable IRQ6 (EXTI0); bit 6 of NVIC_ISER0
    NVIC->ISER[0] |= (1UL << 6);         //Enable IRQ 6

    // ------------------------------------------------------------
    // 5. TIMER 2 CONFIGURATION (~250ms update window)
    // ------------------------------------------------------------
    TIM2->PSC = 15999;	//Assumes 16MHz timer clock
    TIM2->ARR = 250; 	//250ms
    TIM2->SR &= ~(1UL << 0);
    TIM2->CR1 |= (1UL << 0);

    // ------------------------------------------------------------
    // 5. TIMER 3 CONFIGURATION (~250ms update window)
    // ------------------------------------------------------------
    // Timer 3 is 16-bit!!

    TIM3->PSC = 15999;	// Assumes 16MHz timer clock
    TIM3->ARR = 0; 	// Clear to allow dynamic scaling
    TIM3->SR &= ~(1UL << 0);
    TIM3->EGR |= TIM_EGR_UG; // Forces an update event to latch Prescaler value into hw registers

    SequenceState current_state = BLINK_LED2;
    uint8_t toggle_count = 0;
    uint8_t pin_state = 0;

    while (1) {
        // ------------------------------------------------------------
        // SEQUENTIAL STATE MACHINE (Driven by TIM2 ticks)
        // ------------------------------------------------------------
        if (TIM2->SR & (1UL << 0)) {
            TIM2->SR &= ~(1UL << 0); // Clear timer update flag

            // If the priority interrupt is running, halt state transitions
            if (exti_triggered || exti1_triggered) {
                continue;
            }

            switch (current_state) {
                case BLINK_LED2:
                    if (toggle_count < 10) { // 5 blinks = 10 toggles
                    	char *st1 = "NOR -> GREEN -> ";
						char *st2 = toString(toggle_count / 2);

                    	LCD_DisplayString(strcat(st1, st2));

                        pin_state = !pin_state;
                        if (pin_state) GPIOB->BSRR = (1UL << 4);
                        else           GPIOB->BSRR = (1UL << (4 + 16));
                        toggle_count++;
                    } else {
                        GPIOB->BSRR = (1UL << (4 + 16));
                        toggle_count = 0;
                        pin_state = 0;
                        current_state = BLINK_LED3;
                    }
                    break;

                case BLINK_LED3:
                    if (toggle_count < 16) { // 8 blinks = 16 toggles
                    	char *st1 = "NOR -> RED -> ";
						char *st2 = toString(toggle_count / 2);

                    	LCD_DisplayString(strcat(st1, st2));

                        pin_state = !pin_state;
                        if (pin_state) GPIOG->BSRR = (1UL << 7);
                        else           GPIOG->BSRR = (1UL << (7 + 16));
                        toggle_count++;
                    } else {
                        GPIOG->BSRR = (1UL << (7 + 16));
                        toggle_count = 0;
                        pin_state = 0;
                        TIM2->ARR = 2000; // Switch to 2-second pause duration
                        current_state = PAUSE_STATE;
                    }
                    break;

                case PAUSE_STATE:
                    TIM2->ARR = 250; // Restore standard blink interval speed
                    current_state = BLINK_LED2;
                    break;
            }
        }
    }
}

// ------------------------------------------------------------
// 5. INTERRUPT SERVICE ROUTINE (ISR) FOR EXTI2
// ------------------------------------------------------------
void EXTI2_IRQHandler(void) {
    if (EXTI->PR & (1UL << 2)) {
        EXTI->PR = (1UL << 2); // Clear EXTI pending flag

        exti_triggered = 1; // Raise halt flag for the main background thread

        // Execute the emergency sequence: 3 blinks on LED 1 (PI3)
        // 3 complete blinks = 6 alternate toggles
        for (int i = 0; i < 6; i++) {
        	char *st1 = "INT2 -> YELLOW -> ";
			char *st2 = toString(i / 2);

        	LCD_DisplayString(strcat(st1, st2));

            if (i % 2 == 0) {
                GPIOI->BSRR = (1UL << 3);         // Turn LED 1 ON
            } else {
                GPIOI->BSRR = (1UL << (3 + 16));   // Turn LED 1 OFF
            }
            simple_delay(800000);
        }

	 EXTI->PR = (1UL << 2); // Clear EXTI pending flag
	 NVIC->ICPR[0] = (1UL << 8);   // Clear NVIC Pending bit for IRQ8
        exti_triggered = 0; // Drop halt flag, allowing LED 2/3 to resume instantly
    }
}

// ------------------------------------------------------------
// 6. INTERRUPT SERVICE ROUTINE (ISR) FOR EXTI0
// ------------------------------------------------------------
void EXTI0_IRQHandler(void) {
    if (EXTI->PR & (1UL << 0)) {
        EXTI->PR = (1UL << 0); // Clear EXTI pending flag

        exti1_triggered = 1; // Raise halt flag for the main background thread

        // Execute the emergency sequence: 5 blinks on LED 3 (PG7)
        // 5 complete blinks = 10 alternate toggles
        for (int i = 0; i < 10; i++) {
        	char *st1 = "INT2 -> RED -> ";
			char *st2 = toString(i / 2);

        	LCD_DisplayString(strcat(st1, st2));

            if (i % 2 == 0) {
            	GPIOG->BSRR = (1UL << 7);          // Turn LED 3 ON
            } else {
            	GPIOG->BSRR = (1UL << (7 + 16));   // Turn LED 3 OFF
            }
            simple_delay(800000);
        }

	 EXTI->PR = (1UL << 0); // Clear EXTI pending flag
	 NVIC->ICPR[0] = (1UL << 6);   // Clear NVIC Pending bit for IRQ6
        exti1_triggered = 0; // Drop halt flag, allowing LED 2/3 to resume instantly
    }
}
