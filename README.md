# LINduino-code
Teensy 3.2 (arduino) and ATTINY85 (digispark) LIN bus accessories for the steering wheel project.
Adding an 8 gate shifter now using an Atmega328p (arduino pro mini).

HI. This project is a stream project at twitch.tv/oh_bother. 
This is a slap-dash implementation of the minimum possible LIN bus protocol in order to 
get button presses from a mercedes steering wheel and use them to "push buttons" on a 
Granite Devices Simucube setup. 

While the wheel uses LIN bus why not invent a few "accessory" devices and have them send data back to the controller as well. An E46 M3 Steptronic shifter, Gas brake and clutch pedals from a junked 2015 bmw featuring a load cell and magnetometer, and a 3d printed H shifter (with some modifications).

The main controller calls out to all the devices possible in constant order (no scheduling) then latches the data to physical outputs.
Updates to the project will include digitally spoofing the Granite Devices bluetooth "wireless wheel" modules over serial.
This is a bit-banged bufferless moment by moment implementation of the LIN bus based off reading datasheets,
there are much much better projects on github no less that do this better. 

Anyway thanks for checking it out and I apologize if this code is seen as professional in any way at all whatsoever.
