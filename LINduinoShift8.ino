/* LIN arduino
 *  ---------- LIN Slave ------------------
 *  gets lin request, latches inputs then replies
 *  this is for a simuCUBE wheel setup with a LIN wheel, shifter, and pedals
 *  
 *  
 * LIN bus protocol:
 * https://www.csselectronics.com/screen/page/lin-bus-protocol-intro-basics/language/en
 * https://hackaday.com/2017/05/26/embed-with-elliot-lin-is-for-hackers/
 * 
 * Built for the open H shifter project build. 8 switches
 * this code will exist on a pro mini for all the extra pinout
 * 
 * const byte shift8_addy = 0x1F; // 0|111110|00|1    011111 31
 * 
 * H shifter buttons bits (2)
 * 1 2 3 4 5 6 7 Reverse (blank)
 */

// addy check now features ubit_old, so brake time is only from good signals. (untested)

//-------------Settings----------------
//device address
const byte addy = 0x1F; // 0|111110|00|1    011111 31

//timings
#define UBITOFF -6 //us offset to get to square center (3us for pin high/low write)
#define BBITS 8 //how many bits to acknowledge break
#define TURNWAIT 3 //miliseconds after bad addy or other fail(8 bit packet)
#define HOFFSET 1 //half bit offset in number of right shifts (1 = 1/2, 2 = 1/4)

//-------------Pins-------------------
//soft uart
#define TXPIN 12 
#define RXPIN 11 

//inputs
#define SHIFT_1 2 
#define SHIFT_2 3 
#define SHIFT_3 4 
#define SHIFT_4 5 
#define SHIFT_5 6 
#define SHIFT_6 7 
#define SHIFT_7 8 
#define SHIFT_R 9 

//debug
#define SHOWPIN 10

//-------Data storage array------------
uint8_t data[2] = {0, 0};
const uint8_t dataSize = 2;

//------------Globals------------
//timers, these are limited due to short usec timings
unsigned long ubit = 50; //time a bit
unsigned long ubit_old = 50; //prev accept timing
unsigned long hbit = 25; //half a bit
unsigned long timeOut = 0;
unsigned long timeSet = 0;

//state machine
uint8_t stater = 0;

//-------------SETUP----------
void setup(){
  //bus pins
  pinMode(TXPIN, OUTPUT);
  pinMode(RXPIN, INPUT);

  //microPinis
  pinMode(SHIFT_1, INPUT_PULLUP);
  pinMode(SHIFT_2, INPUT_PULLUP);
  pinMode(SHIFT_3, INPUT_PULLUP);
  pinMode(SHIFT_4, INPUT_PULLUP);
  pinMode(SHIFT_5, INPUT_PULLUP);
  pinMode(SHIFT_6, INPUT_PULLUP);
  pinMode(SHIFT_7, INPUT_PULLUP);
  pinMode(SHIFT_R, INPUT_PULLUP);

  //debug
  pinMode(SHOWPIN, OUTPUT);
//  digitalWrite(SHOWPIN, LOW);
  bugPulse();

  //states
  digitalWrite(TXPIN, HIGH);
}

//-------------LOOOOOOP----------
void loop(){
  switch(stater){
    case 0:
      if(!digitalRead(RXPIN)){stater++;}
      break;   
       
    case 1:
      if(breakCheck()){stater++;}
      else{stater = 5;}
      break;
      
    case 2:
      if(syncCheck()){stater++;}
      else{stater = 5;}
      break;
  
    case 3:
      if(addyCheck()){stater++;}
      else{stater = 5;}
      break;
  
    case 4:
      latchInputs();
      delayMicroseconds(ubit*2); //delay for bus master
      bugPulse();
      replyStuff();
      stater++;
      break;

      
    default:
      penaltyBox();
      stater = 0;
      break;
  }
}

//------------- checkers ------------
bool breakCheck(){
  timerStart(ubit*BBITS);
  while(!digitalRead(RXPIN)){}//wait for signal
  if(timerCheck()){
    return 0;
  }else{
    return 1;
  }
}

