#include "core_pins.h"
#include "usb_serial.h"

#include "usb2_serial.h"
usb2_serial_class HSerial; // defined in usb2_serial.h

extern "C" void usb2_init();

void logg(uint32_t del, const char *txt)
{ static uint32_t to;
  if(millis()-to > del)
  {
    digitalWriteFast(13, !digitalReadFast(13));
    Serial.print("usb1 : "); Serial.println(txt); 
    HSerial.print("usb2 : "); HSerial.println(txt); 
    to=millis();
  }
}

extern "C" void setup()
{ 
  //while(!Serial || millis()<4000);
  Serial.println("USB2 test");

 	usb2_init();

  pinMode(13,OUTPUT);
}

extern "C" void loop()
{ 
  logg(1000,"loop");
}
