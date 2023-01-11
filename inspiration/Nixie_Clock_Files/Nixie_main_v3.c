/*
 * File:        Nixie_main.c
 * Author:      N. Brent Burns
 * Compiler:    XC8 v1.43
 * 
 * Started: February 25, 2018, 9:33 PM (while drunk)
 * 
 * Info:    This .C code is for a 12-Hour Nixie Tube Clock circuit.
 *          This PIC determines/controls/outputs the accurate 1Hz clock signal. 
 *          It also correctly determines and feeds time display values to the shift registers (logic-level). 
 * 
 *          Originally started with a 40-pin PIC18F4520
 *              but switched to a 14-pin PIC16F15325 to reduce overall pin count/size.
 * 
 *          Need an external crystal for final design (way more fucking accurate). 
 *          This code needs to produce a 44-bit number (to send out Sync. serially)
 *              to represent the time of a 12-hour clock (H1H0:M1M0:S1S0), details below. 
 *
 *          If you want a 24-hour clock, you have to rework the code and
 *              output a 45-bit number (to account for the additional digit of H1)
 * 
 *          Full details of this project (circuit/SCH/PCB/etc.) can be found at:
 *              "Projects" tab at http://crystal.uta.edu/~burns/
 * 
 *          This project was inspired by (straight up stolen from honestly):
 *              Threeneuron: https://threeneurons.files.wordpress.com/2015/08/hv_supply_kit.pdf
 *                  for the High Voltage Power Supply for my Nixie Circuit
 *              Dave Jones (EEVblog): https://www.youtube.com/watch?v=7uogKucrPks&list=PLvOlSehNtuHutdg1kZkG7aAYhjoJnk2fc
 *                  for the general design of driving (logically) the Nixe Tubes
 */

/*
 * DETAILED NOTES:  
 * 
 * Want a 12-hour clock format
 * 12hr x 60min x 60sec = 43,200 dec (use a single global var to store this value, unsigned int of 16 bits will do)
 * 44-bit binary format for shift registers (H1H0:M1M0:S1S0): 
 *            H1: 1,0   H0: 2,1-9,0,1   M1: 0-5   M0: 0-9   S1: 0-5   S0: 0-9           (decimal digits)
 *              H1           H0           M1        M0        S1        S0
 *      (MSB)   xx       xxxxxxxxxx    xxxxxx   xxxxxxxxxx  xxxxxx  xxxxxxxxxx   (LSB)  (take up this many binary bits)
 * 
 * Need to serially shift out this 44-bit number 8-bits at a time
 * 
 * One-way communication PIC-->Shift Registers
 * Use the sync. SPI module of the PIC18
 * 
 * PIC16F15325 Pins         TPIC6595 Shift Register Pins/Buses
 *  SCK (pin 10)---------------SRCK   (clock for shifting out serial (binary) data, 44 HIGH-to-LOW pulses, idle low, rising edge data out/in)
 *                                     max frequency: 50MHz (I think), aim much slower, but still much faster than 1Hz to beat the actual clock display
 *  SDO (pin 9)----------------SER_IN (actual binary data output, 44 bits)
 *  C2  (pin 8)----------------RCK    (single clock pulse (idle low, pulse HIGH-to-LOW) to latch in all shifted data at once)
 *                                     this RCK needs to happen every 1sec (EXACTLY)
 * 
 * PIC16F15325 Pins         Rotary Encoder Connections
 *  INT/RA2 (pin 11)---------------Push Button  (Normally pulled HIGH via resistor, pulsed LOW when user pressed the encoder's push button)
 *                                              (used as an External Interrupt to toggle through User Set Mode to adjust the clock's time)
 *  RC4 (pin 6)--------------------Channel A    (Ch. A and B used together to get direction (inc/dec) and adjust time when the user rotates the encoder)
 *  RC3 (pin 7)--------------------Channel B    (User must have previously pressed the Push Button for the rotation to adjust the time)
 *                                     
 * 
 * General structure (Free Running Mode):
 *      1. Calculate current time vars (H1H0:M1M0:S1S0)
 *      2. Encode each var into the correct binary format
 *      3. Combine into one/two/three binary numbers to send out serially
 *      4. Load binary number(s), start SRCK, binary number(s) are outputted to shift registers
 *      5. Stop SRCK when last bit goes out
 *      6. When 1sec (1Hz) timer interrupt goes off, pulse the RCK (latches the shift registers, Nixie tubes show new numbers)
 *      7. Increment the current time vars
 *      8. Go back to step 1
 * 
 * General structure (User Set Mode): 
 *      1. External INT pin interrupt connected to the Push Button switch of the Rotary Encoder
 *      2. This push action cycles through individually adjusting (inc/dec) the HOURS, MINS, and then SECS
 *      3. One more push puts the clock back into normal Free Running Mode, starting off wherever you adjusted to
 *      4. The rotary encoder increments/decrements individual digit segments and updates in real-time (as instant as possible)
 */

