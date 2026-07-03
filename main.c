#include "cyhal.h"
#include "cybsp.h"
#include <stdio.h>
#include <string.h> 
#include <stdlib.h> 
#include <stdbool.h> 
#include <stdint.h> 

/* Global UART object */
cyhal_uart_t uart_obj;

/* ---- MATRIX KEYPAD PINS (HC-35S : KEY-A MODE) ---- */
cyhal_gpio_t row_pins[4] = { P9_0, P9_1, P9_2, P9_3 };
cyhal_gpio_t col_pins[4] = { P9_4, P9_5, P9_6, P9_7 };

/* Key layout (4x4) - Mapped to match your physical order */
char keymap[4][4] =
{
    // R0 (P9_0) maps to the physical bottom row
    {'*','0','#','D'}, 
    {'7','8','9','C'},  
    {'4','5','6','B'},  
    {'1','2','3','A'}   // R3 (P9_3) maps to the physical top row (1, 2, 3, A)
};

/* ---- GLOBAL STATE MANAGEMENT ---- */
#define SEQUENCE_WINDOW_SIZE 6
#define EXTENDED_ARRAY_SIZE 9 
#define HEX_DIGIT_COUNT 4 // Max digits for Hex input

// Sequence Quest State
int seq[EXTENDED_ARRAY_SIZE]; 
size_t input_count = 0;
bool is_sequence_valid = false;

// Hex-to-BCD State
uint16_t hex_value = 0;
size_t hex_input_count = 0; // Tracks number of digits entered
bool hex_input_in_progress = false; // New flag to track active input

// Application Mode State
int current_mode = 0; // 0: Menu, 1: Sequence Quest, 2: Hex-to-BCD


/* ----------- UART Helper Functions ----------- */

/**
 * @brief Prints a null-terminated string to UART.
 * @param str The string to print.
 */
void uart_print(const char *str)
{
    size_t len = strlen(str);
    if (len > 0)
    {
        cyhal_uart_write(&uart_obj, (void*)str, &len);
    }
}

// Custom function to print an integer to UART
void uart_print_int(int val) 
{
    // Safety buffer
    char buffer[20]; 
    sprintf(buffer, "%d", val);
    uart_print(buffer);
}

// Custom function to print an unsigned 16-bit integer in Hex format to UART
void uart_print_hex(uint16_t val) 
{
    // Safety buffer
    char buffer[8]; 
    sprintf(buffer, "0x%X", val);
    uart_print(buffer);
}

// Prints the main menu
void print_main_menu()
{
    uart_print("\r\n--- Main Menu ---\r\n");
    uart_print("Select Mode:\r\n");
    uart_print("1: Sequence Quest (Initial 6 numbers)\r\n");
    uart_print("2: Hex to BCD Conversion (Max 4 digits, press '#' to convert)\r\n");
    uart_print("Press '*' to return to this menu.\r\n");
}


/* ----------- Sequence Quest Functions (Mode 1 - Unchanged) ----------- */

void print_initial_prompt()
{
    uart_print("\r\n--- Sequence Generator (Mode 1) ---\r\n");
    uart_print("Enter initial 6 numbers of the sequence (0-9, '*' to reset):\r\n");
}

void reset_input_sequence()
{
    memset(seq, 0, sizeof(seq));
    input_count = 0;
    is_sequence_valid = false;
    print_initial_prompt();
}

