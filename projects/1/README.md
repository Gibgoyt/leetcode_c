# Project 1: Hello World (Blink) in Pure C

Welcome to your very first embedded systems project! In this project, we are ignoring the "Arduino" training wheels and writing **Pure C (Bare-Metal)** code using the official STMicroelectronics CMSIS headers.

This project will make an LED blink. This simple act proves:
1. Your toolchain (compiler) is working.
2. You successfully linked your code to the specific memory regions of the STM32F401RE chip.
3. Your computer can successfully talk to the board and flash code onto it.

---

## 1. Hardware Setup & Breadboard

### Powering the Board
1. Get a **Mini-USB cable** (Note: this is *Mini-USB*, not Micro-USB or USB-C. It's slightly chunkier).
2. Plug the Mini-USB end into the top port of your NUCLEO-F401RE board (the ST-LINK portion).
3. Plug the other end into your computer.
4. You should see a red `PWR` LED turn on, indicating the board has power. A large LED near the USB port (`COM`) might also flash red/green.

### Breadboard Setup (Optional but highly recommended)
The code we wrote targets **Pin PA5**. Conveniently, ST already connected `PA5` to a tiny **built-in Green LED (LD2)** on the board. So, if you just flash the code, you will see a built-in LED blink!

However, since you have a breadboard, let's wire up an external one to the exact same pin:
1. Grab a standard **LED** (any color) and a **Resistor** (anything from 220Ω to 1kΩ will work).
2. Place the LED on the breadboard. Remember, LEDs have a positive side (Anode, longer leg) and a negative side (Cathode, shorter leg).
3. Connect one side of the **resistor** to the **Cathode (shorter leg)** of the LED.
4. Connect the other side of the **resistor** to a **GND (Ground)** pin on the Nucleo board.
   * *Nucleo GND Pins can be found on the left header (CN7), pins 19 or 20, or right header (CN10), pin 9.*
5. Connect a wire from the **Anode (longer leg)** of the LED to **Pin PA5** on the Nucleo board.
   * *Pin PA5 is located on the right header (CN10), pin 11. It's labeled `D13` on the inner Arduino-style headers.*

---

## 2. Software Setup

To write code in pure C, you need two main tools:
1. **The Compiler:** To translate C into ARM machine code.
2. **The Flasher:** To push that machine code over the USB cable onto the microcontroller's flash memory.

### Installing the Tools (Linux / Ubuntu / Debian)
Open your terminal and run:
```bash
sudo apt update
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi make stlink-tools
```
* `gcc-arm-none-eabi`: The C compiler for ARM Cortex-M processors.
* `make`: A tool to run our compilation scripts.
* `stlink-tools`: Provides the `st-flash` command to program the board.

*(If you are on macOS, you can use Homebrew: `brew install arm-none-eabi-gcc stlink make`)*

---

## 3. How to Compile and Flash

Now that your tools are installed and your board is plugged in, open your terminal, navigate to this project folder (`projects/1`), and run:

### Step 1: Compile the Code
```bash
make
```
This command reads the `Makefile`, grabs your C code (`src/main.c`), the ST startup code, and the ST device headers, and compiles them. 
If successful, you will see a new `build/` directory containing a file named `blink.bin`.

### Step 2: Send the Code to the Board
Ensure your board is plugged into your computer, then run:
```bash
make flash
```
You will see output indicating that `st-flash` is erasing the memory and writing your new `blink.bin` file to address `0x08000000`.

**Look at your board!** The built-in green LED (and your breadboard LED, if you wired it up) should now be blinking steadily!

---

## What is actually happening in the code?
Take a look inside `src/main.c`. 
Unlike your computer, microcontrollers keep most of their internal hardware turned OFF by default to save power. 
1. We first turn on the "clock" for GPIO Port A (which powers up the pins we want to use).
2. We configure Pin A5 as an Output.
3. We enter an infinite loop (`while(1)`) where we tell the pin to turn on (Go HIGH), wait, and turn off (Go LOW).