// PIC16F15325 Configuration Bit Settings
// 'C' source line config statements
#pragma config FEXTOSC = HS       // External Oscillator mode selection bits (HS (crystal oscillator) above 4MHz; PFM set to high power)
#pragma config RSTOSC = EXT1X     // Power-up default value for COSC bits (EXTOSC operating per FEXTOSC bits)
#pragma config CLKOUTEN = OFF     // Clock Out Enable bit (CLKOUT function is disabled; i/o or oscillator function on OSC2)
#pragma config CSWEN = ON         // Clock Switch Enable bit (Writing to NOSC and NDIV is allowed)
#pragma config FCMEN = OFF        // Fail-Safe Clock Monitor Enable bit (FSCM timer disabled)
#pragma config MCLRE = ON         // Master Clear Enable bit (MCLR pin is Master Clear function)
#pragma config PWRTE = OFF        // Power-up Timer Enable bit (PWRT disabled)
#pragma config LPBOREN = OFF      // Low-Power BOR enable bit (ULPBOR disabled)
#pragma config BOREN = OFF        // Brown-out reset enable bits (Brown-out reset disabled)
#pragma config BORV = LO          // Brown-out Reset Voltage Selection (Brown-out Reset Voltage (VBOR) set to 1.9V on LF, and 2.45V on F Devices)
#pragma config ZCD = OFF          // Zero-cross detect disable (Zero-cross detect circuit is disabled at POR.)
#pragma config PPS1WAY = OFF      // Peripheral Pin Select one-way control (The PPSLOCK bit can be set and cleared repeatedly by software)
#pragma config STVREN = OFF       // Stack Overflow/Underflow Reset Enable bit (Stack Overflow or Underflow will not cause a reset)
#pragma config WDTCPS = WDTCPS_31 // WDT Period Select bits (Divider ratio 1:65536; software control of WDTPS)
#pragma config WDTE = OFF         // WDT operating mode (WDT Disabled, SWDTEN is ignored)
#pragma config WDTCWS = WDTCWS_7  // WDT Window Select bits (window always open (100%); software control; keyed access not required)
#pragma config WDTCCS = SC        // WDT input clock selector (Software Control)
#pragma config BBSIZE = BB512     // Boot Block Size Selection bits (512 words boot block size)
#pragma config BBEN = OFF         // Boot Block Enable bit (Boot Block disabled)
#pragma config SAFEN = OFF        // SAF Enable bit (SAF disabled)
#pragma config WRTAPP = OFF       // Application Block Write Protection bit (Application Block not write protected)
#pragma config WRTB = OFF         // Boot Block Write Protection bit (Boot Block not write protected)
#pragma config WRTC = OFF         // Configuration Register Write Protection bit (Configuration Register not write protected)
#pragma config WRTSAF = OFF       // Storage Area Flash Write Protection bit (SAF not write protected)
#pragma config LVP = OFF          // Low Voltage Programming Enable bit (High Voltage on MCLR/Vpp must be used for programming)
#pragma config CP = OFF           // UserNVM Program memory code protection bit (UserNVM code protection disabled)

#include <xc.h>
#include <math.h>

#define _XTAL_FREQ 12288000 // Fosc = 12.288MHz (external crystal)