void run_validation_and_generation()
{
    bool valid = true;

    // 1. Validate current 6-number sequence (P3, P4, P5)
    for (int i = 3; i < 6; i++) {
        if (seq[i] != seq[i - 2] + seq[i - 3]) {
            valid = false;
            break;
        }
    }

    if (valid) 
    {
        is_sequence_valid = true;
        
        // 2. Generate next three terms (P6, P7, P8)
        for (int i = 6; i < EXTENDED_ARRAY_SIZE; i++) {
            seq[i] = seq[i - 2] + seq[i - 3];
        }

        uart_print("\r\nResult: Valid\r\n");

        // Print generated numbers
        uart_print("Next three numbers in sequence: ");
        for (int i = 6; i < EXTENDED_ARRAY_SIZE; i++) {
            uart_print_int(seq[i]);
            uart_print(" ");
        }
        uart_print("\r\n");
        
        // 3. Prepare for next round (shift last 6 numbers into the new window)
        for (int i = 0; i < SEQUENCE_WINDOW_SIZE; i++) {
             seq[i] = seq[i + 3];
        }
        
        // 4. Print the newly set input sequence
        uart_print("\r\nNew input sequence automatically set as: ");
        for (int i = 0; i < SEQUENCE_WINDOW_SIZE; i++) {
            uart_print_int(seq[i]);
            uart_print(" ");
        }
        uart_print("\r\n");
        
        // 5. Print the prompt
        uart_print("Press '#' to generate the next step or '*' to reset.\r\n");
        
        cyhal_system_delay_ms(10); 

    } else {
        is_sequence_valid = false;
        uart_print("\r\nResult: Invalid\r\n");
        uart_print("Output: -1\r\n");
        uart_print("Press '*' to reset and re-enter sequence.\r\n");
    }
}


/* ----------- Hex-to-BCD Conversion Functions (Mode 2 - MODIFIED) ----------- */

void print_hex_prompt()
{
    uart_print("\r\n--- Hex to BCD Converter (Mode 2) ---\r\n");
    uart_print("Enter Hex digits (0-9, A-D). Max 4 digits. Press '#' to convert.\r\n");
    uart_print("Current Hex Input: ");
}

void reset_hex_conversion()
{
    hex_value = 0;
    hex_input_count = 0;
    hex_input_in_progress = false;
    print_hex_prompt();
}

void hex_to_bcd_conversion()
{
    if (hex_input_count == 0) {
        uart_print("\r\nNo Hex digit entered. Conversion aborted.\r\n");
        reset_hex_conversion();
        return;
    }
    
    uart_print("\r\n--- Conversion Result ---\r\n");
    uart_print("Input Digits: ");
    uart_print_int(hex_input_count);
    uart_print("\r\nHex Value: ");
    uart_print_hex(hex_value);
    uart_print("\r\nDecimal Value: ");
    uart_print_int(hex_value); // Printing the integer value automatically performs the conversion
    uart_print("\r\n");
    
    // Set up for next input
    reset_hex_conversion();
}

int key_to_hex_val(char key)
{
    if (key >= '0' && key <= '9') {
        return key - '0';
    } else if (key >= 'A' && key <= 'D') {
        // 'A' is 10, 'B' is 11, 'C' is 12, 'D' is 13
        return key - 'A' + 10;
    }
    return -1; // Invalid key
}

void handle_hex_input(char key)
{
    int val = key_to_hex_val(key);
    
    if (val != -1) // Valid Hex digit (0-9, A-D)
    {
        if (hex_input_count < HEX_DIGIT_COUNT)
        {
            // Shift the current value left by 4 bits and add the new digit
            hex_value = (hex_value << 4) | (uint16_t)val;
            hex_input_count++;
            hex_input_in_progress = true; // Mark input as started

            // Echo input
            char echo_buffer[2] = {key, '\0'};
            uart_print(echo_buffer);
            
            if (hex_input_count == HEX_DIGIT_COUNT)
            {
                // Max 4 digits reached
                uart_print(" (Max digits reached). Press '#' to convert.\r\n");
            }
        } else {
            uart_print("\r\nMax 4 digits reached. Press '#' to convert.\r\n");
        }
    }
    // MODIFIED: Conversion now runs if input is in progress, regardless of digit count
    else if (key == '#' && hex_input_in_progress)
    {
        hex_to_bcd_conversion();
    }
    else if (key == '#')
    {
        uart_print("\r\nNo digit entered yet. Enter a Hex digit (0-9, A-D).\r\n");
    }
}


