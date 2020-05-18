/* LIN arduino
 *  ---------- LIN Slave ------------------
 *  gets lin request, latches inputs then replies
 *  this is for a simuCUBE wheel setup with a LIN wheel, shifter, and pedals
 *  
 * LIN bus protocol:
 * https://www.csselectronics.com/screen/page/lin-bus-protocol-intro-basics/language/en
 * https://hackaday.com/2017/05/26/embed-with-elliot-lin-is-for-hackers/
 * 
 * The bmw steptronic shifter uses 12v high or low outputs to give
 * park, netural, shift up and shift down. 
 * The shifter mixes plus and minus for "drive" so this decodes that
 * 
 * the 6 bit address has two parity bits that probably should be calculated
 * 0|011110|01|1
 * 
 * packet:
 * 0|R N D M P 0 0 0|1
 * 
 * to make a tiny85 full digispark high registers have to be set to 5d for full
 * pinout. pins with zener diodes just used as digital so compatible
 *
 * The timings in the code need a lot of review, there are small delays for uart stabalization
 * 
 * load cell!
 * update() should be called at least as often as HX711 sample rate; >10Hz@10SPS, >80Hz@80SPS 
 */

//-------------Settings----------------
//device address
const byte addy = 0x5F; // 0|111101|01|1

//timings
#define UBITOFF -8 //us offset (3us for pin high/low write)was 8
#define BBITS 10 //how many bits to acknowledge break
#define TURNWAIT 0 //4 //miliseconds after bad addy or other fail(8 bit packet)
#define HOFFSET 1 //half bit offset in number of right shifts (1 = 1/2, 2 = 1/4)

//loadcell
#define LOADLATCH 100 //read load cell in milliseconds

//-------------Pins-------------------
//soft uart
#define TXPIN 4 //pin 3
#define RXPIN 3 //pin 2

//inputs
#define CLUTCH A1 //pin 1
#define GASGASGAS A0 //pin 7

//load cell
#define DATPIN 1 //pin 6
#define CLKPIN 0 //pin 5

//debug
//#define SHOWPIN 5

//-------Data storage array------------
uint8_t data[7] = {0, 0, 0, 0, 0, 0, 0};
const uint8_t dataSize = 7;

//------------Globals------------
//timers, these are limited due to short usec timings
unsigned long ubit = 50; //time a bit
unsigned long hbit = 0; //half a bit
unsigned long timeOut = 0;
unsigned long timeSet =0;

//state machine
uint8_t stater = 0;

//loadcell
unsigned long loadTime = 0; //another global timer

//-------------SETUP----------
void setup(){
  //debug
//  pinMode(SHOWPIN, OUTPUT);
//  digitalWrite(SHOWPIN, HIGH);
  //  bugPulse();
  
  //bus pins
  pinMode(TXPIN, OUTPUT);
  pinMode(RXPIN, INPUT);

  //microPinis
  pinMode(CLUTCH, INPUT);
  pinMode(GASGASGAS, INPUT);

  //load cell
  pinMode(CLKPIN, OUTPUT);
  pinMode(DATPIN, INPUT);

  //initial states
  digitalWrite(TXPIN, HIGH);
  digitalWrite(CLKPIN, LOW);
}

//-------------LOOOOOOP----------
void loop(){
  //state machine
  meanBeanMachine();

  //data from pedal
  //perfectCell();
  
}
//------------ uart state machine ------------
void meanBeanMachine(){
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
      replyStuff();    
      delay(1);
      perfectCell();//do the data read for load cell
      stater++;
      break;

    default:
      penaltyBox();
      stater = 0;
      break;
  }
}

//this will update the 16 bit entries of the global data array
//from the 24 bit hx711
//this may conflict with bitstart, since it needs to run a lot
void perfectCell(){
//  if(millis() - loadTime > LOADLATCH){
    if(!digitalRead(DATPIN)){
      data[5] = shiftIn(DATPIN, CLKPIN, MSBFIRST); //flipped for reasons
      data[4] = shiftIn(DATPIN, CLKPIN, MSBFIRST);
      uint8_t noise = shiftIn(DATPIN, CLKPIN, MSBFIRST);
      digitalWrite(CLKPIN, HIGH);
      digitalWrite(CLKPIN, LOW);
    }
//    loadTime = millis();
//  }
  
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
  ubit = ((ubit - timeSet) >> 3) + UBITOFF; //(shift 3 for divide by 8)
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

  //this technically also checks parity so there's no need for xoring
  return addor == addy;
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

  for(uint8_t i = 0; i <=7; i++){
    delayMicroseconds(ubit);
    bitWrite(thingy, i, digitalRead(RXPIN));
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
    sum += datas[i];
    if (sum >= 256){sum -= 255;}
  }

  //reverse the return
   return (~sum & 0xFF);
}

//-------------MICROCONTROLLER----------
void latchInputs(){
  uint16_t gas = analogRead(GASGASGAS);
  uint16_t clutch = analogRead(CLUTCH);

  for(uint8_t i = 0; i < 8 ; i++){
//================TROUBLESHOOTING===================
    bitWrite(data[0], i, bitRead(gas, i));
    bitWrite(data[1], i, bitRead(gas, i+8));
//    bitSet(data[0], i);
//    bitSet(data[1], i);
  }
  for(uint8_t i = 0; i < 8 ; i++){
    bitWrite(data[2], i, bitRead(clutch, i));
    bitWrite(data[3], i, bitRead(clutch, i+8));
  }

  data[dataSize - 1] = checkSum(addy, data, dataSize);

  //overrides for testing
//  data[0] = 0xFF;
//  data[1] = 0xFF;
//  data[2] = 0xFF;
//  data[3] = 0xFF;
//  data[4] = 0xFF;
//  data[5] = 0xFF;
//  data[6] = 0xFF;


//  data[0] = lowByte(gas);
//  data[1] = highByte(gas);
//  data[2] = lowByte(clutch);
//  data[3] = highByte(clutch);

}

//---------------debug----------------
//void bugPulse(){
//  digitalWrite(SHOWPIN, HIGH);
//  digitalWrite(SHOWPIN, LOW);
//}

//void writeData(uint8_t fistfull){
//  for(int i = 0; i <= 7; i++){
//    digitalWrite(SHOWPIN, bitRead(fistfull, i));
//    delayMicroseconds(UBIT);
//  }
//}