#define RCK_Latch_Signal    PORTCbits.RC2 // output IO pulse to latch out Shift Register data
#define Encoder_Ch_A        PORTCbits.RC4 // input from Channel A of the Encoder (RPG)
#define Encoder_Ch_B        PORTCbits.RC3 // input from Channel B of the Encoder (RPG)

#define MODE_FREE_RUNNING   0   // clock increments freely and normally
#define MODE_EDIT_HOURS     1   // user can use the encoder to select/adjust HOURS digits
#define MODE_EDIT_MINS      2   // user can use the encoder to select/adjust MINUTES digits
#define MODE_EDIT_SECS      3   // user can use the encoder to select/adjust SECONDS digits

unsigned char currentMode = 0;  // 4 different modes the clock can be in (incremented by the push button of the encoder)
unsigned int myTime = 0;        // 0 to 43,200 decimal (12hr x 60min x 60sec = 43,200)
unsigned char waitFlag = 0;     // set in the timer ISR, used in main() to prevent unnec. time calculations

// Function Prototypes
void interrupt ISR_High(void);
void sendDataOut(void);
void latchOutData(void);

void main(void) 
{
    unsigned char i = 0;
    unsigned char aState_curr = 0;
    unsigned char aState_prev = 0;
    unsigned char adjustSecs = 0;  // only valid values 0-59
    unsigned char adjustMins = 0;  // only valid values 0-59
    unsigned char adjustHours = 0; // only valid values 1-12, but remember 12 truly means 0
    
    ANSELA = 0; // all digital I/O (no analog inputs)
    ANSELC = 0; // all digital I/O (no analog inputs)
    
    // TRIS I/O Settings
    //      C5 = Don't Care (or output testing BLUE_LED)
    //      C4 = Input for Encoder Channel A
    //      C3 = Input for Encoder Channel B
    //      C2 = Output for RCK Data Latch Output Pulse to shift registers
    //      C1 = SPI SDO 44-bit serial output data (SER_IN) to shift registers
    //      C0 = SPI Clock for the SRCK serial clock to shift registers
    TRISC = 0b011000;
    TRISA = 0b111111; //All inputs, OSC pins, Ext. INT, etc.
    
    // Turn off everything in the beginning
    RCK_Latch_Signal = 0;
    PORTCbits.RC0 = 0; // SCK/SCL
    PORTCbits.RC1 = 0; // SDO
    
    // PPS Settings (Peripheral Pin Select) (mapping)
    // want SDO on Pin 9 (RC1) & SCK on Pin 10 (RC0)
    RC0PPS = 0x15;   // RC0->MSSP1:SCK1;
    RC1PPS = 0x16;   // RC1->MSSP1:SDO1;
    
    // SPI settings (to send data/clock to the shift registers)
    SSP1CON1bits.SSPEN  = 1; // Just turn ON and leave on
    SSP1STATbits.CKE    = 1; // IDLE state is LOW (0), data transmitted out on rising edge of SCK
    SSP1CON1bits.SSPM3  = 0; // 0010 SPI Clock = Fosc/64
    SSP1CON1bits.SSPM2  = 0; // 0010 SPI Clock = Fosc/64
    SSP1CON1bits.SSPM1  = 1; // 0010 SPI Clock = Fosc/64
    SSP1CON1bits.SSPM0  = 0; // 0010 SPI Clock = Fosc/64
    
    // Clear all the outputs of the shift registers (turn off all Nixies)
    for(i=0; i<6; i++)
    {
        SSP1BUF = 0b00000000; // 6 x 8 bits for all the shift register outputs
        while(SSP1STATbits.BF == 0);
    }
    
    // Latch everything out of the shift registers all at once
    latchOutData();
    
    // Timer settings (to generate the exact 1Hz Nixie clock latch signal)
    /**
     * d = 1.0sec
     * Fosc = 12.288MHz = 12,288,000 Hz
     * Fin = Fosc / 4 = 12.288MHz / 4 = 3.072MHz
     * 
     * Using 16-bit mode (0-65,535)
     * Use prescaler 1:64 and postscaler 1:1 to fit into 16-bit register (<65k)
     * 
     * 3,072,000 / 64 / 1 = 48,000
     * Preload = 65,536 - 48,000 = 17,536 = 0x4480
     */
    
    T0CON0bits.T0EN     = 0; // timer0 off for the moment
    T0CON0bits.T016BIT  = 1; // 16-bit timer0 mode
    T0CON0bits.T0OUTPS3 = 0; // 0000 1:1 postscaler (no postscaler)
    T0CON0bits.T0OUTPS2 = 0; // 0000 1:1 postscaler (no postscaler)
    T0CON0bits.T0OUTPS1 = 0; // 0000 1:1 postscaler (no postscaler)
    T0CON0bits.T0OUTPS0 = 0; // 0000 1:1 postscaler (no postscaler)
    
    T0CON1bits.T0CS2 = 0;   // 010 Timer0 clock source = Fosc/4
    T0CON1bits.T0CS1 = 1;   // 010 Timer0 clock source = Fosc/4
    T0CON1bits.T0CS0 = 0;   // 010 Timer0 clock source = Fosc/4
    T0CON1bits.T0ASYNC = 0; // sync. timer0 counter with Fosc/4 (why the fuck not)
    T0CON1bits.T0CKPS3 = 0; // 0110 1:64 prescaler
    T0CON1bits.T0CKPS2 = 1; // 0110 1:64 prescaler
    T0CON1bits.T0CKPS1 = 1; // 0110 1:64 prescaler
    T0CON1bits.T0CKPS0 = 0; // 0110 1:64 prescaler
    
    TMR0H = 0x44; // preload value = 0x4480 (LOAD HIGH FIRST!!!)
    TMR0L = 0x80; // preload value = 0x4480
    
    // Interrupt settings (1Hz timer0 interrupt, external switch interrupt)
    PIE0bits.TMR0IE = 1;    // enable timer0 interrupt
    INTCONbits.INTEDG = 1;  // INT rising edge triggered from PB Switch of the Encoder
    PIE0bits.INTE = 1;      // enable external INT interrupt
    PIR0bits.INTF = 0;
    INTCONbits.PEIE = 1;
    INTCONbits.GIE  = 1;
    
    T0CON0bits.T0EN = 1;   // start timer0

    // main routine while loop
    while(1)
    {
        while(currentMode == MODE_FREE_RUNNING)
        {
            if(waitFlag == 0) // Only do if ready, don't perform unnec. calculations
            {
                sendDataOut();
                waitFlag = 1;
            }
        }
        
        // User Set Mode: Adjust the Hours Digits of the Clock
        while(currentMode == MODE_EDIT_HOURS)
        {
            // Read the Encoder (might make it function-based later)
            aState_curr = PORTCbits.RC4; // Encoder Channel A output

            if(aState_curr != aState_prev) // Did a rotation occur?
            {
                adjustHours = myTime / 3600; // Starting point for Hours adjustment (find the current H1:H0)
                
                if(aState_curr != PORTCbits.RC3) // Compare Ch. A vs B, CW or CCW?, increment or decrement?
                {
                    adjustHours++;
                    
                    // weird case, "adjusting" a 0 to a 12 is done in sendDataOut()
                    if(adjustHours >= 12) // want the incrementing to be cyclical
                        adjustHours = 0;
                }
                else
                {
                    adjustHours--;
                    
                    // weird case again, if decrement lower than zero (rollover to high int value)
                    if(adjustHours >= 13) // want the decrementing to be cyclical
                        adjustHours = 11;
                }

                // could combine these next two lines together, but it's a little more clear this way
                myTime = myTime - ((myTime / 3600)*3600); // subtract all the possible hours from the overall time
                myTime = myTime + (adjustHours*3600);     // add back the new adjusted hours to the overall time
                
                sendDataOut();

                // Latch everything out of the shift registers all at once
                latchOutData();

                aState_prev = aState_curr;
            }
        }
        
        // User Set Mode: Adjust the Minutes Digits of the Clock
        while(currentMode == MODE_EDIT_MINS)
        {
            // Read the Encoder (might make it function-based later)
            aState_curr = PORTCbits.RC4; // Encoder Channel A output

            if(aState_curr != aState_prev) // Did a rotation occur?
            {
                adjustMins = (myTime / 60) % 60; // Starting point for Minutes adjustment  (find the current M1:M0)
                
                if(aState_curr != PORTCbits.RC3) // Compare Ch. A vs B, CW or CCW?, increment or decrement?
                {
                    adjustMins++;
                    
                    if(adjustMins >= 60) // want the incrementing to be cyclical
                        adjustMins = 0;
                }
                else
                {
                    adjustMins--;
                    
                    if(adjustMins >= 60) // want the decrementing to be cyclical
                        adjustMins = 59;
                }

                myTime = myTime - (((myTime / 60) % 60) * 60); // subtract all the possible minutes from the overall time
                myTime = myTime + (adjustMins*60);           // add back the new adjusted minutes to the overall time
                
                sendDataOut();

                // Latch everything out of the shift registers all at once
                latchOutData();

                aState_prev = aState_curr;
            }
        }
        
        // User Set Mode: Adjust the Seconds Digits of the Clock
        while(currentMode == MODE_EDIT_SECS)
        {
            // Read the Encoder (might make it function-based later)
            aState_curr = PORTCbits.RC4; // Encoder Channel A output

            if(aState_curr != aState_prev) // Did a rotation occur?
            {
                adjustSecs = myTime % 60; // Starting point for Seconds adjustment  (find the current S1:S0)
                
                if(aState_curr != PORTCbits.RC3) // Compare Ch. A vs B, CW or CCW?, increment or decrement?
                {
                    adjustSecs++;
                    
                    if(adjustSecs >= 60) // want the incrementing to be cyclical
                        adjustSecs = 0;
                }
                else
                {
                    adjustSecs--;
                    
                    if(adjustSecs >= 60) // want the decrementing to be cyclical
                        adjustSecs = 59;
                }

                myTime = myTime - (myTime % 60);    // subtract all the possible seconds from the overall time
                myTime = myTime + adjustSecs;       // add back the new adjusted seconds to the overall time
                
                sendDataOut();

                // Latch everything out of the shift registers all at once
                latchOutData();

                aState_prev = aState_curr;
            }
        }    
    } // end of while(1)
} // end of void main(void)