bool syncCheck(){
  while(digitalRead(RXPIN)){} //gets us to the start bit

  while(!digitalRead(RXPIN)){}//wait for expiration of start bit
  
  timeSet = micros();

  //wait out 0x55
  for(uint8_t i = 4; i > 0; i--){
    while(digitalRead(RXPIN)){}
    while(!digitalRead(RXPIN)){}
  }

  //calc time passed
  ubit = micros();
  ubit = ((ubit - timeSet) >> 3) + UBITOFF; //(shift 3 for divide by 8) alternate: ((ubit - timeSet - UBITOFF) >> 3);
  hbit = ubit >> HOFFSET;
  return 1;
}


bool addyCheck(){
  uint8_t addor = 0;
  if(bitStart()){
    addor = getBits();
  }else{
    addor = 0;
  }
  if(addor == addy){
    //store timing
    ubit_old = ubit;
    return 1;
  }else{
    ubit = ubit_old;
    return 0;
  }
}

//-------------Transmit functions--------
//send the prepared data packet
void replyStuff(){
  for(int i=0; i < dataSize; i++){
    bitsSend(data[i]);
  }
}

//send a single byte with start and stop bit
void bitsSend(uint8_t stuff){
  //start bit
  digitalWrite(TXPIN, LOW);
  delayMicroseconds(ubit);
  
  //address w/ parity (lsb first)
  for(int i = 0; i <= 7; i++){
    bool thing = bitRead(stuff, i);
    digitalWrite(TXPIN, thing);
    delayMicroseconds(ubit);
  }

  //stop bit
  digitalWrite(TXPIN, HIGH);
  delayMicroseconds(ubit);

  //interbit timing
  delayMicroseconds(ubit*2);
}

//-------------Receive Functions---------------
//why is this separate?
//prepare to read a string of bits or a single bit
//the timecheck takes so long that it causes issues
bool bitStart(){
  while(digitalRead(RXPIN)){}//stuck here for bits, should have timeout escape pulsein?
  delayMicroseconds(hbit); //we're now in the middle of the start bit?
  return 1;
}

//use bitStart to initialize
uint8_t getBits(){
  uint8_t thingy = 0;

  bool stuffs = 1;

  for(uint8_t i = 0; i <=7; i++){
    delayMicroseconds(ubit);
//    digitalWrite(SHOWPIN, HIGH);
    bitWrite(thingy, i, digitalRead(RXPIN));
//    digitalWrite(SHOWPIN, LOW);
  }

  //allow the last bit to expire
  delayMicroseconds(ubit);
  
  return thingy;
}

//------------Timer--------------
void penaltyBox(){
  delay(TURNWAIT);
}

void timerStart(unsigned long uSeconds){
  timeSet = micros();
  timeOut = uSeconds;
}

//1 is good 0 is expired
bool timerCheck(){
  //this takes 8us to evaluate(with diagnostic register writes)
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
//if this needs speed the logic of only having one position at once can
void latchInputs(){
// sequential logic shit (ignore)
//  bool plus = !digitalRead(SHIFT_P);
//  bool minus = !digitalRead(SHIFT_M);
//  bool drive = 0;
//  
//  if(plus && minus){
//    plus = 0;
//    minus = 0;
//    drive = 1;
//  }
  
  bitWrite(data[0], 0, !digitalRead(SHIFT_1));
  bitWrite(data[0], 1, !digitalRead(SHIFT_2));
  bitWrite(data[0], 2, !digitalRead(SHIFT_3));
  bitWrite(data[0], 3, !digitalRead(SHIFT_4));
  bitWrite(data[0], 4, !digitalRead(SHIFT_5));
  bitWrite(data[0], 5, !digitalRead(SHIFT_6));
  bitWrite(data[0], 6, !digitalRead(SHIFT_7));
  bitWrite(data[0], 7, !digitalRead(SHIFT_R));
  

  data[dataSize - 1] = checkSum(addy, data, dataSize);
}

//---------------debug----------------
void bugPulse(){
  digitalWrite(SHOWPIN, HIGH);
  delayMicroseconds(25);
  digitalWrite(SHOWPIN, LOW);
}

//void writeData(uint8_t fistfull){
//  for(int i = 0; i <= 7; i++){
//    digitalWrite(SHOWPIN, bitRead(fistfull, i));
//    delayMicroseconds(UBIT);
//  }
//}