/* ----------- Hardware Setup Functions (Unchanged) ----------- */

void uart_init()
{
    const cyhal_uart_cfg_t uart_config =
    {
        .data_bits = 8,
        .stop_bits = 1,
        .parity = CYHAL_UART_PARITY_NONE,
        .rx_buffer = NULL,
        .rx_buffer_size = 0
    };
    cyhal_uart_init(&uart_obj, CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                     NC, NC, NULL, &uart_config);
    cyhal_uart_set_baud(&uart_obj, 115200, NULL);
}

void keypad_init()
{
    for(int i=0;i<4;i++)
    {
        // Rows: Output, strong drive, initially LOW
        cyhal_gpio_init(row_pins[i], CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 0);
        
        // Columns: Input, PULL-DOWN. 
        cyhal_gpio_init(col_pins[i], CYHAL_GPIO_DIR_INPUT, CYHAL_GPIO_DRIVE_PULLDOWN, 0); 
    }
}

char keypad_get_key()
{
    for(int r=0; r<4; r++)
    {
        // 1. Set current row HIGH (drive/strobe the row)
        cyhal_gpio_write(row_pins[r], 1); 

        cyhal_system_delay_ms(2); 

        for(int c=0; c<4; c++)
        {
            // 2. Check for HIGH (1) on column 
            if(cyhal_gpio_read(col_pins[c]) == 1)
            {
                // Reset row pin before returning to stop the strobe
                cyhal_gpio_write(row_pins[r], 0); 
                return keymap[r][c];
            }
        }
        
        // 3. Reset current row LOW before moving to the next row
        cyhal_gpio_write(row_pins[r], 0);
    }
    return 0;
}

// Keypad Debounce
void wait_for_key_release(char key)
{
    // Wait until the currently pressed key is released
    while(keypad_get_key() != 0) 
    {
        cyhal_system_delay_ms(1);
    }
    // Simple software debounce delay
    cyhal_system_delay_ms(100); 
}


/* ----------- Main Application Loop ----------- */
int main(void)
{
    cybsp_init();
    uart_init();
    keypad_init();
    
    // Initial setup
    print_main_menu();
    current_mode = 0; // Start in Menu mode

    while(1)
    {
        char key = keypad_get_key();

        if(key != 0)
        {
            // --- Reset/Menu Key Handling (Highest Priority) ---
            if (key == '*')
            {
                current_mode = 0; // Return to Menu
                uart_print("\r\n*** Returning to Main Menu ***\r\n");
                print_main_menu();
            }
            // --- Mode Selection ---
            else if (current_mode == 0)
            {
                if (key == '1')
                {
                    current_mode = 1;
                    reset_input_sequence();
                }
                else if (key == '2')
                {
                    current_mode = 2;
                    reset_hex_conversion();
                }
                else
                {
                    uart_print("\r\nInvalid selection. Press 1 or 2.\r\n");
                }
            }
            // --- Sequence Quest Logic (Mode 1) ---
            else if (current_mode == 1)
            {
                if (key >= '0' && key <= '9')
                {
                    if (!is_sequence_valid) // Still entering initial 6
                    {
                        if (input_count < SEQUENCE_WINDOW_SIZE)
                        {
                            seq[input_count] = key - '0';
                            input_count++;
                            char echo_buffer[2] = {key, '\0'};
                            uart_print(echo_buffer);
                            uart_print(" ");

                            if (input_count == SEQUENCE_WINDOW_SIZE)
                            {
                                run_validation_and_generation(); 
                            }
                        }
                    }
                }
                else if (key == '#' && is_sequence_valid)
                {
                    run_validation_and_generation(); // Generate next step
                }
            }
            // --- Hex to BCD Logic (Mode 2) ---
            else if (current_mode == 2)
            {
                handle_hex_input(key);
            }

            wait_for_key_release(key);
        }
        
        cyhal_system_delay_ms(10); 
    }
}