// HIGH Priority Interrupt Service Routine
void interrupt ISR_High(void)
{
    // Was the 1Hz TIMER0 interrupt triggered?
    if((PIE0bits.TMR0IE == 1) && (PIR0bits.TMR0IF == 1))
    {
        // reload the preload value immediately, CRUCIAL!!!
        TMR0H = 0x44; // preload value = 0x4480 (LOAD HIGH FIRST!!!)
        TMR0L = 0x80; // preload value = 0x4480
    
        // Latch everything out of the shift registers all at once
        latchOutData();
        
        myTime++;
        waitFlag = 0;
        PIR0bits.TMR0IF = 0;  // Clear flag
    }
    
    // Was the PB Switch of the Encoder pushed?
    if((PIE0bits.INTE == 1) && (PIR0bits.INTF == 1))
    {
//        __delay_ms(10);
        PIE0bits.TMR0IE = 0;   // temp. disable the timer0 interrupt
        T0CON0bits.T0EN = 0;   // pause/stop timer0
        PIR0bits.TMR0IF = 0;
        
        
        currentMode++;
        
        if(currentMode >= 4)
            currentMode = 0;

        PIR0bits.INTF = 0;
        
        if(currentMode == MODE_FREE_RUNNING) // in free-running mode, want timer to increment clock normally at 1Hz
        {
            // reload the preload value immediately, CRUCIAL!!!
            TMR0H = 0x44; // preload value = 0x4480 (LOAD HIGH FIRST!!!)
            TMR0L = 0x80; // preload value = 0x4480
            PIE0bits.TMR0IE = 1;    // re-enable timer0 interrupt
            T0CON0bits.T0EN = 1;    // restart timer0
        }  
    }
}

