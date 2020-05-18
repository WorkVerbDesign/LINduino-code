/* LIN arduino
 *  ---------- LIN MASTER ------------------
 *  requests address and sets outputs based on bits received
 *  this is for a simuCUBE wheel setup with a LIN wheel, shifter, and pedals
 *  since LIN is based on fixed known structures and the Teensy is huge
 *  I'm relying on fixed global arrays for storage/recall
 *  No task scheduling, and no CRC or parity checks.. YET.
 *  
 *  may need i2c port expander
 *  15 buttons on wheel
 *  5 "buttons" on shifter
 *  2 i2c
 *  2 tx/rx
 *  
 *  teensy has exactly this many.
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
 * shifter buttons (first bit)
 * Reverse Neutral Drive Shift- Shift+ 0 0 0
 * 
 * no CRC yet, response is good may not be a need
 */

//-------------Pins-------------------
//tx/rx, can be any pin
#define TXPIN 10
#define RXPIN 9

//shifter pins
#define PIN_SH_R 13
#define PIN_SH_N 14
#define PIN_SH_D 15
#define PIN_SH_P 16
#define PIN_SH_M 17

//wheel pins
#define PIN_WH_SP 18
#define PIN_WH_SM 19
#define PIN_WH_UP 20
#define PIN_WH_DN 21
#define PIN_WH_TK 22
#define PIN_WH_MU 23

//pedal diagnostic pins
#define CLUTCH 3
#define BRAKE 4
#define GAS 5

//to sync the scope
#define DIAGNOSTIC 6

//-------------Settings----------------
#define UBIT 50 //uS per bit as a baseline
#define HALFBIT 11 //half of bit (tweaked)
#define BREAKOUT 12 //bit timer before no data = move on
#define BREAKBITS 14 //how many bits to acknowledge break
#define TURNWAIT 6 //miliseconds after bad addy (8 bit packet)
#define RISEFALL 3 //wait for signal to rise or fall when reading

//the address (without start and stop) is 6 bits with two parity bits
// start|LSB...MSB|PARITY|STOP
// parity is xor of even bits followed by xor of odd bits
// I guess 0 (lsb) is even.
// x|012345|67|x
// s|lsbMsb|eo|s
const byte wheel_addy = 0x9C; // 0|001110|01|1
const byte shift_addy = 0x1E; // 0|011110|00|1
const byte pedal_addy = 0x5F; // 0|111101|01|1
//const byte shift_addy = 0x55; // 0|101010|10|1 to help timing alignment

//-------global arrays to store data in------------
//arrays
uint8_t wheel[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
uint8_t shift[2] = {0, 0};
uint8_t pedal[7] = {0, 0, 0, 0, 0, 0, 0};

//#positions in the arrrays
const uint8_t wheelSize = 9;
const uint8_t shiftSize = 2;
const uint8_t pedalSize = 7;

//------------Globals------------
float timeSet = 0;
float timeOut = 0;

//-------------SETUP----------
void setup() {
  //debuuuuug analog resolutionator
  analogWriteResolution(10);
  
  //bus pins
  pinMode(TXPIN, OUTPUT);
  pinMode(RXPIN, INPUT);

  //micro pins
  pinMode(PIN_SH_R, OUTPUT);
  pinMode(PIN_SH_N, OUTPUT);
  pinMode(PIN_SH_D, OUTPUT);
  pinMode(PIN_SH_P, OUTPUT);
  pinMode(PIN_SH_M, OUTPUT);
  pinMode(PIN_WH_SP, OUTPUT);
  pinMode(PIN_WH_SM, OUTPUT);
  pinMode(PIN_WH_UP, OUTPUT);
  pinMode(PIN_WH_DN, OUTPUT);
  pinMode(PIN_WH_TK, OUTPUT);
  pinMode(PIN_WH_MU, OUTPUT);

  //troubleshoot
  pinMode(DIAGNOSTIC, OUTPUT);
  digitalWrite(DIAGNOSTIC, LOW);

  //diagnosticLED
  pinMode(CLUTCH, OUTPUT);
  pinMode(BRAKE, OUTPUT);
  pinMode(GAS, OUTPUT);
  
  digitalWrite(CLUTCH, LOW);
  digitalWrite(BRAKE, LOW);
  digitalWrite(GAS, LOW);

  //inital pin set
  digitalWrite(TXPIN, HIGH);

  digitalWrite(PIN_SH_R, HIGH);
  digitalWrite(PIN_SH_N, HIGH);
  digitalWrite(PIN_SH_D, HIGH);
  digitalWrite(PIN_SH_P, HIGH);
  digitalWrite(PIN_SH_M, HIGH);
  digitalWrite(PIN_WH_SP, HIGH);
  digitalWrite(PIN_WH_SM, HIGH);
  digitalWrite(PIN_WH_UP, HIGH);
  digitalWrite(PIN_WH_DN, HIGH);
  digitalWrite(PIN_WH_TK, HIGH);
  digitalWrite(PIN_WH_MU, HIGH);

  //Serial
  Serial.begin(9600);
  Serial.println("ready");
}


//-------------LOOOOOOP----------
//right now the loop reads everyone then writes, I may want this to go in sequence
void loop() {
  linEveryone();
  digitalOuts();
  serialPrints();
  
  delay(20);

}

//packets are known this is just for organization
void linEveryone(){
  busWheel();
  delay(10);  
  busShift();
  delay(10);
  busPedals();
  delay(10);
}


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

//-------------MICROCONTROLLER----------
void digitalOuts(){
  wheelOuts();
  shiftOuts();
  pedalOuts();
}

//outputs wheel buttons
void wheelOuts(){
  digitalWrite(PIN_WH_SP, !bitRead(wheel[2], 2));
  digitalWrite(PIN_WH_SM, !bitRead(wheel[2], 3));
  
  digitalWrite(PIN_WH_UP, !bitRead(wheel[0], 0));
  digitalWrite(PIN_WH_DN, !bitRead(wheel[0], 1));
  digitalWrite(PIN_WH_TK, !bitRead(wheel[1], 1));
  digitalWrite(PIN_WH_MU, !bitRead(wheel[1], 3));
}

//outputs shifter positions
void shiftOuts(){
  digitalWrite(PIN_SH_R, !bitRead(shift[0], 0));
  digitalWrite(PIN_SH_N, !bitRead(shift[0], 1));
  digitalWrite(PIN_SH_D, !bitRead(shift[0], 2));
  digitalWrite(PIN_SH_P, !bitRead(shift[0], 3));
  digitalWrite(PIN_SH_M, !bitRead(shift[0], 4));
}

//outputs pedal positions with digital pots
void pedalOuts(){
  //pull stuff out of the array
  uint16_t gas = word(pedal[1], pedal[0]);
  uint16_t clutch = word(pedal[3], pedal[2]);
  uint16_t brake = word(pedal[5], pedal[4]);

  //do stuff with the stuff because stuff
  analogWrite(GAS, gas);
  analogWrite(CLUTCH, clutch);
  analogWrite(BRAKE, brake); 
}

//-------------Serial prints-------------
void serialPrints(){
  printWheel();
  printShift();
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
  Serial.print(pedal_addy, HEX);
  
  for(int i = 0; i < 6; i++){
    Serial.print("|");
    printBits(pedal[i]);
  }

  Serial.print("|");
  Serial.print(pedal[7], HEX);
  
  Serial.println();
}

//prints binary with leading 0s
void printBits(uint8_t beets){
  for(int j=0; j<=7; j++){
    Serial.print(bitRead(beets,j));
  }
}
