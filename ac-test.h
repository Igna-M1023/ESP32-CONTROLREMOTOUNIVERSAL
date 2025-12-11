#ifndef AC_TEST_H
#define AC_TEST_H

#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRutils.h>

const uint16_t kIrLed = 33; 
IRac ac(kIrLed);

bool escaneando = false;          
bool usuario_s = false;       
int numprotocolo = 1;      

void opEscaneo() {
  ac.next.model = 1;
  ac.next.mode = stdAc::opmode_t::kCool;
  ac.next.celsius = true;
  ac.next.degrees = 25;
  ac.next.fanspeed = stdAc::fanspeed_t::kMedium;
  ac.next.swingv = stdAc::swingv_t::kOff;
  ac.next.swingh = stdAc::swingh_t::kOff;
  ac.next.light = false;
  ac.next.beep = false;
  ac.next.econo = false;
  ac.next.filter = false;
  ac.next.turbo = false;
  ac.next.quiet = false;
  ac.next.sleep = -1;
  ac.next.clean = false;
  ac.next.clock = -1;
  ac.next.power = false;
}

void EmpezarEscaneo() {
  numprotocolo = 1;
  escaneando = true;
  usuario_s = false;
  Serial.println("--- Iniciando escaneo AC ---");
}

void RespuestaUsuario(bool respuestap) {
  if (!escaneando) return; 

  if (respuestap) {
    Serial.println("ES ESTE");
    escaneando = false; 
    usuario_s = false;
  } else {
    Serial.println("NO ES ESTE");
    usuario_s = false; 
  }
}

void bucleEscaneo(BLECharacteristic* pChar) {
  if (!escaneando || usuario_s) {
    return;
  }

  if (numprotocolo >= kLastDecodeType) {
    Serial.println("Se terminaron los protocolos!=!!=");
    pChar->setValue("FIN_PROC");
    pChar->notify();
    escaneando = false;
    return;
  }

  decode_type_t protocolo = (decode_type_t)numprotocolo;

  if (ac.isProtocolSupported(protocolo)) {
    String nombreprotocolo = typeToString(protocolo);
    
    Serial.println("Probando " + nombreprotocolo);
    String mensaje = nombreprotocolo;
    pChar->setValue(mensaje.c_str());
    pChar->notify();

    ac.next.protocol = protocolo;
    ac.next.power = true;
    ac.sendAc();

    usuario_s = true; 
  }

  numprotocolo++;
}

#endif