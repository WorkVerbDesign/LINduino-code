#include <MCP23017.h>
#include <Wire.h>

/* 10/30/2020
 * Main LIN bus program
 * 
 * Polls addresses on the LIN bus and then outputs through
 * mcp42100 digital POTS and mcp23017 port xpander
 *  
 * LIN bus protocol:
 * https://www.csselectronics.com/screen/page/lin-bus-protocol-intro-basics/language/en
 * https://hackaday.com/2017/05/26/embed-with-elliot-lin-is-for-hackers/
 * 
 * 
 * wheel buttons (first 3 bytes)
 * up down right left + - call endcall 
 * 0 talk back mute ok 0 0 0 
 * horn 0 shift+ shift- 0 1 1 1  
 * 
 * shifter buttons (2)
 * Reverse Neutral Drive Shift- Shift+ 0 0 0 (blank)
 * 
 * H shifter buttons bits (2)
 * 1 2 3 4 5 6 7 Reverse (blank)
 * 
 * pedal "buttons" bytes (8)
 * gas(low) gas(high) clutch(low) clutch(high) brake(low) brake(mid) brake(high) (blank)
 * 
 * ebrake "bwutton" (4)
 * ebrake(low) ebrake(mid) ebrake(high)(blank)
 * 
 * e46 light panel aka light "buttons" (4)
 * slider(low) slider(high) photo(0-255) auto off park on brightButton 0 0 0
 * 
 * no CRC yet, response is good may not be a need
 */

//================ ADDYS ===============
#define MCP23017_ADDR 0x27
MCP23017 mcp = MCP23017(MCP23017_ADDR);

//the address (without start and stop) is 6 bits with two parity bits
// start|LSB...MSB|PARITY|STOP
// x|012345|67|x
// s|lsbMsb|p0 p1|s
// parity are xor of:
// P0 = 0,1,2,4
// P1 = inverted 1,3,4,5
// addy table: http://ww1.microchip.com/downloads/en/appnotes/00002059b.pdf
// these need to be moved to address groups that reflect the size of the data returned.

const byte wheel_addy = 0x9C; // 0|001110|01|1    011100 28
const byte shift_addy = 0xDD; // 0|101110|11|1    011101 29
const byte pedal_addy = 0x5E; // 0|011110|10|1    011110 30
const byte shift8_addy = 0x1F; // 0|111110|00|1    011111 31
//const byte ebrake_addy = 0x20; // 0|000001|00|1   100000 32
//const byte light_addy = 0x61; // 0|100001|10|1 100001 33
//const byte lightCtrl_addy = 0xC5; // 0|110001|01|1 100011 35 from microship datasheet http://ww1.microchip.com/downloads/en/DeviceDoc/51714a.pdf
//const byte light_addy = 0x61; // 0|001111|00|1 111100 3C

//const byte shift_addy = 0x55; // 0|101010|10|1 to help timing alignment

//================ PINS ===============
#define SDAPIN 18
#define SCLPIN 19

#define POTCS 15
#define POTCL 14
#define POTSI 13

#define TXPIN 10
#define RXPIN 11

//#define EXTRA_1 
//#define EXTRA_2
//#define EXTRA_3 
//#define EXTRA_4

//#define PWM_1
//#define PWM_2
//#define PWM_3
//#define PWM_4

//to sync the scope
#define DIAGNOSTIC 6

//================ VAARS ===============
float timeSet = 0;
float timeOut = 0;