void sendDataOut(void)
{
    // Temp time calculation variables
    unsigned int hours = 0;
    unsigned int mins  = 0;
    unsigned int secs  = 0;
    unsigned int h1    = 0;
    unsigned int h0    = 0;
    unsigned int m1    = 0;
    unsigned int m0    = 0;
    unsigned int s1    = 0;
    unsigned int s0    = 0;

    // Vars for the final weird binary format for the shift registers 
    unsigned int bin_hour1 = 0;
    unsigned int bin_hour0 = 0;
    unsigned int bin_min1  = 0;
    unsigned int bin_min0  = 0;
    unsigned int bin_sec1  = 0;
    unsigned int bin_sec0  = 0;

    // Disable external INT when doing stuff below so INT0 user switch
    // doesn't interrupt during a crucial time
    PIE0bits.INTE = 0;
            
    // STEP 1
    // Split the myTime (total seconds) var into hours, minutes, seconds
    // and then split each once more to extract the individual digits

    if(myTime >= 43200) // 12 hours expressed in 43,200 seconds
        myTime = 0;     // reset the clock counter var

    // HH:MM:SS encoding (12-hour format, no AM/PM)
    // H1 H0 : M1 M0 : S1 S0    (more detailed)

    secs = myTime % 60;
    s1 = secs / 10; 
    s0 = secs % 10;

    mins = (myTime / 60) % 60;
    m1 = mins / 10;
    m0 = mins % 10;

    hours = myTime / 3600;

    if(hours == 0) //special case
        hours = 12;

    h1 = hours / 10;
    h0 = hours % 10;

    // STEP 2
    // Encode/format the above vars into the "odd" binary output for shift registers
    // For each individual binary number, only a single bit may be HIGH (1)

    // 44-bit binary format for shift registers (H1H0:M1M0:S1S0): 
    //      H1: 1,0   H0: 2,1-9,0,1   M1: 0-5   M0: 0-9   S1: 0-5   S0: 0-9
    //         H1           H0           M1        M0        S1        S0
    // (MSB)   xx       xxxxxxxxxx    xxxxxx   xxxxxxxxxx  xxxxxx  xxxxxxxxxx   (LSB)

    //                          H1H0    H0      M1 |M0    M0     S1  |S0    S0
    // More Detailed: (MSB) 0000xxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx  (LSB)
    // Have to send out in 8-bit chunks

    // Need to serially shift out this 44-bit number 8-bits at a time
    // Send out starting with the MSB side (hours side)

    // encode and send data out serially 
    // (but not displayed yet, that's in the Timer ISR which calls latchOutData() and pulses the RCK pin)
    bin_hour1 = (unsigned int)pow(2,h1); //0-bit of shiftReg is connected to Nixie "0", and logically so on...
    bin_hour0 = (unsigned int)pow(2,h0);
    bin_min1  = (unsigned int)pow(2,m1);
    bin_min0  = (unsigned int)pow(2,m0);
    bin_sec1  = (unsigned int)pow(2,s1);
    bin_sec0  = (unsigned int)pow(2,s0);

    // might need to wrap this stuff with all interrupts disabled to be safe
    SSP1BUF = 0b00001111 & (((bin_hour1<<2)&0b1100) | ((bin_hour0>>8)&0b11));
    while(SSP1STATbits.BF == 0);

    SSP1BUF = bin_hour0;
    while(SSP1STATbits.BF == 0);

    SSP1BUF = ((bin_min1<<2)&0b11111100) | ((bin_min0>>8)&0b11);
    while(SSP1STATbits.BF == 0);

    SSP1BUF = bin_min0;
    while(SSP1STATbits.BF == 0);

    SSP1BUF = ((bin_sec1<<2)&0b11111100) | ((bin_sec0>>8)&0b11);
    while(SSP1STATbits.BF == 0);

    SSP1BUF = bin_sec0;
    while(SSP1STATbits.BF == 0);
    
    // Re-Enable the external INT when waiting
    PIE0bits.INTE = 1;
}

void latchOutData(void)
{
    // Disable external INT when doing stuff below so INT0 user switch
    // doesn't interrupt during a crucial time
    PIE0bits.INTE = 0;
    
    // Pulse RCK, idle LOW then HIGH-LOW pulse
    // fool around with how fast I can do this
    // Latch everything out of the shift registers all at once
    RCK_Latch_Signal = 1;
    __delay_ms(10);
    RCK_Latch_Signal = 0;
    
    // Re-Enable the external INT when waiting
    PIE0bits.INTE = 1;
}

// end of Nixie_main.c (peace)

