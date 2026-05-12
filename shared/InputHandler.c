#include "InputHandler.h"
#include "main.h"

// Global input state
InputState current_input = {0};

// Track button presses in interrupt
static volatile uint8_t btn2_raw_press = 0;
static volatile uint8_t btn3_raw_press = 0;
static volatile uint8_t btn4_raw_press = 0;  // 新增：BTN4原始按键标记

void Input_Init(void) {
    // GPIO and EXTI already initialized by MX_GPIO_Init() in main.c
    // Just reset the state
    current_input.btn2_pressed = 0;
    current_input.btn3_pressed = 0;
    current_input.btn4_pressed = 0;  // 新增：初始化BTN4状态
    btn2_raw_press = 0;
    btn3_raw_press = 0;
    btn4_raw_press = 0;  // 新增：初始化BTN4原始标记
}

void Input_Read(void) {
    // Copy the button press flags from interrupt to current input state
    // This is read once per frame by the main loop
    current_input.btn2_pressed = btn2_raw_press;
    current_input.btn3_pressed = btn3_raw_press;
    current_input.btn4_pressed = btn4_raw_press;  // 新增：读取BTN4状态
    
    // Reset the flags after reading so they only trigger once
    btn2_raw_press = 0;
    btn3_raw_press = 0;
    btn4_raw_press = 0;  // 新增：重置BTN4标记
}

// ===== INTERRUPT CALLBACK FOR BUTTONS =====
// Called by hardware when button is pressed
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    static uint32_t last_btn2_interrupt = 0;
    static uint32_t last_btn3_interrupt = 0;
    static uint32_t last_btn4_interrupt = 0;  // 新增：BTN4消抖计时
    uint32_t current_time = HAL_GetTick();
    
    // Handle BT2
    if (GPIO_Pin == BTN2_Pin) {
        // Software debouncing (200ms)
        if ((current_time - last_btn2_interrupt) > 200) {
            last_btn2_interrupt = current_time;
            
            // Toggle LED to indicate button press
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
            
            // Set flag indicating button was pressed
            btn2_raw_press = 1;
        }
    }
    
    // Handle BT3 (joystick button)
    if (GPIO_Pin == BTN3_Pin) {
        // Software debouncing (200ms)
        if ((current_time - last_btn3_interrupt) > 200) {
            last_btn3_interrupt = current_time;
            
            // Toggle LED to indicate button press
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
            
            // Set flag indicating button was pressed
            btn3_raw_press = 1;
        }
    }

    if (GPIO_Pin == BTN4_Pin) {  // 注意：BTN4_Pin必须在main.h中定义！
        // Software debouncing (200ms)  和其他按键相同的消抖逻辑
        if ((current_time - last_btn4_interrupt) > 200) {
            last_btn4_interrupt = current_time;
            
            // Toggle LED to indicate button press（可选：保持和其他按键一致的LED反馈）
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
            
            // Set flag indicating button was pressed
            btn4_raw_press = 1;
        }
    }
}