//arrays
uint8_t wheel[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
uint8_t shift[2] = {0, 0};
uint8_t pedal[7] = {0, 0, 0, 0, 0, 0, 0};
uint8_t shift8[2] = {0, 0};
uint8_t ebrake[3] = {0, 0, 0};
uint8_t light[4] = {0, 0, 0, 0};

//#positions in the arrrays
const uint8_t wheelSize = 9;
const uint8_t shiftSize = 2;
const uint8_t pedalSize = 7;
const uint8_t shift8Size = 2;
const uint8_t ebrakeSize = 3;
const uint8_t lightSize = 4;

//================ SETTINGS =============
#define UBIT 50 //uS per bit as a baseline
#define HALFBIT 11 //half of bit (tweaked)
#define BREAKOUT 12 //bit timer before no data = move on
#define BREAKBITS 14 //how many bits to acknowledge break
#define TURNWAIT 6 //miliseconds after bad addy (8 bit packet)
#define RISEFALL 3 //wait for signal to rise or fall when reading
#define PACKETDEL 4 //time between lin bus asks

//================ SETUP ===============
void setup() {
  //LIN pins
  pinMode(TXPIN, OUTPUT);
  pinMode(RXPIN, INPUT);
  
  digitalWrite(TXPIN, HIGH);
  
  //wire
  Wire.setSDA(SDAPIN);
  Wire.setSCL(SCLPIN); 
  Wire.begin();

  //serial
  Serial.begin(115200);
  Serial.println("Serial Start");


  //"spi"
  pinMode(POTCS, OUTPUT);
  pinMode(POTCL, OUTPUT);
  pinMode(POTSI, OUTPUT);

  digitalWrite(POTCS, HIGH);

  //diagnostic
  pinMode(DIAGNOSTIC, OUTPUT);

  //teensy actual pinouts
//  pinMode(EXTRA_1, OUTPUT);
//  pinMode(EXTRA_2, OUTPUT);
//  pinMode(EXTRA_3, OUTPUT);
//  pinMode(EXTRA_4, OUTPUT);

  //analog indicators
//  pinMode(PWM_1, OUTPUT);
//  pinMode(PWM_2, OUTPUT);
//  pinMode(PWM_3, OUTPUT);
//  pinMode(PWM_4, OUTPUT);
    
  //init port expander
  mcp.init();
  
  //set ports as output
  mcp.portMode(MCP23017Port::A, 0); 
  mcp.portMode(MCP23017Port::B, 0); 
  
  //reset ports
  mcp.writeRegister(MCP23017Register::GPIO_A, 0xFF);
  mcp.writeRegister(MCP23017Register::GPIO_B, 0xFF);

  //pots initial pos
  digiPot(0,128, 128);
  digiPot(1,128, 128);
}

//================ LOOP ===============
void loop() {
  //LIN
  busWheel();
  delay(PACKETDEL);  
  busShift();
  delay(PACKETDEL);
  busPedals();
  delay(PACKETDEL);
  busShift8();
//  delay(PACKETDEL);
//  busEbrake();
//  delay(PACKETDEL);
//  busLight();

  //OUTPUTS
  buttonOuts();
  potOuts();

  //DIAGNOSTIC
  serialPrints();

  //DELAY
  delay(10);
}
//================ FUNCTIONS ===============
//-----------------Transceiver Functions--------------
//gets wheel packets and puts in global wheel array
void busWheel(){
  callAddress(wheel_addy);

  for(int i = 0; i < wheelSize; i++){
    if(bitStart()){
    
      wheel[i] = getBits();
      
      //inter-data-byte timing
      delayMicroseconds(UBIT*2);
    }else{
      return;
    }
  }
  //checksum
  //Serial.println(checkSum(wheel_addy, wheel, wheelSize), HEX);
}

//gets shifter packets and puts in global shift array
void busShift(){
  callAddress(shift_addy);  
  
  for(int i = 0; i < shiftSize; i++){
    if(bitStart()){
    
      shift[i] = getBits();
      
      //inter-data-byte timing
      delayMicroseconds(UBIT*2);
    }else{
      return;
    }
  }
}

//gets pedal packets and puts in global pedal array
void busPedals(){
  callAddress(pedal_addy);

  digitalWrite(DIAGNOSTIC, HIGH);
  delayMicroseconds(50);
  digitalWrite(DIAGNOSTIC, LOW);
  
  for(int i = 0; i < pedalSize; i++){
    if(bitStart()){
    
      pedal[i] = getBits();
      
      //inter-data-byte timing
      delayMicroseconds(UBIT*2);
    }else{
      return;
    }
  }
}
//gets shifter packets and puts in global shift array
void busShift8(){
  callAddress(shift8_addy);  
  
  for(int i = 0; i < shiftSize; i++){
    if(bitStart()){
    
      shift[i] = getBits();
      
      //inter-data-byte timing
      delayMicroseconds(UBIT*2);
    }else{
      return;
    }
  }
}
//bussy
void busEbrake(){
  
}
//uwu
void busLight(){
  
}
//-------------Transmitter Functions----------

//general call to address as specified by LIN protocol
void callAddress(byte addy){
  //send break, wake up device
  breakSend(); 

  //sync
  delayMicroseconds(UBIT*2);
  bitsSend(0x55);

  //address send
  delayMicroseconds(UBIT);
  bitsSend(addy);

  //interbit timing and alignment
  delayMicroseconds(UBIT);
}

void breakSend(){
  digitalWrite(TXPIN, LOW);
  delayMicroseconds(UBIT * (BREAKBITS + 1));
  digitalWrite(TXPIN, HIGH);
}

void bitsSend(uint8_t stuff){
  //start bit
  digitalWrite(TXPIN, LOW);
  delayMicroseconds(UBIT);
  
  //address w/ parity (lsb first)
  for(int i = 0; i <= 7; i++){
    bool thing = bitRead(stuff, i);
    digitalWrite(TXPIN, thing);
    delayMicroseconds(UBIT);
  }

  //stop bit
  digitalWrite(TXPIN, HIGH);
  delayMicroseconds(UBIT);
}


//-------------Receive Functions---------------
bool bitStart(){
  timerStart(UBIT*BREAKOUT);
  while(digitalRead(RXPIN)){//stuck here for bits
    if(!timerCheck()){return 0;}
  }
  delayMicroseconds(HALFBIT); //we're now in the middle of the start bit
  return 1;
}

//use bitStart to initialize
uint8_t getBits(){
  uint8_t thingy = 0;
  
  for(int i = 0; i <=7; i++){
//    delayMicroseconds(UBIT-3);
//    digitalWrite(DIAGNOSTIC, HIGH);
//    delayMicroseconds(3);
//    digitalWrite(DIAGNOSTIC, LOW);

    delayMicroseconds(UBIT + RISEFALL); //additional stabalizing ubit tweak for micro speed
    bitWrite(thingy, i, digitalRead(RXPIN));
  }
  //delayMicroseconds(UBIT); //into stop bit

  return thingy;
}

//------------Timer--------------
void timerStart(int uSeconds){
  timeSet = micros();
  timeOut = uSeconds;
}

//1 is good 0 is expired
bool timerCheck(){
    if(micros() - timeSet > timeOut){
      return 0;
    }else{
      return 1;
    }
}

//------------Checksum--------------
uint8_t checkSum(uint8_t addr, uint8_t *datas, uint8_t entries) {
  uint16_t sum = addr; //& 0x3F;
  
  for (int i = 0; i < entries - 1; i++) {
    //sum ^= datas[i];
    sum += datas[i];
    if (sum >= 256){sum -= 255;}
  }

  //reverse the return
   return (~sum & 0xFF);
}

//-------------PIN OUTPUTS---------
//outputs wheel buttons
/*
 * wheel buttons (first 3 bytes)
 * up down right left + - call endcall 
 * 0 talk back mute ok 0 0 0 
 * horn 0 shift+ shift- 0 1 1 1  
 * 
 * shifter buttons (first bit)
 * Reverse Neutral Drive Shift- Shift+ 0 0 0
 * 
*/
void buttonOuts(){
  uint8_t packetA = 0xFF;
  uint8_t packetB = 0xFF;
  
  //A0 -> X12 Lower 1 | wheel+shift plus
  bitWrite(packetA, 0,!(bitRead(wheel[2], 2) || bitRead(shift[0], 4)));
  //A1 -> X12 Lower 2 | wheel+shift minus
  bitWrite(packetA, 1,!(bitRead(wheel[2], 3) || bitRead(shift[0], 3)));

  //A0 -> X12 Lower 1 | wheel plus
  //bitWrite(packetA, 0,!bitRead(wheel[2], 2));
  //A1 -> X12 Lower 2 | wheel minus
  //bitWrite(packetA, 1,!bitRead(wheel[2], 3));

  //A2 -> X12 Lower 3 | wheel up
  bitWrite(packetA, 2,!bitRead(wheel[0], 0));
  //A3 -> X12 Lower 4 | wheel down
  bitWrite(packetA, 3,!bitRead(wheel[0], 1));
  //A4 -> X12 Lower 5 | wheel right
  bitWrite(packetA, 4,!bitRead(wheel[0], 2));
  //A5 -> X12 Lower 6 | wheel left
  bitWrite(packetA, 5,!bitRead(wheel[0], 3));
  //A6 -> X12 Lower 7 | wheel ok
  bitWrite(packetA, 6,!bitRead(wheel[1], 4));
  
  //A7 -> NOT CONNECTED
  //bitWrite(packetA, 7,!bitRead(wheel[?], ?));
  bitWrite(packetA, 7, 1);


  //B0 -> X12 Upper 7 | wheel back
  bitWrite(packetB, 0,!bitRead(wheel[1], 2));
  //B1 -> X12 Upper 6 | shifter Rev
  //bitWrite(packetB, 1,!bitRead(shift[0], 0));
  //fix rev
  //bitWrite(packetB, 1,1);
  //opposite rev
  bitWrite(packetB, 1,bitRead(shift[0], 0));
  //B2 -> X12 Upper 5 | shifter Neutral
  bitWrite(packetB, 2,!bitRead(shift[0], 1));
  //B3 -> X12 Upper 4 | shifter Drive
  //bitWrite(packetB, 3,!bitRead(shift[0], 2));
  //drive is the new resting pos.
  bitWrite(packetB, 3, 1);
  //B4 -> X12 Upper 3 | shift minus
  //bitWrite(packetB, 4,!bitRead(shift[0], 3));  
  //B5 -> X12 Upper 2 | wheel vol+
  bitWrite(packetB, 5,!bitRead(wheel[0], 4));
  //B6 -> X12 Upper 1 | shift plus
  //bitWrite(packetB, 6,!bitRead(shift[0], 4));

  //cursed for some reason
  //B7 -> X11 Upper 1 ADC12_IN9 | wheel vol-
  bitWrite(packetB, 7,!bitRead(wheel[0], 5));
  
  mcp.writeRegister(MCP23017Register::GPIO_A, packetB);
  mcp.writeRegister(MCP23017Register::GPIO_B, packetA);

  //extra
  //digitalWrite(EXTRA_1, );
  //digitalWrite(EXTRA_2, );
  //digitalWrite(EXTRA_3, );
  //digitalWrite(EXTRA_4, );
}

//outputs pedal positions with digital pots
/*
 * pedal[7] = {gas, gas, clutch, clutch, brake, brake, checksum}
 */

void potOuts(){
  //pull stuff out of the array
  uint16_t gas = word(pedal[1], pedal[0]);
  uint16_t clutch = word(pedal[3], pedal[2]);
  uint16_t brake = word(pedal[5], pedal[4]);

  //notes
  //gas = 884 - 265
  //clutch = 1023 - 300
  //brake = 64800 - 59000

  //constrain
  gas = constrain(gas, 265, 884);
  brake = constrain(brake, 59000, 64800);
  clutch = constrain(clutch, 300, 1023);

  //scale
  uint8_t gas8 = map(gas, 265, 884, 0, 255);
  uint8_t brake8 = map(brake, 59000, 64800, 0, 255);
  uint8_t clutch8 = map(clutch, 300, 1023, 0, 255);

  //output output
  digiPot(0,gas8, clutch8);
  digiPot(1,brake8, 0);

    //write PWM to indicators
//  analogWrite(PWM_1, gas8);
//  analogWrite(PWM_2, clutch8);
//  analogWrite(PWM_3, brake8); 
//  analogWrite(PWM_4, ebrake8); 
}

//shift out to pots
void digiPot(bool reg, uint8_t val1, uint8_t val2){
  uint8_t CMD = 0x12;
  if(reg){
    CMD = 0x11;
  }
  
  //set CS pin to low to select the chip:
  digitalWrite(POTCS, LOW);
  
  // send the command and value via SPI:
  shiftOut(POTSI, POTCL, MSBFIRST, CMD);
  shiftOut(POTSI, POTCL, MSBFIRST, val1);

  shiftOut(POTSI, POTCL, MSBFIRST, CMD);
  shiftOut(POTSI, POTCL, MSBFIRST, val2);

  //execute the command:
  digitalWrite(POTCS, HIGH);
}

//-------------Serial prints-------------
void serialPrints(){
  //printWheel();
  //printShift();
  printPedal();
}

//prints out the wheel data, which as a lot of junk
//first 3 are data, last is CRC, rest is static
void printWheel(){
  Serial.print(wheel_addy, HEX);
//  
//  for(int i = 0; i < 3; i++){
//    Serial.print("|");
//    printBits(wheel[i]);
//  }

  for(int i = 0; i < 8; i++){
    Serial.print("|");
    Serial.print(wheel[i], HEX);
  }
  
  Serial.print("|");
  Serial.print(wheel[8], HEX);

  Serial.println();
}

//shifter is just 8 bits, 5 of which are doing anything
void printShift(){
  Serial.print(shift_addy, HEX);

  Serial.print("|");
  printBits(shift[0]);

  Serial.print("|");
  printBits(shift[1]);

  Serial.println();
}

//pedal is 16 bits per stored in an 8 bit array
void printPedal(){
  //Serial.print(pedal_addy, HEX);
  
  for(int i = 0; i < 6; i++){
    Serial.print("|");
    printBits(pedal[i]);
  }

  Serial.print("|");
  
  Serial.print(pedal[7], HEX);

  Serial.print("|");
  
  uint16_t gas = word(pedal[1], pedal[0]);
  uint16_t clutch = word(pedal[3], pedal[2]);
  uint16_t brake = word(pedal[5], pedal[4]);

  Serial.print(gas,DEC);
  Serial.print("|");
  Serial.print(clutch,DEC);
  Serial.print("|");
  Serial.print(brake, DEC);
  
  Serial.println();
}

//prints binary with leading 0s
void printBits(uint8_t beets){
  for(int j=7; j>=0; j--){
    Serial.print(bitRead(beets,j));
  }
}
//--------------Diagnostic-------------------
